#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::AnalyzeExceptionStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, "throw-statement"))
	{
		dump_.Add(output_parent, AnalyzeThrowExpression(node, scope).node);
		return true;
	}
	if (!arena_->IsTag(node, "try-block")) return false;
	AnalyzeTryStatement(node, scope, output_parent);
	return true;
}

void SemanticAnalyzer::StageExceptionalFullExpression(
	std::uint32_t expression, std::uint32_t statement, ScopeId scope, bool force)
{
	force = StageNestedTemplateTemporaryCleanup(expression, statement) || force;
	if (!force && InitializationActionsAreNonthrowing(expression)) return;
	const ScopeId stop = exception_cleanup_stops_.empty() ? kNoScope :
		exception_cleanup_stops_.back();
	if (!HasUnwindDestructionActions(scope, stop)) return;
	const std::size_t first_edge = dump_.edges.size();
	ScopeId segment = scope;
	bool first_handler = true;
	for (std::size_t i = exception_handler_cleanup_stops_.size(); i != 0; --i)
	{
		const ScopeId handler_stop = exception_handler_cleanup_stops_[i - 1];
		bool crossed = false;
		for (ScopeId current = segment;
			current != kNoScope && current != stop;
			current = current < scope_parents_.size() ?
				scope_parents_[current] : kNoScope)
			if (current == handler_stop)
			{
				crossed = true;
				break;
			}
		if (!crossed) break;
		AppendUnwindDestructionActions(segment, statement, handler_stop);
		const std::uint32_t exit = MakeDump(DUMP_DESTRUCTOR_ACTION);
		DumpNode& action = dump_.nodes[exit];
		action.unwind_only = true;
		action.exception_handler_exit = true;
		action.exception_cleanup_region_exit = first_handler;
		dump_.Add(statement, exit);
		first_handler = false;
		segment = handler_stop;
	}
	AppendUnwindDestructionActions(segment, statement, stop);
	if (dump_.edges.size() == first_edge) return;
	dump_.nodes[statement].full_expression_staging = true;
	for (std::size_t edge = first_edge; edge < dump_.edges.size(); ++edge)
	{
		DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind == DUMP_DESTRUCTOR_ACTION && action.unwind_only)
			action.full_expression_staging = true;
	}
}

void SemanticAnalyzer::StageAutomaticScalarInitializerException(
	std::uint32_t expression, std::uint32_t variable, ScopeId scope,
	BindingId binding, TypeId type, bool eligible)
{
	if (!eligible ||
		program_->bindings[binding].storage_class != STORAGE_CLASS_NONE ||
		program_->types.IsReference(type) || IsInitializerListType(type) ||
		IsClassObjectType(type)) return;
	StageExceptionalFullExpression(expression, variable, scope);
}

void SemanticAnalyzer::StageControlFullExpression(
	std::uint32_t expression, std::uint32_t statement, ScopeId scope)
{
	const std::size_t edge_count = dump_.edges.size();
	AppendFullExpressionDestructionActions(expression, statement);
	if (dump_.edges.size() == edge_count)
	{
		StageExceptionalFullExpression(expression, statement, scope);
		return;
	}
	MarkFullExpressionCalls(expression);
	AppendUnwindDestructionActions(scope, statement);
}

bool SemanticAnalyzer::HasUnwindDestructionActions(ScopeId scope,
	ScopeId stop_exclusive) const
{
	ScopeId current = scope < nearest_lifetime_scopes_.size() ?
		nearest_lifetime_scopes_[scope] : kNoScope;
	while (current != kNoScope && current != stop_exclusive)
	{
		if (current >= scope_lifetimes_.size())
			throw std::logic_error("indexed lifetime scope has no obligations");
		if (!scope_lifetimes_[current].empty()) return true;
		const ScopeId parent = scope_parents_[current];
		current = parent != kNoScope &&
			parent < nearest_lifetime_scopes_.size() ?
			nearest_lifetime_scopes_[parent] : kNoScope;
	}
	return false;
}

