#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

bool SyntaxContainsTag(const SyntaxArena& arena, NodeId node,
	const char* tag, std::unordered_set<NodeId>* visited)
{
	if (!visited->insert(node).second) return false;
	if (arena.IsTag(node, tag)) return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (SyntaxContainsTag(
			arena, arena.EdgeChild(edge), tag, visited)) return true;
	return false;
}

std::string StaticAssertLocation(const SyntaxArena& arena, NodeId node)
{
	const std::string& file = arena.SourceFile(node);
	return file.empty() ? std::string() :
		" at " + file + ":" + std::to_string(arena.SourceLine(node)) +
		":" + std::to_string(arena.SourceColumn(node));
}

}

std::size_t SemanticAnalyzer::RequestedAlignment(NodeId node, ScopeId scope)
{
	std::size_t result = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId alignment = arena_->EdgeChild(edge);
		if (arena_->IsTag(alignment, ::cppgm::syntax::STAG_GNU_ATTRIBUTE))
		{
			const std::string name = arena_->SemanticPayload(alignment);
			if (name != "aligned" && name != "__aligned__") continue;
			NodeId argument = kNoNode;
			for (std::uint32_t argument_edge = arena_->FirstEdge(alignment);
				argument_edge != kNoEdge;
				argument_edge = arena_->NextEdge(argument_edge))
			{
				const NodeId child = arena_->EdgeChild(argument_edge);
				if (arena_->IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE_NONLITERAL_ARGUMENT))
					throw std::runtime_error("invalid aligned attribute argument");
				if (!arena_->IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE_ARGUMENT)) continue;
				if (argument != kNoNode)
					throw std::runtime_error("aligned attribute has multiple arguments");
				argument = child;
			}
			const std::int64_t parsed = argument == kNoNode ? 16 :
				ParseInteger(arena_->SemanticPayload(argument));
			if (parsed < 0)
				throw std::runtime_error("invalid requested alignment");
			const std::uint64_t value = static_cast<std::uint64_t>(parsed);
			if (value != 0 && ((value & (value - 1)) != 0 ||
				value > std::numeric_limits<std::size_t>::max()))
				throw std::runtime_error("invalid requested alignment");
			result = std::max(result, static_cast<std::size_t>(value));
			continue;
		}
		if (!arena_->IsTag(alignment, ::cppgm::syntax::STAG_ALIGNMENT_SPECIFIER)) continue;
		const NodeId operand = FirstSemanticChild(alignment);
		if (operand == kNoNode)
			throw std::runtime_error("empty alignment specifier");
		std::uint64_t value = 0;
		if (arena_->IsTag(operand, ::cppgm::syntax::STAG_TYPE_ID))
		{
			const NodeId specifiers = FindChild(operand, ::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ);
			const NodeId name = specifiers == kNoNode ? kNoNode :
				FirstSemanticChild(specifiers);
			const BindingId variable_template = name == kNoNode ? kNoBinding :
				InstantiateVariableTemplate(name, scope);
			LookupResult constant;
			if (variable_template != kNoBinding)
				constant.ordinary = variable_template;
			else if (name != kNoNode &&
				arena_->IsTag(name,
					::cppgm::syntax::STAG_TYPE_NAME))
				constant = LookupSyntaxName(name, scope, LOOKUP_ORDINARY);
			if (constant.ordinary != kNoBinding)
			{
				const ExpressionInfo expression = AnalyzeNamedValue(
					PayloadSource(name), scope, kNoType, name);
				if (!expression.constant || expression.value < 0)
					throw std::runtime_error(
						"nonconstant alignment specifier");
				value = static_cast<std::uint64_t>(expression.value);
			}
			else
			{
				const TypeId type = BuildTypeId(operand, scope);
				EnsureClassDefinition(type);
				value = program_->AlignOf(type);
			}
		}
		else
		{
			const ExpressionInfo expression = AnalyzeExpression(operand, scope);
			if (!expression.constant || expression.value < 0)
				throw std::runtime_error("nonconstant alignment specifier");
			value = static_cast<std::uint64_t>(expression.value);
		}
		if (value == 0) continue;
		if ((value & (value - 1)) != 0 ||
			value > std::numeric_limits<std::size_t>::max())
			throw std::runtime_error("invalid requested alignment");
		result = std::max(result, static_cast<std::size_t>(value));
	}
	return result;
}

