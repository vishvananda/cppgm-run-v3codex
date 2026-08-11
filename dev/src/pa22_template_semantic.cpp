#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

std::size_t NoAliasTemplatePattern()
{
	return std::numeric_limits<std::size_t>::max();
}

bool SyntaxUsesTemplateParameter(const SyntaxArena& arena, NodeId node,
	const std::unordered_set<NameId>& names)
{
	if (names.count(arena.SemanticPayloadId(node)) != 0) return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (SyntaxUsesTemplateParameter(
			arena, arena.EdgeChild(edge), names)) return true;
	return false;
}

enum AliasTemplateInstantiationState
{
	ALIAS_TEMPLATE_NOT_STARTED,
	ALIAS_TEMPLATE_IN_PROGRESS,
	ALIAS_TEMPLATE_SUCCEEDED,
	ALIAS_TEMPLATE_EXPECTED_FAILURE,
	ALIAS_TEMPLATE_HARD_FAILURE
};

bool EquivalentAliasTemplateParameters(
	const std::vector<TemplateParameter>& left,
	const std::vector<TemplateParameter>& right)
{
	if (left.size() != right.size()) return false;
	for (std::size_t i = 0; i < left.size(); ++i)
		if (left[i].kind != right[i].kind || left[i].pack != right[i].pack ||
			left[i].template_parameters.size() !=
				right[i].template_parameters.size() ||
			left[i].dependent_type != right[i].dependent_type ||
			(!left[i].dependent_type &&
			 left[i].kind == TEMPLATE_ARGUMENT_INTEGRAL &&
			 left[i].value_type != right[i].value_type)) return false;
	return true;
}

std::string LambdaIdentityComponent(const std::string& context,
	std::size_t token_first, std::size_t token_last, std::uint32_t ordinal)
{
	std::string result("__lambda_");
	result.reserve(result.size() + context.size() + 48);
	for (std::size_t i = 0; i < context.size(); ++i)
	{
		const unsigned char value = static_cast<unsigned char>(context[i]);
		result += std::isalnum(value) || value == '_' ?
			static_cast<char>(value) : '_';
	}
	std::ostringstream suffix;
	suffix << "_t" << token_first << '_' << token_last << "_n" << ordinal;
	result += suffix.str();
	return result;
}

}

bool SemanticAnalyzer::HasTargetTypedSpecializedMemberImmediate(
	const ExpressionInfo& destination, const ExpressionInfo& value) const
{
	if (value.converted_scalar_target == kNoType ||
		destination.node >= dump_.nodes.size() ||
		value.node >= dump_.nodes.size()) return false;
	const DumpNode& source = dump_.nodes[value.node];
	const DumpNode& target = dump_.nodes[destination.node];
	if (source.kind != DUMP_LITERAL || !source.constant ||
		source.enum_arithmetic_conversion ||
		target.kind != DUMP_MEMBER_EXPRESSION ||
		target.binding == kNoBinding ||
		target.binding >= program_->bindings.size()) return false;
	const EntityId owner = program_->bindings[target.binding].member_owner;
	if (owner == kNoEntity || owner >= program_->entities.size() ||
		program_->entities[owner].template_argument_count == 0) return false;
	const TypeId destination_type = program_->types.RemoveTopCv(
		EffectiveType(destination.type));
	return IsIntegral(source.type, true) &&
		IsIntegral(destination_type, true) &&
		destination_type == value.converted_scalar_target;
}

