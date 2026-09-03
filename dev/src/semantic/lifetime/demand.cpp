#include "semantic/analysis/analyzer.h"
#include "semantic/diagnostics/template_witness.h"

namespace cppgm
{
namespace semantic
{

void Analyzer::CompleteTranslationUnitDemand()
{
	if (source_type_view_)
		for (std::size_t i = 0; i < functions_.size(); ++i)
		{
			const FunctionInfo& function = functions_[i];
			if (function.definition_body != kNoNode &&
				!program_->bindings[function.binding].compiler_generated)
				DemandRuntimeDefinition(function.binding);
		}
	DemandMaterializedConstructorActions(root_);
	if (function_templates_.empty() && class_templates_.empty())
		for (std::size_t i = 0; i < hidden_friend_anchor_by_entity_.size(); ++i)
			if (hidden_friend_anchor_by_entity_[i] != kNoBinding &&
				!GetFunction(hidden_friend_anchor_by_entity_[i]).constexpr_function)
				DemandFunction(hidden_friend_anchor_by_entity_[i]);
	ReplayRequiredFunctionDemandEdges();
	std::size_t default_demand = 0;
	std::size_t function_demand = 0;
	std::size_t member_definition_demand = 0;
	while (member_definition_demand <
			demanded_class_template_member_definitions_.size() ||
		default_demand < demanded_default_constructor_entities_.size() ||
		function_demand < demanded_functions_.size())
	{
		while (member_definition_demand <
			demanded_class_template_member_definitions_.size())
			ApplyDemandedClassTemplateMemberDefinitions(
				demanded_class_template_member_definitions_[
					member_definition_demand++]);
		while (default_demand < demanded_default_constructor_entities_.size())
			EmitDefaultConstructor(
				demanded_default_constructor_entities_[default_demand++]);
		while (function_demand < demanded_functions_.size())
			EmitDemandedFunction(demanded_functions_[function_demand++]);
	}
}

void Analyzer::MarkFunctionObjectOutputRoot(BindingId binding)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	program_->bindings[binding].object_output_root = true;
	const BindingId lifecycle_base =
		program_->bindings[binding].lifecycle_base_entry;
	if (lifecycle_base != kNoBinding &&
		lifecycle_base < program_->bindings.size())
		program_->bindings[lifecycle_base].object_output_root = true;
	if (binding < constructor_base_entry_by_binding_.size())
	{
		const BindingId base = constructor_base_entry_by_binding_[binding];
		if (base != kNoBinding && base < program_->bindings.size())
			program_->bindings[base].object_output_root = true;
	}
	if (binding < destructor_base_entry_by_binding_.size())
	{
		const BindingId base = destructor_base_entry_by_binding_[binding];
		if (base != kNoBinding && base < program_->bindings.size())
			program_->bindings[base].object_output_root = true;
	}
}

void Analyzer::DemandRuntimeFunction(BindingId binding,
	FunctionDemandReason reason)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	if (template_witness_)
		template_witness_->RecordRequireDefinition(binding);
	RecordFunctionDemand(binding, reason);
	EnsureFunctionExceptionSpecification(binding);
	if (current_function_context_ != kNoBinding &&
		!FunctionObjectDefinitionRequired(current_function_context_)) return;
	DemandRuntimeDefinition(binding);
}

bool Analyzer::FunctionObjectDefinitionRequired(
	BindingId binding) const
{
	if (binding == kNoBinding) return false;
	binding = program_->bindings[binding].canonical;
	const BindingRecord& record = program_->bindings[binding];
	return !record.explicit_instantiation_suppressed &&
		(!record.inline_function || record.emission_demanded ||
		 record.object_output_root);
}

bool Analyzer::EntityHasTemplateLifecycleContext(EntityId entity) const
{
	for (std::size_t depth = 0; entity != kNoEntity &&
		depth < program_->entities.size(); ++depth)
	{
		const EntityRecord& record = program_->entities[entity];
		if (record.template_argument_begin != kNoBinding) return true;
		if (record.local_context != kNoBinding &&
			GetFunction(record.local_context).template_specialization)
			return true;
		entity = record.enclosing_class;
	}
	return false;
}