void SemanticAnalyzer::RecordExpressionFacts(const ExpressionInfo& value)
{
	if (value.node == kNoDumpEdge) return;
	DumpNode& node = dump_.nodes[value.node];
	const ConstexprAddressValue* address =
		ConstexprAddressAt(value.constexpr_address);
	const bool null_address = address &&
		address->kind == CONSTEXPR_ADDRESS_NULL;
	// Floating literal identity is retained for lowering, but PA15's runtime
	// control-flow lowering must not reinterpret a PA21 semantic float fact as
	// an optimization request.  An integral source that was converted to a
	// floating target still needs its original integer fact for PA15 to lower
	// the source side of that conversion.
	const bool retain_integral_source =
		value.floating_constant && IsIntegral(node.type, true);
	node.constant = value.constant &&
		(!value.floating_constant || retain_integral_source) &&
		value.constexpr_object == kNoConstexprObject &&
		(value.constexpr_address == kNoConstexprAddress || null_address);
	node.integer_literal_zero = value.integer_literal_zero;
	if (IsMemberPointer(value.type) && node.binding == kNoBinding)
		node.binding = value.binding;
	if ((!value.floating_constant || retain_integral_source) &&
		value.constexpr_object == kNoConstexprObject &&
		(value.constexpr_address == kNoConstexprAddress || null_address))
	{
		node.constant_value = null_address ? 0 : value.value;
		node.constant_high = null_address ? 0 :
			ExpressionScalar(value).integral_high;
	}
}

bool SemanticAnalyzer::IsUnsignedIntegral(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		return entity.underlying != kNoType &&
			IsUnsignedIntegral(entity.underlying);
	}
	if (record.kind == TYPE_BITINT) return record.bitint_unsigned;
	if (record.kind != TYPE_FUNDAMENTAL) return false;
	switch (record.fundamental)
	{
	case FUND_UNSIGNED_CHAR:
	case FUND_UNSIGNED_SHORT_INT:
	case FUND_UNSIGNED_INT:
	case FUND_UNSIGNED_LONG_INT:
	case FUND_UNSIGNED_LONG_LONG_INT:
	case FUND_UINT128:
	case FUND_CHAR16_T:
	case FUND_CHAR32_T:
		return true;
	default:
		return false;
	}
}

std::size_t SemanticAnalyzer::IntegralWidth(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.underlying == kNoType)
			throw std::logic_error("integral named type has no underlying type");
		return IntegralWidth(entity.underlying);
	}
	if (record.kind == TYPE_BITINT)
	{
		if (record.dependent_bound_parameter != kNoTemplateParameter ||
			record.bound == 0)
			throw std::logic_error("dependent _BitInt has no fixed width");
		return static_cast<std::size_t>(record.bound);
	}
	if (record.kind != TYPE_FUNDAMENTAL || !IsIntegral(type))
		throw std::logic_error("integral width requested for non-integral type");
	if (record.fundamental == FUND_BOOL) return 1;
	return program_->SizeOf(type) * 8;
}

std::int64_t SemanticAnalyzer::NormalizeIntegralConstant(TypeId type,
	std::int64_t value) const
{
	const std::size_t width = IntegralWidth(type);
	if (width == 1) return value != 0;
	if (width > 64) return value;
	if (width == 0)
		throw std::logic_error("unsupported integral constant width");
	std::uint64_t bits = static_cast<std::uint64_t>(value);
	if (width < 64)
	{
		const std::uint64_t mask = (std::uint64_t(1) << width) - 1;
		bits &= mask;
		if (!IsUnsignedIntegral(type) &&
			(bits & (std::uint64_t(1) << (width - 1))) != 0)
			bits |= ~mask;
	}
	return static_cast<std::int64_t>(bits);
}