ExpressionInfo SemanticAnalyzer::AnalyzeCapturelessLambda(NodeId node,
	ScopeId scope, TypeId target)
{
	++lambda_closure_requests_;
	const NodeId introducer = FindChild(node, "lambda-introducer");
	if (introducer == kNoNode || PayloadSource(introducer) != "[]")
		throw std::runtime_error(
			"only captureless lambda expressions are supported in PA22");
	const NodeId declarator = FindChild(node, "lambda-declarator");
	const NodeId body = FindChild(node, "compound-statement");
	if (body == kNoNode)
		throw std::logic_error("lambda expression has no retained body");
	if (current_function_context_ == kNoBinding)
		throw std::runtime_error("lambda expression has no function context");
	const BindingId enclosing = program_->bindings[
		current_function_context_].canonical;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(enclosing) << 32) | node;
	const CompactIndexSequence* indexed = lambda_closure_index_.Find(key);
	std::size_t fact_index = 0;
	if (indexed)
	{
		if (indexed->Size() != 1)
			throw std::logic_error("ambiguous canonical lambda closure fact");
		fact_index = (*indexed)[0];
		if (fact_index >= lambda_closures_.size() ||
			lambda_closures_[fact_index].syntax != node ||
			lambda_closures_[fact_index].function != enclosing)
			throw std::logic_error("corrupt canonical lambda closure index");
		++lambda_closure_cache_hits_;
	}
	else
	{
		bool mutable_call = false;
		bool nonthrowing = false;
		bool variadic_call = false;
		std::vector<ParameterInfo> call_parameters;
		NodeId parameter_clause = kNoNode;
		NodeId trailing_return = kNoNode;
		if (declarator != kNoNode)
		{
			parameter_clause = FindChild(declarator, "parameter-clause");
			if (parameter_clause == kNoNode)
				throw std::logic_error(
					"lambda declarator has no parameter clause");
			call_parameters = BuildParameters(
				parameter_clause, scope, &variadic_call);
			trailing_return = FindChild(declarator, "trailing-return-type");
			mutable_call =
				FindChild(declarator, "lambda-specifier") != kNoNode;
			const NodeId exception = FindChild(
				declarator, "noexcept-specification");
			if (exception != kNoNode)
			{
				if (FirstSemanticChild(exception) != kNoNode)
					throw std::runtime_error(
						"dependent lambda noexcept is outside the PA22 subset");
				nonthrowing = true;
			}
		}
		if (lambda_count_by_function_.size() <= enclosing)
			lambda_count_by_function_.resize(
				static_cast<std::size_t>(enclosing) + 1, 0);
		const std::uint32_t ordinal =
			lambda_count_by_function_[enclosing]++;
		ScopeId namespace_owner = scope;
		while (namespace_owner != kNoScope &&
			program_->KindOfScope(namespace_owner) != SCOPE_NAMESPACE)
			namespace_owner = program_->ParentScope(namespace_owner);
		if (namespace_owner == kNoScope)
			throw std::logic_error("lambda closure has no namespace owner");
		const BindingRecord& context = program_->bindings[enclosing];
		const std::string context_name = program_->names.Get(
			context.qualified_name != 0 ? context.qualified_name : context.name);
		const std::string leaf_spelling = LambdaIdentityComponent(context_name,
			arena_->TokenFirst(node), arena_->TokenLast(node), ordinal);
		const NameId leaf = program_->names.Intern(leaf_spelling);
		const NameId emission = EmissionName(namespace_owner, leaf);
		const EntityId entity = program_->NewEntity(emission, NAMED_CLASS, true,
			kNoType, namespace_owner, leaf);
		EntityRecord& closure = program_->entities[entity];
		closure.local_context = enclosing;
		closure.lambda_closure = true;
		closure.lambda_ordinal = ordinal;
		closure.object_size = 1;
		closure.object_alignment = 1;
		closure.natural_alignment = 1;
		closure.layout_complete = true;
		closure.destructible = true;
		closure.trivial_destructor = true;
		closure.empty_class = true;
		const TypeId closure_type = closure.type;
		const NameId member_prefix = program_->names.Intern(
			program_->names.Get(emission) + "::");
		const ScopeId member_scope = NewScope(namespace_owner, SCOPE_CLASS,
			leaf, member_prefix);
		program_->SetEntityScope(entity, member_scope);
		program_->SetTypeName(member_scope, leaf, closure_type);
		const BindingId injected = program_->AddBinding(member_scope,
			BIND_TYPE, leaf, closure_type, false, 0, NAMED_CLASS);
		program_->bindings[injected].member_owner = entity;
		if (entity_data_members_.size() <= entity)
			entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
		if (entity_layout_members_.size() <= entity)
			entity_layout_members_.resize(static_cast<std::size_t>(entity) + 1);
		if (entity_constructors_.size() <= entity)
			entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
		const NameId call_name = program_->names.Intern("operator()");
		TypeId result_type = program_->types.Fundamental(FUND_VOID);
		if (trailing_return != kNoNode)
		{
			const ScopeId return_scope = NewScope(scope, SCOPE_FUNCTION,
				call_name, ScopePrefixId(scope));
			BindFunctionParameterPackElement(return_scope,
				FunctionParameterPackName(parameter_clause), kNoBinding);
			for (std::size_t i = 0; i < call_parameters.size(); ++i)
				if (call_parameters[i].name != 0)
				{
					const BindingId parameter = program_->AddBinding(return_scope,
						BIND_PARAMETER, call_parameters[i].name,
						ParameterBindingType(call_parameters[i]));
					BindFunctionParameterPackElement(return_scope,
						call_parameters[i].pack_name, parameter);
				}
			const NodeId type_id = FindChild(trailing_return, "type-id");
			if (type_id == kNoNode)
				throw std::runtime_error(
					"lambda trailing return type is missing its type-id");
			result_type = BuildTypeId(type_id, return_scope);
		}
		std::vector<TypeId> parameter_types;
		parameter_types.reserve(call_parameters.size());
		for (std::size_t i = 0; i < call_parameters.size(); ++i)
			parameter_types.push_back(call_parameters[i].function_type);
		const TypeId call_type = program_->types.Function(result_type,
			parameter_types, variadic_call,
			mutable_call ? CV_NONE : CV_CONST);
		const BindingId call_operator = DeclareFunction(member_scope, call_name,
			call_type, call_parameters, true, false,
			STORAGE_CLASS_NONE, LANGUAGE_LINKAGE_CPP, nonthrowing);
		BindingRecord& call_binding = program_->bindings[call_operator];
		call_binding.member_owner = entity;
		call_binding.access = ACCESS_PUBLIC;
		FunctionInfo& call = GetMutableFunction(call_operator);
		call.member_owner = closure_type;
		call.lexical_scope = scope;
		call.definition_body = body;
		call.deferred = true;
		call.definition_in_class = true;
		if (trailing_return == kNoNode)
			call.placeholder_return_kind = PLACEHOLDER_DECLARATOR_VALUE;
		RegisterClassMemberFunction(entity, call_operator);
		PublishInlineFunctionFacts(call_operator, true);
		(void)EnsureImplicitDestructor(entity);
		fact_index = lambda_closures_.size();
		lambda_closures_.push_back(LambdaClosureFact(node, enclosing, entity,
			call_operator, ordinal));
		lambda_closure_index_.Ensure(key).Push(fact_index);
		if (trailing_return == kNoNode)
			AnalyzeRetainedPlaceholderFunctionBody(call_operator);
	}
	const TypeId closure_type =
		program_->entities[lambda_closures_[fact_index].entity].type;
	const std::uint32_t initializer = MakeDump(
		DUMP_BRACED_INIT_LIST, closure_type, VALUE_PRVALUE);
	ExpressionInfo result;
	result.node = initializer;
	result.type = closure_type;
	result.category = VALUE_PRVALUE;
	++expression_count_;
	return ApplyTarget(result, target);
}

void SemanticAnalyzer::DemandMaterializedConstructorActions(
	std::uint32_t node, bool demand_calls)
{
	if (node >= dump_.nodes.size())
		throw std::logic_error("invalid materialized-constructor demand root");
	struct Visit
	{
		std::uint32_t node, next_edge;
		bool entered;

		explicit Visit(std::uint32_t node_value)
			: node(node_value), next_edge(kNoDumpEdge), entered(false) {}
	};
	std::vector<Visit> pending(1, Visit(node));
	while (!pending.empty())
	{
		Visit& visit = pending.back();
		const std::uint32_t current = visit.node;
		DumpNode& record = dump_.nodes[current];
		if (!visit.entered)
		{
			++materialized_demand_visits_;
			visit.entered = true;
			visit.next_edge = record.first_edge;
			if (demand_calls && record.kind == DUMP_CALL_EXPRESSION &&
				record.first_edge != kNoDumpEdge)
			{
				const DumpNode& callee = dump_.nodes[
					dump_.edges[record.first_edge].child];
				if (callee.kind == DUMP_CALLEE && callee.binding != kNoBinding)
				{
					const BindingId binding =
						program_->bindings[callee.binding].canonical;
					if (binding < function_fact_by_binding_.size() &&
						function_fact_by_binding_[binding] != kNoDumpEdge &&
						!GetFunction(binding).defined)
					{
						GetMutableFunction(binding).deferred = true;
						DemandRuntimeFunction(binding);
					}
				}
			}
			if (record.kind == DUMP_TEMPORARY_OBJECT &&
				record.pending_constructor_demand)
			{
				if (record.first_edge == kNoDumpEdge ||
					dump_.edges[record.first_edge].next != kNoDumpEdge)
					throw std::logic_error(
						"materialized constructor has invalid recipe");
				const DumpNode& recipe = dump_.nodes[
					dump_.edges[record.first_edge].child];
				const DumpNode& action = recipe.kind == DUMP_BRACED_INIT_LIST &&
					recipe.value_constructor != kNoDumpEdge ?
						dump_.nodes[recipe.value_constructor] : recipe;
				if (action.kind != DUMP_CONSTRUCTOR_ACTION ||
					action.binding == kNoBinding)
					throw std::logic_error(
						"materialized constructor demand has no action");
				record.pending_constructor_demand = false;
				DemandConstructorDefinition(action.binding);
			}
		}
		if (visit.next_edge == kNoDumpEdge)
		{
			pending.pop_back();
			continue;
		}
		const std::uint32_t edge = visit.next_edge;
		visit.next_edge = dump_.edges[edge].next;
		pending.push_back(Visit(dump_.edges[edge].child));
	}
}

