#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

// Translation-unit direct-call facts in compact function-index space.  The
// forward and reverse edge ranges are CSR arrays; duplicate call edges are
// retained because their count is useful to inlining policy.
struct InlineCallGraph
{
  std::vector<std::size_t> definition_by_symbol;
  std::vector<std::size_t> edge_offsets;
  std::vector<std::size_t> edges;
  std::vector<std::size_t> reverse_offsets;
  std::vector<std::size_t> reverse_edges;
  std::vector<std::size_t> callee_first_order;
  std::vector<std::size_t> component;
  std::vector<unsigned char> recursive;
  std::size_t component_count = 0;

  static std::size_t no_function()
  {
    return static_cast<std::size_t>(-1);
  }
};

InlineCallGraph analyze_inline_call_graph(
  const lowir_model::LowirProgram & program, Stats * stats = 0);

}  // namespace lowir_opt
