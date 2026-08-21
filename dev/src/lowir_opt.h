#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <cstdint>

namespace lowir_opt {

struct Stats
{
  std::size_t functions = 0;
  std::size_t input_instructions = 0;
  std::size_t output_instructions = 0;
  std::size_t instruction_visits = 0;
  std::size_t block_visits = 0;
  std::size_t cfg_edge_visits = 0;
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
  std::size_t worklist_pushes = 0;
  std::size_t dataflow_updates = 0;
  std::size_t inline_direct_edges = 0;
  std::size_t inline_sccs = 0;
  std::size_t inline_recursive_functions = 0;
  std::size_t inline_call_visits = 0;
  std::size_t inline_candidate_calls = 0;
  std::size_t inline_calls = 0;
  std::size_t inline_cloned_instructions = 0;
  std::size_t inline_input_instructions = 0;
  std::size_t inline_output_instructions = 0;
  std::size_t inline_reject_recursive = 0;
  std::size_t inline_reject_no_inline = 0;
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
  std::size_t inline_changed_callers = 0;
  std::size_t inline_eh_blocked_records = 0;
  std::size_t inline_revisited_callers = 0;
  std::size_t budget_skips = 0;
  std::size_t rewrites = 0;
  std::size_t simplify_runs = 0;
  std::size_t simplify_changes = 0;
  std::size_t simplify_candidate_skips = 0;
  std::size_t gvn_expression_probes = 0;
  std::size_t gvn_expression_hits = 0;
  std::size_t gvn_expression_keys = 0;
  std::size_t gvn_expression_peak_scope = 0;
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
  std::uint64_t inline_nanoseconds = 0;
  std::uint64_t simplify_nanoseconds = 0;
  std::uint64_t dce_nanoseconds = 0;
  std::uint64_t cfg_nanoseconds = 0;
  std::uint64_t slot_nanoseconds = 0;
  std::uint64_t forward_slot_nanoseconds = 0;
  std::uint64_t local_slot_nanoseconds = 0;
  std::uint64_t remove_slot_nanoseconds = 0;
  std::uint64_t promote_slot_nanoseconds = 0;
  std::uint64_t dead_store_nanoseconds = 0;
  std::uint64_t cleanup_resume_nanoseconds = 0;
  std::uint64_t cleanup_tail_nanoseconds = 0;
  std::uint64_t loop_nanoseconds = 0;
  std::uint64_t licm_nanoseconds = 0;
  std::uint64_t elapsed_nanoseconds = 0;
};

// Optimize one already-typed LowIR program in place. Level zero deliberately
// performs no transform; parsing and serialization provide canonicalization.
void optimize(lowir_model::LowirProgram & program, int level,
              Stats * stats = 0);

}  // namespace lowir_opt
