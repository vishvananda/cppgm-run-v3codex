#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

namespace cppgm
{
namespace semantic
{

void Analyzer::AnalyzeCondition(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool switch_condition)
{
	const std::uint32_t condition = MakeDump(DUMP_CONDITION);
	dump_.Add(output_parent, condition);
	NodeId declaration_node = node;
	const NodeId first_child = FirstSemanticChild(node);
	if (first_child != kNoNode &&
		arena_->IsTag(first_child, ::cppgm::syntax::STAG_CONDITION_DECLARATION))
		declaration_node = first_child;
	const NodeId specifiers = FindChild(declaration_node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	if (specifiers != kNoNode)
	{
		const NodeId declarator = FindChild(declaration_node, ::cppgm::syntax::STAG_DECLARATOR);
		const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), true);
		ExpressionInfo placeholder_initializer;
		DeclaratorInfo parsed = BuildVariableDeclarator(declaration_node,
			declarator, spec, scope, true, &placeholder_initializer);
		const BindingId binding = program_->AddBinding(scope, BIND_VARIABLE,
			parsed.name, parsed.type);
		const NodeId initializer = FindChild(declaration_node, ::cppgm::syntax::STAG_INITIALIZER);
		ExpressionInfo value;
		if (spec.placeholder_auto)
			value = placeholder_initializer;
		else
			value = AnalyzeVariableInitializer(initializer,
				scope, parsed.type, true);
		if (constexpr_evaluation_depth_ != 0 && value.constant &&
			IsIntegral(parsed.type, true))
		{
			program_->bindings[binding].constant = true;
			program_->bindings[binding].value =
				NormalizeIntegralConstant(parsed.type, value.value);
		}
		const std::uint32_t declaration = MakeDump(DUMP_CONDITION_DECLARATION);
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		dump_.Add(variable, value.node);
		dump_.Add(declaration, variable);
		dump_.Add(condition, declaration);
		if (switch_condition)
		{
			if (!IsIntegral(parsed.type, true) &&
				EntityOf(parsed.type) == kNoEntity)
				ThrowSemanticError("invalid switch condition");
			if (!IsIntegral(parsed.type, true))
			{
				ExpressionInfo declared;
				declared.node = MakeDump(DUMP_ID_EXPRESSION, parsed.type,
					VALUE_LVALUE, parsed.name, binding);
				declared.type = parsed.type;
				declared.category = VALUE_LVALUE;
				declared.binding = binding;
				++expression_count_;
				const ExpressionInfo converted = ApplyExplicitConversion(declared,
					program_->types.Fundamental(FUND_INT));
				dump_.Add(condition, converted.node);
			}
		}
		else if (!IsArithmetic(parsed.type) && !IsPointer(parsed.type) &&
			!IsMemberPointer(parsed.type))
		{
			ExpressionInfo declared;
			declared.node = MakeDump(DUMP_ID_EXPRESSION, parsed.type,
				VALUE_LVALUE, parsed.name, binding);
			declared.type = parsed.type;
			declared.category = VALUE_LVALUE;
			declared.binding = binding;
			++expression_count_;
			const ExpressionInfo converted = ApplyExplicitConversion(declared,
				program_->types.Fundamental(FUND_BOOL));
			dump_.Add(condition, converted.node);
		}
		RegisterConditionLifetime(scope, binding, parsed.type, value, condition);
		return;
	}
	ExpressionInfo value = AnalyzeExpression(FirstSemanticChild(node), scope);
	if (switch_condition)
	{
		if (!IsIntegral(value.type, true))
			ThrowSemanticError("invalid switch condition");
	}
	else if (!IsArithmetic(value.type) && !IsPointer(value.type) &&
		!IsNullptr(value.type) && !IsMemberPointer(value.type))
		value = ApplyExplicitConversion(value,
			program_->types.Fundamental(FUND_BOOL));
	dump_.Add(condition, value.node);
	const std::size_t first_cleanup_edge = dump_.edges.size();
	AppendFullExpressionDestructionActions(value.node, condition);
	const std::uint32_t first = dump_.nodes[condition].first_edge;
	if (first != kNoDumpEdge && dump_.edges[first].next != kNoDumpEdge)
	{
		dump_.nodes[condition].full_expression_staging = true;
		MarkFullExpressionCalls(value.node);
		AppendUnwindDestructionActions(scope, condition);
	}
	else if (HasActiveInitializerListBacking(scope))
	{
		dump_.nodes[condition].full_expression_staging = true;
		dump_.nodes[condition].initializer_list_lifetime_observation = true;
		MarkInitializerListLifetimeCalls(value.node);
	}
	else if (dump_.edges.size() == first_cleanup_edge)
		StageExceptionalFullExpression(value.node, condition, scope);
}

}
}
