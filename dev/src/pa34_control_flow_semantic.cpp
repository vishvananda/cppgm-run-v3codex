#include "pa12_semantic_detail.h"

#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::AnalyzeHostedSelectionStatement(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	if (!arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_IF_STATEMENT)) return false;
	const NodeId marker = FindChild(node, "constexpr-selection");
	const NodeId init_syntax = FindChild(node, "selection-init-statement");
	if (marker == kNoNode && init_syntax == kNoNode) return false;
	const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
		ScopePrefixId(scope));
	std::uint32_t owner = output_parent;
	if (init_syntax != kNoNode)
	{
		owner = MakeDump(DUMP_COMPOUND_STATEMENT);
		dump_.Add(output_parent, owner);
		const NodeId value = FirstSemanticChild(init_syntax);
		if (value == kNoNode)
			throw std::runtime_error("selection init-statement is empty");
		if (arena_->IsTag(value, ::cppgm::pa10_syntax_detail::STAG_ALIAS_DECLARATION))
			AnalyzeDeclaration(value, control, owner, true);
		else
		{
			const std::uint32_t init = MakeDump(DUMP_FOR_INIT_STATEMENT);
			dump_.Add(owner, init);
			if (IsDeclaration(value))
				AnalyzeDeclaration(value, control, init, true);
			else
			{
				const ExpressionInfo expression = MaterializeDiscardedClassResult(
					AnalyzeExpression(value, control));
				dump_.Add(init, expression.node);
				StageControlFullExpression(expression.node, init, control);
			}
		}
	}
	if (marker == kNoNode)
	{
		const std::uint32_t statement = MakeDump(DUMP_IF_STATEMENT);
		dump_.Add(owner, statement);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_CONDITION))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_THEN) ||
				arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_ELSE))
			{
				const std::uint32_t branch = MakeDump(
					arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_THEN) ? DUMP_THEN : DUMP_ELSE);
				dump_.Add(statement, branch);
				AnalyzeSubstatement(FirstSemanticChild(child), control, branch);
			}
		}
	}
	else
	{
		const NodeId condition_syntax = FindChild(node, "condition");
		const NodeId expression_syntax = condition_syntax == kNoNode ?
			kNoNode : FirstSemanticChild(condition_syntax);
		if (expression_syntax == kNoNode ||
			arena_->IsTag(expression_syntax, ::cppgm::pa10_syntax_detail::STAG_CONDITION_DECLARATION))
			throw std::runtime_error(
				"constexpr if requires an expression condition");
		++constant_expression_required_depth_;
		ExpressionInfo condition;
		try
		{
			condition = ApplyContextualBool(
				AnalyzeExpression(expression_syntax, control));
		}
		catch (...)
		{
			--constant_expression_required_depth_;
			throw;
		}
		--constant_expression_required_depth_;
		if (!condition.constant || !IsIntegral(condition.type, true))
			throw std::runtime_error(
				"constexpr if requires an integral constant expression");
		const bool select_then = condition.value != 0;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_THEN) &&
				!arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_ELSE)) continue;
			const bool is_then = arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_THEN);
			if (is_then == select_then)
				AnalyzeSubstatement(
					FirstSemanticChild(child), control, owner);
		}
	}
	AppendScopeDestructionActions(control, owner, scope);
	return true;
}

}
}
