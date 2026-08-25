#pragma once

#include "lowir_native.h"

#include <iosfwd>

namespace lowir_native {

void report_elf_string_table_stats(std::ostream & output,
  const Stats & stats);
void report_code_shape_stats(std::ostream & output, const Stats & stats);
void report_edge_staging_stats(std::ostream & output, const Stats & stats);
void report_function_census(std::ostream & output, const Stats & stats);

}
