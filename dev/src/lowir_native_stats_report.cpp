#include "lowir_native_stats_report.h"

#include <ostream>

namespace lowir_native {

namespace {

const char * const kMovementReasonNames[NMR_COUNT] = {
  "parameter_home",
  "source_slot",
  "scalar_temporary",
  "object_temporary",
  "call_boundary",
  "cleanup_eh",
  "width_normalization",
  "address_materialization",
  "encoder_fallback",
};

const char * const kTemporaryHomeReasonNames[THR_COUNT] = {
  "scalar_value",
  "object_value",
  "live_across_call",
  "edge_live",
  "register_pressure",
  "address_escape",
  "call_result",
  "extended_representation",
};

}

void report_elf_string_table_stats(std::ostream & output,
    const Stats & stats)
{
  output << " final_shared_string_entries="
         << stats.final_shared_string_entries
         << " final_section_string_reuses="
         << stats.final_section_string_reuses
         << " final_symbol_string_reuses="
         << stats.final_symbol_string_reuses
         << " final_string_suffix_aliases="
         << stats.final_string_suffix_aliases;
}

void report_codegen_pipeline_stats(std::ostream & output,
    const Stats & stats)
{
  output << " functions=" << stats.functions
         << " lowir_instructions=" << stats.lowir_instructions
         << " mir_instructions=" << stats.mir_instructions
         << " machine_opt_input=" << stats.machine_opt_input_instructions
         << " machine_opt_output=" << stats.machine_opt_output_instructions
         << " machine_opt_visits=" << stats.machine_opt_instruction_visits
         << " machine_opt_cfg_edges=" << stats.machine_opt_cfg_edge_visits
         << " machine_opt_pushes=" << stats.machine_opt_worklist_pushes
         << " machine_opt_rewrites=" << stats.machine_opt_rewrites
         << " machine_opt_identity_moves=" << stats.machine_opt_identity_moves
         << " machine_opt_frameless_functions="
         << stats.machine_opt_frameless_functions
         << " machine_opt_frameless_call_functions="
         << stats.machine_opt_frameless_call_functions
         << " machine_opt_frameless_saved_registers="
         << stats.machine_opt_frameless_saved_registers
         << " machine_opt_peak_bytes=" << stats.machine_opt_peak_analysis_bytes
         << " live_location_scans=" << stats.live_location_scans
         << " live_location_value_visits="
         << stats.live_location_value_visits
         << " live_location_alias_queries="
         << stats.live_location_alias_queries
         << " live_location_updates=" << stats.live_location_updates
         << " spill_attempts=" << stats.spill_attempts
         << " spill_value_visits=" << stats.spill_value_visits
         << " spill_candidates=" << stats.spill_candidates
         << " spill_full_scan_fallbacks=" << stats.spill_full_scan_fallbacks
         << " spills=" << stats.spills
         << " temporary_frame_homes_created="
         << stats.temporary_frame_homes_created
         << " temporary_frame_homes_reused="
         << stats.temporary_frame_homes_reused
         << " exact_forward_edge_values=" << stats.exact_forward_edge_values
         << " exact_forward_edge_register_retains="
         << stats.exact_forward_edge_register_retains
         << " planned_edge_register_retains="
         << stats.planned_edge_register_retains
         << " planned_value_registers=" << stats.planned_value_registers
         << " planned_register_grants=" << stats.planned_register_grants
         << " planned_edge_residencies=" << stats.planned_edge_residencies
         << " planned_interval_releases=" << stats.planned_interval_releases;
}

void report_code_shape_stats(std::ostream & output, const Stats & stats)
{
  output << " three_operand_adds_selected="
         << stats.three_operand_adds_selected
         << " load_address_register_takeovers="
         << stats.load_address_register_takeovers;
  for(std::size_t reason = 0; reason < NMR_COUNT; ++reason) {
    output << " movement_" << kMovementReasonNames[reason]
           << "_instructions="
           << stats.movement_instructions_by_reason[reason]
           << " movement_" << kMovementReasonNames[reason]
           << "_loads=" << stats.movement_loads_by_reason[reason]
           << " movement_" << kMovementReasonNames[reason]
           << "_stores=" << stats.movement_stores_by_reason[reason]
           << " movement_" << kMovementReasonNames[reason]
           << "_register_copies="
           << stats.movement_register_copies_by_reason[reason]
           << " movement_" << kMovementReasonNames[reason]
           << "_addresses=" << stats.movement_addresses_by_reason[reason]
           << " movement_" << kMovementReasonNames[reason]
           << "_normalizations="
           << stats.movement_normalizations_by_reason[reason];
  }
  for(std::size_t reason = 0; reason < THR_COUNT; ++reason) {
    output << " temporary_home_" << kTemporaryHomeReasonNames[reason]
           << "_requests=" << stats.temporary_home_requests_by_reason[reason]
           << " temporary_home_" << kTemporaryHomeReasonNames[reason]
           << "_creations=" << stats.temporary_home_creations_by_reason[reason]
           << " temporary_home_" << kTemporaryHomeReasonNames[reason]
           << "_reuses=" << stats.temporary_home_reuses_by_reason[reason];
  }
}

void report_edge_staging_stats(std::ostream & output, const Stats & stats)
{
  output << " edge_staging_total=" << stats.edge_staging_total
         << " planned_rematerialized_addresses="
         << stats.planned_rematerialized_addresses
         << " planned_rematerialized_global_addresses="
         << stats.planned_rematerialized_global_addresses
         << " planned_rematerialized_constant_indexes="
         << stats.planned_rematerialized_constant_indexes
         << " planned_direct_copy_edge_homes="
         << stats.planned_direct_copy_edge_homes
         << " planned_direct_call_edge_homes="
         << stats.planned_direct_call_edge_homes
         << " edge_staging_gpr=" << stats.edge_staging_gpr
         << " edge_staging_xmm=" << stats.edge_staging_xmm
         << " edge_staging_eh=" << stats.edge_staging_eh
         << " edge_staging_loop_invariant="
         << stats.edge_staging_loop_invariant
         << " edge_staging_crosses_call="
         << stats.edge_staging_crosses_call
         << " edge_staging_narrow_alias="
         << stats.edge_staging_narrow_alias
         << " edge_staging_fixed_clobber="
         << stats.edge_staging_fixed_clobber
         << " edge_staging_single_use=" << stats.edge_staging_single_use
         << " edge_staging_multi_use=" << stats.edge_staging_multi_use
         << " edge_staging_addr_slot=" << stats.edge_staging_addr_slot
         << " edge_staging_addr_global=" << stats.edge_staging_addr_global
         << " edge_staging_addr_other=" << stats.edge_staging_addr_other
         << " edge_staging_index_constant="
         << stats.edge_staging_index_constant
         << " edge_staging_index_variable="
         << stats.edge_staging_index_variable
         << " planner_use_tail_candidates="
         << stats.planner_use_tail_candidates
         << " planner_use_tail_assignments="
         << stats.planner_use_tail_assignments
         << " planned_use_tail_promotions="
         << stats.planned_use_tail_promotions
         << " planned_use_tail_busy_fails="
         << stats.planned_use_tail_busy_fails
         << " planner_local_phi_candidates="
         << stats.planner_local_phi_candidates
         << " planner_local_phi_assignments="
         << stats.planner_local_phi_assignments
         << " planned_local_phi_promotions="
         << stats.planned_local_phi_promotions
         << " planned_local_phi_busy_fails="
         << stats.planned_local_phi_busy_fails
         << " planner_cyclic_region_candidates="
         << stats.planner_cyclic_region_candidates
         << " planner_cyclic_region_assignments="
         << stats.planner_cyclic_region_assignments
         << " planned_cyclic_region_grants="
         << stats.planned_cyclic_region_grants
         << " planned_cyclic_region_residencies="
         << stats.planned_cyclic_region_residencies
         << " planned_cyclic_region_busy_fails="
         << stats.planned_cyclic_region_busy_fails
         << " edge_staging_by_kind=";
  for(std::size_t i = 0; i < stats.edge_staging_by_kind.size(); ++i) {
    if(i) output << ',';
    output << stats.edge_staging_by_kind[i];
  }
}


void report_function_census(std::ostream & output, const Stats & stats)
{
  for(std::size_t i = 0; i < stats.function_census_lines.size(); ++i)
    output << stats.function_census_lines[i] << '\n';
}

}