void SemanticAnalyzer::DemandRetainedRuntimeCalls(std::uint32_t node)
{
	if (node >= dump_.nodes.size())
		throw std::logic_error("invalid retained-call demand root");
	std::vector<std::uint32_t> pending(1, node);
	while (!pending.empty())
	{
		const std::uint32_t current = pending.back();
		pending.pop_back();
		DumpNode& record = dump_.nodes[current];
		if (record.runtime_call_demand_scanned) continue;
		record.runtime_call_demand_scanned = true;
		++materialized_demand_visits_;
		if (record.kind == DUMP_CALL_EXPRESSION &&
			record.pending_runtime_call_demand)
		{
			record.pending_runtime_call_demand = false;
			if (record.first_edge != kNoDumpEdge)
			{
				const DumpNode& callee = dump_.nodes[
					dump_.edges[record.first_edge].child];
				if (callee.kind == DUMP_CALLEE && callee.binding != kNoBinding)
				{
					const BindingId binding =
						program_->bindings[callee.binding].canonical;
					if (binding < function_fact_by_binding_.size() &&
						function_fact_by_binding_[binding] != kNoDumpEdge)
					{
						if (!GetFunction(binding).defined)
							GetMutableFunction(binding).deferred = true;
						DemandRuntimeFunction(binding);
					}
				}
			}
		}
		for (std::uint32_t edge = record.first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
}

bool SemanticAnalyzer::TryBuildElidedClassValueTransfer(TypeId type,
	const ExpressionInfo& source, BindingId selected_constructor,
	ExpressionInfo* result)
{
	if (!retain_lowering_facts_ || result == 0 || source.node >= dump_.nodes.size() ||
		selected_constructor == kNoBinding) return false;
	const FunctionInfo& constructor = GetFunction(selected_constructor);
	if (constructor.special_member != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
		constructor.special_member != SPECIAL_MEMBER_MOVE_CONSTRUCTOR) return false;
	const DumpNode& materialized = dump_.nodes[source.node];
	if (materialized.kind != DUMP_TEMPORARY_OBJECT ||
		materialized.reference_call_materialization ||
		materialized.first_edge == kNoDumpEdge ||
		dump_.edges[materialized.first_edge].next != kNoDumpEdge) return false;
	const std::uint32_t recipe_node = dump_.edges[materialized.first_edge].child;
	const DumpNode& recipe = dump_.nodes[recipe_node];
	const TypeId recipe_type = recipe.kind == DUMP_CONSTRUCTOR_ACTION ?
		recipe.operand_type : recipe.type;
	if (program_->types.RemoveTopCv(EffectiveType(recipe_type)) !=
		program_->types.RemoveTopCv(EffectiveType(type)) ||
		recipe.explicit_user_conversion_call) return false;
	const bool conversion_result = recipe.user_conversion_call &&
		!recipe.explicit_user_conversion_call;
	if (!constructor.trivial_special_member && !conversion_result) return false;
	ExpressionInfo direct;
	direct.node = recipe_node;
	direct.type = recipe_type;
	direct.category = VALUE_PRVALUE;
	*result = BuildDirectClassValueTransfer(direct, type, selected_constructor);
	return true;
}

void SemanticAnalyzer::StageNestedTemplateTemporaryCleanup(
	std::uint32_t expression, std::uint32_t statement, ScopeId scope)
{
	for (std::uint32_t edge = dump_.nodes[statement].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		const DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind != DUMP_DESTRUCTOR_ACTION ||
			action.lifetime_object == kNoDumpEdge) continue;
		const EntityId entity = DestructedEntity(action.operand_type);
		if (entity == kNoEntity ||
			program_->entities[entity].template_argument_count == 0 ||
			program_->entities[entity].enclosing_class == kNoEntity) continue;
		MarkFullExpressionCalls(expression);
		AppendUnwindDestructionActions(scope, statement);
		return;
	}
}

void SemanticAnalyzer::StageReturnTemporaryCleanup(
	std::uint32_t expression, std::uint32_t statement, ScopeId scope)
{
	std::vector<std::uint32_t> tracked_temporaries;
	for (std::uint32_t edge = dump_.nodes[statement].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		const DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind == DUMP_DESTRUCTOR_ACTION &&
			action.full_expression_staging &&
			action.lifetime_object != kNoDumpEdge)
		{
			tracked_temporaries.push_back(action.lifetime_object);
			dump_.nodes[action.lifetime_object].
				managed_full_expression_cleanup = true;
		}
	}
	if (tracked_temporaries.empty()) return;

	struct Visit
	{
		std::uint32_t node;
		bool below_call;
		Visit(std::uint32_t node_value, bool below_call_value)
			: node(node_value), below_call(below_call_value) {}
	};
	std::vector<Visit> pending(1, Visit(expression, false));
	bool managed = false;
	while (!pending.empty() && !managed)
	{
		const Visit visit = pending.back();
		pending.pop_back();
		++temporary_dependency_visits_;
		const DumpNode& record = dump_.nodes[visit.node];
		managed = visit.below_call && record.managed_full_expression_cleanup;
		const bool below_call = visit.below_call ||
			record.kind == DUMP_CALL_EXPRESSION;
		for (std::uint32_t edge = record.first_edge;
			!managed && edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(Visit(dump_.edges[edge].child, below_call));
	}
	if (!managed)
	{
		for (std::size_t i = 0; i < tracked_temporaries.size(); ++i)
			dump_.nodes[tracked_temporaries[i]].
				managed_full_expression_cleanup = false;
		return;
	}

	// A temporary used as an object or argument of an enclosing call is live
	// while that call runs.  Publish one explicit cleanup region so lowering
	// can install the typed destructor set before the call without recognizing
	// the spelling or kind of any argument (including a closure).
	MarkFullExpressionCalls(expression, true);
	for (std::uint32_t edge = dump_.nodes[statement].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind == DUMP_DESTRUCTOR_ACTION &&
			action.full_expression_staging)
			action.managed_full_expression_cleanup = true;
	}
	AppendUnwindDestructionActions(scope, statement);
	for (std::uint32_t edge = dump_.nodes[statement].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		DumpNode& action = dump_.nodes[dump_.edges[edge].child];
		if (action.kind == DUMP_DESTRUCTOR_ACTION && action.unwind_only)
			action.full_expression_staging = true;
	}
}

