#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <vector>

namespace cppgm
{
namespace semantic
{

void Analyzer::AnalyzeCompound(NodeId node, ScopeId scope,
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

NodeId Analyzer::FunctionDefinitionPart(
	NodeId node, const char* tag) const
{
	const NodeId direct = FindChild(node, tag);
	if (direct != kNoNode) return direct;
	const NodeId function_try = FindChild(node, ::cppgm::syntax::STAG_FUNCTION_TRY_BLOCK);
	return function_try == kNoNode ? kNoNode : FindChild(function_try, tag);
}

std::uint32_t Analyzer::BeginFunctionTryRegion(
	std::uint32_t function, NodeId syntax, std::uint32_t* region)
{
	*region = kNoDumpEdge;
	if (syntax == kNoNode) return function;
	*region = MakeDump(DUMP_TRY_STATEMENT);
	dump_.Add(function, *region);
	return *region;
}

ExpressionInfo Analyzer::AnalyzeStatementExpression(
	NodeId node, ScopeId scope, TypeId target)
{
	const NodeId body = FindChild(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT);
	if (body == kNoNode)
		ThrowSemanticError("statement expression has no body");
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
			arena_->IsTag(items[i], ::cppgm::syntax::STAG_EXPRESSION_STATEMENT) &&
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

void Analyzer::AnalyzeFunctionTryHandlers(NodeId node, ScopeId scope,
	std::uint32_t output_parent, FunctionTryBodyKind body_kind)
{
	if (body_kind == FUNCTION_TRY_BODY_NONE)
		ThrowInternalCompilerError("function try region has no body role");
	dump_.nodes[output_parent].function_try_body = body_kind;
	bool catches_all = false;
	std::size_t handlers = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (!arena_->IsTag(child, ::cppgm::syntax::STAG_HANDLER)) continue;
		const NodeId declaration = FindChild(child, ::cppgm::syntax::STAG_EXCEPTION_DECLARATION);
		catches_all = catches_all || (declaration != kNoNode &&
			FindChild(declaration, ::cppgm::syntax::STAG_ELLIPSIS) != kNoNode);
		AnalyzeExceptionHandler(child, scope, output_parent);
		++handlers;
	}
	if (handlers == 0)
		ThrowSemanticError("function try block has no handler");
	if (!catches_all) AppendUnwindDestructionActions(scope, output_parent);
}

}
}
