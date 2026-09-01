#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

namespace cppgm
{
namespace semantic
{

bool Analyzer::IsInitializerListType(
	TypeId type, TypeId* element_type) const
{
	while (type != kNoType)
	{
		const TypeRecord& top = program_->types.Get(type);
		if (top.kind != TYPE_LVALUE_REFERENCE &&
			top.kind != TYPE_RVALUE_REFERENCE) break;
		type = top.child;
	}
	type = program_->types.RemoveTopCv(type);
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind != TYPE_NAMED ||
		record.entity >= class_template_pattern_by_entity_.size()) return false;
	const std::uint32_t pattern =
		class_template_pattern_by_entity_[record.entity];
	if (pattern == kNoDumpEdge || pattern >= class_templates_.size() ||
		!class_templates_[pattern].initializer_list_template) return false;
	const EntityRecord& entity = program_->entities[record.entity];
	if (entity.template_argument_begin == kNoBinding ||
		entity.template_argument_count != 1) return false;
	const TemplateArgument argument =
		StoredTemplateArgument(entity.template_argument_begin);
	if (argument.kind != TEMPLATE_ARGUMENT_TYPE || argument.type == kNoType)
		return false;
	if (element_type) *element_type = argument.type;
	return true;
}

bool Analyzer::InitializerListDefinitionReplayInProgress(
	EntityId entity) const
{
	if (entity >= class_template_pattern_by_entity_.size()) return false;
	const std::uint32_t pattern = class_template_pattern_by_entity_[entity];
	if (pattern == kNoDumpEdge || pattern >= class_templates_.size() ||
		!class_templates_[pattern].initializer_list_template) return false;
	const BindingId binding = program_->entities[entity].declaration;
	return binding < class_template_specialization_states_.size() &&
		class_template_specialization_states_[binding] == 1;
}

bool Analyzer::IsInitializerListFunction(TypeId type) const
{
	const TypeRecord& function = program_->types.Get(type);
	if (function.kind != TYPE_FUNCTION) return false;
	const TypeId* parameters = program_->types.Parameters(type);
	for (std::size_t i = 0; i < function.parameter_count; ++i)
		if (IsInitializerListType(parameters[i])) return true;
	return false;
}

bool Analyzer::IsStandardInitializerListTemplate(NameId name,
	ScopeId owner, const std::vector<TemplateParameter>& parameters) const
{
	while (owner != kNoScope && program_->IsInlineNamespace(owner))
		owner = program_->ParentScope(owner);
	return program_->names.Get(name) == "initializer_list" &&
		owner != kNoScope && program_->KindOfScope(owner) == SCOPE_NAMESPACE &&
		program_->IsStandardNamespace(owner) &&
		parameters.size() == 1 &&
		parameters[0].kind == TEMPLATE_ARGUMENT_TYPE && !parameters[0].pack;
}

void Analyzer::ConfigureInitializerListSpecialization(TypeId type)
{
	TypeId element = kNoType;
	if (!IsInitializerListType(type, &element)) return;
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity) return;
	EntityRecord& record = program_->entities[entity];
	if (!record.layout_complete)
	{
		record.object_size = 16;
		record.object_alignment = 8;
		record.natural_alignment = 8;
		record.layout_complete = true;
		record.empty_class = false;
	}
	record.complete = true;
	record.default_constructible = true;
	record.destructible = true;
	record.trivial_destructor = true;
}

bool Analyzer::DeduceInitializerListElementType(
	NodeId list, ScopeId scope, TypeId* element_type)
{
	if (list == kNoNode || !arena_->IsTag(list, ::cppgm::syntax::STAG_BRACED_INIT_LIST) ||
		!element_type) return false;
	TypeId deduced = kNoType;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId syntax = arena_->EdgeChild(edge);
		ExpressionInfo value;
		if (!ReusePreparedBracedExpression(syntax, kNoType, &value))
			value = AnalyzeExpression(syntax, scope);
		const TypeId current = program_->types.RemoveTopCv(Decay(value.type));
		if (deduced == kNoType) deduced = current;
		else if (deduced != current) return false;
	}
	if (deduced == kNoType) return false;
	*element_type = deduced;
	return true;
}

bool Analyzer::TryAnalyzeInitializerListVariable(NodeId expression,
	ScopeId scope, TypeId type, EntityId class_entity, bool local,
	ExpressionInfo* initializer)
{
	if (expression == kNoNode ||
		!arena_->IsTag(expression, ::cppgm::syntax::STAG_BRACED_INIT_LIST) ||
		!IsInitializerListType(type)) return false;
	*initializer = AnalyzeInitializerList(expression, scope, type);
	initializer->type = type;
	initializer->category = VALUE_NONE;
	*initializer = FinalizeVariableInitializer(
		*initializer, type, class_entity, local);
	return true;
}

bool Analyzer::ExtendInitializerListVariableLifetime(TypeId type,
	ScopeId scope, std::uint32_t initializer, bool control_dependent)
{
	if (!IsInitializerListType(type) || control_dependent) return false;
	std::vector<std::uint32_t> temporaries;
	CollectTemporaryObjects(initializer, &temporaries);
	if (temporaries.empty()) return false;
	AddTemporaryLifetimeObligation(scope, temporaries.back());
	return true;
}