bool SemanticAnalyzer::TryFoldConstantClassConversion(
	const ExpressionInfo& value, BindingId conversion, TypeId target,
	std::int64_t* result)
{
	// Constant-expression evaluation belongs to the generic evaluator, which
	// enforces the constexpr declaration requirement.  This owner-local fact is
	// only an O0 canonicalization for an otherwise observable ordinary call.
	if (constant_expression_required_depth_ != 0 ||
		constexpr_evaluation_depth_ != 0) return false;
	++constant_conversion_fact_requests_;
	const EntityId entity = EntityOf(value.type);
	if (entity == kNoEntity || !IsIntegral(target, true)) return false;
	const EntityRecord& object = program_->entities[entity];
	if (!object.empty_class || !object.trivial_default_constructor ||
		!object.trivial_destructor) return false;
	conversion = program_->bindings[conversion].canonical;
	FunctionInfo function = GetFunction(conversion);
	const EntityId conversion_owner =
		program_->bindings[conversion].member_owner;
	if (function.conversion_function && function.definition_body == kNoNode &&
		conversion_owner != kNoEntity &&
		conversion_owner < class_template_pattern_by_entity_.size() &&
		class_template_pattern_by_entity_[conversion_owner] != kNoDumpEdge)
	{
		DemandClassTemplateMemberDefinitions(conversion_owner);
		const BindingId specialization =
			program_->entities[conversion_owner].declaration;
		if (specialization != kNoBinding && specialization <
			class_template_member_definition_demand_states_.size())
			ApplyDemandedClassTemplateMemberDefinitions(specialization);
		function = GetFunction(conversion);
	}
	if (!function.conversion_function ||
		function.definition_body == kNoNode ||
		program_->types.RemoveTopCv(EffectiveType(function.conversion_target)) !=
		program_->types.RemoveTopCv(EffectiveType(target))) return false;
	const std::unordered_map<BindingId, std::int64_t>::const_iterator cached =
		constant_conversion_return_values_.find(conversion);
	if (cached != constant_conversion_return_values_.end())
	{
		++constant_conversion_fact_cache_hits_;
		*result = cached->second;
		return true;
	}
	NodeId statement = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(function.definition_body);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		if (statement != kNoNode) return false;
		statement = arena_->EdgeChild(edge);
	}
	if (statement == kNoNode || !arena_->IsTag(statement, ::cppgm::syntax::STAG_RETURN_STATEMENT))
		return false;
	const NodeId expression = FirstSemanticChild(statement);
	if (expression == kNoNode || !arena_->IsTag(expression, ::cppgm::syntax::STAG_ID_EXPRESSION))
		return false;
	const NamePath path = StructuredNamePath(expression);
	const NameId name = path.Empty() ?
		program_->names.UseInterned(arena_->SemanticPayloadId(expression)) :
		path.Last();
	const LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
	{
		if (conversion_owner == kNoEntity ||
			conversion_owner >= class_template_pattern_by_entity_.size())
			return false;
		const std::size_t pattern =
			class_template_pattern_by_entity_[conversion_owner];
		const EntityRecord& owner = program_->entities[conversion_owner];
		if (pattern == kNoDumpEdge || pattern >= class_templates_.size() ||
			owner.template_argument_begin == kNoBinding) return false;
		const std::vector<TemplateParameter>& parameters =
			class_templates_[pattern].parameters;
		const std::vector<TemplateArgument> arguments = StoredTemplateArguments(
			owner.template_argument_begin, owner.template_argument_count);
		for (std::size_t i = 0; i < parameters.size() && i < arguments.size(); ++i)
			if (parameters[i].name == name &&
				arguments[i].kind == TEMPLATE_ARGUMENT_INTEGRAL)
			{
				*result = NormalizeIntegralConstant(target, arguments[i].value);
				constant_conversion_return_values_.insert(
					std::make_pair(conversion, *result));
				return true;
			}
		return false;
	}
	const BindingRecord& member = program_->bindings[
		program_->bindings[found.ordinary].canonical];
	if (member.kind != BIND_VARIABLE || member.non_static_data_member ||
		!member.constant || !IsIntegral(member.type, true)) return false;
	*result = NormalizeIntegralConstant(target, member.value);
	constant_conversion_return_values_.insert(
		std::make_pair(conversion, *result));
	return true;
}

