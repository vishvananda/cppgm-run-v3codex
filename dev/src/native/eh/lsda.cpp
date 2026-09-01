#include "native/eh/lsda.h"

#include "native/driver/stats.h"
#include "native/errors.h"
#include "native/object/elf_format.h"

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
  return left.landing_pad_offset == right.landing_pad_offset &&
    left.action_block == right.action_block;
}

std::size_t range_end(std::size_t start, std::size_t length)
{
  if(length > static_cast<std::size_t>(-1) - start)
    native_errors::ThrowResourceLimit("host EH range overflow");
  return start + length;
}

void append_sparse_entry(std::vector<CallSiteTableEntry> & entries,
                         const CallSiteTableEntry & entry)
{
  if(entry.length == 0) return;
  const std::size_t end = range_end(entry.start, entry.length);
  if(entries.empty()) {
    entries.push_back(entry);
    return;
  }

  CallSiteTableEntry & previous = entries.back();
  const std::size_t previous_end = range_end(previous.start, previous.length);
  if(entry.start < previous_end)
    native_errors::ThrowInternal("overlapping host EH call-site ranges");
  if(entry.start == previous_end && !entry.has_landing_pad &&
     !previous.has_landing_pad) {
    previous.length = end - previous.start;
    return;
  }
  entries.push_back(entry);
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

void sparse_call_site_table(
    const HostFunctionLayout & function,
    std::vector<CallSiteTableEntry> & result, Stats * stats)
{
  const std::vector<HostFunctionLayout::CallSite> & protected_sites =
    function.call_sites;
  const std::vector<HostFunctionLayout::UnwindRange> & unprotected_sites =
    function.unprotected_unwind_ranges;
  result.clear();
  result.reserve(protected_sites.size() + unprotected_sites.size());

  std::size_t unprotected_index = 0;
  for(std::size_t protected_index = 0;
      protected_index < protected_sites.size(); ++protected_index) {
    const HostFunctionLayout::CallSite & protected_site =
      protected_sites[protected_index];
    CallSiteTableEntry unprotected_hull;
    while(unprotected_index < unprotected_sites.size() &&
          unprotected_sites[unprotected_index].start < protected_site.start) {
      const HostFunctionLayout::UnwindRange & source =
        unprotected_sites[unprotected_index++];
      const std::size_t end = range_end(source.start, source.length);
      if(end > protected_site.start)
        native_errors::ThrowInternal("overlapping protected host EH range");
      if(unprotected_hull.length == 0) {
        unprotected_hull.start = source.start;
        unprotected_hull.length = source.length;
      } else {
        const std::size_t hull_end = range_end(
          unprotected_hull.start, unprotected_hull.length);
        if(source.start < hull_end)
          native_errors::ThrowInternal("overlapping unprotected host EH ranges");
        unprotected_hull.length = end - unprotected_hull.start;
      }
    }
    append_sparse_entry(result, unprotected_hull);

    CallSiteTableEntry entry;
    entry.start = protected_site.start;
    entry.length = protected_site.length;
    entry.landing_pad_offset = protected_site.landing_pad_offset;
    entry.action_block = protected_site.action_block;
    entry.has_landing_pad = true;
    if(entry.landing_pad_offset == 0)
      native_errors::ThrowInternal("zero host EH landing-pad offset");
    append_sparse_entry(result, entry);
  }

  CallSiteTableEntry trailing_hull;
  while(unprotected_index < unprotected_sites.size()) {
    const HostFunctionLayout::UnwindRange & source =
      unprotected_sites[unprotected_index++];
    const std::size_t end = range_end(source.start, source.length);
    if(trailing_hull.length == 0) {
      trailing_hull.start = source.start;
      trailing_hull.length = source.length;
    } else {
      const std::size_t hull_end = range_end(
        trailing_hull.start, trailing_hull.length);
      if(source.start < hull_end)
        native_errors::ThrowInternal("overlapping unprotected host EH ranges");
      trailing_hull.length = end - trailing_hull.start;
    }
  }
  append_sparse_entry(result, trailing_hull);

  if(!result.empty() && range_end(result.back().start,
                                   result.back().length) > function.size)
    native_errors::ThrowInternal("host EH coverage exceeds function size");
  if(stats) {
    std::size_t covered_bytes = 0;
    for(std::size_t i = 0; i < result.size(); ++i) {
      covered_bytes += result[i].length;
      if(!result[i].has_landing_pad)
        ++stats->eh_lsda_unprotected_call_sites;
    }
    stats->eh_lsda_uncovered_code_bytes += function.size - covered_bytes;
  }
}

}  // namespace lsda_detail
}  // namespace lowir_native
