#pragma once

#include <cstddef>
#include <vector>

#include "lowir/model/program.h"

namespace lowir_native {

struct Stats;

namespace object_elf_detail {
struct HostFunctionLayout;
}

namespace lsda_detail {

struct CallSiteTableEntry
{
  std::size_t start = 0;
  std::size_t length = 0;
  std::size_t landing_pad_offset = 0;
  lowir_model::BlockId action_block;
  bool has_landing_pad = false;
};

void record_unprotected_unwind_range(
    object_elf_detail::HostFunctionLayout & function,
    std::size_t start, std::size_t length);
void coalesce_call_sites(object_elf_detail::HostFunctionLayout & function,
                         Stats * stats);
void sparse_call_site_table(
    const object_elf_detail::HostFunctionLayout & function,
    std::vector<CallSiteTableEntry> & result, Stats * stats);

}  // namespace lsda_detail
}  // namespace lowir_native