ExpressionInfo SemanticAnalyzer::ApplyContextualBool(ExpressionInfo value)
{
	const TypeId boolean = program_->types.Fundamental(FUND_BOOL);
	if (program_->types.RemoveTopCv(EffectiveType(value.type)) == boolean)
	{
		value.type = boolean;
		return value;
	}
	if (IsArithmetic(value.type) || IsPointer(Decay(value.type)) ||
		IsNullptr(value.type))
	{
		value = ApplyTarget(value, boolean);
		value.type = boolean;
		return value;
	}
	const CallConversionFact conversion =
		ConvertingFunction(value, boolean, true);
	std::int64_t constant = 0;
	if (conversion.conversion_function != kNoBinding &&
		!GetFunction(conversion.conversion_function).explicit_conversion &&
		TryFoldConstantClassConversion(
			value, conversion.conversion_function, boolean, &constant))
	{
		ExpressionInfo folded = MakeLiteral(boolean, InternNumber(constant));
		folded.constant = true;
		folded.value = constant;
		RecordExpressionFacts(folded);
		return folded;
	}
	return ApplyExplicitConversion(value, boolean);
}

ExpressionInfo SemanticAnalyzer::AnalyzeNoexcept(NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode)
		throw std::runtime_error("noexcept expression has no operand");
	++unevaluated_depth_;
	++constant_evaluation_suppressed_depth_;
	ExpressionInfo analyzed;
	try
	{
		analyzed = AnalyzeExpression(operand, scope);
	}
	catch (...)
	{
		--constant_evaluation_suppressed_depth_;
		--unevaluated_depth_;
		throw;
	}
	--constant_evaluation_suppressed_depth_;
	--unevaluated_depth_;
	if (CandidateSubstitutionFailed()) return ExpressionInfo();
	const bool nonthrowing = InitializationActionsAreNonthrowing(analyzed.node);
	ExpressionInfo result = MakeLiteral(
		program_->types.Fundamental(FUND_BOOL),
		program_->names.Intern(nonthrowing ? "true" : "false"));
	result.constant = true;
	result.value = nonthrowing ? 1 : 0;
	SetExpressionScalar(&result, ConstexprScalarValue(result.value));
	RecordExpressionFacts(result);
	return result;
}

ExpressionInfo SemanticAnalyzer::ApplyClassObjectTarget(
	ExpressionInfo value, TypeId target)
{
	const CallConversionFact conversion =
		ConvertingFunction(value, target, false);
	if (conversion.rank == CONVERSION_INVALID)
		return ApplyTarget(value, target);
	std::int64_t constant = 0;
	if (conversion.conversion_function != kNoBinding &&
		TryFoldConstantClassConversion(
			value, conversion.conversion_function, target, &constant))
	{
		ExpressionInfo folded = MakeLiteral(target, InternNumber(constant));
		folded.constant = true;
		folded.value = constant;
		RecordExpressionFacts(folded);
		return folded;
	}
	return ApplyCallArgument(value, target, &conversion);
}

