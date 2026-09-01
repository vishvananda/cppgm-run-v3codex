#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <string>
#include <utility>

namespace cppgm
{
namespace semantic
{

bool Analyzer::AnalyzeControlFlowLabelOrGoto(NodeId node,
	ScopeId scope, std::uint32_t output_parent)
{
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_LABELED_STATEMENT))
	{
		const NameId name = program_->names.UseInterned(arena_->PayloadId(node));
		const std::uint32_t statement = MakeDump(DUMP_LABELED_STATEMENT,
			kNoType, VALUE_NONE, name);
		dump_.Add(output_parent, statement);
		RegisterControlFlowLabel(name, scope);
		const NodeId child = FirstSemanticChild(node);
		if (child == kNoNode) ThrowSemanticError("label without statement");
		AnalyzeStatement(child, scope, statement);
		return true;
	}
	if (!arena_->IsTag(node, ::cppgm::syntax::STAG_GOTO_STATEMENT)) return false;
	const NameId name = program_->names.UseInterned(arena_->PayloadId(node));
	const std::uint32_t statement = MakeDump(DUMP_GOTO_STATEMENT,
		kNoType, VALUE_NONE, name);
	dump_.Add(output_parent, statement);
	RegisterControlFlowGoto(statement, name, scope);
	return true;
}

BindingId Analyzer::DelegatingConstructorCleanupDestructor(
	TypeId owner_type, EntityId entity, bool base_entry)
{
	if (program_->entities[entity].trivial_destructor) return kNoBinding;
	BindingId destructor = DestructorForType(owner_type);
	if (destructor == kNoBinding)
		ThrowInternalCompilerError(
			"delegating constructor has no destructor identity");
	return base_entry ? EnsureDestructorBaseEntry(destructor) : destructor;
}

void Analyzer::BeginFunctionControlFlowFacts()
{
	FunctionControlFlowFactState saved;
	saved.contexts.swap(exception_control_contexts_);
	saved.current_context = current_exception_control_context_;
	saved.labels.swap(control_flow_labels_);
	saved.pending_gotos.swap(pending_control_flow_gotos_);
	function_control_flow_stack_.push_back(std::move(saved));
	exception_control_contexts_.clear();
	exception_control_contexts_.push_back(
		ExceptionControlContextFact(kNoDumpEdge, 0));
	current_exception_control_context_ = 0;
	control_flow_labels_.clear();
	pending_control_flow_gotos_.clear();
}

void Analyzer::FinishFunctionControlFlowFacts()
{
	if (current_exception_control_context_ != 0 ||
		exception_control_contexts_.empty())
		ThrowInternalCompilerError("unbalanced semantic exception context");
	if (!pending_control_flow_gotos_.empty())
		ThrowSemanticError("goto names an undefined label");
	if (function_control_flow_stack_.empty())
		ThrowInternalCompilerError("semantic control-flow stack underflow");
	FunctionControlFlowFactState saved =
		std::move(function_control_flow_stack_.back());
	function_control_flow_stack_.pop_back();
	exception_control_contexts_.swap(saved.contexts);
	current_exception_control_context_ = saved.current_context;
	control_flow_labels_.swap(saved.labels);
	pending_control_flow_gotos_.swap(saved.pending_gotos);
}

void Analyzer::PushExceptionControlContext()
{
	if (exception_control_contexts_.empty())
		BeginFunctionControlFlowFacts();
	const std::uint32_t parent = current_exception_control_context_;
	if (parent >= exception_control_contexts_.size())
		ThrowInternalCompilerError("invalid semantic exception context");
	if (exception_control_contexts_.size() >= kNoDumpEdge)
		ThrowSemanticResourceLimit("too many nested exception contexts");
	current_exception_control_context_ = static_cast<std::uint32_t>(
		exception_control_contexts_.size());
	exception_control_contexts_.push_back(ExceptionControlContextFact(parent,
		exception_control_contexts_[parent].depth + 1));
}

void Analyzer::PopExceptionControlContext()
{
	if (current_exception_control_context_ == 0 ||
		current_exception_control_context_ >= exception_control_contexts_.size())
		ThrowInternalCompilerError("semantic exception context underflow");
	current_exception_control_context_ =
		exception_control_contexts_[current_exception_control_context_].parent;
}

