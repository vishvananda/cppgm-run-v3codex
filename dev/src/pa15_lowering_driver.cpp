#include "pa15_lowering.h"

#include "pa12_semantic.h"
#include "pa15_force_inline.h"
#include "pa15_graph_lowering.h"
#include "pa15_lowir_model.h"
#include "pa15_lowir_render.h"
#include "pa15_lowering_support.h"

#include <algorithm>
#include <chrono>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_detail;
using namespace pa15_lowering_support;

namespace
{

class GraphConsumer : public SemanticGraphConsumer
{
public:
	GraphConsumer(TypedProgram& program, LowIRLoweringStats* stats,
		std::size_t source_ordinal)
		: program_(program), stats_(stats), source_ordinal_(source_ordinal) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		LowerSemanticGraph(graph, program_, stats_, source_ordinal_);
		if (stats_)
			stats_->lowering_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count());
	}

private:
	TypedProgram& program_;
	LowIRLoweringStats* stats_;
	std::size_t source_ordinal_;
};

SymbolId AddLifecycleHelperSymbol(TypedProgram* program,
	const std::string& proposed)
{
	std::size_t& count = program->symbol_name_counts[proposed];
	const std::string name = count++ == 0 ? proposed :
		proposed + "__sym" + std::to_string(count);
	const SymbolId symbol = static_cast<SymbolId>(program->symbols.size());
	program->symbols.push_back(Symbol(Symbol::FUNCTION_SYMBOL, name,
		std::string(), false, true, false));
	Symbol& record = program->symbols.back();
	record.definition_emitted = true;
	record.referenced = true;
	return symbol;
}

void CoalesceLifecycleRole(TypedProgram* program, LowIRLoweringStats* stats,
	bool initializer)
{
	std::vector<std::size_t> owners;
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		const Function& function = program->functions[i];
		if (initializer ? function.initializer : function.finalizer)
			owners.push_back(i);
	}
	if (owners.size() <= 1) return;

	const SymbolId role_symbol = program->functions[owners[0]].symbol;
	std::vector<SymbolId> helpers;
	helpers.reserve(owners.size());
	for (std::size_t i = 0; i < owners.size(); ++i)
	{
		Function& function = program->functions[owners[i]];
		if (i == 0)
			function.symbol = AddLifecycleHelperSymbol(program,
				initializer ? "__cppgm_tu_init" : "__cppgm_tu_fini");
		function.initializer = false;
		function.finalizer = false;
		program->symbols[function.symbol].referenced = true;
		helpers.push_back(function.symbol);
	}

	Function aggregate;
	aggregate.symbol = role_symbol;
	aggregate.result = LowVoid();
	aggregate.initializer = initializer;
	aggregate.finalizer = !initializer;
	Block entry("entry");
	entry.selected = true;
	for (std::size_t i = 0; i < helpers.size(); ++i)
	{
		const std::size_t index = initializer ? i : helpers.size() - i - 1;
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION, helpers[index], LowPtr());
		entry.instructions.push_back(call);
	}
	entry.instructions.push_back(Instruction(Instruction::RETURN_VOID));
	entry.terminated = true;
	aggregate.blocks.push_back(entry);
	aggregate.block_order.push_back(static_cast<BlockId>(0));
	program->functions.push_back(aggregate);
	if (stats)
	{
		++stats->functions;
		++stats->blocks;
		stats->instructions += helpers.size() + 1;
	}
}

void CoalesceLifecycleFunctions(TypedProgram* program,
	LowIRLoweringStats* stats)
{
	CoalesceLifecycleRole(program, stats, true);
	CoalesceLifecycleRole(program, stats, false);
	std::size_t initializer = program->functions.size();
	std::size_t finalizer = program->functions.size();
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		if (program->functions[i].initializer) initializer = i;
		if (program->functions[i].finalizer) finalizer = i;
	}
	if (initializer < program->functions.size() && finalizer < initializer)
		std::rotate(program->functions.begin() + finalizer,
			program->functions.begin() + initializer,
			program->functions.begin() + initializer + 1);
}

