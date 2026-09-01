#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <vector>

namespace cppgm
{
namespace semantic
{

bool Analyzer::AnalyzeExplicitVariableInstantiation(
	NodeId target, ScopeId scope, bool definition)
{
	if (definition || !arena_->IsTag(target, ::cppgm::syntax::STAG_SIMPLE_DECLARATION) ||
		program_->KindOfScope(scope) != SCOPE_NAMESPACE) return false;
	const NodeId list = FindChild(target, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
	const NodeId item = list == kNoNode ? kNoNode : FirstSemanticChild(list);
	const NodeId declarator = item == kNoNode ? kNoNode :
		FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
	if (declarator == kNoNode) return false;
	const std::uint32_t first = arena_->FirstEdge(list);
	if (first != kNoEdge && arena_->NextEdge(first) != kNoEdge)
		ThrowSemanticError(
			"explicit variable instantiation has multiple declarators");
	const ScopeId owner = ResolveStructuredDeclaratorOwner(declarator, scope);
	if (owner == kNoScope) return false;
	const NameId name = DeclaratorName(declarator);
	const LookupResult found = program_->LookupDirect(
		owner, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding ||
		program_->bindings[found.ordinary].kind != BIND_VARIABLE ||
		program_->bindings[found.ordinary].member_owner == kNoEntity)
		return false;
	BindingRecord& binding = program_->bindings[
		program_->bindings[found.ordinary].canonical];
	binding.explicit_instantiation_suppressed = true;
	return true;
}

bool Analyzer::ConstructorSubobjectsAreEmpty(BindingId constructor)
{
	const FunctionInfo& function = GetFunction(constructor);
	if (function.constructor_initializer != kNoNode ||
		function.definition_body == kNoNode ||
		FirstSemanticChild(function.definition_body) != kNoNode)
		return false;
	const EntityId entity = program_->bindings[constructor].member_owner;
	if (entity == kNoEntity || entity >= entity_data_members_.size())
		return false;
	const auto empty_default = [this](EntityId subobject) {
		if (program_->entities[subobject].trivial_default_constructor)
			return true;
		if (subobject >= entity_constructors_.size()) return false;
		BindingId selected = kNoBinding;
		const std::vector<BindingId>& candidates = entity_constructors_[subobject];
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const FunctionInfo& candidate = GetFunction(candidates[i]);
			std::size_t required = candidate.parameters.size();
			while (required != 0 && candidate.parameters[required - 1].
				default_argument != kNoNode) --required;
			if (!candidate.constructor || candidate.deleted_constructor ||
				required != 0) continue;
			if (selected != kNoBinding) return false;
			selected = candidates[i];
		}
		if (selected == kNoBinding) return false;
		std::vector<BindingId> dependencies;
		return EmptyDefaultConstructorChain(selected, &dependencies);
	};
	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		const BindingRecord& member = program_->bindings[members[i]];
		if (member.has_default_member_initializer) return false;
		TypeId type = member.type;
		TypeRecord shape = program_->types.Get(type);
		while (shape.kind == TYPE_ARRAY || shape.kind == TYPE_QUALIFIED)
		{
			type = shape.child;
			shape = program_->types.Get(type);
		}
		if (shape.kind == TYPE_NAMED && !empty_default(shape.entity))
			return false;
	}
	for (std::size_t i = 0;
		i < program_->entities[entity].direct_base_count; ++i)
		if (!empty_default(program_->DirectBase(entity, i).entity)) return false;
	return true;
}

ScopeId Analyzer::ResolveStructuredDeclaratorOwner(
	NodeId declarator, ScopeId scope, bool routed_owner)
{
	if (routed_owner) return program_->ParentScope(scope);
	const NamePath path = DeclaratorNamePath(declarator);
	const NodeId structure = DeclaratorNameStructure(declarator);
	if (structure == kNoNode || (!path.global && path.Size() <= 1))
		return kNoScope;
	ScopeId owner = kNoScope;
	(void)LookupStructuredName(
		structure, scope, LOOKUP_ORDINARY, &owner);
	return owner;
}

void Analyzer::MergeFunctionRedeclarationParameters(
	FunctionInfo* function, const std::vector<ParameterInfo>& parameters,
	bool definition)
{
	if (!function)
		ThrowInternalCompilerError("missing function redeclaration fact");
	if (!definition || parameters.empty())
	{
		if (function->parameters.empty() && !parameters.empty())
			function->parameters = parameters;
		return;
	}
	std::vector<ParameterInfo> replacement = parameters;
	if (replacement.size() == function->parameters.size())
		for (std::size_t i = 0; i < replacement.size(); ++i)
		{
			if (replacement[i].name == 0)
				replacement[i].name = function->parameters[i].name;
			if (replacement[i].pack_name == 0)
				replacement[i].pack_name = function->parameters[i].pack_name;
			if (replacement[i].default_argument == kNoNode)
			{
				replacement[i].default_argument =
					function->parameters[i].default_argument;
				replacement[i].default_scope =
					function->parameters[i].default_scope;
			}
		}
	function->parameters.swap(replacement);
}

BindingId Analyzer::MatchingInjectedClassTemplateSpecialization(
	const LookupResult& found, std::size_t pattern,
	const std::vector<TemplateArgument>& arguments) const
{
	if (found.type == kNoType || pattern >= class_templates_.size())
		return kNoBinding;
	const TypeRecord& type = program_->types.Get(
		program_->types.RemoveTopCv(found.type));
	if (type.kind != TYPE_NAMED ||
		type.entity == class_templates_[pattern].marker_entity ||
		type.entity >= class_template_pattern_by_entity_.size() ||
		class_template_pattern_by_entity_[type.entity] != pattern)
		return kNoBinding;
	const EntityRecord& entity = program_->entities[type.entity];
	if (entity.template_argument_begin == kNoBinding ||
		entity.template_argument_count != arguments.size()) return kNoBinding;
	return StoredTemplateArguments(entity.template_argument_begin,
		entity.template_argument_count) == arguments ?
		entity.declaration : kNoBinding;
}

}
}
