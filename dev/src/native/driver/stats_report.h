#pragma once

#include "native/driver/stats.h"

#include <ostream>

namespace lowir_native {

void report_elf_string_table_stats(std::ostream & output,
  const Stats & stats);
void report_codegen_pipeline_stats(std::ostream & output,
  const Stats & stats);
void report_code_shape_stats(std::ostream & output, const Stats & stats);
void report_edge_staging_stats(std::ostream & output, const Stats & stats);
inline void report_codegen_result_stats(
    std::ostream & output, const Stats & stats)
{
  output << " narrow_call_result_normalizations_omitted="
         << stats.narrow_call_result_normalizations_omitted
         << " redundant_integer_normalizations_omitted="
         << stats.redundant_integer_normalizations_omitted
         << " fused_integer_normalization_moves="
         << stats.fused_integer_normalization_moves
         << " scratch_carried_reloads=" << stats.scratch_carried_reloads;
  report_code_shape_stats(output, stats);
  report_edge_staging_stats(output, stats);
  output << " shared_storage_lifetime_extensions="
         << stats.shared_storage_lifetime_extensions
         << " reclaim_attempts=" << stats.reclaim_attempts
         << " reclaim_parameter_visits=" << stats.reclaim_parameter_visits
         << " reclaims=" << stats.reclaims;
}
void report_function_census(std::ostream & output, const Stats & stats);

}