void SemanticAnalyzer::SelectClassTemplateMemberOwner(
	std::size_t pattern_index, ClassTemplateMemberPattern* member)
{
	if (!member || pattern_index >= class_templates_.size())
		throw std::logic_error("invalid class template member owner");
	ClassTemplatePattern& owner = class_templates_[pattern_index];
	member->owner_partial_pattern = kNoDumpEdge;
	bool concrete = true;
	for (std::size_t i = 0;
		i < member->canonical_owner_arguments.size(); ++i)
		if (member->canonical_owner_arguments[i].IsDependent() ||
			(member->canonical_owner_arguments[i].kind == TEMPLATE_ARGUMENT_TYPE &&
			 FunctionTemplateTypeIsDependent(
				member->canonical_owner_arguments[i].type)))
			concrete = false;
	if (concrete)
	{
		member->concrete_owner = InstantiateClassTemplate(
			pattern_index, member->canonical_owner_arguments);
		if (member->concrete_owner == kNoBinding)
			throw std::runtime_error(
				"invalid concrete class template member owner");
		if (member->concrete_owner <
			class_template_partial_selections_.size())
			member->owner_partial_pattern = class_template_partial_selections_[
				member->concrete_owner].pattern;
		return;
	}
	for (std::size_t partial_index = 0;
		partial_index < owner.partial_specializations.size(); ++partial_index)
	{
		ClassTemplatePartialPattern& partial =
			owner.partial_specializations[partial_index];
		if (!MaterializeTemplatePartialArguments(owner.parameters,
			partial.parameters, partial.arguments, partial.lexical_scope,
			&partial.canonical_arguments,
			&partial.canonical_argument_state)) continue;
		FunctionTemplateDeduction partial_from_member(partial.parameters);
		FunctionTemplateDeduction member_from_partial(member->parameters);
		if (!MatchTemplatePartialArguments(partial.parameters,
			partial.canonical_arguments, member->canonical_owner_arguments,
			&partial_from_member) ||
			!MatchTemplatePartialArguments(member->parameters,
				member->canonical_owner_arguments, partial.canonical_arguments,
				&member_from_partial)) continue;
		if (partial_index > std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("too many class partial patterns");
		member->owner_partial_pattern =
			static_cast<std::uint32_t>(partial_index);
		break;
	}
	if (member->owner_partial_pattern == kNoDumpEdge &&
		!ClassTemplateMemberNamesPrimaryParameters(
			member->parameters, member->canonical_owner_arguments))
		throw std::runtime_error(
			"class template member owner is not a declared specialization");
}

ScopeId SemanticAnalyzer::TemplateLexicalScope(
	ScopeId source, ScopeId owner) const
{
	if (program_->KindOfScope(owner) != SCOPE_CLASS) return source;
	for (ScopeId current = source; current != kNoScope;
		current = program_->ParentScope(current))
		if (current == owner) return source;
	return owner;
}

bool SemanticAnalyzer::RouteClassTemplateMemberDefinition(
	const ClassTemplateMemberPattern& definition, std::size_t component,
	ScopeId owner_scope, ScopeId lexical_scope, bool demanded)
{
	if (component >= definition.nested_owner_path.size() ||
		component >= definition.nested_owner_argument_lists.size() ||
		definition.nested_owner_argument_lists[component] == kNoNode)
		return false;
	const NameId name = definition.nested_owner_path[component];
	const LookupResult found = program_->LookupDirect(
		owner_scope, name, LOOKUP_SCOPE_CARRIER);
	const std::size_t pattern_index = FindClassTemplateIndex(found, name);
	if (pattern_index == NoAliasTemplatePattern()) return false;

	const NodeId declaration = definition.declaration;
	if (!arena_->IsTag(declaration, "template-declaration")) return false;
	const NodeId clause = FindChild(declaration, "template-parameter-clause");
	const NodeId list = clause == kNoNode ? kNoNode :
		FindChild(clause, "template-parameter-list");
	if (list == kNoNode) return false;
	std::vector<TemplateParameter> parameters;
	std::vector<NameId> parameter_names;
	std::vector<NodeId> defaults;
	ParseTemplateParameters(list, lexical_scope, &parameters,
		&parameter_names, &defaults);
	if (parameters.empty()) return false;
	NodeId target = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(declaration);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (child != clause) target = child;
	}
	if (target == kNoNode) return false;

	std::vector<NodeId> owner_arguments;
	const NodeId argument_list =
		definition.nested_owner_argument_lists[component];
	for (std::uint32_t edge = arena_->FirstEdge(argument_list);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
		owner_arguments.push_back(arena_->EdgeChild(edge));
	ClassTemplateMemberPattern routed;
	routed.lexical_scope = lexical_scope;
	routed.declaration = target;
	routed.value_use_requires_storage =
		definition.value_use_requires_storage;
	routed.parameters.swap(parameters);
	routed.nested_owner_path.assign(
		definition.nested_owner_path.begin() + component + 1,
		definition.nested_owner_path.end());
	routed.nested_owner_argument_lists.assign(
		definition.nested_owner_argument_lists.begin() + component + 1,
		definition.nested_owner_argument_lists.end());
	std::vector<NameId> declared_owner_path = routed.nested_owner_path;
	for (std::size_t nested = 0;
		nested < routed.nested_owner_argument_lists.size(); ++nested)
		if (routed.nested_owner_argument_lists[nested] != kNoNode)
		{
			declared_owner_path.resize(nested + 1);
			break;
		}
	std::uint8_t owner_shape_state = 0;
	ClassTemplatePattern& pattern = class_templates_[pattern_index];
	if (pattern.defined && !declared_owner_path.empty() &&
		FindChild(pattern.declaration, "base-clause") == kNoNode &&
		!RetainedClassDeclaresNestedPath(
			pattern.declaration, declared_owner_path))
		throw std::runtime_error(
			"class template member has a missing nested owner");
	if (!MaterializeTemplatePartialArguments(pattern.parameters,
		routed.parameters, owner_arguments, lexical_scope,
		&routed.canonical_owner_arguments, &owner_shape_state) ||
		owner_shape_state != 1)
		throw std::runtime_error(
			"nested class template member owner pattern is not deducible");
	SelectClassTemplateMemberOwner(pattern_index, &routed);

	std::deque<ClassTemplateMemberPattern>& definitions = demanded ?
		pattern.demanded_member_definitions : pattern.member_definitions;
	definitions.push_back(routed);
	const std::vector<BindingId> specializations =
		pattern.specialization_bindings;
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const BindingId specialization = specializations[i];
		const EntityId entity = EntityOf(
			program_->bindings[specialization].type);
		if (entity == kNoEntity)
			throw std::logic_error(
				"nested class specialization has no entity");
		const EntityRecord& record = program_->entities[entity];
		if (record.template_argument_begin == kNoBinding)
			throw std::logic_error(
				"nested class specialization has no arguments");
		const std::vector<TemplateArgument> arguments =
			StoredTemplateArguments(record.template_argument_begin,
				record.template_argument_count);
		if (demanded)
			QueueClassTemplateMemberDefinitions(
				pattern_index, specialization);
		else ApplyClassTemplateMemberDefinitions(
			pattern_index, specialization, arguments);
	}
	return true;
}

