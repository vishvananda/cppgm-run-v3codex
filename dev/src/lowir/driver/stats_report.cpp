#include "lowir/driver/stats_report.h"

#include "abi/itanium/abi_mangle_stats.h"
#include "lowir/analysis/function_reachability.h"
#include "lowir/analysis/inline.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/io/prepare.h"
#include "native/mir/model.h"
#include "lowering/api.h"

#include <ostream>

namespace lowir_driver_stats_report
{

namespace
{

std::size_t InstructionCount(const lowir_model::Program& program)
{
	std::size_t result = 0;
	for(std::size_t i = 0; i < program.functions.size(); ++i)
		for(std::size_t j = 0; j < program.functions[i].blocks.size(); ++j)
			result += program.functions[i].blocks[j].instructions.size();
	return result;
}

}

void ReportAbiResolution(std::ostream& output,
	const abi_mangle::AbiMangleStats& stats)
{
	output << " abi_type_cache_requests=" << stats.resolved_type_cache_requests
		 << " abi_type_cache_hits=" << stats.resolved_type_cache_hits
		 << " abi_canonical_types=" << stats.canonical_types
		 << " abi_canonical_arguments=" << stats.canonical_arguments
		 << " abi_canonical_expressions=" << stats.canonical_expressions
		 << " abi_typed_expression_operations="
		 << stats.typed_expression_operations
		 << " abi_text_expression_operations="
		 << stats.text_expression_operations
		 << " abi_typed_builtin_types=" << stats.typed_builtin_types
		 << " abi_text_builtin_types=" << stats.text_builtin_types
		 << " abi_typed_standard_substitutions="
		 << stats.typed_standard_substitutions
		 << " abi_text_standard_substitutions="
		 << stats.text_standard_substitutions
		 << " abi_typed_vendor_qualifiers=" << stats.typed_vendor_qualifiers
		 << " abi_text_vendor_qualifiers=" << stats.text_vendor_qualifiers
		 << " abi_typed_array_bounds=" << stats.typed_array_bounds
		 << " abi_text_array_bounds=" << stats.text_array_bounds
		 << " abi_typed_local_presentations="
		 << stats.typed_local_presentations
		 << " abi_text_local_presentations=" << stats.text_local_presentations
		 << " abi_typed_type_source_names=" << stats.typed_type_source_names
		 << " abi_text_type_source_names=" << stats.text_type_source_names
		 << " abi_typed_type_tags=" << stats.typed_type_tags
		 << " abi_text_type_tags=" << stats.text_type_tags
		 << " abi_typed_argument_source_names="
		 << stats.typed_argument_source_names
		 << " abi_text_argument_source_names="
		 << stats.text_argument_source_names
		 << " abi_typed_local_source_names=" << stats.typed_local_source_names
		 << " abi_text_local_source_names=" << stats.text_local_source_names
		 << " abi_typed_literal_suffixes=" << stats.typed_literal_suffixes
		 << " abi_text_literal_suffixes=" << stats.text_literal_suffixes
		 << " abi_typed_main_contexts=" << stats.typed_main_contexts
		 << " abi_external_assembly_names=" << stats.external_assembly_names
		 << " abi_external_c_function_names=" << stats.external_c_function_names
		 << " abi_external_builtin_runtime_names="
		 << stats.external_builtin_runtime_names
		 << " abi_external_c_variable_names=" << stats.external_c_variable_names
		 << " abi_external_global_tls_names=" << stats.external_global_tls_names
		 << " abi_definition_cache_hits=" << stats.definition_cache_hits
		 << " abi_path_components=" << stats.path_components
		 << " abi_text_type_path_components=" << stats.text_type_path_components
		 << " abi_text_function_path_components="
		 << stats.text_function_path_components
		 << " abi_text_object_path_components="
		 << stats.text_object_path_components
		 << " abi_text_entity_path_components="
		 << stats.text_entity_path_components
		 << " abi_text_substitution_path_components="
		 << stats.text_substitution_path_components
		 << " abi_substitution_lookups=" << stats.substitution_lookups
		 << " abi_substitution_hits=" << stats.substitution_hits
		 << " abi_substitution_entries=" << stats.substitution_entries;
}

void FinalizeOptimizer(const lowir_model::Program& program,
	const lowir_model::FunctionPruningSummary& pruning,
	lowir_opt::Stats* stats, std::uint64_t elapsed_nanoseconds)
{
	if(!stats) return;
	lowir_opt::collect_retained_inline_census(program, stats);
	stats->inline_reachable_functions = pruning.reachable_functions;
	stats->inline_pruned_functions = pruning.pruned_functions;
	stats->inline_unreachable_weak_functions = pruning.unreachable_weak_functions;
	stats->inline_unreachable_internal_functions =
		pruning.unreachable_internal_functions;
	stats->inline_retained_external_strong = pruning.retained_external_strong;
	stats->inline_retained_address_or_relocation =
		pruning.retained_address_or_relocation;
	stats->inline_retained_direct_call = pruning.retained_direct_call;
	stats->inline_retained_lifecycle = pruning.retained_lifecycle;
	stats->inline_retained_object_output_root =
		pruning.retained_object_output_root;
	stats->inline_retained_object_output_root_weak =
		pruning.retained_object_output_root_weak;
	stats->inline_retained_object_output_root_internal =
		pruning.retained_object_output_root_internal;
	stats->output_instructions = InstructionCount(program);
	stats->elapsed_nanoseconds = elapsed_nanoseconds;
}

void ReportOptimizer(std::ostream& output, const std::string& input,
	const lowir_opt::Stats& stats)
{
	output << "pa37_opt_stats"
		 << (input.empty() ? "" : " input=") << input
		 << " functions=" << stats.functions
		 << " input_instructions=" << stats.input_instructions
		 << " output_instructions=" << stats.output_instructions
		 << " instruction_visits=" << stats.instruction_visits
		 << " block_visits=" << stats.block_visits
		 << " cfg_edge_visits=" << stats.cfg_edge_visits
		 << " value_index_builds=" << stats.value_index_builds
		 << " value_index_reuses=" << stats.value_index_reuses
		 << " value_index_invalidations=" << stats.value_index_invalidations
		 << " value_index_instruction_visits="
		 << stats.value_index_instruction_visits
		 << " value_index_operand_visits="
		 << stats.value_index_operand_visits
		 << " value_index_allocations=" << stats.value_index_allocations
		 << " value_index_peak_bytes=" << stats.value_index_peak_bytes
		 << " cfg_analysis_builds=" << stats.cfg_analysis_builds
		 << " cfg_analysis_reuses=" << stats.cfg_analysis_reuses
		 << " cfg_analysis_invalidations=" << stats.cfg_analysis_invalidations
		 << " dominator_analysis_builds=" << stats.dominator_analysis_builds
		 << " dominator_analysis_reuses=" << stats.dominator_analysis_reuses
		 << " loop_analysis_builds=" << stats.loop_analysis_builds
		 << " loop_analysis_reuses=" << stats.loop_analysis_reuses
		 << " loop_backedges=" << stats.loop_backedges
		 << " loops_discovered=" << stats.loops_discovered
		 << " loop_block_memberships=" << stats.loop_block_memberships
		 << " loop_exits=" << stats.loop_exits
		 << " licm_candidates=" << stats.licm_candidates
		 << " licm_hoisted=" << stats.licm_hoisted
		 << " licm_loads_hoisted=" << stats.licm_loads_hoisted
		 << " sroa_candidates=" << stats.sroa_candidates
		 << " sroa_slots_replaced=" << stats.sroa_slots_replaced
		 << " sroa_field_slots=" << stats.sroa_field_slots
		 << " sroa_memory_rewrites=" << stats.sroa_memory_rewrites
		 << " sroa_copy_expansions=" << stats.sroa_copy_expansions
		 << " licm_preheaders_created=" << stats.licm_preheaders_created
		 << " licm_no_preheader=" << stats.licm_no_preheader
		 << " licm_eh_skips=" << stats.licm_eh_skips
		 << " licm_budget_skips=" << stats.licm_budget_skips
		 << " induction_variables=" << stats.induction_variables
		 << " induction_strength_reductions="
		 << stats.induction_strength_reductions
		 << " loop_exits_canonicalized=" << stats.loop_exits_canonicalized
		 << " dead_loops_removed=" << stats.dead_loops_removed
		 << " o3_loops_considered=" << stats.o3_loops_considered
		 << " o3_loops_unrolled=" << stats.o3_loops_unrolled
		 << " o3_unroll_iterations=" << stats.o3_unroll_iterations
		 << " o3_unroll_cloned_instructions="
		 << stats.o3_unroll_cloned_instructions
		 << " o3_unroll_candidate_skips="
		 << stats.o3_unroll_candidate_skips
		 << " o3_unroll_trip_skips=" << stats.o3_unroll_trip_skips
		 << " o3_unroll_budget_skips=" << stats.o3_unroll_budget_skips
		 << " o3_unroll_instruction_visits="
		 << stats.o3_unroll_instruction_visits
		 << " o3_unroll_peak_scratch_bytes="
		 << stats.o3_unroll_peak_scratch_bytes
		 << " o3_loop_inline_pairs_considered="
		 << stats.o3_loop_inline_pairs_considered
		 << " o3_loop_inline_candidates="
		 << stats.o3_loop_inline_candidates
		 << " o3_loop_inline_calls=" << stats.o3_loop_inline_calls
		 << " o3_loop_inline_cloned_instructions="
		 << stats.o3_loop_inline_cloned_instructions
		 << " o3_loop_inline_peak_analysis_bytes="
		 << stats.o3_loop_inline_peak_analysis_bytes
		 << " late_inline_direct_edges="
		 << stats.late_inline_direct_edges
		 << " late_inline_call_visits="
		 << stats.late_inline_call_visits
		 << " late_inline_calls=" << stats.late_inline_calls
		 << " late_inline_cloned_instructions="
		 << stats.late_inline_cloned_instructions
		 << " late_inline_changed_callers="
		 << stats.late_inline_changed_callers
		 << " worklist_pushes=" << stats.worklist_pushes
		 << " dataflow_updates=" << stats.dataflow_updates
		 << " inline_direct_edges=" << stats.inline_direct_edges
		 << " inline_sccs=" << stats.inline_sccs
		 << " inline_recursive_functions=" << stats.inline_recursive_functions
		 << " inline_call_visits=" << stats.inline_call_visits
		 << " inline_candidate_calls=" << stats.inline_candidate_calls
		 << " inline_calls=" << stats.inline_calls
		 << " inline_cloned_instructions=" << stats.inline_cloned_instructions
		 << " inline_hint_candidates=" << stats.inline_hint_candidates
		 << " inline_hint_calls=" << stats.inline_hint_calls
		 << " inline_hint_size_rejects=" << stats.inline_hint_size_rejects
		 << " inline_single_call_candidates="
		 << stats.inline_single_call_candidates
		 << " inline_single_call_calls=" << stats.inline_single_call_calls
		 << " inline_single_call_instructions="
		 << stats.inline_single_call_instructions
		 << " inline_single_call_discarded_bodies="
		 << stats.inline_single_call_discarded_bodies
		 << " inline_single_call_budget_skips="
		 << stats.inline_single_call_budget_skips
		 << " inline_single_call_caller_budget_skips="
		 << stats.inline_single_call_caller_budget_skips
		 << " inline_single_call_translation_unit_budget_skips="
		 << stats.inline_single_call_translation_unit_budget_skips
		 << " inline_single_call_translation_unit_budget="
		 << stats.inline_single_call_translation_unit_budget
		 << " inline_single_call_translation_unit_budget_remaining="
		 << stats.inline_single_call_translation_unit_budget_remaining
		 << " inline_input_instructions=" << stats.inline_input_instructions
		 << " inline_output_instructions=" << stats.inline_output_instructions
		 << " inline_reject_recursive=" << stats.inline_reject_recursive
		 << " inline_reject_no_inline=" << stats.inline_reject_no_inline
		 << " inline_reject_loop_body=" << stats.inline_reject_loop_body
		 << " inline_reject_argument_shape=" << stats.inline_reject_argument_shape
		 << " inline_reject_variadic=" << stats.inline_reject_variadic
		 << " inline_reject_callee_size=" << stats.inline_reject_callee_size
		 << " inline_reject_prepared_size=" << stats.inline_reject_prepared_size
		 << " inline_reject_landing=" << stats.inline_reject_landing
		 << " inline_reject_eh_visibility=" << stats.inline_reject_eh_visibility
		 << " inline_reject_eh_unwind=" << stats.inline_reject_eh_unwind
		 << " inline_reject_callee_eh=" << stats.inline_reject_callee_eh
		 << " inline_reachable_functions=" << stats.inline_reachable_functions
		 << " inline_pruned_functions=" << stats.inline_pruned_functions
		 << " inline_unreachable_weak_functions="
		 << stats.inline_unreachable_weak_functions
		 << " inline_unreachable_internal_functions="
		 << stats.inline_unreachable_internal_functions
		 << " inline_retained_external_strong="
		 << stats.inline_retained_external_strong
		 << " inline_retained_address_or_relocation="
		 << stats.inline_retained_address_or_relocation
		 << " inline_retained_direct_call=" << stats.inline_retained_direct_call
		 << " inline_retained_lifecycle=" << stats.inline_retained_lifecycle
		 << " inline_retained_object_output_root="
		 << stats.inline_retained_object_output_root
		 << " inline_retained_object_output_root_weak="
		 << stats.inline_retained_object_output_root_weak
		 << " inline_retained_object_output_root_internal="
		 << stats.inline_retained_object_output_root_internal
		 << " inline_retained_direct_edges="
		 << stats.inline_retained_direct_edges
		 << " inline_retained_discardable_definitions="
		 << stats.inline_retained_discardable_definitions
		 << " inline_retained_discardable_calls="
		 << stats.inline_retained_discardable_calls
		 << " inline_retained_discardable_instructions="
		 << stats.inline_retained_discardable_instructions
		 << " inline_retained_discardable_leaf_definitions="
		 << stats.inline_retained_discardable_leaf_definitions
		 << " inline_retained_discardable_eh_definitions="
		 << stats.inline_retained_discardable_eh_definitions
		 << " inline_retained_discardable_recursive_definitions="
		 << stats.inline_retained_discardable_recursive_definitions
		 << " inline_retained_discardable_no_inline_definitions="
		 << stats.inline_retained_discardable_no_inline_definitions
		 << " inline_retained_nonpositive_leaf_definitions="
		 << stats.inline_retained_nonpositive_leaf_definitions
		 << " inline_retained_nonpositive_leaf_calls="
		 << stats.inline_retained_nonpositive_leaf_calls
		 << " inline_retained_nonpositive_leaf_instructions="
		 << stats.inline_retained_nonpositive_leaf_instructions
		 << " inline_retained_nonpositive_leaf_estimated_savings="
		 << stats.inline_retained_nonpositive_leaf_estimated_savings
		 << " post_prune_inline_direct_edges="
		 << stats.post_prune_inline_direct_edges
		 << " post_prune_inline_calls=" << stats.post_prune_inline_calls
		 << " post_prune_inline_instructions="
		 << stats.post_prune_inline_instructions
		 << " post_prune_inline_discarded_bodies="
		 << stats.post_prune_inline_discarded_bodies
		 << " post_prune_inline_budget_skips="
		 << stats.post_prune_inline_budget_skips
		 << " post_prune_inline_changed_callers="
		 << stats.post_prune_inline_changed_callers
		 << " post_prune_inline_considered_single_calls="
		 << stats.post_prune_inline_considered_single_calls
		 << " post_prune_inline_reject_recursive="
		 << stats.post_prune_inline_reject_recursive
		 << " post_prune_inline_reject_no_inline="
		 << stats.post_prune_inline_reject_no_inline
		 << " post_prune_inline_reject_argument_shape="
		 << stats.post_prune_inline_reject_argument_shape
		 << " post_prune_inline_reject_variadic="
		 << stats.post_prune_inline_reject_variadic
		 << " post_prune_inline_reject_size="
		 << stats.post_prune_inline_reject_size
		 << " post_prune_inline_reject_landing="
		 << stats.post_prune_inline_reject_landing
		 << " post_prune_inline_reject_eh_unwind="
		 << stats.post_prune_inline_reject_eh_unwind
		 << " post_prune_inline_reject_callee_eh="
		 << stats.post_prune_inline_reject_callee_eh
		 << " post_prune_inline_translation_unit_budget="
		 << stats.post_prune_inline_translation_unit_budget
		 << " post_prune_inline_translation_unit_budget_remaining="
		 << stats.post_prune_inline_translation_unit_budget_remaining
		 << " inline_changed_callers=" << stats.inline_changed_callers
		 << " inline_eh_blocked_records=" << stats.inline_eh_blocked_records
		 << " inline_revisited_callers=" << stats.inline_revisited_callers
		 << " inline_eh_regions_analyzed=" << stats.inline_eh_regions_analyzed
		 << " inline_eh_regions_removed=" << stats.inline_eh_regions_removed
		 << " inline_eh_ambiguous_functions="
		 << stats.inline_eh_ambiguous_functions
		 << " inline_no_unwind_published_after_strip="
		 << stats.inline_no_unwind_published_after_strip
		 << " partial_inline_census_direct_calls="
		 << stats.partial_inline_census_direct_calls
		 << " partial_inline_census_eligible_calls="
		 << stats.partial_inline_census_eligible_calls
		 << " partial_inline_census_eligible_callees="
		 << stats.partial_inline_census_eligible_callees
		 << " partial_inline_census_hint_calls="
		 << stats.partial_inline_census_hint_calls
		 << " partial_inline_census_constant_calls="
		 << stats.partial_inline_census_constant_calls
		 << " partial_inline_census_constant_actuals="
		 << stats.partial_inline_census_constant_actuals
		 << " partial_inline_census_loop_calls="
		 << stats.partial_inline_census_loop_calls
		 << " partial_inline_census_repeated_callee_calls="
		 << stats.partial_inline_census_repeated_callee_calls
		 << " partial_inline_census_prefix_blocks="
		 << stats.partial_inline_census_prefix_blocks
		 << " partial_inline_census_prefix_instructions="
		 << stats.partial_inline_census_prefix_instructions
		 << " partial_inline_census_bailout_edges="
		 << stats.partial_inline_census_bailout_edges
		 << " partial_inline_census_prefix_0_8="
		 << stats.partial_inline_census_prefix_0_8
		 << " partial_inline_census_prefix_9_12="
		 << stats.partial_inline_census_prefix_9_12
		 << " partial_inline_census_prefix_13_16="
		 << stats.partial_inline_census_prefix_13_16
		 << " partial_inline_census_prefix_17_24="
		 << stats.partial_inline_census_prefix_17_24
		 << " partial_inline_census_prefix_over_24="
		 << stats.partial_inline_census_prefix_over_24
		 << " partial_inline_census_reject_recursive="
		 << stats.partial_inline_census_reject_recursive
		 << " partial_inline_census_reject_no_inline="
		 << stats.partial_inline_census_reject_no_inline
		 << " partial_inline_census_reject_argument_shape="
		 << stats.partial_inline_census_reject_argument_shape
		 << " partial_inline_census_reject_variadic="
		 << stats.partial_inline_census_reject_variadic
		 << " partial_inline_census_reject_object_result="
		 << stats.partial_inline_census_reject_object_result
		 << " partial_inline_census_reject_no_fast_return="
		 << stats.partial_inline_census_reject_no_fast_return
		 << " partial_inline_census_reject_no_bailout="
		 << stats.partial_inline_census_reject_no_bailout
		 << " partial_inline_census_call_stops="
		 << stats.partial_inline_census_call_stops
		 << " partial_inline_census_store_stops="
		 << stats.partial_inline_census_store_stops
		 << " partial_inline_census_eh_stops="
		 << stats.partial_inline_census_eh_stops
		 << " partial_inline_census_other_stops="
		 << stats.partial_inline_census_other_stops
		 << " partial_inline_census_backedge_stops="
		 << stats.partial_inline_census_backedge_stops
		 << " partial_inline_census_join_stops="
		 << stats.partial_inline_census_join_stops
		 << " predicate_range_folds=" << stats.predicate_range_folds
		 << " o3_terminal_phi_runs=" << stats.o3_terminal_phi_runs
		 << " o3_terminal_phi_merges=" << stats.o3_terminal_phi_merges
		 << " o3_terminal_phi_incoming_edges="
		 << stats.o3_terminal_phi_incoming_edges
		 << " o3_terminal_phi_cloned_instructions="
		 << stats.o3_terminal_phi_cloned_instructions
		 << " o3_terminal_phi_round_cap_hits="
		 << stats.o3_terminal_phi_round_cap_hits
		 << " adjacent_scalar_copy_runs="
		 << stats.adjacent_scalar_copy_runs
		 << " adjacent_scalar_copy_groups="
		 << stats.adjacent_scalar_copy_groups
		 << " adjacent_scalar_copy_bytes="
		 << stats.adjacent_scalar_copy_bytes
		 << " overwritten_zero_inits=" << stats.overwritten_zero_inits
		 << " overwritten_zero_bytes=" << stats.overwritten_zero_bytes
		 << " ipa_direct_call_visits=" << stats.ipa_direct_call_visits
		 << " ipa_instruction_visits=" << stats.ipa_instruction_visits
		 << " ipa_candidate_functions=" << stats.ipa_candidate_functions
		 << " ipa_address_observable_rejects="
		 << stats.ipa_address_observable_rejects
		 << " ipa_uniform_parameters=" << stats.ipa_uniform_parameters
		 << " ipa_disagreeing_parameters=" << stats.ipa_disagreeing_parameters
		 << " ipa_substituted_operands=" << stats.ipa_substituted_operands
		 << " ipa_dead_parameters=" << stats.ipa_dead_parameters
		 << " ipa_calls_rewritten=" << stats.ipa_calls_rewritten
		 << " ipa_arguments_removed=" << stats.ipa_arguments_removed
		 << " ipa_functions_changed=" << stats.ipa_functions_changed
		 << " ipa_specialized_clones=" << stats.ipa_specialized_clones
		 << " ipa_cloned_instructions=" << stats.ipa_cloned_instructions
		 << " ipa_clone_budget_skips=" << stats.ipa_clone_budget_skips
		 << " ipa_table_prefilter_clones="
		 << stats.ipa_table_prefilter_clones
		 << " ipa_table_prefilter_calls=" << stats.ipa_table_prefilter_calls
		 << " ipa_peak_analysis_bytes=" << stats.ipa_peak_analysis_bytes
		 << " repeat_stable_function_visits="
		 << stats.repeat_stable_function_visits
		 << " repeat_stable_functions=" << stats.repeat_stable_functions
		 << " repeat_stable_call_sites=" << stats.repeat_stable_call_sites
		 << " repeat_stable_signatures=" << stats.repeat_stable_signatures
		 << " repeat_stable_reuses=" << stats.repeat_stable_reuses
		 << " repeat_stable_budget_skips="
		 << stats.repeat_stable_budget_skips
		 << " repeat_stable_peak_analysis_bytes="
		 << stats.repeat_stable_peak_analysis_bytes
		 << " budget_skips=" << stats.budget_skips
		 << " rewrites=" << stats.rewrites
		 << " simplify_runs=" << stats.simplify_runs
		 << " simplify_changes=" << stats.simplify_changes
		 << " simplify_candidate_skips=" << stats.simplify_candidate_skips
		 << " gvn_expression_probes=" << stats.gvn_expression_probes
		 << " gvn_expression_hits=" << stats.gvn_expression_hits
		 << " gvn_expression_keys=" << stats.gvn_expression_keys
		 << " gvn_expression_peak_scope="
		 << stats.gvn_expression_peak_scope
		 << " memory_gvn_runs=" << stats.memory_gvn_runs
		 << " memory_gvn_classes=" << stats.memory_gvn_classes
		 << " memory_gvn_merge_versions="
		 << stats.memory_gvn_merge_versions
		 << " memory_gvn_load_probes=" << stats.memory_gvn_load_probes
		 << " memory_gvn_loads_eliminated="
		 << stats.memory_gvn_loads_eliminated
		 << " memory_gvn_unknown_barriers="
		 << stats.memory_gvn_unknown_barriers
		 << " memory_gvn_eh_functions=" << stats.memory_gvn_eh_functions
		 << " memory_gvn_eh_barriers=" << stats.memory_gvn_eh_barriers
		 << " memory_gvn_eh_skips=" << stats.memory_gvn_eh_skips
		 << " memory_gvn_budget_skips=" << stats.memory_gvn_budget_skips
		 << " pre_runs=" << stats.pre_runs
		 << " pre_candidates=" << stats.pre_candidates
		 << " pre_full_redundancies=" << stats.pre_full_redundancies
		 << " pre_partial_redundancies=" << stats.pre_partial_redundancies
		 << " pre_inserted_expressions=" << stats.pre_inserted_expressions
		 << " pre_inserted_phis=" << stats.pre_inserted_phis
		 << " pre_availability_probes=" << stats.pre_availability_probes
		 << " pre_critical_edge_skips=" << stats.pre_critical_edge_skips
		 << " pre_eh_skips=" << stats.pre_eh_skips
		 << " pre_budget_skips=" << stats.pre_budget_skips
		 << " dce_runs=" << stats.dce_runs
		 << " dce_changes=" << stats.dce_changes
		 << " dce_candidate_skips=" << stats.dce_candidate_skips
		 << " cfg_runs=" << stats.cfg_runs
		 << " cfg_changes=" << stats.cfg_changes
		 << " slot_runs=" << stats.slot_runs
		 << " slot_changes=" << stats.slot_changes
		 << " forward_slot_runs=" << stats.forward_slot_runs
		 << " forward_slot_changes=" << stats.forward_slot_changes
		 << " local_slot_runs=" << stats.local_slot_runs
		 << " local_slot_changes=" << stats.local_slot_changes
		 << " remove_slot_runs=" << stats.remove_slot_runs
		 << " remove_slot_changes=" << stats.remove_slot_changes
		 << " promote_slot_runs=" << stats.promote_slot_runs
		 << " promote_slot_changes=" << stats.promote_slot_changes
		 << " promote_eligible_slots=" << stats.promote_eligible_slots
		 << " promote_sparse_meets=" << stats.promote_sparse_meets
		 << " promote_sparse_state_entries="
		 << stats.promote_sparse_state_entries
		 << " promote_sparse_merge_facts="
		 << stats.promote_sparse_merge_facts
		 << " promote_blocked_join_loads="
		 << stats.promote_blocked_join_loads
		 << " promote_blocked_join_slots="
		 << stats.promote_blocked_join_slots
		 << " promote_blocked_join_functions="
		 << stats.promote_blocked_join_functions
		 << " promote_blocked_ordinary_loads="
		 << stats.promote_blocked_ordinary_loads
		 << " promote_blocked_loop_loads="
		 << stats.promote_blocked_loop_loads
		 << " promote_blocked_eh_loads="
		 << stats.promote_blocked_eh_loads
		 << " promote_phi_instructions="
		 << stats.promote_phi_instructions
		 << " promote_phi_incoming_edges="
		 << stats.promote_phi_incoming_edges
		 << " promote_phi_loads="
		 << stats.promote_phi_loads
		 << " promote_phi_budget_skips="
		 << stats.promote_phi_budget_skips
		 << " promote_peak_transient_bytes="
		 << stats.promote_peak_transient_bytes
		 << " small_object_runs=" << stats.small_object_runs
		 << " small_object_changes=" << stats.small_object_changes
		 << " small_object_candidates=" << stats.small_object_candidates
		 << " small_objects_promoted=" << stats.small_objects_promoted
		 << " small_object_memory_rewrites="
		 << stats.small_object_memory_rewrites
		 << " small_object_copies_rewritten="
		 << stats.small_object_copies_rewritten
		 << " addressed_scalar_candidates="
		 << stats.addressed_scalar_candidates
		 << " addressed_scalars_promoted="
		 << stats.addressed_scalars_promoted
		 << " addressed_scalar_memory_rewrites="
		 << stats.addressed_scalar_memory_rewrites
		 << " addressed_scalar_copies_rewritten="
		 << stats.addressed_scalar_copies_rewritten
		 << " dead_store_runs=" << stats.dead_store_runs
		 << " dead_store_changes=" << stats.dead_store_changes
		 << " cleanup_resume_runs=" << stats.cleanup_resume_runs
		 << " cleanup_resume_block_visits=" << stats.cleanup_resume_block_visits
		 << " cleanup_resume_blocks_removed="
		 << stats.cleanup_resume_blocks_removed
		 << " cleanup_tail_runs=" << stats.cleanup_tail_runs
		 << " cleanup_tail_block_visits=" << stats.cleanup_tail_block_visits
		 << " cleanup_tail_groups_shared=" << stats.cleanup_tail_groups_shared
		 << " cleanup_tail_blocks_rewritten="
		 << stats.cleanup_tail_blocks_rewritten
		 << " cleanup_tail_instructions_removed="
		 << stats.cleanup_tail_instructions_removed
		 << " cold_sunk_definitions=" << stats.cold_sunk_definitions
		 << " duplicate_block_loads_removed="
		 << stats.duplicate_block_loads_removed
		 << " staged_copies_forwarded=" << stats.staged_copies_forwarded
		 << " unreachable_terminator_blocks="
		 << stats.unreachable_terminator_blocks
		 << " unreachable_edges_removed=" << stats.unreachable_edges_removed
		 << " inline_ns=" << stats.inline_nanoseconds
		 << " ipa_ns=" << stats.ipa_nanoseconds
		 << " simplify_ns=" << stats.simplify_nanoseconds
		 << " memory_gvn_ns=" << stats.memory_gvn_nanoseconds
		 << " pre_ns=" << stats.pre_nanoseconds
		 << " dce_ns=" << stats.dce_nanoseconds
		 << " cfg_ns=" << stats.cfg_nanoseconds
		 << " slot_ns=" << stats.slot_nanoseconds
		 << " forward_slot_ns=" << stats.forward_slot_nanoseconds
		 << " local_slot_ns=" << stats.local_slot_nanoseconds
		 << " remove_slot_ns=" << stats.remove_slot_nanoseconds
		 << " promote_slot_ns=" << stats.promote_slot_nanoseconds
		 << " small_object_ns=" << stats.small_object_nanoseconds
		 << " dead_store_ns=" << stats.dead_store_nanoseconds
		 << " cleanup_resume_ns=" << stats.cleanup_resume_nanoseconds
		 << " cleanup_tail_ns=" << stats.cleanup_tail_nanoseconds
		 << " unreachable_ns=" << stats.unreachable_nanoseconds
		 << " loop_ns=" << stats.loop_nanoseconds
		 << " o3_unroll_ns=" << stats.o3_unroll_nanoseconds
		 << " o3_loop_inline_ns=" << stats.o3_loop_inline_nanoseconds
		 << " o3_terminal_phi_ns=" << stats.o3_terminal_phi_nanoseconds
		 << " late_inline_ns=" << stats.late_inline_nanoseconds
		 << " post_prune_inline_ns="
		 << stats.post_prune_inline_nanoseconds
		 << " partial_inline_census_ns="
		 << stats.partial_inline_census_nanoseconds
		 << " repeat_stable_ns=" << stats.repeat_stable_nanoseconds
		 << " licm_ns=" << stats.licm_nanoseconds
		 << " elapsed_ns=" << stats.elapsed_nanoseconds;
	output << " inline_retained_discardable_definition_matrix=";
	for (std::size_t i = 0;
		i < stats.inline_retained_discardable_definition_matrix.size(); ++i)
	{
		if (i) output << ',';
		output << stats.inline_retained_discardable_definition_matrix[i];
	}
	output << " inline_retained_discardable_call_matrix=";
	for (std::size_t i = 0;
		i < stats.inline_retained_discardable_call_matrix.size(); ++i)
	{
		if (i) output << ',';
		output << stats.inline_retained_discardable_call_matrix[i];
	}
	output << " inline_retained_discardable_instruction_matrix=";
	for (std::size_t i = 0;
		i < stats.inline_retained_discardable_instruction_matrix.size(); ++i)
	{
		if (i) output << ',';
		output << stats.inline_retained_discardable_instruction_matrix[i];
	}
	output << '\n';
}

void ReportPreparation(std::ostream& output, const std::string& path,
	const cppgm::lowering::Stats& stats,
	const lowir_model::LowirPreparationStats& preparation_stats)
{
	for (std::size_t fallback = 0;
		fallback < stats.post_inline_retained_conservative_fallback_names.size();
		++fallback)
		output << "pa15_retained_fallback file=" << path << " symbol="
			<< stats.post_inline_retained_conservative_fallback_names[fallback]
			<< '\n';
	for (std::size_t internal = 0;
		internal < stats.post_inline_unreachable_internal_names.size(); ++internal)
		output << "pa15_unreachable_internal file=" << path << " symbol="
			<< stats.post_inline_unreachable_internal_names[internal] << '\n';
	output << "pa37_prepare_stats"
		 << " file=" << path
		 << " typed_name_entries=" << preparation_stats.typed_name_entries
		 << " typed_name_bytes=" << preparation_stats.typed_name_bytes
		 << " adapter_prefix_renders=" << preparation_stats.adapter_prefix_renders
		 << " adapter_prefix_bytes=" << preparation_stats.adapter_prefix_bytes
		 << " adapter_integer_renders=" << preparation_stats.adapter_integer_renders
		 << " adapter_integer_bytes=" << preparation_stats.adapter_integer_bytes
		 << " adapter_literal_materializations="
		 << preparation_stats.adapter_literal_materializations
		 << " adapter_pool_calls="
		 << preparation_stats.adapter_string_pool.intern_calls
		 << " adapter_pool_hits=" << preparation_stats.adapter_string_pool.intern_hits
		 << " adapter_pool_misses="
		 << preparation_stats.adapter_string_pool.intern_misses
		 << " adapter_pool_hash_bytes="
		 << preparation_stats.adapter_string_pool.hash_bytes
		 << " adapter_pool_slot_probes="
		 << preparation_stats.adapter_string_pool.slot_probes
		 << " lowir_string_entries=" << preparation_stats.lowir_string_entries
		 << " lowir_spelling_bytes=" << preparation_stats.lowir_spelling_bytes
		 << " lowir_string_storage_bytes="
		 << preparation_stats.lowir_string_storage_bytes
		 << " lowir_model_storage_bytes="
		 << preparation_stats.lowir_model_storage_bytes
		 << " typed_lowir_peak_live_bytes="
		 << preparation_stats.typed_lowir_peak_live_bytes
		 << " reference_operand_visits="
		 << preparation_stats.reference_operand_visits
		 << " referenced_symbols=" << preparation_stats.referenced_symbols
		 << " declaration_visits=" << preparation_stats.declaration_visits
		 << " retained_declarations=" << preparation_stats.retained_declarations
		 << " function_order_visits=" << preparation_stats.function_order_visits
		 << " function_moves=" << preparation_stats.function_moves
		 << " function_copies=" << preparation_stats.function_copies
		 << " alias_order_visits=" << preparation_stats.alias_order_visits
		 << " alias_moves=" << preparation_stats.alias_moves
		 << " serialized_operand_visits="
		 << preparation_stats.serialized_operand_visits
		 << " derived_operand_visits=" << preparation_stats.derived_operand_visits
		 << " boundary_call_visits=" << preparation_stats.boundary_call_visits
		 << " exports=" << preparation_stats.exports
		 << " frontend_canonical_ns="
		 << preparation_stats.frontend_canonical_nanoseconds
		 << " serialized_canonical_ns="
		 << preparation_stats.serialized_canonical_nanoseconds
		 << " derived_facts_ns=" << preparation_stats.derived_facts_nanoseconds
		 << '\n';
}

void ReportCompilePhases(std::ostream& output,
	const cppgm::lowering::Stats& stats,
	const lowir_model::LowirPreparationStats& preparation_stats,
	std::uint64_t typed_pipeline_nanoseconds,
	std::uint64_t adapter_nanoseconds,
	std::uint64_t text_parse_nanoseconds,
	std::uint64_t prune_nanoseconds,
	std::uint64_t debug_nanoseconds,
	std::uint64_t lowir_opt_nanoseconds)
{
	const cppgm::semantic::Stats& semantic = stats.semantic;
	const std::uint64_t typed_accounted_nanoseconds =
		semantic.elapsed_nanoseconds + stats.lowering_nanoseconds;
	const std::uint64_t typed_glue_nanoseconds = typed_pipeline_nanoseconds >
		typed_accounted_nanoseconds ?
		typed_pipeline_nanoseconds - typed_accounted_nanoseconds : 0;
	const std::uint64_t preparation_nanoseconds =
		preparation_stats.frontend_canonical_nanoseconds +
		preparation_stats.serialized_canonical_nanoseconds +
		preparation_stats.derived_facts_nanoseconds;
	const std::uint64_t adapter_build_nanoseconds = adapter_nanoseconds >
		preparation_nanoseconds ?
		adapter_nanoseconds - preparation_nanoseconds : 0;
	output << " typed_identity_paths=" << stats.typed_identity_paths
		 << " typed_identity_types=" << stats.typed_identity_types
		 << " local_source_names_scanned="
		 << stats.local_presentation.source_names_scanned
		 << " local_source_name_bytes="
		 << stats.local_presentation.source_name_bytes
		 << " local_reservation_matches="
		 << stats.local_presentation.reservation_matches
		 << " local_temporary_reservations="
		 << stats.local_presentation.temporary_reservations
		 << " local_temporary_probes=" << stats.local_presentation.temporary_probes
		 << " local_temporary_hits=" << stats.local_presentation.temporary_hits
		 << " block_order_functions="
		 << stats.local_presentation.block_order_functions
		 << " block_order_comparisons="
		 << stats.local_presentation.block_order_comparisons
		 << " block_order_characters="
		 << stats.local_presentation.block_order_characters
		 << " typed_identity_bytes=" << stats.typed_identity_bytes
		 << " typed_bytes=" << stats.typed_storage_bytes
		 << " preprocess_ns=" << semantic.preprocessing.elapsed_nanoseconds
		 << " parse_ns=" << semantic.parse_nanoseconds
		 << " semantic_ns=" << semantic.analysis_nanoseconds
		 << " frontend_ns=" << semantic.elapsed_nanoseconds
		 << " lowering_ns=" << stats.lowering_nanoseconds
		 << " typed_glue_ns=" << typed_glue_nanoseconds
		 << " adapter_build_ns=" << adapter_build_nanoseconds
		 << " preparation_ns=" << preparation_nanoseconds
		 << " adapt_ns=" << adapter_nanoseconds
		 << " text_parse_ns=" << text_parse_nanoseconds
		 << " prune_ns=" << prune_nanoseconds
		 << " debug_ns=" << debug_nanoseconds
		 << " lowir_opt_ns=" << lowir_opt_nanoseconds
		 << " lowir_type_record_bytes=" << sizeof(lowir_model::LowType)
		 << " lowir_operand_record_bytes=" << sizeof(lowir_model::Operand)
		 << " lowir_instruction_record_bytes="
		 << sizeof(lowir_model::Instruction)
		 << " mir_operand_record_bytes=" << sizeof(mir_model::Operand)
		 << " mir_instruction_record_bytes=" << sizeof(mir_model::Instruction)
		 << '\n';
}

}