void SemanticAnalyzer::ValidateStaticAssertionsInBlock(NodeId block,
	ScopeId scope, std::uint32_t detached_output)
{
	const ScopeId local = NewScope(scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	for (std::uint32_t edge = arena_->FirstEdge(block); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, ::cppgm::syntax::STAG_STATIC_ASSERT_DECLARATION))
			AnalyzeStaticAssert(child, local);
		else if (arena_->IsTag(child, ::cppgm::syntax::STAG_SIMPLE_DECLARATION) ||
			arena_->IsTag(child, ::cppgm::syntax::STAG_ALIAS_DECLARATION) ||
			arena_->IsTag(child, ::cppgm::syntax::STAG_USING_DECLARATION))
			AnalyzeDeclaration(child, local, detached_output, true);
		else if (arena_->IsTag(child, ::cppgm::syntax::STAG_COMPOUND_STATEMENT))
			ValidateStaticAssertionsInBlock(child, local, detached_output);
		else
			for (std::uint32_t nested = arena_->FirstEdge(child);
				nested != kNoEdge; nested = arena_->NextEdge(nested))
			{
				const NodeId statement = arena_->EdgeChild(nested);
				if (arena_->IsTag(statement, ::cppgm::syntax::STAG_COMPOUND_STATEMENT))
					ValidateStaticAssertionsInBlock(
						statement, local, detached_output);
			}
	}
}

void SemanticAnalyzer::ValidateOrdinaryMemberFunctionBody(BindingId function)
{
	FunctionInfo& info = GetMutableFunction(function);
	if (!info.defined || info.definition_body == kNoNode) return;
	if (info.placeholder_return_kind != PLACEHOLDER_DECLARATOR_NONE)
	{
		AnalyzeRetainedPlaceholderFunctionBody(function);
		return;
	}
	std::unordered_set<NodeId> visited;
	if (!SyntaxContainsTag(*arena_, info.definition_body,
		"static-assert-declaration", &visited)) return;
	const TypeId output_type = AdaptMemberFunctionType(info.binding);
	const ScopeId function_scope = NewScope(info.lexical_scope, SCOPE_FUNCTION,
		program_->bindings[info.binding].name, ScopePrefixId(info.owner));
	BindFunctionParameterPackElement(
		function_scope, info.parameter_pack_name, kNoBinding);
	if (info.member_owner != kNoType)
	{
		const TypeId this_type = program_->types.Parameters(output_type)[0];
		program_->AddBinding(function_scope, BIND_PARAMETER,
			program_->names.Intern("this"), this_type);
	}
	for (std::size_t i = 0; i < info.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = info.parameters[i];
		const BindingId binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, ParameterBindingType(parameter));
		BindFunctionParameterPackElement(
			function_scope, parameter.pack_name, binding);
	}
	const TypeId previous_return = current_return_type_;
	const EntityId previous_class = current_class_context_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = program_->types.Get(info.type).child;
	current_class_context_ = program_->bindings[info.binding].member_owner;
	current_function_context_ = program_->bindings[info.binding].canonical;
	const std::uint32_t detached = MakeDump(DUMP_COMPOUND_STATEMENT);
	++unevaluated_depth_;
	try
	{
		ValidateStaticAssertionsInBlock(
			info.definition_body, function_scope, detached);
	}
	catch (...)
	{
		--unevaluated_depth_;
		current_return_type_ = previous_return;
		current_class_context_ = previous_class;
		current_function_context_ = previous_function;
		throw;
	}
	--unevaluated_depth_;
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
}

void SemanticAnalyzer::ValidateOrdinaryMemberFunctionBodies(EntityId entity)
{
	if (entity < class_template_pattern_by_entity_.size() &&
		class_template_pattern_by_entity_[entity] != kNoDumpEdge)
		return;
	if (entity >= entity_member_functions_.size()) return;
	const std::size_t function_count = entity_member_functions_[entity].size();
	for (std::size_t i = 0; i < function_count; ++i)
		ValidateOrdinaryMemberFunctionBody(
			entity_member_functions_[entity][i]);
}