bool SemanticAnalyzer::TemplateTemplateParameterMatches(
	const std::vector<TemplateParameter>& expected,
	const std::vector<TemplateParameter>& actual) const
{
	const bool expected_pack = HasTrailingTemplateParameterPack(expected);
	const std::size_t fixed = FixedTemplateParameterCount(expected);
	if (actual.size() < fixed) return false;
	for (std::size_t i = 0; i < fixed; ++i)
	{
		if (expected[i].kind != actual[i].kind ||
			expected[i].pack != actual[i].pack) return false;
		if (expected[i].kind == TEMPLATE_ARGUMENT_INTEGRAL &&
			(expected[i].dependent_type != actual[i].dependent_type ||
			 (!expected[i].dependent_type &&
			  expected[i].value_type != actual[i].value_type))) return false;
		if (expected[i].kind == TEMPLATE_ARGUMENT_TEMPLATE &&
			!TemplateTemplateParameterMatches(expected[i].template_parameters,
				actual[i].template_parameters)) return false;
	}
	if (expected_pack)
	{
		for (std::size_t i = fixed; i < actual.size(); ++i)
			if (actual[i].kind != expected.back().kind) return false;
		return true;
	}
	if (actual.size() < expected.size()) return false;
	for (std::size_t i = fixed; i < expected.size(); ++i)
		if (expected[i].kind != actual[i].kind ||
			expected[i].pack != actual[i].pack) return false;
	for (std::size_t i = expected.size(); i < actual.size(); ++i)
		if (!actual[i].pack && actual[i].default_argument == kNoNode)
			return false;
	return true;
}

bool SemanticAnalyzer::TemplateTemplateParameterMatchesAtScope(
	const std::vector<TemplateParameter>& expected,
	const std::vector<TemplateParameter>& actual, ScopeId scope)
{
	std::unordered_set<NameId> local_names;
	return TemplateTemplateParameterMatchesAtScope(
		expected, actual, scope, &local_names);
}

bool SemanticAnalyzer::TemplateTemplateParameterMatchesAtScope(
	const std::vector<TemplateParameter>& expected,
	const std::vector<TemplateParameter>& actual, ScopeId scope,
	std::unordered_set<NameId>* local_names)
{
	const bool expected_pack = HasTrailingTemplateParameterPack(expected);
	const std::size_t fixed = FixedTemplateParameterCount(expected);
	if (actual.size() < fixed) return false;
	std::vector<NameId> introduced_names;
	for (std::size_t i = 0; i < fixed; ++i)
	{
		if (expected[i].kind != actual[i].kind ||
			expected[i].pack != actual[i].pack) return false;
		if (expected[i].kind == TEMPLATE_ARGUMENT_INTEGRAL)
		{
			if (!expected[i].dependent_type)
			{
				if (actual[i].dependent_type ||
					expected[i].value_type != actual[i].value_type)
					return false;
			}
			else
			{
				const bool locally_dependent = SyntaxUsesTemplateParameter(
					*arena_, expected[i].specifiers, *local_names) ||
					(expected[i].declarator != kNoNode &&
					 SyntaxUsesTemplateParameter(
						*arena_, expected[i].declarator, *local_names));
				if (locally_dependent)
				{
					if (!actual[i].dependent_type) return false;
				}
				else
				{
					const TypeId resolved = ResolveTemplateParameterType(
						expected[i], scope);
					if (CandidateSubstitutionFailed() || resolved == kNoType ||
						actual[i].dependent_type ||
						resolved != actual[i].value_type) return false;
				}
			}
		}
		if (expected[i].kind == TEMPLATE_ARGUMENT_TEMPLATE &&
			!TemplateTemplateParameterMatchesAtScope(
				expected[i].template_parameters,
				actual[i].template_parameters, scope, local_names)) return false;
		if (expected[i].name != 0 && local_names->insert(
			expected[i].name).second)
			introduced_names.push_back(expected[i].name);
	}
	bool result = true;
	if (expected_pack)
	{
		for (std::size_t i = fixed; i < actual.size(); ++i)
			if (actual[i].kind != expected.back().kind) result = false;
	}
	else
	{
		if (actual.size() < expected.size()) result = false;
		for (std::size_t i = fixed; result && i < expected.size(); ++i)
			if (expected[i].kind != actual[i].kind ||
				expected[i].pack != actual[i].pack) result = false;
		for (std::size_t i = expected.size(); result && i < actual.size(); ++i)
			if (!actual[i].pack && actual[i].default_argument == kNoNode)
				result = false;
	}
	for (std::size_t i = 0; i < introduced_names.size(); ++i)
		local_names->erase(introduced_names[i]);
	return result;
}

bool SemanticAnalyzer::BuildTemplateTemplateArgument(NodeId syntax,
	ScopeId scope, const TemplateParameter& parameter,
	TemplateArgument* argument)
{
	return BuildTemplateTemplateArgument(
		syntax, scope, scope, parameter, argument);
}

