// Source-facing PA11 projections over the canonical semantic graph.
#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <string>
#include <vector>

namespace cppgm { namespace semantic {

void Analyzer::RecordSourceTypeOverride(
	BindingId binding, TypeId type)
{
	if (!source_type_view_ || binding == kNoBinding ||
		binding >= program_->bindings.size()) return;
	if (program_->bindings[binding].type == type) return;
	source_type_override_bindings_.push_back(binding);
	source_type_override_types_.push_back(type);
}

void Analyzer::ApplySourceTypeOverrides()
{
	if (source_type_override_bindings_.size() !=
		source_type_override_types_.size())
		ThrowInternalCompilerError("source-view type overrides diverged");
	for (std::size_t i = 0; i < source_type_override_bindings_.size(); ++i)
		program_->bindings[source_type_override_bindings_[i]].type =
			source_type_override_types_[i];
}

void Analyzer::ProjectSourceClassTemplate(
	NodeId declaration, ScopeId scope,
	const std::vector<TemplateParameter>& parameters)
{
	const ScopeId parameter_scope = program_->NewScope(
		scope, SCOPE_TEMPLATE_PARAMETERS, 0);
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		const TemplateParameter& parameter = parameters[i];
		if (parameter.kind != TEMPLATE_ARGUMENT_TYPE &&
			parameter.kind != TEMPLATE_ARGUMENT_TEMPLATE)
			ThrowSemanticError(
				"non-type template parameter is outside PA11");
		if (parameter.name == 0) continue;
		const NamedFlavor flavor = parameter.kind == TEMPLATE_ARGUMENT_TEMPLATE ?
			NAMED_TEMPLATE_PARAMETER : NAMED_TYPENAME_PARAMETER;
		const EntityId entity = program_->NewEntity(
			parameter.name, flavor, true, kNoType, parameter_scope);
		EntityRecord& parameter_entity = program_->entities[entity];
		parameter_entity.layout_complete = true;
		parameter_entity.object_size = 1;
		parameter_entity.nonvirtual_size = 1;
		parameter_entity.object_alignment = 1;
		parameter_entity.nonvirtual_alignment = 1;
		parameter_entity.natural_alignment = 1;
		const TypeId type = parameter_entity.type;
		program_->SetTypeName(parameter_scope, parameter.name, type);
		program_->AddBinding(
			parameter_scope, BIND_TYPE, parameter.name, type);
	}
	(void)AnalyzeClass(declaration, parameter_scope,
		std::string(), false);
}

} }
