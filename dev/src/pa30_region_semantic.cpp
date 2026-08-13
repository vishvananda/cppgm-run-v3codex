#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::AnalyzeCompound(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const ScopeId block = NewScope(scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t compound = MakeDump(DUMP_COMPOUND_STATEMENT);
	dump_.Add(output_parent, compound);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (IsDeclaration(child))
			AnalyzeDeclaration(child, block, compound, true);
		else AnalyzeStatement(child, block, compound);
	}
	AppendScopeDestructionActions(block, compound, CompoundCleanupStop(scope));
}

NodeId SemanticAnalyzer::FunctionDefinitionPart(
	NodeId node, const char* tag) const
{
	const NodeId direct = FindChild(node, tag);
	if (direct != kNoNode) return direct;
	const NodeId function_try = FindChild(node, "function-try-block");
	return function_try == kNoNode ? kNoNode : FindChild(function_try, tag);
}

ExpressionInfo SemanticAnalyzer::AnalyzeStatementExpression(
	NodeId node, ScopeId scope, TypeId target)
{
	const NodeId body = FindChild(node, "compound-statement");
	if (body == kNoNode)
		throw std::runtime_error("statement expression has no body");
	std::vector<NodeId> items;
	for (std::uint32_t edge = arena_->FirstEdge(body); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		items.push_back(arena_->EdgeChild(edge));
	const ScopeId block = NewScope(
		scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t expression = MakeDump(DUMP_STATEMENT_EXPRESSION);
	ExpressionInfo value;
	value.type = program_->types.Fundamental(FUND_VOID);
	value.category = VALUE_PRVALUE;
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		const bool result_item = i + 1 == items.size() &&
			arena_->IsTag(items[i], "expression-statement") &&
			FirstSemanticChild(items[i]) != kNoNode;
		if (result_item)
		{
			value = AnalyzeExpression(FirstSemanticChild(items[i]), block);
			const std::uint32_t result = MakeDump(
				DUMP_STATEMENT_EXPRESSION_RESULT, value.type, value.category);
			dump_.Add(result, value.node);
			dump_.Add(expression, result);
		}
		else if (IsDeclaration(items[i]))
			AnalyzeDeclaration(items[i], block, expression, true);
		else AnalyzeStatement(items[i], block, expression);
	}
	AppendScopeDestructionActions(
		block, expression, CompoundCleanupStop(scope));
	dump_.nodes[expression].type = value.type;
	dump_.nodes[expression].category = value.category;
	value.node = expression;
	++expression_count_;
	return ApplyTarget(value, target);
}

void SemanticAnalyzer::AnalyzeFunctionTryHandlers(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool rethrows)
{
	dump_.nodes[output_parent].function_try_block = true;
	dump_.nodes[output_parent].function_try_rethrows = rethrows;
	bool catches_all = false;
	std::size_t handlers = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (!arena_->IsTag(child, "handler")) continue;
		const NodeId declaration = FindChild(child, "exception-declaration");
		catches_all = catches_all || (declaration != kNoNode &&
			FindChild(declaration, "ellipsis") != kNoNode);
		AnalyzeExceptionHandler(child, scope, output_parent);
		++handlers;
	}
	if (handlers == 0)
		throw std::runtime_error("function try block has no handler");
	if (!catches_all) AppendUnwindDestructionActions(scope, output_parent);
}

}
}