bool SemanticAnalyzer::BuildTemplateTemplateArgument(NodeId syntax,
	ScopeId lookup_scope, ScopeId parameter_scope,
	const TemplateParameter& parameter, TemplateArgument* argument)
{
	NodeId type_id = arena_->IsTag(syntax, "type-id") ? syntax :
		FindChild(syntax, "type-id");
	if (type_id == kNoNode) return false;
	const NodeId specifiers = FindChild(type_id, "type-specifier-seq");
	const NodeId name = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (name == kNoNode) return false;
	const NodeId structured = FindChild(name, "structured-type-name");
	const LookupResult found = structured == kNoNode ?
		LookupSpelling(lookup_scope, PayloadSource(name), LOOKUP_TYPE) :
		LookupStructuredName(name, lookup_scope, LOOKUP_TYPE);
	if (found.type == kNoType && structured != kNoNode)
	{
		const NamePath path = StructuredNamePath(structured);
		NamePath owner_path;
		owner_path.global = path.global;
		if (!path.Empty()) owner_path.Push(path[0]);
		const TypeId owner = LookupPath(
			lookup_scope, owner_path, LOOKUP_TYPE).type;
		for (std::size_t ordinal = 0;
			ordinal < function_template_shape_parameters_.size(); ++ordinal)
			if (owner == function_template_shape_parameters_[ordinal])
			{
				if (dependent_template_argument_shapes_.size() <= name)
					dependent_template_argument_shapes_.resize(
						static_cast<std::size_t>(name) + 1, kNoType);
				TypeId& shape = dependent_template_argument_shapes_[name];
				if (shape == kNoType)
				{
					const NameId shape_name = program_->names.Intern(
						"__dependent_member_template_shape_" +
						std::to_string(name));
					const EntityId entity = program_->NewEntity(shape_name,
						NAMED_TEMPLATE_PARAMETER, false, kNoType,
						program_->GlobalScope(), shape_name);
					shape = program_->types.Named(entity);
				}
				*argument = TemplateArgument(TEMPLATE_ARGUMENT_TEMPLATE, shape,
					0, static_cast<std::uint32_t>(ordinal));
				return true;
			}
	}
	if (found.type == kNoType) return false;
	if (found.type_declaration != kNoBinding &&
		!CanAccessMember(found.type_declaration, found.naming_class))
		throw std::runtime_error("inaccessible template argument");
	const NameId requested = structured == kNoNode ?
		program_->names.Intern(PayloadSource(name)) :
		StructuredNamePath(structured).Last();
	const std::size_t class_index =
		FindClassTemplateIndex(found, requested);
	const std::size_t alias_index =
		FindAliasTemplateIndex(found, requested);
	const std::vector<TemplateParameter>* actual = 0;
	if (class_index != NoAliasTemplatePattern())
		actual = &class_templates_[class_index].parameters;
	else if (alias_index != NoAliasTemplatePattern())
		actual = &alias_templates_[alias_index].parameters;
	if (!actual || !TemplateTemplateParameterMatchesAtScope(
		parameter.template_parameters, *actual, parameter_scope)) return false;
	argument->kind = TEMPLATE_ARGUMENT_TEMPLATE;
	argument->type = found.type;
	if (class_index != NoAliasTemplatePattern() &&
		class_templates_[class_index].template_parameter_proxy)
		argument->dependent_parameter =
			class_templates_[class_index].template_parameter_ordinal;
	return true;
}