ExpressionInfo SemanticAnalyzer::AnalyzeThrowExpression(
	NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode)
	{
		if (exception_handler_depth_ == 0)
			throw std::runtime_error("rethrow outside an exception handler");
		ExpressionInfo result;
		result.node = MakeDump(DUMP_THROW_EXPRESSION,
			program_->types.Fundamental(FUND_VOID), VALUE_PRVALUE);
		result.type = program_->types.Fundamental(FUND_VOID);
		result.category = VALUE_PRVALUE;
		++expression_count_;
		RecordExpressionFacts(result);
		return result;
	}

	ExpressionInfo value = AnalyzeExpression(operand, scope);
	const TypeId thrown_type = program_->types.RemoveTopCv(Decay(value.type));
	if (IsVoid(thrown_type))
		throw std::runtime_error("cannot throw an expression of type void");
	if (value.type != thrown_type)
		value = ApplyExplicitConversion(value, thrown_type);
	const EntityId entity = DestructedEntity(thrown_type);
	if (entity != kNoEntity &&
		!program_->entities[entity].trivial_destructor)
	{
		const BindingId destructor = DestructorForType(thrown_type);
		if (destructor == kNoBinding ||
			GetFunction(destructor).deleted_destructor ||
			!CanAccessMember(destructor, entity))
			throw std::runtime_error(
				"thrown exception object is not destructible");
		DemandFunction(destructor);
	}
	ExpressionInfo result;
	result.node = MakeDump(DUMP_THROW_EXPRESSION,
		program_->types.Fundamental(FUND_VOID), VALUE_PRVALUE);
	dump_.nodes[result.node].operand_type = thrown_type;
	dump_.Add(result.node, value.node);
	result.type = program_->types.Fundamental(FUND_VOID);
	result.category = VALUE_PRVALUE;
	++expression_count_;
	RecordExpressionFacts(result);
	return result;
}

void SemanticAnalyzer::AnalyzeExceptionHandler(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const ScopeId handler_scope = NewScope(
		scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t handler = MakeDump(DUMP_HANDLER);
	dump_.Add(output_parent, handler);
	const NodeId declaration = FindChild(node, "exception-declaration");
	if (declaration == kNoNode)
		throw std::runtime_error("exception handler has no declaration");
	if (FindChild(declaration, "ellipsis") == kNoNode)
	{
		const NodeId specifiers = FindChild(declaration, "decl-specifier-seq");
		if (specifiers == kNoNode)
			throw std::runtime_error("exception handler has no type");
		const NodeId declarator = FindChild(declaration, "declarator");
		const SpecInfo spec = BuildSpecifiers(specifiers, handler_scope,
			std::string(), declarator != kNoNode, true);
		DeclaratorInfo parsed;
		parsed.type = spec.type;
		if (declarator != kNoNode)
			parsed = BuildDeclarator(declarator, spec.type, handler_scope);
		if (parsed.type == kNoType || IsVoid(parsed.type) ||
			program_->types.Get(EffectiveType(parsed.type)).kind == TYPE_ARRAY ||
			program_->types.Get(EffectiveType(parsed.type)).kind == TYPE_FUNCTION)
			throw std::runtime_error("invalid exception handler type");
		dump_.nodes[handler].type = parsed.type;
		dump_.nodes[handler].operand_type =
			program_->types.RemoveTopCv(EffectiveType(parsed.type));
		if (parsed.name != 0)
		{
			const BindingId binding = program_->AddBinding(handler_scope,
				BIND_VARIABLE, parsed.name, parsed.type);
			dump_.nodes[handler].binding = binding;
			dump_.nodes[handler].text = parsed.name;
			dump_.Add(handler, MakeDump(DUMP_VARIABLE, parsed.type,
				VALUE_NONE, parsed.name, binding));
			AddLifetimeObligation(handler_scope, binding, parsed.type);
		}
	}
	const NodeId body = FindChild(node, "compound-statement");
	if (body == kNoNode)
		throw std::runtime_error("exception handler has no body");
	++exception_handler_depth_;
	exception_handler_cleanup_stops_.push_back(scope);
	AnalyzeCompound(body, handler_scope, handler);
	exception_handler_cleanup_stops_.pop_back();
	--exception_handler_depth_;
	AppendScopeDestructionActions(handler_scope, handler, scope);
}

void SemanticAnalyzer::AnalyzeTryStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const std::uint32_t statement = MakeDump(DUMP_TRY_STATEMENT);
	dump_.Add(output_parent, statement);
	bool saw_body = false;
	bool catches_all = false;
	std::size_t handler_count = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "compound-statement") && !saw_body)
		{
			exception_cleanup_stops_.push_back(scope);
			AnalyzeCompound(child, scope, statement);
			exception_cleanup_stops_.pop_back();
			saw_body = true;
		}
		else if (arena_->IsTag(child, "handler"))
		{
			const NodeId declaration = FindChild(child, "exception-declaration");
			catches_all = catches_all || (declaration != kNoNode &&
				FindChild(declaration, "ellipsis") != kNoNode);
			AnalyzeExceptionHandler(child, scope, statement);
			++handler_count;
		}
	}
	if (!saw_body || handler_count == 0)
		throw std::runtime_error("invalid try statement");
	if (!catches_all) AppendUnwindDestructionActions(scope, statement);
}

}
}
