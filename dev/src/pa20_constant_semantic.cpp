#include "pa12_semantic_detail.h"
#include "exceptions.h"

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

}

std::size_t SemanticAnalyzer::RequestedAlignment(NodeId node, ScopeId scope)
{
	std::size_t result = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId alignment = arena_->EdgeChild(edge);
		if (!arena_->IsTag(alignment, "alignment-specifier")) continue;
		const NodeId operand = FirstSemanticChild(alignment);
		if (operand == kNoNode)
			throw std::runtime_error("empty alignment specifier");
		std::uint64_t value = 0;
		if (arena_->IsTag(operand, "type-id"))
		{
			const NodeId specifiers = FindChild(operand, "type-specifier-seq");
			const NodeId name = specifiers == kNoNode ? kNoNode :
				FirstSemanticChild(specifiers);
			const LookupResult constant = name != kNoNode &&
				arena_->IsTag(name, "type-name") ?
				FindChild(name, "structured-type-name") != kNoNode ?
					LookupStructuredName(name, scope, LOOKUP_ORDINARY) :
				LookupSpelling(scope, PayloadSource(name), LOOKUP_ORDINARY) :
				LookupResult();
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
	// an optimization request.
	node.constant = value.constant && !value.floating_constant &&
		value.constexpr_object == kNoConstexprObject &&
		(value.constexpr_address == kNoConstexprAddress || null_address);
	node.integer_literal_zero = value.integer_literal_zero;
	if (!value.floating_constant &&
		value.constexpr_object == kNoConstexprObject &&
		(value.constexpr_address == kNoConstexprAddress || null_address))
		node.constant_value = null_address ? 0 : value.value;
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
	if (record.kind != TYPE_FUNDAMENTAL) return false;
	switch (record.fundamental)
	{
	case FUND_UNSIGNED_CHAR:
	case FUND_UNSIGNED_SHORT_INT:
	case FUND_UNSIGNED_INT:
	case FUND_UNSIGNED_LONG_INT:
	case FUND_UNSIGNED_LONG_LONG_INT:
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
	if (width > 64 || width == 0)
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
	const EntityId entity = EntityOf(value.type);
	if (entity == kNoEntity || !IsIntegral(target, true)) return false;
	const EntityRecord& object = program_->entities[entity];
	if (!object.empty_class || !object.trivial_default_constructor ||
		!object.trivial_destructor) return false;
	const FunctionInfo& function = GetFunction(conversion);
	if (!function.conversion_function || !function.constexpr_function ||
		function.definition_body == kNoNode ||
		program_->types.RemoveTopCv(EffectiveType(function.conversion_target)) !=
		program_->types.RemoveTopCv(EffectiveType(target))) return false;
	NodeId statement = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(function.definition_body);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		if (statement != kNoNode) return false;
		statement = arena_->EdgeChild(edge);
	}
	if (statement == kNoNode || !arena_->IsTag(statement, "return-statement"))
		return false;
	const NodeId expression = FirstSemanticChild(statement);
	if (expression == kNoNode || !arena_->IsTag(expression, "id-expression"))
		return false;
	const NamePath path = StructuredNamePath(expression);
	const NameId name = path.Empty() ?
		program_->names.Intern(PayloadSource(expression)) : path.Last();
	const LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding) return false;
	const BindingRecord& member = program_->bindings[
		program_->bindings[found.ordinary].canonical];
	if (member.kind != BIND_VARIABLE || member.non_static_data_member ||
		!member.constant || !IsIntegral(member.type, true)) return false;
	*result = NormalizeIntegralConstant(target, member.value);
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
		if (arena_->IsTag(child, "static-assert-declaration"))
			AnalyzeStaticAssert(child, local);
		else if (arena_->IsTag(child, "simple-declaration") ||
			arena_->IsTag(child, "alias-declaration") ||
			arena_->IsTag(child, "using-declaration"))
			AnalyzeDeclaration(child, local, detached_output, true);
		else if (arena_->IsTag(child, "compound-statement"))
			ValidateStaticAssertionsInBlock(child, local, detached_output);
		else
			for (std::uint32_t nested = arena_->FirstEdge(child);
				nested != kNoEdge; nested = arena_->NextEdge(nested))
			{
				const NodeId statement = arena_->EdgeChild(nested);
				if (arena_->IsTag(statement, "compound-statement"))
					ValidateStaticAssertionsInBlock(
						statement, local, detached_output);
			}
	}
}

void SemanticAnalyzer::ValidateOrdinaryMemberFunctionBody(BindingId function)
{
	FunctionInfo& info = GetMutableFunction(function);
	if (!info.defined || info.definition_body == kNoNode) return;
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
	const std::vector<BindingId> functions = entity_member_functions_[entity];
	for (std::size_t i = 0; i < functions.size(); ++i)
		ValidateOrdinaryMemberFunctionBody(functions[i]);
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
		arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, "braced-init-list"))
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
		if (target == kNoType)
		{
			result.node = BuildAggregateConstructionAction(
				cast_type, result.node, true);
			return materialize_if_evaluated(result);
		}
		if (reference_target) result = MaterializeTemporary(result);
		return ApplyTarget(result, target);
	}
	if (program_->entities[cast_entity].is_aggregate && argument_syntax.empty())
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
		arena_->IsTag(arguments_node, "braced-init-list"), false, true,
		arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, "braced-init-list") ?
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
		throw std::runtime_error("static_assert has no condition");
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
		throw;
	}
	--constant_expression_required_depth_;
	if (!IsIntegral(condition.type, true) || !condition.constant)
		throw std::runtime_error(
			"static_assert requires an integral constant expression");
	if (condition.value == 0)
		throw HardSemanticError("static assertion failed");
}

}
}