std::uint32_t Analyzer::InitializerListBackingTemporary(
	TypeId type, std::uint32_t initializer) const
{
	if (!IsInitializerListType(type) || initializer == kNoDumpEdge ||
		initializer >= dump_.nodes.size() ||
		dump_.nodes[initializer].kind != DUMP_INITIALIZER_LIST)
		return kNoDumpEdge;
	const std::uint32_t edge = dump_.nodes[initializer].first_edge;
	if (edge == kNoDumpEdge || dump_.edges[edge].next != kNoDumpEdge)
		return kNoDumpEdge;
	const std::uint32_t backing = dump_.edges[edge].child;
	return dump_.nodes[backing].kind == DUMP_TEMPORARY_OBJECT ?
		backing : kNoDumpEdge;
}

bool Analyzer::HasActiveInitializerListBacking(ScopeId scope) const
{
	++initializer_list_lifetime_queries_;
	return scope < nearest_initializer_list_lifetime_scopes_.size() &&
		nearest_initializer_list_lifetime_scopes_[scope] != kNoScope;
}

void Analyzer::MarkInitializerListLifetimeScope(
	ScopeId scope, std::uint32_t temporary)
{
	if (!dump_.nodes[temporary].initializer_list_backing) return;
	if (nearest_initializer_list_lifetime_scopes_.size() <= scope)
		nearest_initializer_list_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	nearest_initializer_list_lifetime_scopes_[scope] = scope;
}

void Analyzer::MarkInitializerListLifetimeCalls(std::uint32_t node)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return;
	DumpNode& record = dump_.nodes[node];
	if (record.kind == DUMP_CALL_EXPRESSION)
		record.full_expression_staging = true;
	for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
		edge = dump_.edges[edge].next)
		MarkInitializerListLifetimeCalls(dump_.edges[edge].child);
}

void Analyzer::ConfigureInitializerListBackingLifetime(
	std::uint32_t backing, TypeId element_type)
{
	const EntityId entity = DestructedEntity(element_type);
	if (entity == kNoEntity || program_->entities[entity].trivial_destructor)
		return;
	const BindingId destructor = DestructorForType(element_type);
	if (destructor == kNoBinding || GetFunction(destructor).deleted_destructor)
		ThrowSemanticError(
			"initializer-list element is not destructible");
	dump_.nodes[backing].selected_binding = destructor;
	DemandFunction(destructor);
}

std::uint32_t Analyzer::PrepareNamespaceInitializerListLifetime(
	TypeId type, std::uint32_t initializer, std::uint32_t destructor,
	std::uint32_t* backing)
{
	*backing = InitializerListBackingTemporary(type, initializer);
	if (*backing == kNoDumpEdge) return destructor;
	const std::uint32_t backing_destructor =
		MakeTemporaryDestructorAction(*backing);
	if (backing_destructor == kNoDumpEdge) return destructor;
	if (destructor != kNoDumpEdge)
		ThrowInternalCompilerError(
			"initializer-list object has two namespace destructors");
	return backing_destructor;
}

BindingId Analyzer::SelectInitializerListConstructorPhase(
	ScopeId scope, TypeId initialized_type, NodeId source_list,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<BindingId>& candidates, bool copy_initialization,
	std::vector<CallConversionFact>* selected_conversions, bool quiet,
	NodeId* selected_source)
{
	if (source_list == kNoNode) return kNoBinding;
	const bool empty_list = argument_syntax.empty();
	for (std::size_t c = 0; empty_list && c < candidates.size(); ++c)
	{
		const FunctionInfo& candidate = GetFunction(candidates[c]);
		const TypeRecord& function = program_->types.Get(candidate.type);
		std::size_t required = function.parameter_count;
		while (required != 0 && required <= candidate.parameters.size() &&
			candidate.parameters[required - 1].default_argument != kNoNode)
			--required;
		if (candidate.constructor && required == 0) return kNoBinding;
	}
	const auto select_source = [this, scope, initialized_type, &candidates,
		copy_initialization, selected_conversions, quiet](NodeId source) -> BindingId
	{
		const std::vector<NodeId> syntax(1, source);
		const std::vector<ExpressionInfo> arguments(1);
		std::vector<BindingId> phase_candidates(candidates);
		AppendConstructorTemplateCandidates(initialized_type, arguments,
			&phase_candidates, &syntax, scope);
		std::vector<BindingId> initializer_candidates;
		for (std::size_t c = 0; c < phase_candidates.size(); ++c)
		{
			const FunctionInfo& candidate = GetFunction(phase_candidates[c]);
			const TypeRecord& function = program_->types.Get(candidate.type);
			if (candidate.constructor && function.parameter_count != 0 &&
				IsInitializerListType(
					program_->types.Parameters(candidate.type)[0]))
				initializer_candidates.push_back(phase_candidates[c]);
		}
		if (initializer_candidates.empty()) return kNoBinding;
		std::vector<CallConversionFact> trial_conversions;
		const BindingId trial = SelectConstructor(scope, syntax, arguments,
			initializer_candidates, copy_initialization, false,
			&trial_conversions, true);
		return trial == kNoBinding ? kNoBinding : SelectConstructor(scope,
			syntax, arguments, initializer_candidates, copy_initialization,
			false, selected_conversions, quiet);
	};
	BindingId selected = select_source(source_list);
	NodeId source = source_list;
	if (selected == kNoBinding && argument_syntax.size() == 1 &&
		arena_->IsTag(argument_syntax[0], ::cppgm::syntax::STAG_BRACED_INIT_LIST))
	{
		source = argument_syntax[0];
		selected = select_source(source);
	}
	if (selected != kNoBinding && selected_source) *selected_source = source;
	return selected;
}

