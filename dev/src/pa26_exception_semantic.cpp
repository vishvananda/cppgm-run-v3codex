#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::DemandConstructorUnwindDestructors(
	std::uint32_t body)
{
	if (!complete_constructor_unwind_)
	{
		bool throwing = false;
		std::vector<std::uint32_t> pending(1, body);
		while (!pending.empty() && !throwing)
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			const DumpNode& record = dump_.nodes[node];
			throwing = record.kind == DUMP_THROW_EXPRESSION;
			if (!throwing && record.kind == DUMP_CONSTRUCTOR_ACTION &&
				record.binding != kNoBinding)
			{
				std::vector<NodeId> syntax(
					1, GetFunction(record.binding).definition_body);
				while (!syntax.empty() && !throwing)
				{
					const NodeId current = syntax.back();
					syntax.pop_back();
					if (current == kNoNode) continue;
					throwing = arena_->IsTag(current, "throw-expression") ||
						arena_->IsTag(current, "throw-statement");
					for (std::uint32_t edge = arena_->FirstEdge(current);
						!throwing && edge != kNoEdge;
						edge = arena_->NextEdge(edge))
						syntax.push_back(arena_->EdgeChild(edge));
				}
			}
			for (std::uint32_t edge = record.first_edge;
				!throwing && edge != kNoDumpEdge; edge = dump_.edges[edge].next)
				pending.push_back(dump_.edges[edge].child);
		}
		dump_.nodes[body].unwind_only = throwing;
	}
	else dump_.nodes[body].unwind_only =
		!InitializationActionsAreNonthrowing(body);
	if (!dump_.nodes[body].unwind_only) return;
	for (std::uint32_t edge = dump_.nodes[body].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		const DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if ((action.kind == DUMP_INITIALIZER_ACTION ||
			action.kind == DUMP_BASE_INITIALIZER_ACTION) &&
			action.selected_binding != kNoBinding)
			DemandFunction(action.selected_binding);
	}
}

std::uint32_t SemanticAnalyzer::MakeTemporaryDestructorAction(
	std::uint32_t temporary, BindingId destructor,
	bool preserve_nontrivial_action)
{
	if (temporary == kNoDumpEdge || temporary >= dump_.nodes.size() ||
		dump_.nodes[temporary].kind != DUMP_TEMPORARY_OBJECT)
		throw std::logic_error("temporary destruction has no object identity");
	const TypeId type = dump_.nodes[temporary].type;
	if (IsInitializerListType(type)) return kNoDumpEdge;
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity) return kNoDumpEdge;
	if (!program_->entities[entity].destructible)
		throw std::runtime_error("temporary type is not destructible");
	if (destructor == kNoBinding) destructor = DestructorForType(type);
	if (destructor == kNoBinding)
		throw std::logic_error("temporary class has no destructor identity");
	if (!CanAccessMember(destructor, entity))
		throw std::runtime_error("inaccessible temporary destructor");
	if (program_->entities[entity].trivial_destructor) return kNoDumpEdge;
	const bool dependent_template_object =
		program_->entities[entity].template_argument_count != 0;
	if (!preserve_nontrivial_action && !dependent_template_object &&
		IsElidableAutomaticDestructor(destructor) &&
		!dump_.nodes[temporary].control_dependent_temporary &&
		!dump_.nodes[temporary].projected_subobject_temporary)
		return kNoDumpEdge;
	const std::uint32_t action = MakeDestructorAction(
		type, destructor, kNoBinding, 0, false);
	dump_.nodes[action].lifetime_object = temporary;
	dump_.nodes[action].lifetime_branch_owner =
		dump_.nodes[temporary].lifetime_branch_owner;
	dump_.nodes[action].lifetime_branch_child =
		dump_.nodes[temporary].lifetime_branch_child;
	if (current_function_context_ == kNoBinding ||
		dump_.nodes[action].lifetime_branch_owner == kNoDumpEdge)
		DemandFunction(destructor);
	return action;
}

void SemanticAnalyzer::MarkFullExpressionCalls(std::uint32_t node,
	bool managed_cleanup, bool allocation_call)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return;
	DumpNode& record = dump_.nodes[node];
	bool stage = true;
	if (allocation_call && record.kind == DUMP_CALL_EXPRESSION &&
		record.first_edge != kNoDumpEdge)
	{
		const DumpNode& callee =
			dump_.nodes[dump_.edges[record.first_edge].child];
		stage = callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			!FunctionIsNonthrowing(callee.binding);
	}
	if (stage) record.full_expression_staging = true;
	if (managed_cleanup && record.kind == DUMP_TEMPORARY_OBJECT)
		record.managed_full_expression_cleanup = true;
	bool first = true;
	for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
		edge = dump_.edges[edge].next)
	{
		MarkFullExpressionCalls(dump_.edges[edge].child, managed_cleanup,
			record.kind == DUMP_NEW_EXPRESSION && first);
		first = false;
	}
}

