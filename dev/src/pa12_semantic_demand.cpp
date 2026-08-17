#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

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
	if (binding >= function_demand_head_by_binding_.size()) return;
	for (std::uint32_t edge = function_demand_head_by_binding_[binding];
		edge != kNoDumpEdge; edge = function_demand_edges_[edge].next)
		DemandRuntimeDefinition(function_demand_edges_[edge].callee);
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

}
}