ExpressionInfo SemanticAnalyzer::AnalyzeClassFunctionalCast(TypeId cast_type,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	NodeId arguments_node, TypeId target,
	const std::vector<ExpressionInfo>* prepared_arguments)
{
	const auto materialize_if_evaluated = [this](ExpressionInfo value) {
		return decltype_operand_depth_ == 0 ?
			MaterializeTemporary(value) : value;
	};
	EnsureClassDefinition(cast_type);
	const EntityId cast_entity = EntityOf(cast_type);
	if (cast_entity != kNoEntity &&
		program_->entities[cast_entity].abstract_class)
		return CandidateExpressionFailure(
			"cannot construct an abstract class value");
	bool has_initializer_list_constructor = false;
	const std::vector<BindingId> cast_constructors =
		ConstructorCandidates(cast_entity);
	for (std::size_t i = 0; i < cast_constructors.size(); ++i)
	{
		const FunctionInfo& constructor = GetFunction(cast_constructors[i]);
		const TypeRecord& function = program_->types.Get(constructor.type);
		if (constructor.constructor && function.parameter_count != 0 &&
			IsInitializerListType(
				program_->types.Parameters(constructor.type)[0]))
			has_initializer_list_constructor = true;
	}
	const bool reference_target = target != kNoType &&
		(program_->types.Get(target).kind == TYPE_LVALUE_REFERENCE ||
		 program_->types.Get(target).kind == TYPE_RVALUE_REFERENCE);
	if (argument_syntax.size() == 1)
	{
		const ExpressionInfo operand = prepared_arguments ?
			(*prepared_arguments)[0] : AnalyzeExpression(argument_syntax[0], scope);
		if (operand.type != kNoType && EntityOf(operand.type) != kNoEntity &&
			ConvertingFunction(operand, cast_type, true).rank !=
				CONVERSION_INVALID)
		{
			ExpressionInfo converted =
				ApplyExplicitConversion(operand, cast_type);
			converted = materialize_if_evaluated(converted);
			return target == kNoType ? converted : ApplyTarget(converted, target);
		}
	}
	if (program_->entities[cast_entity].is_aggregate &&
		!program_->entities[cast_entity].has_user_provided_constructor &&
		!has_initializer_list_constructor &&
		arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
	{
		ExpressionInfo result = AnalyzeBracedInit(
			arguments_node, scope, cast_type);
		if (program_->entities[cast_entity].empty_class &&
			dump_.nodes[result.node].kind == DUMP_BRACED_INIT_LIST)
		{
			const std::vector<NodeId> no_arguments;
			const std::uint32_t constructor = BuildConstructorAction(
				cast_type, scope, no_arguments, false, false, false, false);
			dump_.nodes[constructor].value_initialization = true;
			dump_.nodes[result.node].value_initialization = true;
			dump_.nodes[result.node].value_constructor = constructor;
		}
		const TypeId effective_target = target == kNoType ? kNoType :
			program_->types.RemoveTopCv(EffectiveType(target));
		const bool distinct_target = effective_target != kNoType &&
			effective_target !=
				program_->types.RemoveTopCv(EffectiveType(cast_type));
		if (target == kNoType || distinct_target)
		{
			result.node = BuildAggregateConstructionAction(
				cast_type, result.node, true);
			result.category = VALUE_PRVALUE;
			if (distinct_target)
			{
				if (reference_target) result = MaterializeTemporary(result);
				return ApplyTarget(result, target);
			}
			return materialize_if_evaluated(result);
		}
		if (reference_target) result = MaterializeTemporary(result);
		return ApplyTarget(result, target);
	}
	if (program_->entities[cast_entity].is_aggregate &&
		!program_->entities[cast_entity].has_user_provided_constructor &&
		!has_initializer_list_constructor &&
		arguments_node != kNoNode &&
		!argument_syntax.empty() &&
		!arena_->IsTag(arguments_node, ::cppgm::syntax::STAG_BRACED_INIT_LIST))
	{
		std::uint32_t element_edge = arena_->FirstEdge(arguments_node);
		ExpressionInfo result = AnalyzeAggregateInit(
			cast_type, scope, &element_edge);
		if (element_edge != kNoEdge)
			throw std::runtime_error("excess aggregate initializer elements");
		result.node = BuildAggregateConstructionAction(
			cast_type, result.node, true);
		result.category = VALUE_PRVALUE;
		const TypeId effective_target = target == kNoType ? kNoType :
			program_->types.RemoveTopCv(EffectiveType(target));
		const bool distinct_target = effective_target != kNoType &&
			effective_target !=
				program_->types.RemoveTopCv(EffectiveType(cast_type));
		if (target == kNoType || distinct_target)
		{
			if (distinct_target)
			{
				if (reference_target) result = MaterializeTemporary(result);
				return ApplyTarget(result, target);
			}
			return materialize_if_evaluated(result);
		}
		if (reference_target) result = MaterializeTemporary(result);
		return ApplyTarget(result, target);
	}
	if (program_->entities[cast_entity].is_aggregate &&
		!program_->entities[cast_entity].has_user_provided_constructor &&
		!has_initializer_list_constructor &&
		argument_syntax.empty())
	{
		if (target == kNoType)
		{
			const std::vector<NodeId> no_arguments;
			ExpressionInfo initialized;
			initialized.node = BuildConstructorAction(
				cast_type, scope, no_arguments, false, false);
			initialized.type = cast_type;
			initialized.category = VALUE_PRVALUE;
			dump_.nodes[initialized.node].value_initialization = true;
			return materialize_if_evaluated(initialized);
		}
		std::uint32_t empty = kNoEdge;
		ExpressionInfo result = AnalyzeAggregateInit(cast_type, scope, &empty);
		dump_.nodes[result.node].value_initialization = true;
		if (program_->entities[cast_entity].empty_class)
		{
			const std::vector<NodeId> no_arguments;
			const std::uint32_t constructor = BuildConstructorAction(
				cast_type, scope, no_arguments, false, false, false, false);
			dump_.nodes[constructor].value_initialization = true;
			dump_.nodes[result.node].value_constructor = constructor;
		}
		if (reference_target) result = MaterializeTemporary(result);
		return ApplyTarget(result, target);
	}
	ExpressionInfo result;
	result.node = BuildConstructorAction(cast_type, scope, argument_syntax,
		false, arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, ::cppgm::syntax::STAG_BRACED_INIT_LIST), false, true,
		arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, ::cppgm::syntax::STAG_BRACED_INIT_LIST) ?
			arguments_node : kNoNode, prepared_arguments);
	if (result.node == kNoDumpEdge) return ExpressionInfo();
	result.type = cast_type;
	result.category = VALUE_PRVALUE;
	if (argument_syntax.empty() &&
		!program_->entities[cast_entity].has_user_provided_constructor)
		dump_.nodes[result.node].value_initialization = true;
	return target == kNoType ? materialize_if_evaluated(result) :
		ApplyTarget(result, target);
}

