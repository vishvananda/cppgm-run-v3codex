#include "pa12_semantic_detail.h"

#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::IsInitializerListType(
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

bool SemanticAnalyzer::IsStandardInitializerListTemplate(NameId name,
	ScopeId owner, const std::vector<TemplateParameter>& parameters) const
{
	while (owner != kNoScope && program_->IsInlineNamespace(owner))
		owner = program_->ParentScope(owner);
	return program_->names.Get(name) == "initializer_list" &&
		owner != kNoScope && program_->KindOfScope(owner) == SCOPE_NAMESPACE &&
		program_->names.Get(program_->NameOfScope(owner)) == "std" &&
		program_->ParentScope(owner) == program_->GlobalScope() &&
		parameters.size() == 1 &&
		parameters[0].kind == TEMPLATE_ARGUMENT_TYPE && !parameters[0].pack;
}

void SemanticAnalyzer::ConfigureInitializerListSpecialization(TypeId type)
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

bool SemanticAnalyzer::DeduceInitializerListElementType(
	NodeId list, ScopeId scope, TypeId* element_type)
{
	if (list == kNoNode || !arena_->IsTag(list, "braced-init-list") ||
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

bool SemanticAnalyzer::TryAnalyzeInitializerListVariable(NodeId expression,
	ScopeId scope, TypeId type, EntityId class_entity, bool local,
	ExpressionInfo* initializer)
{
	if (expression == kNoNode ||
		!arena_->IsTag(expression, "braced-init-list") ||
		!IsInitializerListType(type)) return false;
	*initializer = AnalyzeInitializerList(expression, scope, type);
	initializer->type = type;
	initializer->category = VALUE_NONE;
	*initializer = FinalizeVariableInitializer(
		*initializer, type, class_entity, local);
	return true;
}

void SemanticAnalyzer::ExtendInitializerListVariableLifetime(TypeId type,
	ScopeId scope, std::uint32_t initializer, bool control_dependent)
{
	if (!IsInitializerListType(type) || control_dependent) return;
	std::vector<std::uint32_t> temporaries;
	CollectTemporaryObjects(initializer, &temporaries);
	if (!temporaries.empty())
		AddTemporaryLifetimeObligation(scope, temporaries.back());
}

BindingId SemanticAnalyzer::SelectInitializerListConstructorPhase(
	ScopeId scope, NodeId source_list,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<BindingId>& candidates, bool copy_initialization,
	std::vector<CallConversionFact>* selected_conversions, bool quiet)
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
	std::vector<BindingId> initializer_candidates;
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		const FunctionInfo& candidate = GetFunction(candidates[c]);
		const TypeRecord& function = program_->types.Get(candidate.type);
		if (!candidate.constructor || function.parameter_count == 0) continue;
		if (IsInitializerListType(
			program_->types.Parameters(candidate.type)[0]))
			initializer_candidates.push_back(candidates[c]);
	}
	if (initializer_candidates.empty()) return kNoBinding;
	const std::vector<NodeId> phase_syntax(1, source_list);
	const std::vector<ExpressionInfo> phase_arguments(1);
	std::vector<CallConversionFact> phase_conversions;
	const BindingId phase = SelectConstructor(scope, phase_syntax,
		phase_arguments, initializer_candidates, copy_initialization, false,
		&phase_conversions, true);
	return phase == kNoBinding ? kNoBinding : SelectConstructor(scope,
		phase_syntax, phase_arguments, initializer_candidates,
		copy_initialization, false, selected_conversions, quiet);
}

ExpressionInfo SemanticAnalyzer::MaterializeBracedConstructorArgument(
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

ExpressionInfo SemanticAnalyzer::AnalyzeInitializerList(
	NodeId list, ScopeId scope, TypeId type)
{
	TypeId element = kNoType;
	if (!IsInitializerListType(type, &element))
		throw std::logic_error("initializer-list object has non-list type");
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
			throw std::runtime_error("excess initializer-list elements");
		backing = MaterializeTemporary(backing);
		dump_.nodes[backing.node].initializer_list_backing = true;
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

ExpressionInfo SemanticAnalyzer::BuildInitializerListFromValues(
	TypeId type, const std::vector<ExpressionInfo>& values)
{
	TypeId element = kNoType;
	if (!IsInitializerListType(type, &element))
		throw std::logic_error("initializer-list values have non-list type");
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
