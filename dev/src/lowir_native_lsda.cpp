#include "lowir_native_lsda.h"

#include "lowir_native.h"
#include "lowir_native_object_elf.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace lowir_native {
namespace lsda_detail {
namespace {

typedef object_elf_detail::HostFunctionLayout HostFunctionLayout;

bool same_handler(const HostFunctionLayout::CallSite & left,
                  const HostFunctionLayout::CallSite & right)
{
  return left.landing_pad == right.landing_pad &&
    left.action_pad == right.action_pad;
}

}  // namespace

void record_unprotected_unwind_range(HostFunctionLayout & function,
                                     std::size_t start, std::size_t length)
{
  HostFunctionLayout::UnwindRange range;
  range.start = start;
  range.length = length;
  function.unprotected_unwind_ranges.push_back(range);
}

void coalesce_call_sites(HostFunctionLayout & function, Stats * stats)
{
  std::vector<HostFunctionLayout::CallSite> & sites = function.call_sites;
  std::vector<HostFunctionLayout::UnwindRange> & barriers =
    function.unprotected_unwind_ranges;

  const std::size_t input_count = sites.size();
  std::size_t output_count = 0;
  std::size_t barrier = 0;
  for(std::size_t i = 0; i < sites.size(); ++i) {
    if(output_count == 0) {
      if(i != 0) sites[0] = std::move(sites[i]);
      ++output_count;
      continue;
    }

    HostFunctionLayout::CallSite & previous = sites[output_count - 1];
    const std::size_t previous_end = previous.start + previous.length;
    while(barrier < barriers.size() &&
          barriers[barrier].start < previous_end)
      ++barrier;
    const bool unprotected_unwind_in_gap =
      barrier < barriers.size() && barriers[barrier].start < sites[i].start;
    if(!unprotected_unwind_in_gap && same_handler(previous, sites[i])) {
      const std::size_t current_end = sites[i].start + sites[i].length;
      previous.length = std::max(previous_end, current_end) - previous.start;
    } else {
      if(output_count != i) sites[output_count] = std::move(sites[i]);
      ++output_count;
    }
  }
  sites.resize(output_count);

  if(stats) {
    stats->eh_lsda_call_sites += sites.size();
    stats->eh_coalesced_call_sites += input_count - sites.size();
  }
}

}  // namespace lsda_detail
}  // namespace lowir_native
