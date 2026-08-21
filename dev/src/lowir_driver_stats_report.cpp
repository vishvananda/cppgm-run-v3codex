#include "lowir_driver_stats_report.h"

#include "lowir_opt.h"
#include "lowir_prepare.h"
#include "mir_model.h"
#include "pa15_lowering.h"

#include <ostream>

namespace lowir_driver_stats_report
{

void ReportOptimizer(std::ostream& output, const std::string& input,
	const lowir_opt::Stats& stats)
{
	output << "pa37_opt_stats"
		 << " input=" << input
		 << " functions=" << stats.functions
		 << " input_instructions=" << stats.input_instructions
		 << " output_instructions=" << stats.output_instructions
		 << " instruction_visits=" << stats.instruction_visits
		 << " block_visits=" << stats.block_visits
		 << " cfg_edge_visits=" << stats.cfg_edge_visits
		 << " cfg_analysis_builds=" << stats.cfg_analysis_builds
		 << " cfg_analysis_reuses=" << stats.cfg_analysis_reuses
		 << " cfg_analysis_invalidations=" << stats.cfg_analysis_invalidations
		 << " dominator_analysis_builds=" << stats.dominator_analysis_builds
		 << " dominator_analysis_reuses=" << stats.dominator_analysis_reuses
		 << " worklist_pushes=" << stats.worklist_pushes
		 << " dataflow_updates=" << stats.dataflow_updates
		 << " inline_direct_edges=" << stats.inline_direct_edges
		 << " inline_sccs=" << stats.inline_sccs
		 << " inline_recursive_functions=" << stats.inline_recursive_functions
		 << " inline_call_visits=" << stats.inline_call_visits
		 << " inline_candidate_calls=" << stats.inline_candidate_calls
		 << " inline_calls=" << stats.inline_calls
		 << " inline_cloned_instructions=" << stats.inline_cloned_instructions
		 << " inline_input_instructions=" << stats.inline_input_instructions
		 << " inline_output_instructions=" << stats.inline_output_instructions
		 << " inline_reject_recursive=" << stats.inline_reject_recursive
		 << " inline_reject_no_inline=" << stats.inline_reject_no_inline
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
		 << " inline_changed_callers=" << stats.inline_changed_callers
		 << " inline_eh_blocked_records=" << stats.inline_eh_blocked_records
		 << " inline_revisited_callers=" << stats.inline_revisited_callers
		 << " budget_skips=" << stats.budget_skips
		 << " rewrites=" << stats.rewrites
		 << " simplify_runs=" << stats.simplify_runs
		 << " simplify_changes=" << stats.simplify_changes
		 << " simplify_candidate_skips=" << stats.simplify_candidate_skips
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
		 << " inline_ns=" << stats.inline_nanoseconds
		 << " simplify_ns=" << stats.simplify_nanoseconds
		 << " dce_ns=" << stats.dce_nanoseconds
		 << " cfg_ns=" << stats.cfg_nanoseconds
		 << " slot_ns=" << stats.slot_nanoseconds
		 << " forward_slot_ns=" << stats.forward_slot_nanoseconds
		 << " local_slot_ns=" << stats.local_slot_nanoseconds
		 << " remove_slot_ns=" << stats.remove_slot_nanoseconds
		 << " promote_slot_ns=" << stats.promote_slot_nanoseconds
		 << " dead_store_ns=" << stats.dead_store_nanoseconds
		 << " cleanup_resume_ns=" << stats.cleanup_resume_nanoseconds
		 << " cleanup_tail_ns=" << stats.cleanup_tail_nanoseconds
		 << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
}

void ReportPreparation(std::ostream& output, const std::string& path,
	const cppgm::LowIRLoweringStats& stats,
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
	const cppgm::LowIRLoweringStats& stats,
	const lowir_model::LowirPreparationStats& preparation_stats,
	std::uint64_t typed_pipeline_nanoseconds,
	std::uint64_t adapter_nanoseconds,
	std::uint64_t text_parse_nanoseconds,
	std::uint64_t prune_nanoseconds,
	std::uint64_t debug_nanoseconds,
	std::uint64_t lowir_opt_nanoseconds)
{
	const cppgm::SemanticAnalysisStats& semantic = stats.semantic;
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
