#pragma once

#include "lowir_native.h"

#include <iosfwd>

namespace lowir_native {

void report_elf_string_table_stats(std::ostream & output,
  const Stats & stats);

}