void Analyzer::ResolveControlFlowGoto(
	const PendingGotoControlFact& source, const LabelControlFact& target)
{
	ScopeId source_ancestor = source.scope;
	ScopeId target_ancestor = target.scope;
	while (source_ancestor != target_ancestor)
	{
		if (source_ancestor == kNoScope || target_ancestor == kNoScope)
			ThrowInternalCompilerError("goto scopes have no common ancestor");
		ScopeId* descendant = source_ancestor > target_ancestor ?
			&source_ancestor : &target_ancestor;
		if (*descendant >= scope_parents_.size())
			ThrowInternalCompilerError("goto scope is invalid");
		*descendant = scope_parents_[*descendant];
	}
	const ScopeId common_scope = source_ancestor;
	std::size_t source_common_count = 0;
	for (std::size_t i = 0; i < source.lifetimes.size(); ++i)
		if (source.lifetimes[i].scope == common_scope)
			source_common_count = source.lifetimes[i].count;
	std::size_t target_common_count = 0;
	for (std::size_t i = 0; i < target.lifetimes.size(); ++i)
		if (target.lifetimes[i].scope == common_scope)
			target_common_count = target.lifetimes[i].count;
	if (target_common_count > source_common_count)
		ThrowSemanticError("goto bypasses object initialization");
	ScopeId entered_scope = target.scope;
	std::size_t target_lifetime = 0;
	while (entered_scope != common_scope)
	{
		if (target_lifetime < target.lifetimes.size() &&
			target.lifetimes[target_lifetime].scope == entered_scope &&
			target.lifetimes[target_lifetime++].count != 0)
			ThrowSemanticError("goto bypasses object initialization");
		entered_scope = scope_parents_[entered_scope];
	}
	const auto append_lifetime_range = [this, &source](
		const std::vector<LifetimeObligation>& obligations,
		std::size_t begin, std::size_t end)
	{
		for (std::size_t i = begin; i != end; --i)
		{
			const LifetimeObligation& obligation = obligations[i - 1];
			const std::uint32_t action = obligation.temporary == kNoDumpEdge ?
				MakeDestructorAction(obligation.type, obligation.destructor,
					obligation.object) :
				MakeTemporaryDestructorAction(obligation.temporary,
					obligation.destructor);
			if (action != kNoDumpEdge) dump_.Add(source.node, action);
			++lexical_cleanup_action_visits_;
		}
	};
	ScopeId scope = source.scope;
	std::size_t lifetime = 0;
	while (scope != common_scope && scope != kNoScope)
	{
		if (lifetime < source.lifetimes.size() &&
			source.lifetimes[lifetime].scope == scope)
		{
			const GotoLifetimeSnapshot& snapshot = source.lifetimes[lifetime++];
			if (snapshot.scope >= scope_lifetimes_.size() ||
				snapshot.count > scope_lifetimes_[snapshot.scope].size())
				ThrowInternalCompilerError("goto lifetime snapshot is invalid");
			const std::vector<LifetimeObligation>& obligations =
				scope_lifetimes_[snapshot.scope];
			append_lifetime_range(obligations, snapshot.count, 0);
		}
		if (scope >= scope_parents_.size())
			ThrowInternalCompilerError("goto source scope is invalid");
		scope = scope_parents_[scope];
	}
	if (scope != common_scope)
		ThrowInternalCompilerError("goto cleanup lost its common scope");
	if (source_common_count != target_common_count)
	{
		if (common_scope >= scope_lifetimes_.size() ||
			source_common_count > scope_lifetimes_[common_scope].size())
			ThrowInternalCompilerError("goto common lifetime snapshot is invalid");
		const std::vector<LifetimeObligation>& obligations =
			scope_lifetimes_[common_scope];
		append_lifetime_range(obligations, source_common_count,
			target_common_count);
	}

	if (source.exception_context >= exception_control_contexts_.size() ||
		target.exception_context >= exception_control_contexts_.size())
		ThrowInternalCompilerError("goto exception context is invalid");
	std::uint32_t context = source.exception_context;
	const std::uint32_t source_depth =
		exception_control_contexts_[context].depth;
	const std::uint32_t target_depth =
		exception_control_contexts_[target.exception_context].depth;
	if (source_depth < target_depth)
		ThrowSemanticError("goto enters a protected region");
	const std::uint32_t exits = source_depth - target_depth;
	for (std::uint32_t i = 0; i < exits; ++i)
		context = exception_control_contexts_[context].parent;
	if (context != target.exception_context)
		ThrowSemanticError("goto crosses protected regions");
	dump_.nodes[source.node].exception_control_exit_count = exits;
}