ExpressionInfo Analyzer::MaterializeBracedConstructorArgument(
	NodeId syntax, ScopeId scope, TypeId parameter_type)
{
	const TypeRecord parameter = program_->types.Get(parameter_type);
	const bool reference = parameter.kind == TYPE_LVALUE_REFERENCE ||
		parameter.kind == TYPE_RVALUE_REFERENCE;
	const TypeId target = reference ? parameter.child : parameter_type;
	ExpressionInfo argument = AnalyzeBracedCallArgument(syntax, scope, target);
	argument.category = VALUE_PRVALUE;
	dump_.nodes[argument.node].category = VALUE_PRVALUE;
	const EntityId entity = EntityOf(target);
	if (entity != kNoEntity && program_->entities[entity].is_aggregate &&
		dump_.nodes[argument.node].kind == DUMP_BRACED_INIT_LIST)
		argument.node = BuildAggregateConstructionAction(target, argument.node);
	if (reference &&
		dump_.nodes[argument.node].kind == DUMP_INITIALIZER_LIST)
	{
		argument = MaterializeTemporary(argument);
		dump_.nodes[argument.node].argument_materialization = true;
	}
	return argument;
}

ExpressionInfo Analyzer::AnalyzeInitializerList(
	NodeId list, ScopeId scope, TypeId type)
{
	TypeId element = kNoType;
	if (!IsInitializerListType(type, &element))
		ThrowInternalCompilerError("initializer-list object has non-list type");
	ConfigureInitializerListSpecialization(type);
	std::size_t count = 0;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge)) ++count;

	const std::uint32_t object = MakeDump(
		DUMP_INITIALIZER_LIST, program_->types.RemoveTopCv(EffectiveType(type)),
		VALUE_PRVALUE);
	dump_.nodes[object].operand_type = element;
	dump_.nodes[object].array_count = count;
	if (count != 0)
	{
		TypeId array_type = program_->types.Array(
			program_->types.Qualify(element, CV_CONST), count);
		std::uint32_t edge = arena_->FirstEdge(list);
		ExpressionInfo backing = AnalyzeArrayAggregateInit(
			array_type, scope, &edge);
		if (edge != kNoEdge)
			ThrowSemanticError("excess initializer-list elements");
		backing = MaterializeTemporary(backing);
		dump_.nodes[backing.node].initializer_list_backing = true;
		ConfigureInitializerListBackingLifetime(backing.node, element);
		dump_.Add(object, backing.node);
	}
	ExpressionInfo result;
	result.node = object;
	result.type = program_->types.RemoveTopCv(EffectiveType(type));
	result.category = VALUE_PRVALUE;
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

ExpressionInfo Analyzer::BuildInitializerListFromValues(
	TypeId type, const std::vector<ExpressionInfo>& values)
{
	TypeId element = kNoType;
	if (!IsInitializerListType(type, &element))
		ThrowInternalCompilerError("initializer-list values have non-list type");
	ConfigureInitializerListSpecialization(type);
	const std::uint32_t object = MakeDump(
		DUMP_INITIALIZER_LIST, program_->types.RemoveTopCv(EffectiveType(type)),
		VALUE_PRVALUE);
	dump_.nodes[object].operand_type = element;
	dump_.nodes[object].array_count = values.size();
	if (!values.empty())
	{
		const TypeId array_type = program_->types.Array(
			program_->types.Qualify(element, CV_CONST), values.size());
		const std::uint32_t list = MakeDump(
			DUMP_BRACED_INIT_LIST, array_type, VALUE_LVALUE);
		for (std::size_t i = 0; i < values.size(); ++i)
			dump_.Add(list, ApplyTarget(values[i], element).node);
		ExpressionInfo backing;
		backing.node = list;
		backing.type = array_type;
		backing.category = VALUE_LVALUE;
		backing = MaterializeTemporary(backing);
		dump_.nodes[backing.node].initializer_list_backing = true;
		ConfigureInitializerListBackingLifetime(backing.node, element);
		dump_.Add(object, backing.node);
	}
	ExpressionInfo result;
	result.node = object;
	result.type = program_->types.RemoveTopCv(EffectiveType(type));
	result.category = VALUE_PRVALUE;
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

}
}