void SemanticAnalyzer::AnalyzeStaticAssert(NodeId node, ScopeId scope)
{
	const NodeId condition_syntax = FirstSemanticChild(node);
	if (condition_syntax == kNoNode)
		throw std::runtime_error("static_assert has no condition" +
			StaticAssertLocation(*arena_, node));
	// A static assertion demanded while forming an unevaluated or discarded
	// expression is still an independent constant-evaluation root.  Preserve
	// the caller's suppression state, but do not let it disable the assertion's
	// required constexpr calls and conversions.
	const std::size_t outer_suppression =
		constant_evaluation_suppressed_depth_;
	constant_evaluation_suppressed_depth_ = 0;
	++constant_expression_required_depth_;
	ExpressionInfo condition;
	try
	{
		condition = AnalyzeExpression(condition_syntax, scope);
		condition = ApplyContextualBool(condition);
	}
	catch (...)
	{
		--constant_expression_required_depth_;
		constant_evaluation_suppressed_depth_ = outer_suppression;
		throw;
	}
	--constant_expression_required_depth_;
	constant_evaluation_suppressed_depth_ = outer_suppression;
	if (!IsIntegral(condition.type, true) || !condition.constant)
		throw std::runtime_error(
			"static_assert requires an integral constant expression" +
			StaticAssertLocation(*arena_, node));
	if (condition.value == 0)
		throw HardSemanticError("static assertion failed" +
			StaticAssertLocation(*arena_, node));
}

}
}