void AccumulateLambdaCaptureStats(SemanticAnalysisStats* target,
	const SemanticAnalysisStats& source)
{
	target->lambda_capture_summary_requests +=
		source.lambda_capture_summary_requests;
	target->lambda_capture_summary_cache_hits +=
		source.lambda_capture_summary_cache_hits;
	target->lambda_capture_syntax_visits +=
		source.lambda_capture_syntax_visits;
	target->lambda_capture_name_uses += source.lambda_capture_name_uses;
}

void AccumulateLifetimeQueryStats(SemanticAnalysisStats* target,
	const SemanticAnalysisStats& source)
{
	target->enclosing_lifetime_queries += source.enclosing_lifetime_queries;
	target->initializer_list_lifetime_queries +=
		source.initializer_list_lifetime_queries;
}

void AccumulateVirtualBaseStats(SemanticAnalysisStats* target,
	const SemanticAnalysisStats& source)
{
	target->virtual_base_layout_edge_visits +=
		source.virtual_base_layout_edge_visits;
	target->virtual_base_layout_facts += source.virtual_base_layout_facts;
	target->virtual_base_layout_lookups += source.virtual_base_layout_lookups;
	target->virtual_base_layout_probes += source.virtual_base_layout_probes;
	target->direct_base_validation_visits +=
		source.direct_base_validation_visits;
	target->polymorphic_virtual_view_lookups +=
		source.polymorphic_virtual_view_lookups;
	target->polymorphic_virtual_view_merges +=
		source.polymorphic_virtual_view_merges;
}

void AccumulateFunctionDemandStats(SemanticAnalysisStats* target,
	const SemanticAnalysisStats& source)
{
	target->demand_worklist_pushes += source.demand_worklist_pushes;
	target->demanded_function_emissions +=
		source.demanded_function_emissions;
	target->definition_validation_only_completions +=
		source.definition_validation_only_completions;
	target->definition_emission_required_completions +=
		source.definition_emission_required_completions;
	target->default_constructor_emissions +=
		source.default_constructor_emissions;
	target->demand_requests += source.demand_requests;
	target->demand_unique_edges += source.demand_unique_edges;
	target->demand_root_edges += source.demand_root_edges;
	target->demand_dependency_edges += source.demand_dependency_edges;
	target->demand_replayed_functions += source.demand_replayed_functions;
	target->demand_replayed_edges += source.demand_replayed_edges;
	target->demand_evaluated_use_requests +=
		source.demand_evaluated_use_requests;
	target->demand_retained_call_requests +=
		source.demand_retained_call_requests;
	target->demand_address_requests += source.demand_address_requests;
	target->demand_lifecycle_requests += source.demand_lifecycle_requests;
	target->demand_vtable_requests += source.demand_vtable_requests;
	target->demand_static_lifecycle_requests +=
		source.demand_static_lifecycle_requests;
	target->demand_exception_cleanup_requests +=
		source.demand_exception_cleanup_requests;
	target->demand_explicit_instantiation_requests +=
		source.demand_explicit_instantiation_requests;
	target->demand_abi_support_requests += source.demand_abi_support_requests;
}

void AccumulateSemanticStorageStats(SemanticAnalysisStats* target,
	const SemanticAnalysisStats& source)
{
	target->semantic_program_storage_bytes +=
		source.semantic_program_storage_bytes;
	target->semantic_dump_storage_bytes += source.semantic_dump_storage_bytes;
	target->semantic_side_storage_bytes += source.semantic_side_storage_bytes;
	target->semantic_shared_string_bytes += source.semantic_shared_string_bytes;
	target->binding_layout_fact_records += source.binding_layout_fact_records;
	target->binding_template_fact_records += source.binding_template_fact_records;
	target->binding_output_fact_records += source.binding_output_fact_records;
	target->binding_operator_fact_records += source.binding_operator_fact_records;
	target->binding_value_records += source.binding_value_records;
	target->semantic_storage_bytes += source.semantic_storage_bytes;
	target->peak_stage_storage_bytes = std::max(
		target->peak_stage_storage_bytes, source.peak_stage_storage_bytes);
}

}

