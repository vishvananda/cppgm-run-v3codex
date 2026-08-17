#include "lowir_native_lsda.h"
#include "lowir_native_object_elf.h"

#include <cassert>

namespace {

typedef lowir_native::object_elf_detail::HostFunctionLayout Layout;

Layout::CallSite site(std::size_t start, const char * landing,
                      const char * action)
{
  Layout::CallSite result;
  result.start = start;
  result.length = 5;
  result.landing_pad = landing;
  result.action_pad = action;
  return result;
}

Layout::UnwindRange barrier(std::size_t start)
{
  Layout::UnwindRange result;
  result.start = start;
  result.length = 5;
  return result;
}

}  // namespace

int main()
{
  Layout no_throw_gap;
  no_throw_gap.call_sites.push_back(site(10, "cleanup", "action"));
  no_throw_gap.call_sites.push_back(site(30, "cleanup", "action"));
  lowir_native::lsda_detail::coalesce_call_sites(no_throw_gap, 0);
  assert(no_throw_gap.call_sites.size() == 1);
  assert(no_throw_gap.call_sites[0].start == 10);
  assert(no_throw_gap.call_sites[0].length == 25);

  Layout throwing_gap;
  throwing_gap.call_sites.push_back(site(10, "cleanup", "action"));
  throwing_gap.call_sites.push_back(site(30, "cleanup", "action"));
  throwing_gap.unprotected_unwind_ranges.push_back(barrier(20));
  lowir_native::lsda_detail::coalesce_call_sites(throwing_gap, 0);
  assert(throwing_gap.call_sites.size() == 2);

  Layout different_landing;
  different_landing.call_sites.push_back(site(10, "cleanup_a", "action"));
  different_landing.call_sites.push_back(site(30, "cleanup_b", "action"));
  lowir_native::lsda_detail::coalesce_call_sites(different_landing, 0);
  assert(different_landing.call_sites.size() == 2);

  Layout different_action;
  different_action.call_sites.push_back(site(10, "cleanup", "action_a"));
  different_action.call_sites.push_back(site(30, "cleanup", "action_b"));
  lowir_native::lsda_detail::coalesce_call_sites(different_action, 0);
  assert(different_action.call_sites.size() == 2);
}