void SemanticAnalyzer::MarkDefaultArgumentSubtree(std::uint32_t node)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return;
	std::vector<std::uint32_t> pending(1, node);
	while (!pending.empty())
	{
		const std::uint32_t current = pending.back();
		pending.pop_back();
		dump_.nodes[current].default_argument = true;
		for (std::uint32_t edge = dump_.nodes[current].first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
}

void SemanticAnalyzer::AppendFullExpressionDestructionActions(
	std::uint32_t expression, std::uint32_t output_parent,
	bool preserve_nontrivial_actions)
{
	std::vector<std::uint32_t> temporaries;
	CollectTemporaryObjects(expression, &temporaries);
	const bool potentially_throwing =
		!InitializationActionsAreNonthrowing(expression);
	const bool requested_explicit_cleanup =
		dump_.nodes[expression].full_expression_staging ||
		dump_.nodes[expression].eager_full_expression_cleanup;
	bool contains_default_argument = false;
	for (std::size_t i = 0; i < temporaries.size(); ++i)
		contains_default_argument = contains_default_argument ||
			dump_.nodes[temporaries[i]].default_argument;
	preserve_nontrivial_actions = !temporaries.empty() &&
		(preserve_nontrivial_actions || contains_default_argument) &&
		potentially_throwing;
	const bool managed_expression =
		(preserve_nontrivial_actions || requested_explicit_cleanup) &&
		dump_.nodes[expression].kind != DUMP_THROW_EXPRESSION;
	bool appended_managed_action = false;
	for (std::size_t i = temporaries.size(); i != 0; --i)
	{
		std::uint32_t action =
			MakeTemporaryDestructorAction(temporaries[i - 1]);
		if (action == kNoDumpEdge && preserve_nontrivial_actions)
			action = MakeTemporaryDestructorAction(
				temporaries[i - 1], kNoBinding, true);
		if (action == kNoDumpEdge) continue;
		const EntityId action_entity =
			DestructedEntity(dump_.nodes[action].operand_type);
		const bool specialization_action = action_entity != kNoEntity &&
			program_->entities[action_entity].template_argument_count != 0;
		const bool polymorphic_specialization_action =
			specialization_action &&
			program_->entities[action_entity].polymorphic_class;
		const bool managed_action =
			(managed_expression ||
			 (polymorphic_specialization_action && potentially_throwing)) &&
			!dump_.nodes[temporaries[i - 1]].initializer_list_backing;
		dump_.nodes[action].full_expression_staging = true;
		dump_.nodes[action].managed_full_expression_cleanup = managed_action;
		dump_.nodes[action].eager_full_expression_cleanup =
			(potentially_throwing &&
			 (!specialization_action || polymorphic_specialization_action)) ||
			requested_explicit_cleanup;
		dump_.Add(output_parent, action);
		appended_managed_action = appended_managed_action || managed_action;
	}
	if (appended_managed_action)
	{
		dump_.nodes[output_parent].full_expression_staging = true;
		MarkFullExpressionCalls(expression, true);
	}
}

void SemanticAnalyzer::FinalizeStaticallyUnreachableBranchCleanup(
	std::uint32_t function_definition)
{
	if (function_definition == kNoDumpEdge ||
		function_definition >= dump_.nodes.size()) return;
	if (++branch_cleanup_scan_epoch_ == 0)
	{
		branch_cleanup_node_epochs_.assign(
			branch_cleanup_node_epochs_.size(), 0);
		branch_cleanup_binding_epochs_.assign(
			branch_cleanup_binding_epochs_.size(), 0);
		branch_cleanup_scan_epoch_ = 1;
	}
	const std::uint32_t epoch = branch_cleanup_scan_epoch_;
	branch_cleanup_node_epochs_.resize(dump_.nodes.size(), 0);
	branch_cleanup_binding_epochs_.resize(program_->bindings.size(), 0);
	branch_cleanup_binding_uses_.resize(program_->bindings.size(), 0);
	branch_cleanup_literal_truth_.resize(program_->bindings.size(), -1);
	std::vector<std::uint32_t> pending(1, function_definition);
	std::vector<std::uint32_t> actions;
	while (!pending.empty())
	{
		const std::uint32_t node = pending.back();
		pending.pop_back();
		if (node >= dump_.nodes.size() ||
			branch_cleanup_node_epochs_[node] == epoch) continue;
		branch_cleanup_node_epochs_[node] = epoch;
		const DumpNode& record = dump_.nodes[node];
		if (record.kind == DUMP_ID_EXPRESSION &&
			record.binding != kNoBinding &&
			record.binding < branch_cleanup_binding_uses_.size())
		{
			if (branch_cleanup_binding_epochs_[record.binding] != epoch)
			{
				branch_cleanup_binding_epochs_[record.binding] = epoch;
				branch_cleanup_binding_uses_[record.binding] = 0;
				branch_cleanup_literal_truth_[record.binding] = -1;
			}
			if (branch_cleanup_binding_uses_[record.binding] !=
				std::numeric_limits<std::uint32_t>::max())
				++branch_cleanup_binding_uses_[record.binding];
		}
		if (record.kind == DUMP_DESTRUCTOR_ACTION &&
			record.lifetime_branch_owner != kNoDumpEdge &&
			record.lifetime_branch_child != kNoDumpEdge)
			actions.push_back(node);
		if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding &&
			record.binding < branch_cleanup_literal_truth_.size() &&
			program_->bindings[record.binding].kind == BIND_VARIABLE &&
			program_->bindings[record.binding].storage_class == STORAGE_CLASS_NONE &&
			IsIntegral(record.type, true) && record.first_edge != kNoDumpEdge &&
			dump_.edges[record.first_edge].next == kNoDumpEdge)
		{
			if (branch_cleanup_binding_epochs_[record.binding] != epoch)
			{
				branch_cleanup_binding_epochs_[record.binding] = epoch;
				branch_cleanup_binding_uses_[record.binding] = 0;
				branch_cleanup_literal_truth_[record.binding] = -1;
			}
			const DumpNode& initializer =
				dump_.nodes[dump_.edges[record.first_edge].child];
			if (initializer.kind == DUMP_LITERAL && initializer.constant)
				branch_cleanup_literal_truth_[record.binding] =
					initializer.constant_value == 0 ? 0 : 1;
		}
		for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
			edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
	for (std::size_t i = 0; i < actions.size(); ++i)
	{
		DumpNode& action = dump_.nodes[actions[i]];
		const std::uint32_t owner = action.lifetime_branch_owner;
		if (owner >= dump_.nodes.size() ||
			dump_.nodes[owner].kind != DUMP_CONDITIONAL_EXPRESSION) continue;
		std::uint32_t children[3] = { kNoDumpEdge, kNoDumpEdge, kNoDumpEdge };
		std::size_t count = 0;
		for (std::uint32_t edge = dump_.nodes[owner].first_edge;
			edge != kNoDumpEdge && count != 3; edge = dump_.edges[edge].next)
			children[count++] = dump_.edges[edge].child;
		if (count != 3 || children[0] >= dump_.nodes.size()) continue;
		const DumpNode& condition = dump_.nodes[children[0]];
		if (condition.kind != DUMP_ID_EXPRESSION ||
			condition.binding == kNoBinding ||
			condition.binding >= branch_cleanup_binding_uses_.size() ||
			branch_cleanup_binding_epochs_[condition.binding] != epoch ||
			branch_cleanup_binding_uses_[condition.binding] != 1 ||
			branch_cleanup_literal_truth_[condition.binding] < 0) continue;
		const bool truth =
			branch_cleanup_literal_truth_[condition.binding] != 0;
		action.lifetime_branch_statically_unreachable =
			(!truth && action.lifetime_branch_child == children[1]) ||
			(truth && action.lifetime_branch_child == children[2]);
	}
	for (std::size_t i = 0; i < actions.size(); ++i)
		if (!dump_.nodes[actions[i]].lifetime_branch_statically_unreachable)
			DemandFunction(dump_.nodes[actions[i]].binding);
}

bool SemanticAnalyzer::RequiresManagedConditionalFullExpression(
	std::uint32_t expression, std::size_t first_edge)
{
	if (!InitializationActionsAreNonthrowing(expression)) return true;
	for (std::size_t edge = first_edge; edge < dump_.edges.size(); ++edge)
	{
		const DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind == DUMP_DESTRUCTOR_ACTION &&
			action.lifetime_object != kNoDumpEdge &&
			dump_.nodes[action.lifetime_object].conditionally_constructed)
			return true;
	}
	return false;
}