void Analyzer::RegisterControlFlowLabel(NameId name, ScopeId scope)
{
	if (exception_control_contexts_.empty()) BeginFunctionControlFlowFacts();
	LabelControlFact target(scope, current_exception_control_context_);
	ScopeId lifetime_scope = scope < nearest_lifetime_scopes_.size() ?
		nearest_lifetime_scopes_[scope] : kNoScope;
	while (lifetime_scope != kNoScope)
	{
		target.lifetimes.push_back(GotoLifetimeSnapshot(lifetime_scope,
			scope_lifetimes_[lifetime_scope].size()));
		const ScopeId parent = scope_parents_[lifetime_scope];
		lifetime_scope = parent != kNoScope &&
			parent < nearest_lifetime_scopes_.size() ?
			nearest_lifetime_scopes_[parent] : kNoScope;
	}
	if (!control_flow_labels_.insert(std::make_pair(name, target)).second)
		ThrowSemanticError("duplicate label");
	const std::pair<std::unordered_multimap<NameId,
		PendingGotoControlFact>::iterator,
		std::unordered_multimap<NameId, PendingGotoControlFact>::iterator> range =
		pending_control_flow_gotos_.equal_range(name);
	for (std::unordered_multimap<NameId,
		PendingGotoControlFact>::iterator i = range.first;
		i != range.second; ++i)
		ResolveControlFlowGoto(i->second, target);
	pending_control_flow_gotos_.erase(range.first, range.second);
}

void Analyzer::RegisterControlFlowGoto(std::uint32_t node,
	NameId name, ScopeId scope)
{
	if (exception_control_contexts_.empty()) BeginFunctionControlFlowFacts();
	PendingGotoControlFact source(
		node, scope, current_exception_control_context_);
	ScopeId lifetime_scope = scope < nearest_lifetime_scopes_.size() ?
		nearest_lifetime_scopes_[scope] : kNoScope;
	while (lifetime_scope != kNoScope)
	{
		if (lifetime_scope >= scope_lifetimes_.size())
			ThrowInternalCompilerError("goto lifetime scope is invalid");
		source.lifetimes.push_back(GotoLifetimeSnapshot(lifetime_scope,
			scope_lifetimes_[lifetime_scope].size()));
		const ScopeId parent = scope_parents_[lifetime_scope];
		lifetime_scope = parent != kNoScope &&
			parent < nearest_lifetime_scopes_.size() ?
			nearest_lifetime_scopes_[parent] : kNoScope;
	}
	const std::unordered_map<NameId, LabelControlFact>::const_iterator target =
		control_flow_labels_.find(name);
	if (target != control_flow_labels_.end())
		ResolveControlFlowGoto(source, target->second);
	else pending_control_flow_gotos_.insert(std::make_pair(name, source));
}

void Analyzer::DemandConstructorUnwindDestructors(
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
					throwing = arena_->IsTag(current, ::cppgm::syntax::STAG_THROW_EXPRESSION) ||
						arena_->IsTag(current, ::cppgm::syntax::STAG_THROW_STATEMENT);
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
			action.kind == DUMP_BASE_INITIALIZER_ACTION ||
			action.kind == DUMP_DELEGATING_INITIALIZER_ACTION) &&
			action.selected_binding != kNoBinding)
			DemandFunction(action.selected_binding);
	}
}

std::uint32_t Analyzer::MakeTemporaryDestructorAction(
	std::uint32_t temporary, BindingId destructor,
	bool preserve_nontrivial_action)
{
	if (temporary == kNoDumpEdge || temporary >= dump_.nodes.size() ||
		dump_.nodes[temporary].kind != DUMP_TEMPORARY_OBJECT)
		ThrowInternalCompilerError("temporary destruction has no object identity");
	const TypeId type = dump_.nodes[temporary].type;
	if (IsInitializerListType(type)) return kNoDumpEdge;
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity) return kNoDumpEdge;
	if (!program_->entities[entity].destructible)
		ThrowSemanticError("temporary type is not destructible");
	if (destructor == kNoBinding) destructor = DestructorForType(type);
	if (destructor == kNoBinding)
		ThrowInternalCompilerError("temporary class has no destructor identity");
	if (!CanAccessMember(destructor, entity))
		ThrowSemanticError("inaccessible temporary destructor");
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

