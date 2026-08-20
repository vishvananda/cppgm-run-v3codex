#include "lowir_native_stats_report.h"

#include <ostream>

namespace lowir_native {

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

}