TypeId SemanticAnalyzer::CreateTemplateTemplateParameterProxy(ScopeId scope,
	const TemplateParameter& parameter, std::size_t ordinal)
{
	if (parameter.kind != TEMPLATE_ARGUMENT_TEMPLATE || parameter.name == 0)
		return kNoType;
	if (ordinal >= kNoTemplateParameter)
		throw std::runtime_error("template parameter ordinal is too large");
	ClassTemplatePattern pattern;
	pattern.owner = scope;
	pattern.lexical_scope = scope;
	pattern.name = parameter.name;
	pattern.parameters = parameter.template_parameters;
	pattern.template_parameter_proxy = true;
	pattern.template_parameter_ordinal = static_cast<std::uint32_t>(ordinal);
	const NameId marker_name = EmissionName(scope, parameter.name);
	pattern.marker_entity = program_->NewEntity(marker_name,
		NAMED_TEMPLATE_PARAMETER, false, kNoType, scope, parameter.name);
	const TypeId marker_type = program_->entities[pattern.marker_entity].type;
	program_->SetTypeName(scope, parameter.name, marker_type);
	program_->AddBinding(scope, BIND_TYPE, parameter.name, marker_type,
		false, 0, NAMED_TEMPLATE_PARAMETER);
	const std::size_t index = class_templates_.size();
	if (index > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many template parameter proxies");
	class_templates_.push_back(pattern);
	if (class_template_pattern_by_entity_.size() <= pattern.marker_entity)
		class_template_pattern_by_entity_.resize(
			static_cast<std::size_t>(pattern.marker_entity) + 1, kNoDumpEdge);
	class_template_pattern_by_entity_[pattern.marker_entity] =
		static_cast<std::uint32_t>(index);
	return marker_type;
}

void SemanticAnalyzer::RegisterAliasTemplate(NodeId declaration,
	ScopeId scope, AccessKind member_access,
	const std::vector<TemplateParameter>& parameters)
{
	const NameId name = program_->names.Intern(arena_->Payload(declaration));
	const NodeId type_id = FindChild(declaration, "type-id");
	if (name == 0 || type_id == kNoNode)
		throw std::runtime_error("invalid alias template declaration");
	const LookupResult old = program_->LookupDirect(scope, name, LOOKUP_TYPE);
	if (old.type != kNoType)
	{
		const std::size_t prior = FindAliasTemplateIndex(old, name);
		if (prior == NoAliasTemplatePattern())
			throw std::runtime_error(
				"alias template conflicts with an existing type");
		AliasTemplatePattern& pattern = alias_templates_[prior];
		if (!EquivalentAliasTemplateParameters(pattern.parameters, parameters))
			throw std::runtime_error(
				"alias template parameter list does not match");
		for (std::size_t i = 0; i < parameters.size(); ++i)
			if (parameters[i].kind == TEMPLATE_ARGUMENT_TEMPLATE &&
				(!TemplateTemplateParameterMatches(
					pattern.parameters[i].template_parameters,
					parameters[i].template_parameters) ||
				 !TemplateTemplateParameterMatches(
					parameters[i].template_parameters,
					pattern.parameters[i].template_parameters)))
				throw std::runtime_error(
					"alias template template-parameter shape mismatch");
		std::vector<TemplateParameter> merged = parameters;
		for (std::size_t i = 0; i < merged.size(); ++i)
			if (merged[i].default_argument == kNoNode)
				merged[i].default_argument =
					pattern.parameters[i].default_argument;
		pattern.parameters.swap(merged);
		pattern.lexical_scope = scope;
		pattern.declaration = declaration;
		pattern.type_id = type_id;
		return;
	}

	AliasTemplatePattern pattern;
	pattern.owner = scope;
	pattern.lexical_scope = scope;
	pattern.specialization_scope = NewScope(scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(scope));
	pattern.name = name;
	pattern.declaration = declaration;
	pattern.type_id = type_id;
	pattern.parameters = parameters;
	const NameId marker_name = EmissionName(scope, name);
	pattern.marker_entity = program_->NewEntity(marker_name,
		NAMED_TEMPLATE_PARAMETER, false, kNoType, scope, name);
	const TypeId marker_type =
		program_->entities[pattern.marker_entity].type;
	program_->SetTypeName(scope, name, marker_type);
	const BindingId binding = program_->AddBinding(scope, BIND_TYPE, name,
		marker_type, false, 0, NAMED_TEMPLATE_PARAMETER);
	const EntityId class_owner = program_->EntityForScope(scope);
	if (class_owner != kNoEntity)
	{
		program_->bindings[binding].member_owner = class_owner;
		program_->bindings[binding].access = member_access;
	}
	const std::size_t index = alias_templates_.size();
	if (index > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many alias templates");
	alias_templates_.push_back(pattern);
	if (alias_template_pattern_by_entity_.size() <= pattern.marker_entity)
		alias_template_pattern_by_entity_.resize(
			static_cast<std::size_t>(pattern.marker_entity) + 1, kNoDumpEdge);
	alias_template_pattern_by_entity_[pattern.marker_entity] =
		static_cast<std::uint32_t>(index);
}

std::size_t SemanticAnalyzer::FindAliasTemplateIndex(
	const LookupResult& found, NameId requested) const
{
	if (found.type == kNoType) return NoAliasTemplatePattern();
	const TypeRecord& type = program_->types.Get(
		program_->types.RemoveTopCv(found.type));
	if (type.kind != TYPE_NAMED ||
		type.entity >= alias_template_pattern_by_entity_.size())
		return NoAliasTemplatePattern();
	const std::uint32_t index =
		alias_template_pattern_by_entity_[type.entity];
	if (index == kNoDumpEdge || index >= alias_templates_.size())
		return NoAliasTemplatePattern();
	// A template-template binding preserves the canonical marker entity while
	// publishing it under the parameter's local spelling.  Identity wins over
	// that spelling just as it does for a primary class-template marker.
	if (type.entity == alias_templates_[index].marker_entity) return index;
	return alias_templates_[index].name == requested ? index :
		NoAliasTemplatePattern();
}

bool SemanticAnalyzer::IsUnqualifiedAliasTemplateName(
	ScopeId scope, const NamePath& path)
{
	if (path.Empty() || path.global || path.Size() != 1) return false;
	const LookupResult marker = LookupSpelling(
		scope, program_->names.Get(path.Last()), LOOKUP_TYPE);
	return FindAliasTemplateIndex(marker, path.Last()) !=
		NoAliasTemplatePattern();
}

LookupResult SemanticAnalyzer::LookupStructuredTypeSpecifier(
	NodeId syntax, ScopeId scope, TypeId deferred_type)
{
	const NamePath path = StructuredNamePath(syntax);
	const bool nondeduced_parameter = deferred_type != kNoType &&
		deferred_type == function_template_nondeduced_type_shape_;
	if (deferred_type != kNoType && (path.global || path.Size() > 1 ||
		nondeduced_parameter || IsUnqualifiedAliasTemplateName(scope, path)))
	{
		LookupResult deferred;
		deferred.type = deferred_type;
		return deferred;
	}
	LookupResult found;
	if (deferred_type == kNoType)
		found = LookupStructuredName(syntax, scope, LOOKUP_TYPE);
	else
	{
		candidate_substitution_failures_.push_back(0);
		try
		{
			found = LookupStructuredName(syntax, scope, LOOKUP_TYPE);
		}
		catch (...)
		{
			candidate_substitution_failures_.pop_back();
			throw;
		}
		const bool formation_failed = CandidateSubstitutionFailed();
		candidate_substitution_failures_.pop_back();
		if (formation_failed)
		{
			found.type = deferred_type;
			return found;
		}
	}
	if (deferred_type != kNoType && found.type != kNoType &&
		!FunctionTemplateTypeIsDependent(found.type))
		found.type = deferred_type;
	return found;
}

TypeId SemanticAnalyzer::InstantiateAliasTemplate(std::size_t index,
	const std::vector<TemplateArgument>& arguments)
{
	if (index >= alias_templates_.size())
		throw std::logic_error("invalid alias template pattern");
	const AliasTemplatePattern& pattern = alias_templates_[index];
	const std::size_t fixed = FixedTemplateParameterCount(pattern.parameters);
	if ((!HasTrailingTemplateParameterPack(pattern.parameters) &&
		 arguments.size() != pattern.parameters.size()) ||
		arguments.size() < fixed) return kNoType;
	for (std::size_t i = 0; i < arguments.size(); ++i)
		if (arguments[i].kind !=
			TemplateParameterForArgument(pattern.parameters, i).kind)
			return kNoType;

	++template_specialization_requests_;
	const TemplateSpecializationKey key =
		CanonicalTemplateSpecializationKey(index, arguments);
	BindingId binding = alias_template_instantiations_.Find(key);
	if (binding != kNoBinding)
	{
		++template_specialization_cache_hits_;
		if (binding >= alias_template_instantiation_states_.size())
			throw std::logic_error(
				"alias specialization has no completion state");
		const std::uint8_t state =
			alias_template_instantiation_states_[binding];
		if (state == ALIAS_TEMPLATE_IN_PROGRESS)
			throw std::runtime_error("recursive alias template specialization");
		if (state == ALIAS_TEMPLATE_EXPECTED_FAILURE)
		{
			if (CandidateSubstitutionActive())
			{
				RecordCandidateSubstitutionFailure();
				return kNoType;
			}
			throw std::runtime_error("invalid alias template specialization");
		}
		if (state == ALIAS_TEMPLATE_HARD_FAILURE)
			throw std::runtime_error("invalid alias template specialization");
		if (state != ALIAS_TEMPLATE_SUCCEEDED)
			throw std::logic_error(
				"alias specialization has an invalid completion state");
		return program_->bindings[binding].type;
	}

	binding = program_->AddBinding(pattern.specialization_scope,
		BIND_TYPE_ALIAS, pattern.name, kNoType);
	if (alias_template_instantiation_states_.size() <= binding)
		alias_template_instantiation_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	alias_template_instantiation_states_[binding] =
		ALIAS_TEMPLATE_IN_PROGRESS;
	alias_template_instantiations_.Insert(key, binding);

	const ScopeId substitution_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	for (std::size_t parameter = 0; parameter < pattern.parameters.size();
		++parameter)
	{
		if (pattern.parameters[parameter].pack)
			BindTemplateArgumentPack(substitution_scope,
				pattern.parameters[parameter], arguments, fixed,
				arguments.size());
		else BindTemplateArgument(substitution_scope,
			pattern.parameters[parameter], arguments[parameter]);
	}
	try
	{
		// Alias substitution establishes a canonical type identity; it does not
		// by itself require the aliased class layout.  Qualified components in
		// the retained type-id still demand their carriers through lookup.  Name
		// access in the retained type-id belongs to the alias declaration, not to
		// whichever use happened to request this specialization.
		++class_template_completion_suppressed_depth_;
		const EntityId previous_class = current_class_context_;
		const EntityId alias_owner = program_->EntityForScope(pattern.owner);
		current_class_context_ = alias_owner;
		TypeId result = kNoType;
		try
		{
			result = BuildTypeId(pattern.type_id, substitution_scope);
		}
		catch (...)
		{
			current_class_context_ = previous_class;
			--class_template_completion_suppressed_depth_;
			throw;
		}
		current_class_context_ = previous_class;
		--class_template_completion_suppressed_depth_;
		if (CandidateSubstitutionFailed())
		{
			alias_template_instantiation_states_[binding] =
				ALIAS_TEMPLATE_EXPECTED_FAILURE;
			return kNoType;
		}
		if (result == kNoType)
		{
			if (!CandidateSubstitutionActive())
				throw std::runtime_error(
					"alias template specialization has no result type");
			RecordCandidateSubstitutionFailure();
			alias_template_instantiation_states_[binding] =
				ALIAS_TEMPLATE_EXPECTED_FAILURE;
			return kNoType;
		}
		program_->bindings[binding].type = result;
		alias_template_instantiation_states_[binding] =
			ALIAS_TEMPLATE_SUCCEEDED;
		return result;
	}
	catch (...)
	{
		alias_template_instantiation_states_[binding] =
			ALIAS_TEMPLATE_HARD_FAILURE;
		throw;
	}
}

bool SemanticAnalyzer::AnalyzeExplicitFunctionInstantiation(
	NodeId target, ScopeId scope, bool definition)
{
	if (program_->KindOfScope(scope) != SCOPE_NAMESPACE)
		throw std::runtime_error(
			"explicit function instantiation must appear at namespace scope");

	NodeId declarator = FindChild(target, "declarator");
	if (arena_->IsTag(target, "simple-declaration"))
	{
		const NodeId list = FindChild(target, "init-declarator-list");
		const NodeId item = list == kNoNode ? kNoNode :
			FirstSemanticChild(list);
		declarator = item == kNoNode ? kNoNode :
			FindChild(item, "declarator");
		if (item != kNoNode)
		{
			const std::uint32_t first = arena_->FirstEdge(list);
			if (first != kNoEdge && arena_->NextEdge(first) != kNoEdge)
				throw std::runtime_error(
					"explicit function instantiation has multiple declarators");
		}
	}
	if (declarator == kNoNode) return false;
	const NodeId structure = DeclaratorNameStructure(declarator);
	const NamePath path = DeclaratorNamePath(declarator);
	if (path.Empty()) return false;
	std::string function_name = program_->names.Get(path.Last());
	if (structure == kNoNode &&
		function_name.compare(0, 8, "operator") == 0)
	{
		const std::size_t arguments = function_name.find('<');
		if (arguments != std::string::npos)
			function_name.erase(arguments);
	}
	const NodeId identifier = FindChild(declarator, "identifier");
	const NodeId name_syntax = structure != kNoNode ? structure : identifier;
	if (name_syntax == kNoNode) return false;

	SpecInfo spec;
	if (arena_->IsTag(target, "special-member-declaration") ||
		arena_->IsTag(target, "special-member-definition"))
		spec.type = program_->types.Fundamental(FUND_VOID);
	else
	{
		const NodeId specifiers = FindChild(target, "decl-specifier-seq");
		if (specifiers == kNoNode) return false;
		spec = BuildSpecifiers(specifiers, scope, std::string(), true);
	}
	DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
	if (!program_->types.IsFunction(parsed.type)) return false;
	if (spec.is_constexpr)
		parsed.type = ApplyConstexprDeclaredFunctionType(
			parsed.type, scope, path.Last(), kNoEntity);

	std::vector<BindingId> candidates;
	NamePath explicit_base;
	std::vector<NodeId> explicit_arguments;
	const bool explicit_template_id = CollectExplicitTemplateArguments(
		name_syntax, &explicit_base, &explicit_arguments);
	if (explicit_template_id)
		candidates = FunctionCandidates(
			scope, function_name, 0, name_syntax);
	else
	{
		const LookupResult ordinary = structure != kNoNode ?
			LookupStructuredName(structure, scope, LOOKUP_ORDINARY) :
			LookupPath(scope, path, LOOKUP_ORDINARY);
		if (ordinary.ordinary != kNoBinding &&
			program_->bindings[ordinary.ordinary].kind == BIND_FUNCTION)
			for (std::size_t i = 0; i < ordinary.OrdinaryCount(); ++i)
				AppendFunctionSet(ordinary.OrdinaryAt(i), &candidates);
	}
	const std::vector<BindingId> template_candidates =
		FunctionTemplateTargetCandidates(scope,
			function_name, parsed.type, name_syntax);
	for (std::size_t i = 0; i < template_candidates.size(); ++i)
		candidates.push_back(template_candidates[i]);

	BindingId selected = kNoBinding;
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const BindingId candidate = program_->bindings[candidates[i]].canonical;
		if (candidate >= program_->bindings.size() ||
			program_->bindings[candidate].kind != BIND_FUNCTION ||
			GetFunction(candidate).type != parsed.type) continue;
		const FunctionInfo& function = GetFunction(candidate);
		const EntityId member_owner =
			program_->bindings[candidate].member_owner;
		const bool templated_owner = member_owner != kNoEntity &&
			program_->entities[member_owner].template_argument_count != 0;
		if (function.template_pattern == kNoDumpEdge &&
			program_->bindings[candidate].template_argument_count == 0 &&
			!templated_owner) continue;
		if (selected != kNoBinding && selected != candidate)
			throw std::runtime_error(
				"ambiguous explicit function instantiation target");
		selected = candidate;
	}
	if (selected == kNoBinding) return false;

	if (function_explicit_instantiation_states_.size() <= selected)
		function_explicit_instantiation_states_.resize(
			static_cast<std::size_t>(selected) + 1, 0);
	std::uint8_t& state =
		function_explicit_instantiation_states_[selected];
	BindingRecord& binding = program_->bindings[selected];
	if (!definition)
	{
		if ((state & 2) != 0)
			throw std::runtime_error(
				"explicit function instantiation declaration follows definition");
		state |= 1;
		binding.explicit_instantiation_suppressed = true;
		return true;
	}
	if ((state & 2) != 0)
		throw std::runtime_error(
			"duplicate explicit function instantiation definition");
	if (!GetFunction(selected).defined)
		throw std::runtime_error(
			"explicit function instantiation target has no definition");
	state |= 2;
	binding.explicit_instantiation_suppressed = false;
	binding.weak_odr = true;
	binding.object_output_root = true;
	DemandFunction(selected);
	return true;
}

}
}