bool SemanticAnalyzer::CollectTemporaryObjects(std::uint32_t node,
	std::vector<std::uint32_t>* temporaries)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return false;
	const DumpNode& root = dump_.nodes[node];
	bool direct_branch_root = root.kind == DUMP_CONDITIONAL_EXPRESSION;
	if (root.kind == DUMP_BINARY_EXPRESSION)
		direct_branch_root =
			root.logical_operation != LOGICAL_OPERATION_NONE;
	return CollectTemporaryObjectsImpl(node, temporaries, false,
		direct_branch_root ? node : kNoDumpEdge, kNoDumpEdge, 0, false);
}

bool SemanticAnalyzer::CollectTemporaryObjectsImpl(std::uint32_t node,
	std::vector<std::uint32_t>* temporaries, bool conditionally_evaluated,
	std::uint32_t branch_owner, std::uint32_t branch_child,
	std::size_t branch_depth, bool projected_subobject)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return false;
	++temporary_dependency_visits_;
	DumpNode& record = dump_.nodes[node];
	if (record.kind == DUMP_CONDITIONAL_ARM) return false;
	const bool short_circuit = record.kind == DUMP_BINARY_EXPRESSION &&
		record.logical_operation != LOGICAL_OPERATION_NONE;
	bool control_dependent = record.kind == DUMP_CONDITIONAL_EXPRESSION ||
		short_circuit;
	std::size_t child_index = 0;
	for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
		edge = dump_.edges[edge].next, ++child_index)
	{
		const std::uint32_t child = dump_.edges[edge].child;
		const bool branch_only =
			(short_circuit && child_index == 1) ||
			(record.kind == DUMP_CONDITIONAL_EXPRESSION && child_index != 0);
		control_dependent = CollectTemporaryObjectsImpl(child, temporaries,
			conditionally_evaluated || branch_only, branch_owner,
			branch_only && branch_depth == 0 ? child : branch_child,
			branch_depth + (branch_only ? 1 : 0),
			projected_subobject ||
				(record.kind == DUMP_CAST_EXPRESSION &&
				 record.base_projection_count != 0)) || control_dependent;
	}
	if (record.kind == DUMP_TEMPORARY_OBJECT)
	{
		record.lifetime_branch_owner = kNoDumpEdge;
		record.lifetime_branch_child = kNoDumpEdge;
		if (conditionally_evaluated)
		{
			record.conditionally_constructed = true;
			// One root guard edge gives lowering a stable path-local cleanup
			// boundary. Deeper dependence retains runtime lifetime state.
			if (branch_owner != kNoDumpEdge && branch_depth == 1 &&
				branch_child != kNoDumpEdge)
			{
				record.lifetime_branch_owner = branch_owner;
				record.lifetime_branch_child = branch_child;
			}
		}
		if (control_dependent) record.control_dependent_temporary = true;
		if (projected_subobject) record.projected_subobject_temporary = true;
		temporaries->push_back(node);
	}
	return control_dependent;
}