LowIRLoweringStats::LowIRLoweringStats()
	: source_bytes(0), semantic(), lowered_nodes(0), functions(0), globals(0),
	  blocks(0), instructions(0), binding_index_probes(0),
	  slot_implicit_object_fact_reads(0),
	  virtual_calls(0), vptr_stores(0), virtual_base_boundary_scan_nodes(0),
	  virtual_base_boundary_facts(0),
	  virtual_base_call_arguments(0),
	  virtual_base_boundary_binding_steps(0),
	  virtual_base_boundary_binding_cache_hits(0),
	  virtual_base_boundary_binding_table_growth(0),
	  vtable_offset_rows(0), vtable_slots(0), vtable_thunk_requests(0),
	  vtable_thunk_cache_hits(0), vtable_thunk_index_probes(0),
	  deleting_destructors(0), rtti_graph_nodes_visited(0),
	  rtti_demand_requests(0), rtti_types_demanded(0),
	  rtti_symbol_lookups(0), rtti_base_dependency_visits(0),
	  cleanup_dispatch_probes(0), cleanup_dispatch_cache_hits(0),
	  cleanup_dispatch_entries(0), conditional_lifetime_slots(0),
	  conditional_lifetime_marks(0), branch_cleanup_actions(0),
	  statement_scheduler_entries(0), statement_scheduler_nested_entries(0),
	  statement_scheduler_tasks(0), statement_scheduler_peak_tasks(0),
	  exception_selector_resets(0), exception_selector_table_growth(0),
	  exception_selector_assignments(0), force_inline_candidates(0),
	  force_inline_recursive_candidates(0), force_inline_call_probes(0),
	  force_inline_calls(0), force_inline_blocks(0),
	  force_inline_cloned_instructions(0), post_inline_reachable_functions(0),
	  post_inline_unreachable_weak_functions(0), post_inline_pruned_functions(0),
	  post_inline_retained_external_strong(0),
	  post_inline_retained_address_or_relocation(0),
	  post_inline_retained_direct_call(0), post_inline_retained_lifecycle(0),
	  post_inline_retained_eh_or_runtime(0),
	  post_inline_retained_required_weak(0),
	  post_inline_retained_conservative_fallback(0),
	  typed_storage_bytes(0), output_bytes(0), lowering_nanoseconds(0),
	  render_nanoseconds(0)
{
}

