#pragma once

#include "lowir/model/program.h"
#include "native/mir/construction.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace host_eh_detail {

struct HostEhRegionPlan
{
  // Zero means no protected region; other values are MIR block index + 1.
  std::vector<std::vector<std::size_t> > call_landing_blocks;
  std::size_t state_count = 0;
  std::size_t edge_count = 0;
  std::size_t protected_call_count = 0;
  std::size_t resume_instruction_count = 0;
};

bool requires_host_eh_storage(const lowir_model::LowirFunction & function);
void collect_host_eh_clauses(mir_model::MirFunction * function);
HostEhRegionPlan analyze_host_eh_regions(
  const mir_model::MirFunction & function,
  const lowir_model::SealedStringPool & strings,
  const std::string & function_name);

}  // namespace host_eh_detail
}  // namespace lowir_native
