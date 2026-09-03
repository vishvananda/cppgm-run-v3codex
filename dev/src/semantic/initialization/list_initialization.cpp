#include "semantic/analysis/analyzer.h"

#include <unordered_set>

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace semantic
{

bool Analyzer::DefaultInitializationOverwritesObject(
	EntityId entity) const
{
	std::vector<EntityId> pending(1, entity);
	std::unordered_set<EntityId> visited;
	while (!pending.empty())
	{
		entity = pending.back();
		pending.pop_back();
		if (entity == kNoEntity || entity >= program_->entities.size())
			return false;
		if (!visited.insert(entity).second) continue;
		const EntityRecord& owner = program_->entities[entity];
		if (owner.has_user_provided_constructor) return false;
		if (owner.flavor == NAMED_UNION)
		{
			if (owner.union_default_member == kNoBinding ||
				!program_->bindings[owner.union_default_member].
					has_default_member_initializer) return false;
			continue;
		}
		for (std::size_t i = 0; i < owner.direct_base_count; ++i)
			pending.push_back(program_->DirectBase(entity, i).entity);
		if (entity >= entity_data_members_.size()) continue;
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
		{
			const BindingRecord& member =
				program_->bindings[entity_data_members_[entity][i]];
			if (member.has_default_member_initializer) continue;
			TypeId type = program_->types.RemoveTopCv(member.type);
			const TypeRecord* record = &program_->types.Get(type);
			while (record->kind == TYPE_ARRAY)
			{
				type = program_->types.RemoveTopCv(record->child);
				record = &program_->types.Get(type);
			}
			if (record->kind != TYPE_NAMED || !IsClassObjectType(type))
				return false;
			pending.push_back(record->entity);
		}
	}
	return true;
}

namespace
{

bool IsClassEntity(const Program& program, EntityId entity)
{
	if (entity == kNoEntity) return false;
	return IsClassNamedFlavor(program.entities[entity].flavor);
}

bool ChainsUserConversion(const FunctionInfo& constructor,
	const CallConversionFact& conversion)
{
	return conversion.constructor != kNoBinding &&
		(constructor.special_member == SPECIAL_MEMBER_COPY_CONSTRUCTOR ||
		 constructor.special_member == SPECIAL_MEMBER_MOVE_CONSTRUCTOR);
}

std::uint64_t BracedFactKey(NodeId node, TypeId type)
{
	return (static_cast<std::uint64_t>(node) << 32) | type;
}

struct CachedConstructorSelection
{
	BindingId selected;
	CallConversionFact inline_conversions[2];
	std::vector<CallConversionFact> overflow_conversions;
	std::size_t conversion_count;
	CachedConstructorSelection(BindingId selected_value,
		const std::vector<CallConversionFact>& conversion_values)
		: selected(selected_value), conversion_count(conversion_values.size())
	{
		if (conversion_count <= 2)
			for (std::size_t i = 0; i < conversion_count; ++i)
				inline_conversions[i] = conversion_values[i];
		else overflow_conversions = conversion_values;
	}
	void CopyConversions(std::vector<CallConversionFact>* output) const
	{
		if (conversion_count > 2) *output = overflow_conversions;
		else output->assign(inline_conversions,
			inline_conversions + conversion_count);
	}
};

}

struct BracedInitializationContext
{
	IndexedSequenceTable prepared_expression_index;
	std::vector<ExpressionInfo> prepared_expressions;
	CallConversionTable leaf_conversions;
	CallConversionTable braced_conversions;
	IndexedSequenceTable braced_conversion_in_progress;
	IndexedSequenceTable direct_selection_index;
	IndexedSequenceTable copy_selection_index;
	std::vector<CachedConstructorSelection> selections;
};

namespace
{

class ScopedBracedInitializationContext
{
public:
	ScopedBracedInitializationContext(BracedInitializationContext*& slot,
		BracedInitializationContext* value)
		: slot_(slot), previous_(slot)
	{
		slot_ = value;
	}
	~ScopedBracedInitializationContext() { slot_ = previous_; }

private:
	BracedInitializationContext*& slot_;
	BracedInitializationContext* previous_;
};

const ExpressionInfo* FindPreparedExpression(
	const BracedInitializationContext& context, NodeId node)
{
	const CompactIndexSequence* found =
		context.prepared_expression_index.Find(node);
	if (!found || found->Size() != 1) return 0;
	const std::size_t index = (*found)[0];
	return index < context.prepared_expressions.size() ?
		&context.prepared_expressions[index] : 0;
}

bool HasDirectPackExpansion(const SyntaxArena& arena, NodeId list)
{
	for (std::uint32_t edge = arena.FirstEdge(list); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (arena.IsTag(arena.EdgeChild(edge), ::cppgm::syntax::STAG_PACK_EXPANSION_EXPRESSION))
			return true;
	return false;
}

}

bool Analyzer::NeedsBracedCallContext(
	const std::vector<NodeId>& arguments) const
{
	if (braced_initialization_context_) return false;
	for (std::size_t i = 0; i < arguments.size(); ++i)
		if (arena_->IsTag(arguments[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST)) return true;
	return false;
}

ExpressionInfo Analyzer::AnalyzeCallInBracedContext(
	NodeId call, ScopeId scope, TypeId target)
{
	BracedInitializationContext context;
	ScopedBracedInitializationContext braced_scope(
		braced_initialization_context_, &context);
	return AnalyzeCall(call, scope, target);
}

ExpressionInfo Analyzer::AnalyzeAssignmentInBracedContext(
	NodeId node, ScopeId scope)
{
	BracedInitializationContext context;
	ScopedBracedInitializationContext braced_scope(
		braced_initialization_context_, &context);
	return AnalyzeAssignment(node, scope);
}

ExpressionInfo Analyzer::AnalyzeUntypedCallArgument(
	NodeId argument, ScopeId scope)
{
	if (!arena_->IsTag(argument, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		return AnalyzeExpression(argument, scope);
	if (!braced_initialization_context_)
		ThrowInternalCompilerError("braced call argument has no fact context");
	PrepareBracedInitialization(argument, scope);
	return ExpressionInfo();
}

ExpressionInfo Analyzer::MaterializeFunctionalCastArgument(
	NodeId syntax, ScopeId scope, TypeId target,
	const ExpressionInfo& prepared)
{
	if (prepared.type != kNoType) return prepared;
	if (!arena_->IsTag(syntax, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		return CandidateExpressionFailure(
			"functional cast argument has no type");
	return AnalyzeBracedInit(syntax, scope, target);
}

CallConversionFact Analyzer::UntypedCallArgumentConversion(
	NodeId argument, ScopeId scope, TypeId target)
{
	if (arena_->IsTag(argument, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		return BracedInitializationConversion(argument, scope, target);
	CallConversionFact result;
	if (HasUniqueFunctionAddressTarget(scope, argument, target))
	{
		result.rank = CONVERSION_EXACT;
		return result;
	}
	std::vector<BindingId> functions = FunctionCandidates(
		scope, arena_->Payload(argument), 0, argument);
	TypeId desired = program_->types.RemoveTopCv(target);
	if (program_->types.Get(desired).kind == TYPE_POINTER)
		desired = program_->types.Get(desired).child;
	const std::vector<BindingId> templates = FunctionTemplateTargetCandidates(
		scope, arena_->Payload(argument), desired, argument);
	for (std::size_t i = 0; i < templates.size(); ++i)
		if (std::find(functions.begin(), functions.end(), templates[i]) ==
			functions.end()) functions.push_back(templates[i]);
	std::size_t matches = 0;
	for (std::size_t i = 0; i < functions.size(); ++i)
		if (GetFunction(functions[i]).type == desired) ++matches;
	result.rank = matches == 1 ? CONVERSION_EXACT : CONVERSION_INVALID;
	return result;
}

ExpressionInfo Analyzer::MaterializeCallArgument(NodeId syntax,
	ScopeId scope, TypeId target, const ExpressionInfo& prepared,
	const CallConversionFact* conversion)
{
	if (prepared.type != kNoType)
		return ApplyCallArgument(prepared, target, conversion);
	if (!arena_->IsTag(syntax, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		return AnalyzeExpression(syntax, scope, target);
	const TypeRecord parameter = program_->types.Get(target);
	const TypeId list_target = parameter.kind == TYPE_LVALUE_REFERENCE ||
		parameter.kind == TYPE_RVALUE_REFERENCE ? parameter.child : target;
	ExpressionInfo argument = AnalyzeBracedCallArgument(
		syntax, scope, list_target);
	argument.category = VALUE_PRVALUE;
	dump_.nodes[argument.node].category = VALUE_PRVALUE;
	if ((parameter.kind == TYPE_LVALUE_REFERENCE ||
		parameter.kind == TYPE_RVALUE_REFERENCE) &&
		dump_.nodes[argument.node].kind == DUMP_INITIALIZER_LIST)
	{
		argument = MaterializeTemporary(argument);
		dump_.nodes[argument.node].argument_materialization = true;
	}
	const EntityId entity = EntityOf(list_target);
	if (entity != kNoEntity && program_->entities[entity].is_aggregate &&
		dump_.nodes[argument.node].kind == DUMP_BRACED_INIT_LIST)
		argument.node = BuildAggregateConstructionAction(
			list_target, argument.node);
	const TypeRecord list_object = program_->types.Get(
		program_->types.RemoveTopCv(list_target));
	if (list_object.kind == TYPE_ARRAY)
		argument = MaterializeTemporary(argument);
	return ApplyCallArgument(argument, target,
		list_object.kind == TYPE_ARRAY ? conversion : 0);
}

ExpressionInfo Analyzer::AnalyzeBracedInit(NodeId node, ScopeId scope,
	TypeId target)
{
	if (target == kNoType) ThrowSemanticError("untyped braced-init-list");
	if (IsInitializerListType(target))
		return AnalyzeInitializerList(node, scope, target);
	EnsureClassDefinition(target);
	ExpressionInfo expanded;
	if (TryAnalyzeExpandedBracedInit(node, scope, target, &expanded))
		return expanded;
	TypeId type = target;
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(type));
	const EntityId class_entity = EntityOf(type);
	if (IsClassEntity(*program_, class_entity))
	{
		bool use_constructors = !program_->entities[class_entity].is_aggregate;
		const std::vector<BindingId> candidates =
			ConstructorCandidates(class_entity);
		for (std::size_t i = 0; !use_constructors && i < candidates.size(); ++i)
		{
			const FunctionInfo& candidate = GetFunction(candidates[i]);
			const TypeRecord& function = program_->types.Get(candidate.type);
			if (candidate.constructor && function.parameter_count != 0 &&
				IsInitializerListType(
					program_->types.Parameters(candidate.type)[0]))
				use_constructors = true;
		}
		if (use_constructors)
		{
			std::vector<NodeId> arguments;
			for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
				edge = arena_->NextEdge(edge))
				arguments.push_back(arena_->EdgeChild(edge));
			ExpressionInfo result;
			result.node = BuildConstructorAction(type, scope, arguments,
				false, true, false, true, node);
			result.type = type;
			result.category = VALUE_NONE;
			SetExpressionDumpObject(&result);
			return result;
		}
		std::uint32_t element_edge = arena_->FirstEdge(node);
		ExpressionInfo result = AnalyzeAggregateInit(type, scope, &element_edge);
		if (element_edge != kNoEdge)
			ThrowSemanticError("excess aggregate initializer elements");
		return result;
	}
	if (array.kind == TYPE_ARRAY)
	{
		std::uint32_t element_edge = arena_->FirstEdge(node);
		const TypeRecord array_element = program_->types.Get(
			program_->types.RemoveTopCv(array.child));
		const bool string_array = element_edge != kNoEdge &&
			array_element.kind == TYPE_FUNDAMENTAL &&
			array_element.fundamental == FUND_CHAR &&
			arena_->NextEdge(element_edge) == kNoEdge &&
			arena_->IsTag(arena_->EdgeChild(element_edge), ::cppgm::syntax::STAG_LITERAL) &&
			arena_->Payload(arena_->EdgeChild(element_edge)).find('"') !=
				std::string::npos;
		ExpressionInfo result = string_array ?
			AnalyzeAggregateElement(type, scope, &element_edge) :
			AnalyzeArrayAggregateInit(type, scope, &element_edge);
		if (element_edge != kNoEdge)
			ThrowSemanticError("excess array initializer elements");
		return result;
	}
	TypeId element = array.kind == TYPE_VECTOR ? array.child : type;
	std::vector<ExpressionInfo> values;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		ExpressionInfo value = AnalyzeExpression(arena_->EdgeChild(edge), scope,
			element);
		if (IsIntegral(element, true) && IsArithmetic(value.type) &&
			!IsIntegral(value.type, true))
			return CandidateExpressionFailure(
				"narrowing list-initialization conversion");
		values.push_back(value);
	}
	if (array.kind == TYPE_VECTOR)
	{
		const std::size_t lanes = static_cast<std::size_t>(array.bound) /
			program_->SizeOf(array.child);
		if (!values.empty() && values.size() != 1 && values.size() != lanes)
			ThrowSemanticError(
				"GNU vector initializer requires one or all lane values");
	}
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST, type,
		VALUE_LVALUE);
	for (std::size_t i = 0; i < values.size(); ++i) dump_.Add(list, values[i].node);
	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	if (values.empty() && (IsArithmetic(type) || array.kind == TYPE_VECTOR))
	{
		dump_.nodes[list].value_initialization = true;
		if (IsArithmetic(type))
			SetExpressionScalar(&result, NormalizeScalarConstant(type,
				ConstexprScalarValue(static_cast<std::int64_t>(0))));
	}
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

void Analyzer::PrepareBracedInitialization(NodeId list, ScopeId scope)
{
	if (!braced_initialization_context_ || list == kNoNode ||
		!arena_->IsTag(list, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		return;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, ::cppgm::syntax::STAG_DESIGNATED_INITIALIZER)) continue;
		if (arena_->IsTag(child, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		{
			PrepareBracedInitialization(child, scope);
			continue;
		}
		if (FindPreparedExpression(*braced_initialization_context_, child))
			continue;
		const ExpressionInfo expression = AnalyzeExpression(child, scope);
		const std::size_t index =
			braced_initialization_context_->prepared_expressions.size();
		braced_initialization_context_->prepared_expressions.push_back(expression);
		braced_initialization_context_->prepared_expression_index.Ensure(child).
			Push(index);
	}
}

bool Analyzer::ReusePreparedBracedExpression(
	NodeId node, TypeId target, ExpressionInfo* result)
{
	if (!braced_initialization_context_) return false;
	const ExpressionInfo* prepared =
		FindPreparedExpression(*braced_initialization_context_, node);
	if (!prepared) return false;
	*result = *prepared;
	if (target == kNoType)
	{
		RecordExpressionFacts(*result);
		return true;
	}
	const CallConversionFact* conversion =
		braced_initialization_context_->leaf_conversions.Find(
			BracedFactKey(node, target));
	if (conversion && conversion->rank != CONVERSION_INVALID)
		*result = conversion->rank == CONVERSION_USER_DEFINED ?
			ApplyCallArgument(*result, target, conversion) :
			ApplyTarget(*result, target, conversion->rank);
	else *result = ApplyTarget(*result, target);
	return true;
}

ExpressionInfo Analyzer::AnalyzePreparedAggregateElement(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	if (!element_edge)
		ThrowInternalCompilerError("aggregate element has no initializer cursor");
	if (braced_initialization_context_ || *element_edge == kNoEdge)
		return AnalyzeAggregateElement(type, scope, element_edge);
	BracedInitializationContext context;
	ScopedBracedInitializationContext braced_scope(
		braced_initialization_context_, &context);
	const NodeId source = arena_->EdgeChild(*element_edge);
	const ExpressionInfo expression = AnalyzeExpression(source, scope);
	context.prepared_expressions.push_back(expression);
	context.prepared_expression_index.Ensure(source).Push(0);
	return AnalyzeAggregateElement(type, scope, element_edge);
}

ExpressionInfo Analyzer::AnalyzeAggregateDescent(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	ScopedBracedInitializationContext braced_scope(
		braced_initialization_context_, 0);
	return AnalyzeAggregateInit(type, scope, element_edge);
}

CallConversionFact Analyzer::PreparedAggregateElementConversion(
	NodeId source, TypeId target, const ExpressionInfo& expression)
{
	const std::uint64_t key = BracedFactKey(source, target);
	CallConversionTable* cache = braced_initialization_context_ ?
		&braced_initialization_context_->leaf_conversions : 0;
	if (cache)
	{
		const CallConversionFact* existing = cache->Find(key);
		if (existing)
		{
			++call_conversion_cache_hits_;
			return *existing;
		}
		++call_conversion_cache_misses_;
	}
	CallConversionFact result;
	result.rank = Conversion(expression, target);
	if (result.rank == CONVERSION_INVALID)
		result = ConvertingFunction(expression, target, false);
	if (cache) cache->Insert(key, result);
	return result;
}

bool Analyzer::IsBracedNarrowing(
	const ExpressionInfo& source, TypeId target,
	const CallConversionFact* conversion) const
{
	if (IsIntegral(target, true) && IsArithmetic(source.type) &&
		!IsIntegral(source.type, true)) return true;
	if (IsIntegral(source.type) && IsIntegral(target, true))
	{
		const auto fundamental_type = [this](TypeId type) -> TypeId
		{
			type = program_->types.RemoveTopCv(EffectiveType(type));
			const TypeRecord record = program_->types.Get(type);
			if (record.kind != TYPE_NAMED) return type;
			const EntityRecord& entity = program_->entities[record.entity];
			return entity.underlying == kNoType ?
				program_->types.Fundamental(FUND_INT) : entity.underlying;
		};
		const auto is_unsigned = [this](TypeId type) -> bool
		{
			const FundamentalKind kind = FundamentalOf(type);
			return kind == FUND_BOOL || kind == FUND_UNSIGNED_CHAR ||
				kind == FUND_UNSIGNED_SHORT_INT || kind == FUND_UNSIGNED_INT ||
				kind == FUND_UNSIGNED_LONG_INT ||
				kind == FUND_UNSIGNED_LONG_LONG_INT ||
				kind == FUND_UINT128 ||
				kind == FUND_CHAR16_T || kind == FUND_CHAR32_T;
		};
		const TypeId from = fundamental_type(source.type);
		const TypeId to = fundamental_type(target);
		std::size_t from_bits = program_->SizeOf(from) * 8;
		std::size_t to_bits = program_->SizeOf(to) * 8;
		const bool from_unsigned = is_unsigned(from);
		const bool to_unsigned = is_unsigned(to);
		if (FundamentalOf(from) == FUND_BOOL) from_bits = 1;
		if (FundamentalOf(to) == FUND_BOOL) to_bits = 1;
		if (source.constant)
		{
			if (to_unsigned)
			{
				if (source.value < 0) return true;
				if (to_bits >= 64) return false;
				const std::uint64_t maximum =
					(static_cast<std::uint64_t>(1) << to_bits) - 1;
				return static_cast<std::uint64_t>(source.value) > maximum;
			}
			if (to_bits >= 64) return false;
			const std::int64_t minimum =
				-(static_cast<std::int64_t>(1) << (to_bits - 1));
			const std::int64_t maximum =
				(static_cast<std::int64_t>(1) << (to_bits - 1)) - 1;
			return source.value < minimum || source.value > maximum;
		}
		if (from_unsigned == to_unsigned) return from_bits > to_bits;
		if (from_unsigned) return from_bits >= to_bits;
		return true;
	}
	if (IsIntegral(source.type) && IsFloating(target))
	{
		if (!source.constant) return true;
		const long double original = static_cast<long double>(source.value);
		switch (FundamentalOf(target))
		{
		case FUND_FLOAT:
			return static_cast<long double>(
				static_cast<float>(source.value)) != original;
		case FUND_DOUBLE:
			return static_cast<long double>(
				static_cast<double>(source.value)) != original;
		case FUND_LONG_DOUBLE:
			return static_cast<long double>(source.value) != original;
		default: break;
		}
	}
	if (!conversion || conversion->rank != CONVERSION_USER_DEFINED)
		return false;
	if (conversion->constructor != kNoBinding)
	{
		const FunctionInfo& constructor =
			GetFunction(conversion->constructor);
		const TypeRecord function = program_->types.Get(constructor.type);
		if (function.parameter_count != 0 && IsBracedNarrowing(source,
			program_->types.Parameters(constructor.type)[0])) return true;
	}
	if (conversion->conversion_function != kNoBinding)
	{
		ExpressionInfo converted;
		converted.type = GetFunction(
			conversion->conversion_function).conversion_target;
		converted.category = VALUE_PRVALUE;
		if (IsBracedNarrowing(converted, target)) return true;
	}
	return false;
}

CallConversionFact Analyzer::BracedInitializationConversion(
	NodeId list, ScopeId scope, TypeId target)
{
	CallConversionFact invalid;
	if (!braced_initialization_context_ || list == kNoNode ||
		!arena_->IsTag(list, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		return invalid;
	const std::uint64_t key = BracedFactKey(list, target);
	const CallConversionFact* existing =
		braced_initialization_context_->braced_conversions.Find(key);
	if (existing)
	{
		++braced_fact_cache_hits_;
		return *existing;
	}
	++braced_fact_cache_misses_;
	if (braced_initialization_context_->braced_conversion_in_progress.Find(key))
		return invalid;
	braced_initialization_context_->braced_conversion_in_progress.Ensure(key).
		Push(0);

	std::vector<NodeId> elements;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		elements.push_back(arena_->EdgeChild(edge));

	TypeId object = program_->types.RemoveTopCv(target);
	const TypeRecord top = program_->types.Get(object);
	CallConversionFact result;
	TypeId referred = top.child;
	while (top.kind == TYPE_LVALUE_REFERENCE &&
		program_->types.Get(program_->types.RemoveTopCv(referred)).kind ==
			TYPE_ARRAY)
		referred = program_->types.Get(
			program_->types.RemoveTopCv(referred)).child;
	if (top.kind == TYPE_LVALUE_REFERENCE && !IsConst(referred))
	{
		braced_initialization_context_->braced_conversions.Insert(key, result);
		return result;
	}
	if (top.kind == TYPE_LVALUE_REFERENCE || top.kind == TYPE_RVALUE_REFERENCE)
		object = program_->types.RemoveTopCv(top.child);
	const EntityId object_entity = EntityOf(object);
	if (IsClassEntity(*program_, object_entity)) EnsureClassDefinition(object);
	const TypeRecord record = program_->types.Get(object);

	TypeId initializer_element = kNoType;
	if (IsInitializerListType(object, &initializer_element))
	{
		result.rank = CONVERSION_EXACT;
		result.initializer_list_conversion = true;
		result.initializer_list_element_rank = CONVERSION_EXACT;
		for (std::size_t i = 0; i < elements.size(); ++i)
		{
			CallConversionFact element;
			if (arena_->IsTag(elements[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
				element = BracedInitializationConversion(
					elements[i], scope, initializer_element);
			else
			{
				const ExpressionInfo* source = FindPreparedExpression(
					*braced_initialization_context_, elements[i]);
				if (!source) return invalid;
				element = CallConversion(*source, initializer_element,
					&braced_initialization_context_->leaf_conversions,
					elements[i]);
				if (IsBracedNarrowing(
					*source, initializer_element, &element))
					element.rank = CONVERSION_INVALID;
			}
			if (element.rank == CONVERSION_INVALID)
			{
				result.rank = CONVERSION_INVALID;
				break;
			}
			result.initializer_list_element_rank = std::max(
				result.initializer_list_element_rank, element.rank);
		}
	}
	else if (record.kind == TYPE_ARRAY)
	{
		if (record.bound != 0 && elements.size() > record.bound)
			result.rank = CONVERSION_INVALID;
		else
		{
			result.rank = CONVERSION_EXACT;
			for (std::size_t i = 0; i < elements.size(); ++i)
			{
				CallConversionFact element;
				if (arena_->IsTag(elements[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
					element = BracedInitializationConversion(
						elements[i], scope, record.child);
				else
				{
					const ExpressionInfo* source = FindPreparedExpression(
						*braced_initialization_context_, elements[i]);
					if (!source) return invalid;
					element = CallConversion(*source, record.child,
						&braced_initialization_context_->leaf_conversions,
						elements[i]);
					if (IsBracedNarrowing(
						*source, record.child, &element))
						element.rank = CONVERSION_INVALID;
				}
				if (element.rank == CONVERSION_INVALID)
				{
					result.rank = CONVERSION_INVALID;
					break;
				}
				result.rank = std::max(result.rank, element.rank);
			}
		}
	}
	else if (record.kind == TYPE_NAMED &&
		IsClassEntity(*program_, record.entity))
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.is_aggregate)
		{
			if (record.entity >= entity_data_members_.size())
				result.rank = CONVERSION_INVALID;
			else
			{
				const std::vector<BindingId>& members =
					entity_data_members_[record.entity];
				const std::size_t limit = entity.flavor == NAMED_UNION ?
					(members.empty() ? 0 : 1) : members.size();
				result.rank = elements.size() > limit ?
					CONVERSION_INVALID : CONVERSION_USER_DEFINED;
				for (std::size_t i = 0;
					result.rank != CONVERSION_INVALID && i < elements.size(); ++i)
				{
					const TypeId member_type =
						program_->bindings[members[i]].type;
					CallConversionFact element;
					if (arena_->IsTag(elements[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
						element = BracedInitializationConversion(
							elements[i], scope, member_type);
					else
					{
						const ExpressionInfo* source = FindPreparedExpression(
							*braced_initialization_context_, elements[i]);
						if (!source) return invalid;
						element = CallConversion(*source, member_type,
							&braced_initialization_context_->leaf_conversions,
							elements[i]);
						if (IsBracedNarrowing(
							*source, member_type, &element))
							element.rank = CONVERSION_INVALID;
					}
					if (element.rank == CONVERSION_INVALID)
						result.rank = CONVERSION_INVALID;
				}
			}
		}
		else
		{
			std::vector<ExpressionInfo> arguments;
			arguments.reserve(elements.size());
			for (std::size_t i = 0; i < elements.size(); ++i)
			{
				if (arena_->IsTag(elements[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
					arguments.push_back(ExpressionInfo());
				else
				{
					const ExpressionInfo* source = FindPreparedExpression(
						*braced_initialization_context_, elements[i]);
					if (!source) return invalid;
					arguments.push_back(*source);
				}
			}
			std::vector<CallConversionFact> conversions;
			const BindingId selected = SelectConstructor(scope, elements,
				arguments, ConstructorCandidates(record.entity), true, true,
				&conversions, true, list, object);
			if (selected != kNoBinding)
			{
				result.rank = CONVERSION_USER_DEFINED;
				result.constructor = selected;
			}
		}
	}
	else if (elements.empty()) result.rank = CONVERSION_EXACT;
	else if (elements.size() != 1) result.rank = CONVERSION_INVALID;
	else if (arena_->IsTag(elements[0], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
		result = BracedInitializationConversion(elements[0], scope, target);
	else
	{
		const ExpressionInfo* source = FindPreparedExpression(
			*braced_initialization_context_, elements[0]);
		if (!source) return invalid;
		result = CallConversion(*source, target,
			&braced_initialization_context_->leaf_conversions, elements[0]);
		if (result.rank != CONVERSION_INVALID &&
			IsBracedNarrowing(*source, target, &result))
			result.rank = CONVERSION_INVALID;
	}
	braced_initialization_context_->braced_conversions.Insert(key, result);
	return result;
}

BindingId Analyzer::SelectConstructor(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<BindingId>& input_candidates, bool copy_initialization,
	bool list_initialization,
	std::vector<CallConversionFact>* selected_conversions,
	bool quiet, NodeId source_list, TypeId initialized_type,
	NodeId* selected_list_source)
{
	const std::uint64_t selection_key = source_list == kNoNode ||
		initialized_type == kNoType ? 0 :
		BracedFactKey(source_list, initialized_type);
	IndexedSequenceTable* selection_index = !braced_initialization_context_ ||
		selection_key == 0 ? 0 : copy_initialization ?
		&braced_initialization_context_->copy_selection_index :
		&braced_initialization_context_->direct_selection_index;
	if (selection_index)
	{
		const CompactIndexSequence* cached = selection_index->Find(selection_key);
		if (cached && cached->Size() == 1)
		{
			++braced_fact_cache_hits_;
			const CachedConstructorSelection& selection =
				braced_initialization_context_->selections[(*cached)[0]];
			if (selected_conversions)
				selection.CopyConversions(selected_conversions);
			const FunctionInfo& constructor = GetFunction(selection.selected);
			if (quiet && (constructor.deleted_constructor ||
				constructor.deleted_special_member ||
				(copy_initialization && constructor.explicit_constructor) ||
				!CanAccessMember(selection.selected))) return kNoBinding;
			if (!quiet)
			{
				if (constructor.deleted_constructor ||
					constructor.deleted_special_member)
					ThrowSemanticError("selected constructor is deleted");
				if (copy_initialization && constructor.explicit_constructor)
					ThrowSemanticError(
						"copy initialization selected an explicit constructor");
				if (!CanAccessMember(selection.selected))
					ThrowSemanticError("inaccessible constructor");
			}
			return selection.selected;
		}
		++braced_fact_cache_misses_;
	}
	std::vector<BindingId> candidates(input_candidates);
	AppendConstructorTemplateCandidates(initialized_type,
		copy_initialization && !list_initialization ?
			LambdaConstructorDeductionArguments(arguments) : arguments, &candidates,
		&argument_syntax, scope);
	const BindingId list_phase = list_initialization ? SelectInitializerListConstructorPhase(
		scope, initialized_type, source_list, argument_syntax, candidates,
		copy_initialization, selected_conversions, quiet,
		selected_list_source) : kNoBinding;
	if (list_phase != kNoBinding) return list_phase;
	const std::size_t arity = argument_syntax.size();
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		ThrowSemanticResourceLimit(
			"constructor conversion table is too large");
	std::vector<CallConversionFact> conversions(candidates.size() * arity);
	std::vector<TypeId> braced_sources(candidates.size() * arity, kNoType);
	CallConversionTable conversion_cache;
	std::vector<bool> viable(candidates.size(), true);
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		++overload_candidates_;
		const FunctionInfo& constructor = GetFunction(candidates[c]);
		const TypeRecord function_type = program_->types.Get(constructor.type);
		if (!constructor.constructor ||
			(copy_initialization && !list_initialization &&
			 constructor.explicit_constructor))
		{
			viable[c] = false;
			continue;
		}
		std::size_t required = function_type.parameter_count;
		while (required != 0 && required <= constructor.parameters.size() &&
			constructor.parameters[required - 1].default_argument != kNoNode)
			--required;
		if (arity < required || (!function_type.variadic &&
			arity > function_type.parameter_count))
		{
			viable[c] = false;
			continue;
		}
		for (std::size_t a = 0; a < arity; ++a)
		{
			CallConversionFact conversion;
			conversion.rank = CONVERSION_ELLIPSIS;
			if (a < function_type.parameter_count)
			{
				// Conversion analysis can instantiate templates and intern function
				// types.  Snapshot this parameter before that work can reallocate
				// TypeTable's parameter storage.
				const TypeId parameter =
					program_->types.Parameters(constructor.type)[a];
				if (arguments[a].type == kNoType)
				{
					if (arena_->IsTag(argument_syntax[a], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
					{
						conversion = BracedInitializationConversion(
							argument_syntax[a], scope, parameter);
						const TypeRecord top = program_->types.Get(parameter);
						TypeId source = program_->types.RemoveTopCv(
							top.kind == TYPE_LVALUE_REFERENCE ||
							top.kind == TYPE_RVALUE_REFERENCE ? top.child : parameter);
						std::uint32_t edge = arena_->FirstEdge(argument_syntax[a]);
						if (edge != kNoEdge && arena_->NextEdge(edge) == kNoEdge &&
							!arena_->IsTag(arena_->EdgeChild(edge), ::cppgm::syntax::STAG_BRACED_INIT_LIST))
						{
							const ExpressionInfo* prepared = FindPreparedExpression(
								*braced_initialization_context_,
								arena_->EdgeChild(edge));
							if (prepared) source = prepared->type;
						}
						braced_sources[c * arity + a] = source;
					}
					else if (HasUniqueFunctionAddressTarget(
						scope, argument_syntax[a], parameter))
						conversion.rank = CONVERSION_EXACT;
					else conversion.rank = CONVERSION_INVALID;
				}
				else
				{
					conversion = CallConversion(arguments[a], parameter,
						&conversion_cache, a);
					// N3485 13.3.3.1/4 excludes this second user
					// conversion only for the copy/move step of class
					// copy-initialization.  A direct-initialization candidate
					// remains viable and participates in ranking.
					if (copy_initialization &&
						ChainsUserConversion(constructor, conversion))
						conversion = CallConversionFact();
					if (list_initialization && source_list != kNoNode &&
						IsBracedNarrowing(
							arguments[a], parameter, &conversion))
						conversion.rank = CONVERSION_INVALID;
				}
			}
			conversions[c * arity + a] = conversion;
			if (conversion.rank == CONVERSION_INVALID) viable[c] = false;
		}
	}
	const auto better = [this, &conversions, &braced_sources, &arguments,
		&candidates, arity](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t a = 0; a < arity; ++a)
		{
			const CallConversionFact& left_conversion =
				conversions[left * arity + a];
			const CallConversionFact& right_conversion =
				conversions[right * arity + a];
			if (left_conversion.rank > right_conversion.rank)
				no_worse = false;
			if (left_conversion.rank < right_conversion.rank)
				strictly_better = true;
			if (left_conversion.rank == right_conversion.rank)
			{
				const int preference = CompareCallConversions(
					left_conversion, right_conversion);
				if (preference < 0) no_worse = false;
				if (preference > 0) strictly_better = true;
			}
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const TypeRecord& left_type =
			program_->types.Get(GetFunction(candidates[left]).type);
		const TypeRecord& right_type =
			program_->types.Get(GetFunction(candidates[right]).type);
		const TypeId* left_parameters =
			program_->types.Parameters(GetFunction(candidates[left]).type);
		const TypeId* right_parameters =
			program_->types.Parameters(GetFunction(candidates[right]).type);
		for (std::size_t a = 0; a < arity; ++a)
		{
			if (a >= left_type.parameter_count ||
				a >= right_type.parameter_count)
				continue;
			ExpressionInfo argument = arguments[a];
			if (argument.type == kNoType)
			{
				const TypeId left_source = braced_sources[left * arity + a];
				const TypeId right_source = braced_sources[right * arity + a];
				if (left_source == kNoType || left_source != right_source) continue;
				argument.type = left_source;
				argument.category = VALUE_PRVALUE;
			}
			const int preference = CompareReferenceBindings(
				argument, left_parameters[a], right_parameters[a]);
			if (preference != 0) return preference > 0;
		}
		const FunctionInfo& left_function = GetFunction(candidates[left]);
		const FunctionInfo& right_function = GetFunction(candidates[right]);
		const int template_preference = CompareFunctionTemplateConstraints(
			left_function, right_function);
		if (template_preference != 0) return template_preference > 0;
		return !left_function.template_specialization &&
			right_function.template_specialization;
	};
	std::size_t champion = candidates.size();
	std::size_t viable_count = 0;
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		if (!viable[c]) continue;
		++viable_count;
		if (champion == candidates.size() || better(c, champion)) champion = c;
	}
	if (viable_count == 0 && quiet) return kNoBinding;
	if (viable_count == 0)
	{
		if (quiet) return kNoBinding;
		ThrowSemanticError("no viable constructor for " +
			std::to_string(arity) + " argument(s) from " +
			std::to_string(candidates.size()) + " candidate(s)");
	}
	if (viable_count != 1)
		for (std::size_t c = 0; c < candidates.size(); ++c)
			if (c != champion && viable[c] && !better(champion, c))
			{
				if (quiet) return kNoBinding;
				ThrowSemanticError("ambiguous constructor");
			}
	const BindingId selected = candidates[champion];
	const FunctionInfo& constructor = GetFunction(selected);
	std::vector<CallConversionFact> selected_facts;
	selected_facts.reserve(arity);
	for (std::size_t a = 0; a < arity; ++a)
		selected_facts.push_back(conversions[champion * arity + a]);
	if (selected_conversions) *selected_conversions = selected_facts;
	if (selection_index)
	{
		const std::size_t index =
			braced_initialization_context_->selections.size();
		braced_initialization_context_->selections.push_back(
			CachedConstructorSelection(selected, selected_facts));
		selection_index->Ensure(selection_key).Push(index);
	}
	if (quiet && (constructor.deleted_constructor ||
		constructor.deleted_special_member ||
		(copy_initialization && constructor.explicit_constructor) ||
		!CanAccessMember(selected))) return kNoBinding;
	if (!quiet &&
		(constructor.deleted_constructor || constructor.deleted_special_member))
		ThrowSemanticError("selected constructor is deleted");
	if (!quiet && copy_initialization && constructor.explicit_constructor)
		ThrowSemanticError(
			"copy initialization selected an explicit constructor");
	if (!quiet && !CanAccessMember(selected))
		ThrowSemanticError("inaccessible constructor");
	return selected;
}

ExpressionInfo Analyzer::AnalyzeBracedCallArgument(
	NodeId list, ScopeId scope, TypeId target)
{
	const TypeId object = program_->types.RemoveTopCv(EffectiveType(target));
	if (IsInitializerListType(object))
		return AnalyzeInitializerList(list, scope, object);
	EnsureClassDefinition(object);
	const EntityId entity = EntityOf(object);
	if (IsClassEntity(*program_, entity) &&
		!program_->entities[entity].is_aggregate)
	{
		std::vector<NodeId> arguments;
		for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			arguments.push_back(arena_->EdgeChild(edge));
		ExpressionInfo result;
		result.node = BuildConstructorAction(object, scope, arguments,
			true, true, false, true, list);
		result.type = object;
		result.category = VALUE_PRVALUE;
		return result;
	}
	return AnalyzeBracedInit(list, scope, target);
}

std::uint32_t Analyzer::BuildConstructorAction(TypeId type,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	bool copy_initialization, bool list_initialization, bool base_subobject,
	bool demand, NodeId source_list,
	const std::vector<ExpressionInfo>* prepared_arguments)
{
	EnsureClassDefinition(type);
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		ThrowInternalCompilerError("constructor action has non-class type");
	if (!base_subobject && program_->entities[entity].abstract_class)
	{
		if (CandidateSubstitutionActive())
		{
			RecordCandidateSubstitutionFailure();
			return kNoDumpEdge;
		}
		ThrowSemanticError("cannot construct an abstract class value");
	}
	bool has_braced_argument = list_initialization && source_list != kNoNode;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		if (argument_syntax[i] != kNoNode &&
			arena_->IsTag(argument_syntax[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
			has_braced_argument = true;
	if (!braced_initialization_context_ && has_braced_argument)
	{
		BracedInitializationContext context;
		ScopedBracedInitializationContext braced_scope(
			braced_initialization_context_, &context);
		return BuildConstructorAction(type, scope, argument_syntax,
			copy_initialization, list_initialization, base_subobject, demand,
			source_list, prepared_arguments);
	}
	if (braced_initialization_context_)
	{
		if (list_initialization && source_list != kNoNode &&
			(!prepared_arguments || !HasDirectPackExpansion(*arena_, source_list)))
			PrepareBracedInitialization(source_list, scope);
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
			if (argument_syntax[i] != kNoNode &&
				arena_->IsTag(argument_syntax[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
				PrepareBracedInitialization(argument_syntax[i], scope);
	}
	std::vector<ExpressionInfo> arguments;
	if (prepared_arguments)
	{
		if (prepared_arguments->size() != argument_syntax.size())
			ThrowInternalCompilerError(
				"prepared constructor argument count mismatch");
		arguments = *prepared_arguments;
	}
	else
	{
		arguments.reserve(argument_syntax.size());
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		{
			if (arena_->IsTag(argument_syntax[i], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
				arguments.push_back(ExpressionInfo());
			else arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
		}
	}
	const std::vector<BindingId>& candidates = ConstructorCandidates(entity);
	std::vector<CallConversionFact> selected_conversions;
	NodeId selected_list_source = source_list;
	BindingId selected = SelectConstructor(scope, argument_syntax,
		arguments, candidates, copy_initialization, list_initialization,
		&selected_conversions, CandidateSubstitutionActive(), source_list,
		program_->types.RemoveTopCv(EffectiveType(type)),
		&selected_list_source);
	if (selected == kNoBinding)
	{
		RecordCandidateSubstitutionFailure();
		return kNoDumpEdge;
	}
	std::vector<NodeId> selected_argument_syntax(argument_syntax);
	const FunctionInfo& selected_function = GetFunction(selected);
	const TypeRecord& selected_type =
		program_->types.Get(selected_function.type);
	if (list_initialization && source_list != kNoNode &&
		selected_type.parameter_count != 0 &&
		IsInitializerListType(
			program_->types.Parameters(selected_function.type)[0]))
	{
		selected_argument_syntax.assign(1, selected_list_source);
		arguments.assign(1, ExpressionInfo());
	}
	const BindingId complete_constructor = selected;
	const bool promoted_deferred_base_entry = base_subobject &&
		selected < constructor_base_entry_by_binding_.size() &&
		constructor_base_entry_by_binding_[selected] == selected &&
		!program_->bindings[selected].constructor_base_entry;
	if (base_subobject) selected = EnsureConstructorBaseEntry(selected);
	const FunctionInfo constructor = GetFunction(selected);
	const TypeRecord function_type = program_->types.Get(constructor.type);
	const TypeId* parameter_data = program_->types.Parameters(constructor.type);
	std::vector<TypeId> parameters;
	if (function_type.parameter_count != 0)
		parameters.assign(parameter_data,
			parameter_data + function_type.parameter_count);
	bool first_argument_converted = false;
	bool materialized_conversion_result = false;
	if (selected_argument_syntax.size() == 1 && !parameters.empty() &&
		arguments[0].type != kNoType)
	{
		arguments[0] = ApplyCallArgument(arguments[0], parameters[0],
			selected_conversions.empty() ? 0 : &selected_conversions[0]);
		first_argument_converted = true;
		ExpressionInfo direct;
		if (TryBuildElidedClassValueTransfer(
			type, arguments[0], selected, &direct))
			return direct.node;
		const DumpNode& materialized = dump_.nodes[arguments[0].node];
		if (materialized.kind == DUMP_TEMPORARY_OBJECT &&
			materialized.first_edge != kNoDumpEdge &&
			dump_.edges[materialized.first_edge].next == kNoDumpEdge)
		{
			const DumpNode& recipe = dump_.nodes[
				dump_.edges[materialized.first_edge].child];
			materialized_conversion_result =
				recipe.kind == DUMP_CALL_EXPRESSION &&
				recipe.user_conversion_call;
		}
	}
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ACTION,
		AdaptMemberFunctionType(selected), VALUE_NONE,
		0, selected);
	dump_.nodes[action].operand_type =
		program_->types.RemoveTopCv(EffectiveType(type));
	dump_.nodes[action].trivial_special_member_action =
		constructor.trivial_special_member &&
		(constructor.implicit_special_member ||
		 constructor.special_member == SPECIAL_MEMBER_COPY_CONSTRUCTOR) &&
		!materialized_conversion_result;
	std::vector<ExpressionInfo> constexpr_arguments;
	constexpr_arguments.reserve(function_type.parameter_count);
	std::vector<BindingId> empty_base_entries;
	const bool empty_constructor_chain = selected_argument_syntax.empty() &&
		(constructor.defaulted_constructor || constructor.implicit_constructor) &&
		EmptyDefaultConstructorChain(selected, &empty_base_entries);
	const bool elide_defaulted_empty = constructor.defaulted_constructor &&
		program_->entities[entity].empty_class;
	const bool elide_implicit_subobject_chain = !host_object_emission_ &&
		constructor.implicit_constructor && !empty_base_entries.empty();
	if (empty_constructor_chain &&
		(elide_defaulted_empty || elide_implicit_subobject_chain))
	{
		dump_.nodes[action].elide_empty_constructor = true;
		if (preserve_constant_initializer_recipe_depth_ == 0 &&
			(program_->KindOfScope(scope) != SCOPE_NAMESPACE ||
			 !IsClassTemplateSpecializationEntity(entity)))
			for (std::size_t i = 0; i < empty_base_entries.size(); ++i)
				DemandFunction(empty_base_entries[i]);
	}
	for (std::size_t a = 0; a < selected_argument_syntax.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < function_type.parameter_count)
		{
			if (argument.type == kNoType)
			{
				if (arena_->IsTag(selected_argument_syntax[a], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
				{
					argument = MaterializeBracedConstructorArgument(
						selected_argument_syntax[a], scope, parameters[a]);
				}
				else argument = AnalyzeExpression(
					selected_argument_syntax[a], scope, parameters[a]);
				argument = ApplyCallArgument(argument, parameters[a]);
			}
			else if (!(a == 0 && first_argument_converted))
				argument = ApplyCallArgument(argument, parameters[a],
					a < selected_conversions.size() ?
						&selected_conversions[a] : 0);
		}
		dump_.Add(action, argument.node);
		if (a < function_type.parameter_count)
			constexpr_arguments.push_back(argument);
	}
	for (std::size_t a = selected_argument_syntax.size();
		a < function_type.parameter_count; ++a)
	{
		if (a >= constructor.parameters.size() ||
			constructor.parameters[a].default_argument == kNoNode)
			ThrowInternalCompilerError("selected constructor lacks a default argument");
		ExpressionInfo argument = AnalyzeExpression(
			constructor.parameters[a].default_argument,
			constructor.parameters[a].default_scope, parameters[a]);
		argument = ApplyCallArgument(argument, parameters[a]);
		MarkDefaultArgumentSubtree(argument.node);
		dump_.Add(action, argument.node);
		constexpr_arguments.push_back(argument);
	}
	if (dump_.nodes[action].trivial_special_member_action &&
		dump_.nodes[action].first_edge != kNoDumpEdge &&
		dump_.edges[dump_.nodes[action].first_edge].next == kNoDumpEdge)
	{
		const std::uint32_t source =
			dump_.edges[dump_.nodes[action].first_edge].child;
		if (dump_.nodes[source].kind == DUMP_TEMPORARY_OBJECT &&
			dump_.nodes[source].first_edge != kNoDumpEdge &&
			dump_.edges[dump_.nodes[source].first_edge].next == kNoDumpEdge)
		{
			const DumpNode& recipe = dump_.nodes[
				dump_.edges[dump_.nodes[source].first_edge].child];
			if (recipe.kind == DUMP_CONSTRUCTOR_ACTION &&
				recipe.operand_type == dump_.nodes[action].operand_type)
				dump_.nodes[source].elided_temporary_storage = true;
		}
	}
	std::uint32_t constexpr_object = kNoConstexprObject;
	if (constant_evaluation_suppressed_depth_ == 0 &&
		(constant_expression_required_depth_ != 0 ||
		 constexpr_evaluation_depth_ != 0) &&
		(constructor.constexpr_function || constructor.defaulted_constructor ||
		 constructor.implicit_constructor) &&
		TryEvaluateConstexprConstructor(
			selected, constexpr_arguments, &constexpr_object))
		PublishDumpObject(action, constexpr_object);
	const EntityId constructor_owner =
		program_->bindings[constructor.binding].member_owner;
	const bool explicitly_defaulted = constructor.defaulted_special_member &&
		!constructor.implicit_special_member &&
		!constructor.synthesized_memberwise_copy &&
		IsClassTemplateSpecializationEntity(constructor_owner);
	const bool compile_time_only = constant_expression_required_depth_ != 0 &&
		constant_initializer_required_depth_ == 0;
	const bool retained_empty_special_member =
		dump_.nodes[action].trivial_special_member_action &&
		constructor_owner != kNoEntity &&
		program_->entities[constructor_owner].empty_class &&
		!program_->entities[constructor_owner].trivial_destructor;
	if (demand && !compile_time_only &&
		preserve_constant_initializer_recipe_depth_ == 0 &&
		!dump_.nodes[action].elide_empty_constructor &&
		(explicitly_defaulted ||
		 !dump_.nodes[action].trivial_special_member_action ||
		 retained_empty_special_member) &&
		!(constructor.implicit_constructor &&
		program_->entities[entity].trivial_default_constructor &&
		!retained_empty_special_member))
		DemandFunction(promoted_deferred_base_entry ?
			complete_constructor : selected);
	if (demand && base_subobject &&
		preserve_constant_initializer_recipe_depth_ == 0 &&
		current_class_context_ != kNoEntity &&
		program_->entities[current_class_context_].polymorphic_class &&
		(!program_->entities[entity].polymorphic_class ||
		 (DestructorForType(program_->entities[entity].type) != kNoBinding &&
		  program_->bindings[DestructorForType(
			program_->entities[entity].type)].virtual_function)) &&
		!GetFunction(complete_constructor).implicit_constructor &&
		!GetFunction(complete_constructor).defaulted_constructor)
		DemandFunction(complete_constructor);
	++expression_count_;
	return action;
}


}
}