TypedProgram BuildTypedLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, LowIRLoweringStats* stats,
	bool complete_constructor_unwind, bool host_object_emission,
	bool prune_unreachable_weak_functions)
{
	if (sources.empty()) throw std::runtime_error("no PA15 source inputs");
	if (stats) *stats = LowIRLoweringStats();
	TypedProgram program;
	program.host_object_emission = host_object_emission;
	for (std::size_t i = 0; i < sources.size(); ++i)
	{
		GraphConsumer consumer(program, stats, i);
		SemanticAnalysisStats semantic_stats;
		ConsumeSemanticTranslationUnit(sources[i].path, sources[i].source, options,
			consumer, stats ? &semantic_stats : 0, complete_constructor_unwind,
			host_object_emission);
		if (stats)
		{
			SemanticAnalysisStats& semantic = stats->semantic;
			stats->source_bytes += sources[i].source.size();
			semantic.interning.Accumulate(semantic_stats.interning);
			semantic.tokens += semantic_stats.tokens;
			semantic.syntax_nodes += semantic_stats.syntax_nodes;
			semantic.semantic_nodes += semantic_stats.semantic_nodes;
			semantic.semantic_edges += semantic_stats.semantic_edges;
			semantic.interned_names += semantic_stats.interned_names;
			semantic.canonical_types += semantic_stats.canonical_types;
			semantic.scopes += semantic_stats.scopes;
			semantic.declarations += semantic_stats.declarations;
			semantic.expressions += semantic_stats.expressions;
			semantic.class_layouts += semantic_stats.class_layouts;
			semantic.class_layout_member_visits += semantic_stats.class_layout_member_visits;
			AccumulateVirtualBaseStats(&semantic, semantic_stats);
			semantic.class_zero_offset_subobject_visits += semantic_stats.class_zero_offset_subobject_visits;
			semantic.special_member_fact_lookups +=
				semantic_stats.special_member_fact_lookups;
			semantic.special_member_subobject_visits +=
				semantic_stats.special_member_subobject_visits;
			semantic.constructor_member_action_visits +=
				semantic_stats.constructor_member_action_visits;
			semantic.constructor_base_action_visits +=
				semantic_stats.constructor_base_action_visits;
			semantic.constructor_delegation_action_visits +=
				semantic_stats.constructor_delegation_action_visits;
			semantic.destructor_subobject_action_visits +=
				semantic_stats.destructor_subobject_action_visits;
			semantic.lexical_cleanup_action_visits +=
				semantic_stats.lexical_cleanup_action_visits;
			semantic.unwind_cleanup_scope_visits +=
				semantic_stats.unwind_cleanup_scope_visits;
			semantic.unwind_cleanup_action_visits +=
				semantic_stats.unwind_cleanup_action_visits;
			AccumulateLifetimeQueryStats(&semantic, semantic_stats);
			semantic.temporary_dependency_visits += semantic_stats.temporary_dependency_visits;
			semantic.materialized_demand_visits +=
				semantic_stats.materialized_demand_visits;
			semantic.nonthrowing_action_visits +=
				semantic_stats.nonthrowing_action_visits;
			semantic.runtime_initializer_visits +=
				semantic_stats.runtime_initializer_visits;
			semantic.static_constant_initializer_visits +=
				semantic_stats.static_constant_initializer_visits;
			semantic.static_constant_dependency_edges +=
				semantic_stats.static_constant_dependency_edges;
			semantic.empty_constructor_chain_requests +=
				semantic_stats.empty_constructor_chain_requests;
			semantic.empty_constructor_chain_cache_hits +=
				semantic_stats.empty_constructor_chain_cache_hits;
			semantic.empty_constructor_chain_entity_visits +=
				semantic_stats.empty_constructor_chain_entity_visits;
			semantic.empty_constructor_chain_dependency_edges +=
				semantic_stats.empty_constructor_chain_dependency_edges;
			semantic.empty_destructor_chain_visits +=
				semantic_stats.empty_destructor_chain_visits;
			semantic.empty_destructor_chain_cache_hits +=
				semantic_stats.empty_destructor_chain_cache_hits;
			semantic.namespace_object_actions +=
				semantic_stats.namespace_object_actions;
			semantic.lookup_queries += semantic_stats.lookup_queries;
			semantic.lookup_scope_visits += semantic_stats.lookup_scope_visits;
			semantic.lookup_edge_visits += semantic_stats.lookup_edge_visits;
			semantic.lookup_cache_hits += semantic_stats.lookup_cache_hits;
			semantic.lookup_cache_misses += semantic_stats.lookup_cache_misses;
			semantic.lookup_cache_invalidations += semantic_stats.lookup_cache_invalidations;
			semantic.lookup_cache_dependency_edges += semantic_stats.lookup_cache_dependency_edges;
			semantic.lookup_cache_invalidation_pushes += semantic_stats.lookup_cache_invalidation_pushes;
			semantic.base_path_queries += semantic_stats.base_path_queries;
			semantic.base_path_cache_hits += semantic_stats.base_path_cache_hits;
			semantic.base_path_cache_misses += semantic_stats.base_path_cache_misses;
			semantic.base_path_edge_visits += semantic_stats.base_path_edge_visits;
			semantic.virtual_base_path_visits +=
				semantic_stats.virtual_base_path_visits;
			semantic.associated_scope_visits +=
				semantic_stats.associated_scope_visits;
			semantic.associated_declaration_visits +=
				semantic_stats.associated_declaration_visits;
			semantic.function_candidate_index_visits +=
				semantic_stats.function_candidate_index_visits;
			semantic.overload_candidates += semantic_stats.overload_candidates;
			semantic.overload_order_comparisons +=
				semantic_stats.overload_order_comparisons;
			semantic.conversion_checks += semantic_stats.conversion_checks;
			semantic.call_conversion_cache_hits +=
				semantic_stats.call_conversion_cache_hits;
			semantic.call_conversion_cache_misses +=
				semantic_stats.call_conversion_cache_misses;
			semantic.braced_fact_cache_hits +=
				semantic_stats.braced_fact_cache_hits;
			semantic.braced_fact_cache_misses +=
				semantic_stats.braced_fact_cache_misses;
			semantic.function_signature_lookups +=
				semantic_stats.function_signature_lookups;
			semantic.polymorphic_classes +=
				semantic_stats.polymorphic_classes;
			semantic.virtual_slots += semantic_stats.virtual_slots;
			semantic.virtual_signature_lookups +=
				semantic_stats.virtual_signature_lookups;
			semantic.virtual_overrides += semantic_stats.virtual_overrides;
			semantic.virtual_slot_lookups +=
				semantic_stats.virtual_slot_lookups;
			semantic.vtable_demands += semantic_stats.vtable_demands;
			semantic.access_checks += semantic_stats.access_checks;
			semantic.access_path_visits += semantic_stats.access_path_visits;
			semantic.access_grant_probes += semantic_stats.access_grant_probes;
			semantic.template_specialization_requests +=
				semantic_stats.template_specialization_requests;
			semantic.template_specialization_cache_hits +=
				semantic_stats.template_specialization_cache_hits;
			semantic.function_template_default_materializations +=
				semantic_stats.function_template_default_materializations;
			semantic.function_template_default_request_cache_hits +=
				semantic_stats.function_template_default_request_cache_hits;
			semantic.function_template_default_failure_cache_hits +=
				semantic_stats.function_template_default_failure_cache_hits;
			semantic.function_template_exception_specification_requests +=
				semantic_stats.function_template_exception_specification_requests;
			semantic.function_template_exception_specification_cache_hits +=
				semantic_stats.function_template_exception_specification_cache_hits;
			semantic.function_template_exception_specification_evaluations +=
				semantic_stats.function_template_exception_specification_evaluations;
			semantic.template_argument_list_requests +=
				semantic_stats.template_argument_list_requests;
			semantic.template_argument_list_cache_hits +=
				semantic_stats.template_argument_list_cache_hits;
			semantic.template_argument_list_index_probes +=
				semantic_stats.template_argument_list_index_probes;
			semantic.template_partition_requests +=
				semantic_stats.template_partition_requests;
			semantic.template_partition_cache_hits +=
				semantic_stats.template_partition_cache_hits;
			semantic.template_partition_index_probes +=
				semantic_stats.template_partition_index_probes;
			semantic.function_template_result_identity_requests +=
				semantic_stats.function_template_result_identity_requests;
			semantic.function_template_result_identity_cache_hits +=
				semantic_stats.function_template_result_identity_cache_hits;
			semantic.function_template_result_identity_index_probes +=
				semantic_stats.function_template_result_identity_index_probes;
			semantic.function_template_result_identity_atom_visits +=
				semantic_stats.function_template_result_identity_atom_visits;
			semantic.function_template_result_identity_syntax_visits +=
				semantic_stats.function_template_result_identity_syntax_visits;
			semantic.function_template_result_identity_environment_probes +=
				semantic_stats.function_template_result_identity_environment_probes;
			semantic.function_template_result_identity_alias_expansions +=
				semantic_stats.function_template_result_identity_alias_expansions;
			semantic.template_partial_candidates +=
				semantic_stats.template_partial_candidates;
			semantic.template_partial_order_comparisons +=
				semantic_stats.template_partial_order_comparisons;
			semantic.template_partial_shape_materializations +=
				semantic_stats.template_partial_shape_materializations;
			semantic.template_partial_shape_cache_hits +=
				semantic_stats.template_partial_shape_cache_hits;
			semantic.template_partial_deduction_visits +=
				semantic_stats.template_partial_deduction_visits;
			semantic.function_template_deduction_visits +=
				semantic_stats.function_template_deduction_visits;
			semantic.lambda_closure_requests +=
				semantic_stats.lambda_closure_requests;
			semantic.lambda_closure_cache_hits +=
				semantic_stats.lambda_closure_cache_hits;
			AccumulateLambdaCaptureStats(&semantic, semantic_stats);
			semantic.constexpr_call_requests +=
				semantic_stats.constexpr_call_requests;
			semantic.constexpr_call_cache_hits +=
				semantic_stats.constexpr_call_cache_hits;
			semantic.constant_conversion_fact_requests +=
				semantic_stats.constant_conversion_fact_requests;
			semantic.constant_conversion_fact_cache_hits +=
				semantic_stats.constant_conversion_fact_cache_hits;
			semantic.constexpr_local_index_probes +=
				semantic_stats.constexpr_local_index_probes;
			semantic.constexpr_scope_index_probes +=
				semantic_stats.constexpr_scope_index_probes;
			semantic.constexpr_object_projection_visits +=
				semantic_stats.constexpr_object_projection_visits;
			semantic.constexpr_step_visits +=
				semantic_stats.constexpr_step_visits;
			semantic.constexpr_max_depth = std::max(
				semantic.constexpr_max_depth,
				semantic_stats.constexpr_max_depth);
			semantic.constexpr_peak_locals = std::max(
				semantic.constexpr_peak_locals,
				semantic_stats.constexpr_peak_locals);
			semantic.constexpr_scratch_peak_nodes = std::max(
				semantic.constexpr_scratch_peak_nodes,
				semantic_stats.constexpr_scratch_peak_nodes);
			AccumulateFunctionDemandStats(&semantic, semantic_stats);
				AccumulateSemanticStorageStats(&semantic, semantic_stats);
			semantic.preprocessing.elapsed_nanoseconds +=
				semantic_stats.preprocessing.elapsed_nanoseconds;
			semantic.parse_nanoseconds += semantic_stats.parse_nanoseconds;
			semantic.analysis_nanoseconds += semantic_stats.analysis_nanoseconds;
			semantic.elapsed_nanoseconds += semantic_stats.elapsed_nanoseconds;
		}
	}
	std::chrono::steady_clock::time_point coalesce_started;
	if (stats) coalesce_started = std::chrono::steady_clock::now();
	CoalesceLifecycleFunctions(&program, stats);
	pa15_force_inline::RewriteProgram(
		&program, stats, prune_unreachable_weak_functions);
	if (stats)
		stats->lowering_nanoseconds += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - coalesce_started).count());
	if (stats)
		stats->typed_storage_bytes = TypedStorageBytes(program);
	return program;
}

void WriteLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	LowIRLoweringStats* stats)
{
	TypedProgram program = BuildTypedLowIRProgram(sources, options, stats);
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	CountingStreamBuffer buffer(output.rdbuf());
	std::ostream rendered(&buffer);
	RenderLowIRProgram(program, rendered);
	rendered.flush();
	if (!rendered || !output)
		throw std::runtime_error("unable to write LowIR output");
	if (stats)
	{
		stats->output_bytes = buffer.Bytes();
		stats->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - render_started).count());
	}
}

}
