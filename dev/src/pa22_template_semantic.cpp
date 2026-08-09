#include "pa12_semantic_detail.h"

#include <limits>
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

bool SemanticAnalyzer::BuildTemplateTemplateArgument(NodeId syntax,
	ScopeId scope, const TemplateParameter& parameter,
	TemplateArgument* argument)
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
		LookupSpelling(scope, PayloadSource(name), LOOKUP_TYPE) :
		LookupStructuredName(name, scope, LOOKUP_TYPE);
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
	if (!actual || !TemplateTemplateParameterMatches(
		parameter.template_parameters, *actual)) return false;
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
	const TemplateSpecializationKey key(index, arguments);
	BindingId binding = alias_template_instantiations_.Find(key);
	if (binding != kNoBinding)
	{
		++template_specialization_cache_hits_;
		if (binding >= alias_template_instantiation_states_.size())
			throw std::logic_error(
				"alias specialization has no completion state");
		const std::uint8_t state =
			alias_template_instantiation_states_[binding];
		if (state == 1)
			throw std::runtime_error("recursive alias template specialization");
		if (state == 3)
			throw std::runtime_error("invalid alias template specialization");
		if (state != 2)
			throw std::logic_error(
				"alias specialization has an invalid completion state");
		return program_->bindings[binding].type;
	}

	binding = program_->AddBinding(pattern.specialization_scope,
		BIND_TYPE_ALIAS, pattern.name, kNoType);
	if (alias_template_instantiation_states_.size() <= binding)
		alias_template_instantiation_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	alias_template_instantiation_states_[binding] = 1;
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
		const TypeId result = BuildTypeId(pattern.type_id, substitution_scope);
		if (result == kNoType)
			throw std::runtime_error(
				"alias template specialization has no result type");
		program_->bindings[binding].type = result;
		alias_template_instantiation_states_[binding] = 2;
		return result;
	}
	catch (...)
	{
		alias_template_instantiation_states_[binding] = 3;
		throw;
	}
}

}
}