void Analyzer::MarkFullExpressionCalls(std::uint32_t node,
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

void Analyzer::MarkDefaultArgumentSubtree(std::uint32_t node)
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

void Analyzer::AppendFullExpressionDestructionActions(
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
		(preserve_nontrivial_actions || requested_explicit_cleanup ||
		 (complete_constructor_unwind_ && potentially_throwing &&
		  !temporaries.empty())) &&
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

void Analyzer::FinalizeStaticallyUnreachableBranchCleanup(
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

bool Analyzer::RequiresManagedConditionalFullExpression(
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

bool Analyzer::CollectTemporaryObjects(std::uint32_t node,
	std::vector<std::uint32_t>* temporaries)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return false;
	const DumpNode& root = dump_.nodes[node];
	bool direct_branch_root = root.kind == DUMP_CONDITIONAL_EXPRESSION;
	if (root.kind == DUMP_BINARY_EXPRESSION)
		direct_branch_root =
			root.logical_operation != LOGICAL_OPERATION_NONE;
	return CollectTemporaryObjectsImpl(node, temporaries, false,
		direct_branch_root ? node : kNoDumpEdge, kNoDumpEdge, 0, false,
		!direct_branch_root);
}

bool Analyzer::CollectTemporaryObjectsImpl(std::uint32_t node,
	std::vector<std::uint32_t>* temporaries, bool conditionally_evaluated,
	std::uint32_t branch_owner, std::uint32_t branch_child,
	std::size_t branch_depth, bool projected_subobject,
	bool collect_conditional_arms)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return false;
	++temporary_dependency_visits_;
	DumpNode& record = dump_.nodes[node];
	if (record.kind == DUMP_CONDITIONAL_ARM)
	{
		if (!collect_conditional_arms || record.first_edge == kNoDumpEdge)
			return false;
		return CollectTemporaryObjectsImpl(
			dump_.edges[record.first_edge].child, temporaries,
			conditionally_evaluated, branch_owner, branch_child, branch_depth,
			projected_subobject, collect_conditional_arms);
	}
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
				 record.base_projection_count != 0), collect_conditional_arms) ||
			control_dependent;
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
			bool direct_branch_child = false;
			if (branch_owner != kNoDumpEdge &&
				branch_owner < dump_.nodes.size())
				for (std::uint32_t edge = dump_.nodes[branch_owner].first_edge;
					edge != kNoDumpEdge; edge = dump_.edges[edge].next)
					if (dump_.edges[edge].child == branch_child)
					{
						direct_branch_child = true;
						break;
					}
			if (branch_owner != kNoDumpEdge && branch_depth == 1 &&
				branch_child != kNoDumpEdge && direct_branch_child)
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

bool Analyzer::AnalyzeExceptionStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_THROW_STATEMENT))
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
	if (!arena_->IsTag(node, ::cppgm::syntax::STAG_TRY_BLOCK)) return false;
	AnalyzeTryStatement(node, scope, output_parent);
	return true;
}

