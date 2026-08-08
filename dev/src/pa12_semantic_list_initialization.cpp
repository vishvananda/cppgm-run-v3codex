#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

bool IsClassEntity(const Program& program, EntityId entity)
{
	if (entity == kNoEntity) return false;
	const NamedFlavor flavor = program.entities[entity].flavor;
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION;
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

}

void SemanticAnalyzer::PrepareBracedInitialization(NodeId list, ScopeId scope)
{
	if (!braced_initialization_context_ || list == kNoNode ||
		!arena_->IsTag(list, "braced-init-list"))
		return;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "braced-init-list"))
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

bool SemanticAnalyzer::ReusePreparedBracedExpression(
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

bool SemanticAnalyzer::IsBracedNarrowing(
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

CallConversionFact SemanticAnalyzer::BracedInitializationConversion(
	NodeId list, ScopeId scope, TypeId target)
{
	CallConversionFact invalid;
	if (!braced_initialization_context_ || list == kNoNode ||
		!arena_->IsTag(list, "braced-init-list"))
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
	if (top.kind == TYPE_LVALUE_REFERENCE || top.kind == TYPE_RVALUE_REFERENCE)
		object = program_->types.RemoveTopCv(top.child);
	const TypeRecord record = program_->types.Get(object);
	CallConversionFact result;

	if (record.kind == TYPE_ARRAY)
	{
		if (record.bound != 0 && elements.size() > record.bound)
			result.rank = CONVERSION_INVALID;
		else
		{
			result.rank = CONVERSION_EXACT;
			for (std::size_t i = 0; i < elements.size(); ++i)
			{
				CallConversionFact element;
				if (arena_->IsTag(elements[i], "braced-init-list"))
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
					if (arena_->IsTag(elements[i], "braced-init-list"))
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
				if (arena_->IsTag(elements[i], "braced-init-list"))
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
	else if (arena_->IsTag(elements[0], "braced-init-list"))
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

BindingId SemanticAnalyzer::SelectConstructor(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<BindingId>& candidates, bool copy_initialization,
	bool list_initialization,
	std::vector<CallConversionFact>* selected_conversions,
	bool quiet, NodeId source_list, TypeId initialized_type)
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
			if (quiet && copy_initialization && constructor.explicit_constructor)
				return kNoBinding;
			if (!quiet)
			{
				if (constructor.deleted_constructor ||
					constructor.deleted_special_member)
					throw std::runtime_error("selected constructor is deleted");
				if (copy_initialization && constructor.explicit_constructor)
					throw std::runtime_error(
						"copy initialization selected an explicit constructor");
				if (!CanAccessMember(selection.selected))
					throw std::runtime_error("inaccessible constructor");
			}
			return selection.selected;
		}
		++braced_fact_cache_misses_;
	}
	const std::size_t arity = argument_syntax.size();
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		throw std::runtime_error("constructor conversion table is too large");
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
		const TypeId* parameters = program_->types.Parameters(constructor.type);
		for (std::size_t a = 0; a < arity; ++a)
		{
			CallConversionFact conversion;
			conversion.rank = CONVERSION_ELLIPSIS;
			if (a < function_type.parameter_count)
			{
				if (arguments[a].type == kNoType)
				{
					const TypeId parameter = parameters[a];
					if (arena_->IsTag(argument_syntax[a], "braced-init-list"))
					{
						conversion = BracedInitializationConversion(
							argument_syntax[a], scope, parameter);
						const TypeRecord top = program_->types.Get(parameter);
						TypeId source = program_->types.RemoveTopCv(
							top.kind == TYPE_LVALUE_REFERENCE ||
							top.kind == TYPE_RVALUE_REFERENCE ? top.child : parameter);
						std::uint32_t edge = arena_->FirstEdge(argument_syntax[a]);
						if (edge != kNoEdge && arena_->NextEdge(edge) == kNoEdge &&
							!arena_->IsTag(arena_->EdgeChild(edge), "braced-init-list"))
						{
							const ExpressionInfo* prepared = FindPreparedExpression(
								*braced_initialization_context_,
								arena_->EdgeChild(edge));
							if (prepared) source = prepared->type;
						}
						braced_sources[c * arity + a] = source;
					}
					else conversion.rank = CONVERSION_INVALID;
				}
				else
				{
					conversion = CallConversion(arguments[a], parameters[a],
						&conversion_cache, a);
					if (list_initialization && source_list != kNoNode &&
						IsBracedNarrowing(
							arguments[a], parameters[a], &conversion))
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
			if (left_conversion.rank == CONVERSION_USER_DEFINED &&
				right_conversion.rank == CONVERSION_USER_DEFINED)
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
		return false;
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
		throw std::runtime_error("no viable constructor for " +
			std::to_string(arity) + " argument(s) from " +
			std::to_string(candidates.size()) + " candidate(s)");
	if (viable_count != 1)
		for (std::size_t c = 0; c < candidates.size(); ++c)
			if (c != champion && viable[c] && !better(champion, c))
			{
				if (quiet) return kNoBinding;
				throw std::runtime_error("ambiguous constructor");
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
	if (quiet && copy_initialization && constructor.explicit_constructor)
		return kNoBinding;
	if (!quiet &&
		(constructor.deleted_constructor || constructor.deleted_special_member))
		throw std::runtime_error("selected constructor is deleted");
	if (!quiet && copy_initialization && constructor.explicit_constructor)
		throw std::runtime_error(
			"copy initialization selected an explicit constructor");
	if (!quiet && !CanAccessMember(selected))
		throw std::runtime_error("inaccessible constructor");
	return selected;
}

ExpressionInfo SemanticAnalyzer::AnalyzeBracedCallArgument(
	NodeId list, ScopeId scope, TypeId target)
{
	const TypeId object = program_->types.RemoveTopCv(EffectiveType(target));
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

std::uint32_t SemanticAnalyzer::BuildConstructorAction(TypeId type,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	bool copy_initialization, bool list_initialization, bool base_subobject,
	bool demand, NodeId source_list)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		throw std::logic_error("constructor action has non-class type");
	bool has_braced_argument = false;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		if (arena_->IsTag(argument_syntax[i], "braced-init-list"))
			has_braced_argument = true;
	if (!braced_initialization_context_ && has_braced_argument)
	{
		BracedInitializationContext context;
		ScopedBracedInitializationContext braced_scope(
			braced_initialization_context_, &context);
		return BuildConstructorAction(type, scope, argument_syntax,
			copy_initialization, list_initialization, base_subobject, demand,
			source_list);
	}
	if (braced_initialization_context_)
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
			if (arena_->IsTag(argument_syntax[i], "braced-init-list"))
				PrepareBracedInitialization(argument_syntax[i], scope);
	std::vector<ExpressionInfo> arguments;
	arguments.reserve(argument_syntax.size());
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
	{
		if (arena_->IsTag(argument_syntax[i], "braced-init-list"))
			arguments.push_back(ExpressionInfo());
		else arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
	}
	const std::vector<BindingId>& candidates = ConstructorCandidates(entity);
	std::vector<CallConversionFact> selected_conversions;
	BindingId selected = SelectConstructor(scope, argument_syntax,
		arguments, candidates, copy_initialization, list_initialization,
		&selected_conversions, false, source_list,
		program_->types.RemoveTopCv(EffectiveType(type)));
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
	if (argument_syntax.size() == 1 && !parameters.empty() &&
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
		constructor.display_name, selected);
	dump_.nodes[action].operand_type =
		program_->types.RemoveTopCv(EffectiveType(type));
	dump_.nodes[action].trivial_special_member_action =
		constructor.trivial_special_member &&
		(constructor.implicit_special_member ||
		 constructor.special_member == SPECIAL_MEMBER_COPY_CONSTRUCTOR) &&
		!materialized_conversion_result;
	std::vector<BindingId> empty_base_entries;
	if (((constructor.defaulted_constructor &&
		  program_->entities[entity].empty_class) ||
		 (constructor.implicit_constructor &&
		  program_->entities[entity].direct_base != kNoEntity &&
		  !program_->entities[
			program_->entities[entity].direct_base].trivial_default_constructor)) &&
		argument_syntax.empty() &&
		EmptyDefaultConstructorChain(selected, &empty_base_entries))
	{
		dump_.nodes[action].elide_empty_constructor = true;
		for (std::size_t i = 0; i < empty_base_entries.size(); ++i)
			DemandFunction(empty_base_entries[i]);
	}
	for (std::size_t a = 0; a < argument_syntax.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < function_type.parameter_count)
		{
			if (argument.type == kNoType)
			{
				const TypeRecord parameter = program_->types.Get(parameters[a]);
				const TypeId list_target = parameter.kind == TYPE_LVALUE_REFERENCE ||
					parameter.kind == TYPE_RVALUE_REFERENCE ?
					parameter.child : parameters[a];
				argument = AnalyzeBracedCallArgument(
					argument_syntax[a], scope, list_target);
				argument.category = VALUE_PRVALUE;
				dump_.nodes[argument.node].category = VALUE_PRVALUE;
				const EntityId list_entity = EntityOf(list_target);
				if (list_entity != kNoEntity &&
					program_->entities[list_entity].is_aggregate &&
					dump_.nodes[argument.node].kind == DUMP_BRACED_INIT_LIST)
					argument.node = BuildAggregateConstructionAction(
						list_target, argument.node);
				argument = ApplyCallArgument(argument, parameters[a]);
			}
			else if (!(a == 0 && first_argument_converted))
				argument = ApplyCallArgument(argument, parameters[a],
					a < selected_conversions.size() ?
						&selected_conversions[a] : 0);
		}
		dump_.Add(action, argument.node);
	}
	for (std::size_t a = argument_syntax.size();
		a < function_type.parameter_count; ++a)
	{
		if (a >= constructor.parameters.size() ||
			constructor.parameters[a].default_argument == kNoNode)
			throw std::logic_error("selected constructor lacks a default argument");
		ExpressionInfo argument = AnalyzeExpression(
			constructor.parameters[a].default_argument,
			constructor.parameters[a].default_scope, parameters[a]);
		argument = ApplyCallArgument(argument, parameters[a]);
		dump_.Add(action, argument.node);
	}
	if (demand && !dump_.nodes[action].elide_empty_constructor &&
		!dump_.nodes[action].trivial_special_member_action &&
		!(constructor.implicit_constructor &&
		program_->entities[entity].trivial_default_constructor))
		DemandFunction(selected);
	++expression_count_;
	return action;
}


}
}
