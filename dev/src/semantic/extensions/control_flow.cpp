#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"


namespace cppgm
{
namespace semantic
{

bool Analyzer::AnalyzeHostedSelectionStatement(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	if (!arena_->IsTag(node, ::cppgm::syntax::STAG_IF_STATEMENT)) return false;
	const NodeId marker = FindChild(node, ::cppgm::syntax::STAG_CONSTEXPR_SELECTION);
	const NodeId init_syntax = FindChild(node, ::cppgm::syntax::STAG_SELECTION_INIT_STATEMENT);
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
			ThrowSemanticError("selection init-statement is empty");
		if (arena_->IsTag(value, ::cppgm::syntax::STAG_ALIAS_DECLARATION))
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
			if (arena_->IsTag(child, ::cppgm::syntax::STAG_CONDITION))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, ::cppgm::syntax::STAG_THEN) ||
				arena_->IsTag(child, ::cppgm::syntax::STAG_ELSE))
			{
				const std::uint32_t branch = MakeDump(
					arena_->IsTag(child, ::cppgm::syntax::STAG_THEN) ? DUMP_THEN : DUMP_ELSE);
				dump_.Add(statement, branch);
				AnalyzeSubstatement(FirstSemanticChild(child), control, branch);
			}
		}
	}
	else
	{
		const NodeId condition_syntax = FindChild(node, ::cppgm::syntax::STAG_CONDITION);
		const NodeId expression_syntax = condition_syntax == kNoNode ?
			kNoNode : FirstSemanticChild(condition_syntax);
		if (expression_syntax == kNoNode ||
			arena_->IsTag(expression_syntax, ::cppgm::syntax::STAG_CONDITION_DECLARATION))
			ThrowSemanticError(
				"constexpr if requires an expression condition");
		ExpressionInfo condition;
		{
			ScopedCounterIncrement required(
				&constant_expression_required_depth_);
			condition = ApplyContextualBool(
				AnalyzeExpression(expression_syntax, control));
		}
		if (!condition.constant || !IsIntegral(condition.type, true))
			ThrowSemanticError(
				"constexpr if requires an integral constant expression");
		const bool select_then = condition.value != 0;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, ::cppgm::syntax::STAG_THEN) &&
				!arena_->IsTag(child, ::cppgm::syntax::STAG_ELSE)) continue;
			const bool is_then = arena_->IsTag(child, ::cppgm::syntax::STAG_THEN);
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