void Analyzer::StageExceptionalFullExpression(
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

void Analyzer::StageAutomaticInitializerException(
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

void Analyzer::StageControlFullExpression(
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

bool Analyzer::HasUnwindDestructionActions(ScopeId scope,
	ScopeId stop_exclusive) const
{
	if (stop_exclusive == kNoScope)
		stop_exclusive = FunctionCleanupStop(scope);
	ScopeId current = scope < nearest_lifetime_scopes_.size() ?
		nearest_lifetime_scopes_[scope] : kNoScope;
	while (current != kNoScope && current != stop_exclusive)
	{
		if (current >= scope_lifetimes_.size())
			ThrowInternalCompilerError("indexed lifetime scope has no obligations");
		if (!scope_lifetimes_[current].empty()) return true;
		const ScopeId parent = scope_parents_[current];
		current = parent != kNoScope &&
			parent < nearest_lifetime_scopes_.size() ?
			nearest_lifetime_scopes_[parent] : kNoScope;
	}
	return false;
}

bool Analyzer::HasEnclosingNontrivialObjectLifetime(
	ScopeId scope, ScopeId stop_exclusive) const
{
	++enclosing_lifetime_queries_;
	if (stop_exclusive == kNoScope)
		stop_exclusive = FunctionCleanupStop(scope);
	const std::uint32_t active =
		scope < scope_nontrivial_object_lifetime_prefixes_.size() ?
			scope_nontrivial_object_lifetime_prefixes_[scope] : 0;
	const ScopeId active_domain =
		scope < scope_lifetime_domains_.size() ?
			scope_lifetime_domains_[scope] : kNoScope;
	const bool same_domain = stop_exclusive != kNoScope &&
		stop_exclusive < scope_lifetime_domains_.size() &&
		scope_lifetime_domains_[stop_exclusive] == active_domain;
	const std::uint32_t stopped =
		same_domain &&
		stop_exclusive < scope_nontrivial_object_lifetime_prefixes_.size() ?
			scope_nontrivial_object_lifetime_prefixes_[stop_exclusive] : 0;
	if (active < stopped)
		ThrowInternalCompilerError(
			"enclosing lifetime prefix is not monotonic");
	return active != stopped;
}

ExpressionInfo Analyzer::AnalyzeThrowExpression(
	NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode)
	{
		if (exception_handler_depth_ == 0)
			ThrowSemanticError("rethrow outside an exception handler");
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
		ThrowSemanticError("cannot throw an expression of type void");
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
			ThrowSemanticError(
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

void Analyzer::AnalyzeExceptionHandler(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const ScopeId handler_scope = NewScope(
		scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t handler = MakeDump(DUMP_HANDLER);
	dump_.Add(output_parent, handler);
	const NodeId declaration = FindChild(node, ::cppgm::syntax::STAG_EXCEPTION_DECLARATION);
	if (declaration == kNoNode)
		ThrowSemanticError("exception handler has no declaration");
	if (FindChild(declaration, ::cppgm::syntax::STAG_ELLIPSIS) == kNoNode)
	{
		const NodeId specifiers = FindChild(declaration, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
		if (specifiers == kNoNode)
			ThrowSemanticError("exception handler has no type");
		const NodeId declarator = FindChild(declaration, ::cppgm::syntax::STAG_DECLARATOR);
		const SpecInfo spec = BuildSpecifiers(specifiers, handler_scope,
			std::string(), declarator != kNoNode, true);
		DeclaratorInfo parsed;
		parsed.type = spec.type;
		if (declarator != kNoNode)
			parsed = BuildDeclarator(declarator, spec.type, handler_scope);
		if (parsed.type == kNoType || IsVoid(parsed.type) ||
			program_->types.Get(EffectiveType(parsed.type)).kind == TYPE_ARRAY ||
			program_->types.Get(EffectiveType(parsed.type)).kind == TYPE_FUNCTION)
			ThrowSemanticError("invalid exception handler type");
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
				ThrowSemanticError(
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
					ThrowSemanticError(
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
	const NodeId body = FindChild(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT);
	if (body == kNoNode)
		ThrowSemanticError("exception handler has no body");
	++exception_handler_depth_;
	exception_handler_cleanup_stops_.push_back(scope);
	PushExceptionControlContext();
	AnalyzeCompound(body, handler_scope, handler);
	PopExceptionControlContext();
	exception_handler_cleanup_stops_.pop_back();
	--exception_handler_depth_;
	AppendScopeDestructionActions(handler_scope, handler, scope);
}

void Analyzer::AnalyzeTryStatement(NodeId node, ScopeId scope,
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
		if (arena_->IsTag(child, ::cppgm::syntax::STAG_COMPOUND_STATEMENT) && !saw_body)
		{
			exception_cleanup_stops_.push_back(scope);
			PushExceptionControlContext();
			AnalyzeCompound(child, scope, statement);
			PopExceptionControlContext();
			exception_cleanup_stops_.pop_back();
			saw_body = true;
		}
		else if (arena_->IsTag(child, ::cppgm::syntax::STAG_HANDLER))
		{
			const NodeId declaration = FindChild(child, ::cppgm::syntax::STAG_EXCEPTION_DECLARATION);
			catches_all = catches_all || (declaration != kNoNode &&
				FindChild(declaration, ::cppgm::syntax::STAG_ELLIPSIS) != kNoNode);
			AnalyzeExceptionHandler(child, scope, statement);
			++handler_count;
		}
	}
	if (!saw_body || handler_count == 0)
		ThrowSemanticError("invalid try statement");
	if (!catches_all) AppendUnwindDestructionActions(scope, statement);
}

}
}
