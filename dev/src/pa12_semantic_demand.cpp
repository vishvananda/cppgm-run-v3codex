#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::CompleteTranslationUnitDemand()
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

void SemanticAnalyzer::MarkFunctionObjectOutputRoot(BindingId binding)
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

void SemanticAnalyzer::DemandRuntimeFunction(BindingId binding,
	FunctionDemandReason reason)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	RecordFunctionDemand(binding, reason);
	EnsureFunctionExceptionSpecification(binding);
	if (current_function_context_ != kNoBinding &&
		!FunctionObjectDefinitionRequired(current_function_context_)) return;
	DemandRuntimeDefinition(binding);
}

bool SemanticAnalyzer::FunctionObjectDefinitionRequired(
	BindingId binding) const
{
	if (binding == kNoBinding) return false;
	binding = program_->bindings[binding].canonical;
	const BindingRecord& record = program_->bindings[binding];
	return !record.explicit_instantiation_suppressed &&
		(!record.inline_function || record.emission_demanded ||
		 record.object_output_root);
}

void SemanticAnalyzer::ReplayFunctionDemandEdges(BindingId binding)
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

void SemanticAnalyzer::ReplayRequiredFunctionDemandEdges()
{
	for (std::size_t i = 0; i < functions_with_demand_edges_.size(); ++i)
		if (FunctionObjectDefinitionRequired(functions_with_demand_edges_[i]))
			ReplayFunctionDemandEdges(functions_with_demand_edges_[i]);
}

void SemanticAnalyzer::DemandRuntimeDefinition(BindingId binding)
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
	if (binding >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[binding] == kNoDumpEdge) return;
	FunctionInfo& function = GetMutableFunction(binding);
	if (!function.deferred ||
		function.definition_state != FUNCTION_DEFINITION_NOT_STARTED) return;
	function.definition_state = FUNCTION_DEFINITION_QUEUED;
	demanded_functions_.push_back(binding);
	++demand_worklist_pushes_;
}

void SemanticAnalyzer::CompleteFunctionDefinition(BindingId binding)
{
	binding = program_->bindings[binding].canonical;
	GetMutableFunction(binding).definition_state =
		FUNCTION_DEFINITION_COMPLETE;
	++demanded_function_emissions_;
	if (!stats_) return;
	if (FunctionObjectDefinitionRequired(binding))
		++stats_->definition_emission_required_completions;
	else ++stats_->definition_validation_only_completions;
}

}
}