bool SemanticAnalyzer::AnalyzeExceptionStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, "throw-statement"))
	{
		const ExpressionInfo expression = AnalyzeThrowExpression(node, scope);
		AppendFullExpressionDestructionActions(expression.node, expression.node);
		StageExceptionalFullExpression(
			expression.node, expression.node, scope, true);
		const std::uint32_t first = dump_.nodes[expression.node].first_edge;
		if (first != kNoDumpEdge && dump_.edges[first].next != kNoDumpEdge)
		{
			dump_.nodes[expression.node].full_expression_staging = true;
			MarkFullExpressionCalls(dump_.edges[first].child, true);
		}
		dump_.Add(output_parent, expression.node);
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
	MarkFullExpressionCalls(expression);
	for (std::size_t edge = first_edge; edge < dump_.edges.size(); ++edge)
	{
		DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind == DUMP_DESTRUCTOR_ACTION && action.unwind_only)
			action.full_expression_staging = true;
	}
}

void SemanticAnalyzer::StageAutomaticInitializerException(
	std::uint32_t expression, std::uint32_t variable, ScopeId scope,
	BindingId binding, TypeId type, bool eligible)
{
	if (!eligible ||
		program_->bindings[binding].storage_class != STORAGE_CLASS_NONE ||
		program_->types.IsReference(type) || IsInitializerListType(type)) return;
	const ScopeId stop = exception_cleanup_stops_.empty() ? kNoScope :
		exception_cleanup_stops_.back();
	dump_.nodes[variable].enclosing_lifetime_cleanup =
		HasEnclosingNontrivialObjectLifetime(scope, stop);
	if (!HasUnwindDestructionActions(scope, stop)) return;
	if (InitializationActionsAreNonthrowing(expression)) return;
	AppendFullExpressionDestructionActions(expression, variable);
	StageExceptionalFullExpression(expression, variable, scope, true);
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

bool SemanticAnalyzer::HasEnclosingNontrivialObjectLifetime(
	ScopeId scope, ScopeId stop_exclusive) const
{
	++enclosing_lifetime_queries_;
	const std::uint32_t active =
		scope < scope_nontrivial_object_lifetime_prefixes_.size() ?
			scope_nontrivial_object_lifetime_prefixes_[scope] : 0;
	const std::uint32_t stopped =
		stop_exclusive != kNoScope &&
		stop_exclusive < scope_nontrivial_object_lifetime_prefixes_.size() ?
			scope_nontrivial_object_lifetime_prefixes_[stop_exclusive] : 0;
	if (active < stopped)
		throw std::logic_error(
			"enclosing lifetime prefix is not monotonic");
	return active != stopped;
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
	BindingId destructor = kNoBinding;
	const EntityId entity = DestructedEntity(thrown_type);
	if (entity != kNoEntity &&
		!program_->entities[entity].trivial_destructor)
	{
		destructor = DestructorForType(thrown_type);
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
	dump_.nodes[result.node].selected_binding = destructor;
	std::uint32_t initializer = IsClassObjectType(thrown_type) ?
		BuildClassValueConstructorAction(
			thrown_type, value, true, false) : value.node;
	while (IsClassObjectType(thrown_type))
	{
		const DumpNode& candidate = dump_.nodes[initializer];
		const std::uint32_t edge = candidate.first_edge;
		if ((candidate.kind == DUMP_CLASS_VALUE_TRANSFER ||
			 candidate.kind == DUMP_TEMPORARY_OBJECT) &&
			edge != kNoDumpEdge && dump_.edges[edge].next == kNoDumpEdge)
		{
			initializer = dump_.edges[edge].child;
			continue;
		}
		if (candidate.kind != DUMP_CONSTRUCTOR_ACTION ||
			candidate.binding == kNoBinding || edge == kNoDumpEdge ||
			dump_.edges[edge].next != kNoDumpEdge)
			break;
		const FunctionInfo& selected = GetFunction(candidate.binding);
		const std::uint32_t temporary = dump_.edges[edge].child;
		const std::uint32_t recipe =
			dump_.nodes[temporary].kind == DUMP_TEMPORARY_OBJECT ?
				dump_.nodes[temporary].first_edge : kNoDumpEdge;
		if ((selected.special_member != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
			 selected.special_member != SPECIAL_MEMBER_MOVE_CONSTRUCTOR) ||
			recipe == kNoDumpEdge || dump_.edges[recipe].next != kNoDumpEdge)
			break;
		initializer = dump_.edges[recipe].child;
	}
	if (IsClassObjectType(thrown_type) &&
		dump_.nodes[initializer].kind == DUMP_CONSTRUCTOR_ACTION)
	{
		dump_.nodes[initializer].elide_empty_constructor = false;
		DemandFunction(dump_.nodes[initializer].binding);
	}
	dump_.Add(result.node, initializer);
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
		if (!program_->types.IsReference(parsed.type) &&
			IsClassObjectType(parsed.type))
		{
			EnsureClassDefinition(parsed.type);
			const EntityId entity = DestructedEntity(parsed.type);
			const BindingId copy = ConstructorForSubobject(
				parsed.type, SPECIAL_MEMBER_COPY_CONSTRUCTOR);
			if (entity == kNoEntity || copy == kNoBinding ||
				GetFunction(copy).deleted_special_member ||
				!CanAccessMember(copy, entity))
				throw std::runtime_error(
					"exception handler object is not copy constructible");
			dump_.nodes[handler].selected_binding = copy;
			dump_.nodes[handler].trivial_special_member_action =
				GetFunction(copy).trivial_special_member;
			DemandFunction(copy);
			const BindingId destructor = DestructorForType(parsed.type);
			if (destructor != kNoBinding &&
				!program_->entities[entity].trivial_destructor)
			{
				if (GetFunction(destructor).deleted_destructor ||
					!CanAccessMember(destructor, entity))
					throw std::runtime_error(
						"exception handler object is not destructible");
				dump_.nodes[handler].object_binding = destructor;
				DemandFunction(destructor);
			}
		}
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