TemplateFunctionLifecycleFact Analyzer::InspectTemplateFunctionLifecycle(
	BindingId binding) const
{
	TemplateFunctionLifecycleFact result;
	if (binding == kNoBinding || binding >= program_->bindings.size())
		return result;
	binding = program_->bindings[binding].canonical;
	if (binding >= program_->bindings.size() ||
		program_->bindings[binding].kind != BIND_FUNCTION) return result;
	const BindingRecord& record = program_->bindings[binding];
	const FunctionInfo& function = GetFunction(binding);
	result.binding = binding;
	result.member_owner = record.member_owner;
	result.definition_state =
		static_cast<std::uint8_t>(function.definition_state);
	const auto note = [&result](bool present,
		TemplateFunctionLifecycleProperty property) {
		if (present) result.properties |= static_cast<std::uint32_t>(property);
	};
	note(function.template_specialization,
		TEMPLATE_FUNCTION_DIRECT_SPECIALIZATION);
	note(EntityHasTemplateLifecycleContext(record.member_owner),
		TEMPLATE_FUNCTION_OWNER_CONTEXT);
	note(EntityHasTemplateLifecycleContext(function.friend_of),
		TEMPLATE_FUNCTION_FRIEND_CONTEXT);
	const bool local_context = record.member_owner != kNoEntity &&
		program_->entities[record.member_owner].local_context != kNoBinding &&
		GetFunction(program_->entities[record.member_owner].local_context).
			template_specialization;
	note(local_context, TEMPLATE_FUNCTION_LOCAL_CONTEXT);
	note(function.explicit_specialization,
		TEMPLATE_FUNCTION_EXPLICIT_SPECIALIZATION);
	const EntityId owner = record.member_owner;
	const BindingId owner_declaration = owner == kNoEntity ? kNoBinding :
		program_->entities[owner].declaration;
	const bool owner_explicit_specialization = owner_declaration != kNoBinding &&
		owner_declaration <
			class_template_explicit_specialization_states_.size() &&
		class_template_explicit_specialization_states_[owner_declaration] != 0;
	note(owner_explicit_specialization,
		TEMPLATE_FUNCTION_OWNER_EXPLICIT_SPECIALIZATION);
	note(record.compiler_generated, TEMPLATE_FUNCTION_COMPILER_GENERATED);
	note(function.inherited_constructor_source != kNoBinding,
		TEMPLATE_FUNCTION_INHERITED_CONSTRUCTOR);
	note(record.explicit_instantiation_suppressed,
		TEMPLATE_FUNCTION_EXPLICIT_INSTANTIATION_SUPPRESSED);
	note(FunctionObjectDefinitionRequired(binding),
		TEMPLATE_FUNCTION_OBJECT_DEFINITION_REQUIRED);
	note(record.inline_function, TEMPLATE_FUNCTION_INLINE);
	note(record.emission_demanded, TEMPLATE_FUNCTION_EMISSION_DEMANDED);
	note(record.object_output_root, TEMPLATE_FUNCTION_OBJECT_OUTPUT_ROOT);
	note(function.defaulted_constructor || function.defaulted_destructor ||
		function.defaulted_special_member, TEMPLATE_FUNCTION_DEFAULTED);
	note(function.definition_in_class, TEMPLATE_FUNCTION_DEFINITION_IN_CLASS);
	note(function.defined, TEMPLATE_FUNCTION_DEFINED);
	bool owner_argument_context = false;
	for (EntityId context = record.member_owner; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (program_->entities[context].template_argument_begin != kNoBinding)
		{
			owner_argument_context = true;
			break;
		}
	note(owner_argument_context,
		TEMPLATE_FUNCTION_OWNER_ARGUMENT_CONTEXT);
	if (binding < function_explicit_instantiation_states_.size())
		result.explicit_instantiation_state =
			function_explicit_instantiation_states_[binding];
	if (owner_declaration <
		class_template_explicit_instantiation_states_.size())
		result.owner_explicit_instantiation_state =
			class_template_explicit_instantiation_states_[owner_declaration];
	return result;
}

void Analyzer::ReplayFunctionDemandEdges(BindingId binding)
{
	binding = program_->bindings[binding].canonical;
	FunctionInfo& function = GetMutableFunction(binding);
	if (function.emission_dependencies_replayed) return;
	function.emission_dependencies_replayed = true;
	if (stats_) ++stats_->demand_replayed_functions;
	if (binding >= function_demand_head_by_binding_.size()) return;
	for (std::uint32_t edge = function_demand_head_by_binding_[binding];
		edge != kNoDumpEdge; edge = function_demand_edges_[edge].next)
	{
		if (stats_) ++stats_->demand_replayed_edges;
		DemandRuntimeDefinition(function_demand_edges_[edge].callee);
	}
}

void Analyzer::ReplayRequiredFunctionDemandEdges()
{
	for (std::size_t i = 0; i < functions_with_demand_edges_.size(); ++i)
		if (FunctionObjectDefinitionRequired(functions_with_demand_edges_[i]))
			ReplayFunctionDemandEdges(functions_with_demand_edges_[i]);
}

void Analyzer::DemandRuntimeDefinition(BindingId binding)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	DemandClassTemplateMemberDefinitions(program_->bindings[binding].member_owner);
	program_->bindings[binding].emission_demanded |=
		program_->bindings[binding].inline_function;
	if (FunctionObjectDefinitionRequired(binding))
		ReplayFunctionDemandEdges(binding);
	if ((program_->bindings[binding].constructor ||
		 program_->bindings[binding].destructor) &&
		program_->bindings[binding].member_owner != kNoEntity &&
		program_->entities[program_->bindings[binding].member_owner].
			polymorphic_class)
		MarkVtableDemand(program_->bindings[binding].member_owner);
	if (binding < constructor_base_entry_by_binding_.size())
	{
		const BindingId base_entry =
			constructor_base_entry_by_binding_[binding];
		if (base_entry != kNoBinding && base_entry != binding)
			DemandRuntimeDefinition(base_entry);
	}
	if (binding < destructor_base_entry_by_binding_.size())
	{
		const BindingId base_entry =
			destructor_base_entry_by_binding_[binding];
		if (base_entry != kNoBinding && base_entry != binding)
			DemandRuntimeDefinition(base_entry);
	}
	QueueDeferredFunctionDefinition(binding);
}

void Analyzer::QueueDeferredFunctionDefinition(BindingId binding)
{
	if (binding >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[binding] == kNoDumpEdge) return;
	FunctionInfo& function = GetMutableFunction(binding);
	if (!function.deferred ||
		function.definition_state != FUNCTION_DEFINITION_NOT_STARTED) return;
	function.definition_state = FUNCTION_DEFINITION_QUEUED;
	demanded_functions_.push_back(binding);
	++demand_worklist_pushes_;
}

void Analyzer::CompleteFunctionDefinition(BindingId binding)
{
	binding = program_->bindings[binding].canonical;
	GetMutableFunction(binding).definition_state =
		FUNCTION_DEFINITION_COMPLETE;
	if (template_witness_)
		template_witness_->RecordFunctionInstantiation(binding);
	++demanded_function_emissions_;
	if (!stats_) return;
	if (FunctionObjectDefinitionRequired(binding))
		++stats_->definition_emission_required_completions;
	else ++stats_->definition_validation_only_completions;
}

}
}
