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

void report_code_shape_stats(std::ostream & output, const Stats & stats)
{
  output << " three_operand_adds_selected="
         << stats.three_operand_adds_selected;
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
