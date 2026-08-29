#pragma once

#include "lowir/model/program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lowir_opt {

// Optional overrides for the O1 inline policy limits, driven by the
// repeatable --inline-limit name=value driver option.  A zero field keeps
// the shipped default; hint_late_cap also widens the hinted callee size cap
// to max(40, hint_late_cap) so a single number states the hinted dose.
struct InlinePolicyOverrides
{
  std::size_t caller_budget = 0;
  std::size_t single_call_limit = 0;
  std::size_t single_call_caller_budget = 0;
  std::size_t hint_late_cap = 0;
};

// Parse one student-facing --inline-limit name=value argument and update the
// matching policy field.  Both lowiropt and cppgm++ use this parser so the
// accepted names and rejection rules stay identical.
void apply_inline_limit_option(InlinePolicyOverrides * limits,
			       const std::string & spec);

struct Stats
{
  static const std::size_t kInlineRetainedUseBucketCount = 7;
  static const std::size_t kInlineRetainedSizeBucketCount = 11;
  static const std::size_t kInlineRetainedMatrixSize =
    kInlineRetainedUseBucketCount * kInlineRetainedSizeBucketCount;
  std::size_t functions = 0;
  std::size_t input_instructions = 0;
  std::size_t output_instructions = 0;
  std::size_t instruction_visits = 0;
  std::size_t block_visits = 0;
  std::size_t cfg_edge_visits = 0;
  std::size_t value_index_builds = 0;
  std::size_t value_index_reuses = 0;
  std::size_t value_index_invalidations = 0;
  std::size_t value_index_instruction_visits = 0;
  std::size_t value_index_operand_visits = 0;
  std::size_t value_index_allocations = 0;
  std::size_t value_index_peak_bytes = 0;
  std::size_t cfg_analysis_builds = 0;
  std::size_t cfg_analysis_reuses = 0;
  std::size_t cfg_analysis_invalidations = 0;
  std::size_t dominator_analysis_builds = 0;
  std::size_t dominator_analysis_reuses = 0;
  std::size_t loop_analysis_builds = 0;
  std::size_t loop_analysis_reuses = 0;
  std::size_t loop_backedges = 0;
  std::size_t loops_discovered = 0;
  std::size_t loop_block_memberships = 0;
  std::size_t loop_exits = 0;
  std::size_t licm_candidates = 0;
  std::size_t licm_hoisted = 0;
  std::size_t licm_loads_hoisted = 0;
  std::size_t licm_preheaders_created = 0;
  std::size_t licm_no_preheader = 0;
  std::size_t licm_eh_skips = 0;
  std::size_t licm_budget_skips = 0;
  std::size_t induction_variables = 0;
  std::size_t induction_strength_reductions = 0;
  std::size_t loop_exits_canonicalized = 0;
  std::size_t dead_loops_removed = 0;
  std::size_t o3_loops_considered = 0;
  std::size_t o3_loops_unrolled = 0;
  std::size_t o3_unroll_iterations = 0;
  std::size_t o3_unroll_cloned_instructions = 0;
  std::size_t o3_unroll_candidate_skips = 0;
  std::size_t o3_unroll_trip_skips = 0;
  std::size_t o3_unroll_budget_skips = 0;
  std::size_t o3_unroll_instruction_visits = 0;
  std::size_t o3_unroll_peak_scratch_bytes = 0;
  std::size_t late_inline_direct_edges = 0;
  std::size_t late_inline_call_visits = 0;
  std::size_t late_inline_calls = 0;
  std::size_t late_inline_cloned_instructions = 0;
  std::size_t late_inline_changed_callers = 0;
  std::size_t worklist_pushes = 0;
  std::size_t dataflow_updates = 0;
  std::size_t inline_direct_edges = 0;
  std::size_t inline_sccs = 0;
  std::size_t inline_recursive_functions = 0;
  std::size_t inline_call_visits = 0;
  std::size_t inline_candidate_calls = 0;
  std::size_t inline_calls = 0;
  std::size_t inline_cloned_instructions = 0;
  std::size_t inline_hint_candidates = 0;
  std::size_t inline_hint_calls = 0;
  std::size_t inline_hint_size_rejects = 0;
  std::size_t inline_single_call_candidates = 0;
  std::size_t inline_single_call_calls = 0;
  std::size_t inline_single_call_instructions = 0;
  std::size_t inline_single_call_discarded_bodies = 0;
  std::size_t inline_single_call_budget_skips = 0;
  std::size_t inline_single_call_caller_budget_skips = 0;
  std::size_t inline_single_call_translation_unit_budget_skips = 0;
  std::size_t inline_single_call_translation_unit_budget = 0;
  std::size_t inline_single_call_translation_unit_budget_remaining = 0;
  std::size_t inline_input_instructions = 0;
  std::size_t inline_output_instructions = 0;
  std::size_t inline_reject_recursive = 0;
  std::size_t inline_reject_no_inline = 0;
  std::size_t inline_reject_loop_body = 0;
  std::size_t inline_reject_argument_shape = 0;
  std::size_t inline_reject_variadic = 0;
  std::size_t inline_reject_callee_size = 0;
  std::size_t inline_reject_prepared_size = 0;
  std::size_t inline_reject_landing = 0;
  std::size_t inline_reject_eh_visibility = 0;
  std::size_t inline_reject_eh_unwind = 0;
  std::size_t inline_reject_callee_eh = 0;
  std::size_t inline_reachable_functions = 0;
  std::size_t inline_pruned_functions = 0;
  std::size_t inline_unreachable_weak_functions = 0;
  std::size_t inline_unreachable_internal_functions = 0;
  std::size_t inline_retained_external_strong = 0;
  std::size_t inline_retained_address_or_relocation = 0;
  std::size_t inline_retained_direct_call = 0;
  std::size_t inline_retained_lifecycle = 0;
  std::size_t inline_retained_object_output_root = 0;
  std::size_t inline_retained_object_output_root_weak = 0;
  std::size_t inline_retained_object_output_root_internal = 0;
  std::size_t inline_retained_direct_edges = 0;
  std::size_t inline_retained_discardable_definitions = 0;
  std::size_t inline_retained_discardable_calls = 0;
  std::size_t inline_retained_discardable_instructions = 0;
  std::size_t inline_retained_discardable_leaf_definitions = 0;
  std::size_t inline_retained_discardable_eh_definitions = 0;
  std::size_t inline_retained_discardable_recursive_definitions = 0;
  std::size_t inline_retained_discardable_no_inline_definitions = 0;
  std::size_t inline_retained_nonpositive_leaf_definitions = 0;
  std::size_t inline_retained_nonpositive_leaf_calls = 0;
  std::size_t inline_retained_nonpositive_leaf_instructions = 0;
  std::size_t inline_retained_nonpositive_leaf_estimated_savings = 0;
  std::array<std::size_t, kInlineRetainedMatrixSize>
    inline_retained_discardable_definition_matrix = {};
  std::array<std::size_t, kInlineRetainedMatrixSize>
    inline_retained_discardable_call_matrix = {};
  std::array<std::size_t, kInlineRetainedMatrixSize>
    inline_retained_discardable_instruction_matrix = {};
  std::size_t post_prune_inline_direct_edges = 0;
  std::size_t post_prune_inline_calls = 0;
  std::size_t post_prune_inline_instructions = 0;
  std::size_t post_prune_inline_discarded_bodies = 0;
  std::size_t post_prune_inline_budget_skips = 0;
  std::size_t post_prune_inline_changed_callers = 0;
  std::size_t post_prune_inline_considered_single_calls = 0;
  std::size_t post_prune_inline_reject_recursive = 0;
  std::size_t post_prune_inline_reject_no_inline = 0;
  std::size_t post_prune_inline_reject_argument_shape = 0;
  std::size_t post_prune_inline_reject_variadic = 0;
  std::size_t post_prune_inline_reject_size = 0;
  std::size_t post_prune_inline_reject_landing = 0;
  std::size_t post_prune_inline_reject_eh_unwind = 0;
  std::size_t post_prune_inline_reject_callee_eh = 0;
  std::size_t post_prune_inline_translation_unit_budget = 0;
  std::size_t post_prune_inline_translation_unit_budget_remaining = 0;
  std::size_t inline_changed_callers = 0;
  std::size_t inline_eh_blocked_records = 0;
  std::size_t inline_revisited_callers = 0;
  std::size_t inline_eh_regions_analyzed = 0;
  std::size_t inline_eh_regions_removed = 0;
  std::size_t inline_eh_ambiguous_functions = 0;
  std::size_t inline_no_unwind_published_after_strip = 0;
  // P31 diagnostic-only entry-prefix census.  These counters describe
  // retained direct calls after the ordinary late-inline wave; collecting
  // them never changes LowIR or production inlining policy.
  std::size_t partial_inline_census_direct_calls = 0;
  std::size_t partial_inline_census_eligible_calls = 0;
  std::size_t partial_inline_census_eligible_callees = 0;
  std::size_t partial_inline_census_hint_calls = 0;
  std::size_t partial_inline_census_constant_calls = 0;
  std::size_t partial_inline_census_constant_actuals = 0;
  std::size_t partial_inline_census_loop_calls = 0;
  std::size_t partial_inline_census_repeated_callee_calls = 0;
  std::size_t partial_inline_census_prefix_blocks = 0;
  std::size_t partial_inline_census_prefix_instructions = 0;
  std::size_t partial_inline_census_bailout_edges = 0;
  std::size_t partial_inline_census_prefix_0_8 = 0;
  std::size_t partial_inline_census_prefix_9_12 = 0;
  std::size_t partial_inline_census_prefix_13_16 = 0;
  std::size_t partial_inline_census_prefix_17_24 = 0;
  std::size_t partial_inline_census_prefix_over_24 = 0;
  std::size_t partial_inline_census_reject_recursive = 0;
  std::size_t partial_inline_census_reject_no_inline = 0;
  std::size_t partial_inline_census_reject_argument_shape = 0;
  std::size_t partial_inline_census_reject_variadic = 0;
  std::size_t partial_inline_census_reject_object_result = 0;
  std::size_t partial_inline_census_reject_no_fast_return = 0;
  std::size_t partial_inline_census_reject_no_bailout = 0;
  std::size_t partial_inline_census_call_stops = 0;
  std::size_t partial_inline_census_store_stops = 0;
  std::size_t partial_inline_census_eh_stops = 0;
  std::size_t partial_inline_census_other_stops = 0;
  std::size_t partial_inline_census_backedge_stops = 0;
  std::size_t partial_inline_census_join_stops = 0;
  std::size_t predicate_range_folds = 0;
  std::size_t adjacent_scalar_copy_runs = 0;
  std::size_t adjacent_scalar_copy_groups = 0;
  std::size_t adjacent_scalar_copy_bytes = 0;
  std::size_t overwritten_zero_inits = 0;
  std::size_t overwritten_zero_bytes = 0;
  std::size_t ipa_direct_call_visits = 0;
  std::size_t ipa_instruction_visits = 0;
  std::size_t ipa_candidate_functions = 0;
  std::size_t ipa_address_observable_rejects = 0;
  std::size_t ipa_uniform_parameters = 0;
  std::size_t ipa_disagreeing_parameters = 0;
  std::size_t ipa_substituted_operands = 0;
  std::size_t ipa_dead_parameters = 0;
  std::size_t ipa_calls_rewritten = 0;
  std::size_t ipa_arguments_removed = 0;
  std::size_t ipa_functions_changed = 0;
  std::size_t ipa_specialized_clones = 0;
  std::size_t ipa_cloned_instructions = 0;
  std::size_t ipa_clone_budget_skips = 0;
  std::size_t ipa_peak_analysis_bytes = 0;
  std::size_t repeat_stable_function_visits = 0;
  std::size_t repeat_stable_functions = 0;
  std::size_t repeat_stable_call_sites = 0;
  std::size_t repeat_stable_signatures = 0;
  std::size_t repeat_stable_reuses = 0;
  std::size_t repeat_stable_budget_skips = 0;
  std::size_t repeat_stable_peak_analysis_bytes = 0;
  std::size_t budget_skips = 0;
  std::size_t rewrites = 0;
  std::size_t simplify_runs = 0;
  std::size_t simplify_changes = 0;
  std::size_t simplify_candidate_skips = 0;
  std::size_t gvn_expression_probes = 0;
  std::size_t gvn_expression_hits = 0;
  std::size_t gvn_expression_keys = 0;
  std::size_t gvn_expression_peak_scope = 0;
  std::size_t memory_gvn_runs = 0;
  std::size_t memory_gvn_classes = 0;
  std::size_t memory_gvn_merge_versions = 0;
  std::size_t memory_gvn_load_probes = 0;
  std::size_t memory_gvn_loads_eliminated = 0;
  std::size_t memory_gvn_unknown_barriers = 0;
  std::size_t memory_gvn_eh_functions = 0;
  std::size_t memory_gvn_eh_barriers = 0;
  std::size_t memory_gvn_eh_skips = 0;
  std::size_t memory_gvn_budget_skips = 0;
  std::size_t pre_runs = 0;
  std::size_t pre_candidates = 0;
  std::size_t pre_full_redundancies = 0;
  std::size_t pre_partial_redundancies = 0;
  std::size_t pre_inserted_expressions = 0;
  std::size_t pre_inserted_phis = 0;
  std::size_t pre_availability_probes = 0;
  std::size_t pre_critical_edge_skips = 0;
  std::size_t pre_eh_skips = 0;
  std::size_t pre_budget_skips = 0;
  std::size_t dce_runs = 0;
  std::size_t dce_changes = 0;
  std::size_t dce_candidate_skips = 0;
  std::size_t cfg_runs = 0;
  std::size_t cfg_changes = 0;
  std::size_t slot_runs = 0;
  std::size_t slot_changes = 0;
  std::size_t forward_slot_runs = 0;
  std::size_t forward_slot_changes = 0;
  std::size_t local_slot_runs = 0;
  std::size_t local_slot_changes = 0;
  std::size_t remove_slot_runs = 0;
  std::size_t remove_slot_changes = 0;
  std::size_t promote_slot_runs = 0;
  std::size_t promote_slot_changes = 0;
  std::size_t promote_eligible_slots = 0;
  std::size_t promote_sparse_meets = 0;
  std::size_t promote_sparse_state_entries = 0;
  std::size_t promote_sparse_merge_facts = 0;
  std::size_t promote_blocked_join_loads = 0;
  std::size_t promote_blocked_join_slots = 0;
  std::size_t promote_blocked_join_functions = 0;
  std::size_t promote_blocked_ordinary_loads = 0;
  std::size_t promote_blocked_loop_loads = 0;
  std::size_t promote_blocked_eh_loads = 0;
  std::size_t promote_phi_instructions = 0;
  std::size_t promote_phi_incoming_edges = 0;
  std::size_t promote_phi_loads = 0;
  std::size_t promote_phi_budget_skips = 0;
  std::size_t promote_peak_transient_bytes = 0;
  std::size_t small_object_runs = 0;
  std::size_t small_object_changes = 0;
  std::size_t small_object_candidates = 0;
  std::size_t small_objects_promoted = 0;
  std::size_t sroa_candidates = 0;
  std::size_t sroa_slots_replaced = 0;
  std::size_t sroa_field_slots = 0;
  std::size_t sroa_memory_rewrites = 0;
  std::size_t sroa_copy_expansions = 0;
  std::size_t small_object_memory_rewrites = 0;
  std::size_t small_object_copies_rewritten = 0;
  std::size_t dead_store_runs = 0;
  std::size_t dead_store_changes = 0;
  std::size_t cleanup_resume_runs = 0;
  std::size_t cleanup_resume_block_visits = 0;
  std::size_t cleanup_resume_blocks_removed = 0;
  std::size_t cleanup_tail_runs = 0;
  std::size_t cleanup_tail_block_visits = 0;
  std::size_t cleanup_tail_groups_shared = 0;
  std::size_t cleanup_tail_blocks_rewritten = 0;
  std::size_t cleanup_tail_instructions_removed = 0;
  std::size_t cold_sunk_definitions = 0;
  std::size_t duplicate_block_loads_removed = 0;
  std::size_t staged_copies_forwarded = 0;
  std::size_t unreachable_terminator_blocks = 0;
  std::size_t unreachable_edges_removed = 0;
  std::uint64_t inline_nanoseconds = 0;
  std::uint64_t ipa_nanoseconds = 0;
  std::uint64_t simplify_nanoseconds = 0;
  std::uint64_t memory_gvn_nanoseconds = 0;
  std::uint64_t pre_nanoseconds = 0;
  std::uint64_t dce_nanoseconds = 0;
  std::uint64_t cfg_nanoseconds = 0;
  std::uint64_t slot_nanoseconds = 0;
  std::uint64_t forward_slot_nanoseconds = 0;
  std::uint64_t local_slot_nanoseconds = 0;
  std::uint64_t remove_slot_nanoseconds = 0;
  std::uint64_t promote_slot_nanoseconds = 0;
  std::uint64_t small_object_nanoseconds = 0;
  std::uint64_t dead_store_nanoseconds = 0;
  std::uint64_t cleanup_resume_nanoseconds = 0;
  std::uint64_t cleanup_tail_nanoseconds = 0;
  std::uint64_t unreachable_nanoseconds = 0;
  std::uint64_t loop_nanoseconds = 0;
  std::uint64_t o3_unroll_nanoseconds = 0;
  std::uint64_t late_inline_nanoseconds = 0;
  std::uint64_t post_prune_inline_nanoseconds = 0;
  std::uint64_t partial_inline_census_nanoseconds = 0;
  std::uint64_t repeat_stable_nanoseconds = 0;
  std::uint64_t licm_nanoseconds = 0;
  std::uint64_t elapsed_nanoseconds = 0;
};

// Optimize one already-typed LowIR program in place. Level zero deliberately
// performs no transform; parsing and serialization provide canonicalization.
void optimize(lowir_model::LowirProgram & program, int level,
              Stats * stats = 0,
              const InlinePolicyOverrides * inline_limits = 0);

}  // namespace lowir_opt
