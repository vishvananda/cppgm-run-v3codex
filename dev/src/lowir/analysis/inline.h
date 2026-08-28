#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

enum PartialInlinePrefixStop
{
  PIPS_CALL = 1 << 0,
  PIPS_STORE = 1 << 1,
  PIPS_EH = 1 << 2,
  PIPS_OTHER = 1 << 3,
  PIPS_BACKEDGE = 1 << 4,
  PIPS_JOIN = 1 << 5
};

// A single deterministic entry-to-return path.  Blocks are stored in entry
// order and every ordinary edge from the path to a block outside it is a
// bailout frontier.
struct PartialInlinePrefix
{
  bool has_fast_return = false;
  std::vector<std::size_t> blocks;
  std::size_t instructions = 0;
  std::size_t bailout_edges = 0;
  unsigned stops = 0;
};

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
  // True when a definition is observed through a typed non-call operand or
  // structured object reference.  This is a dense function-index fact; it is
  // used to distinguish a removable direct-call body from an addressable one.
  std::vector<unsigned char> non_call_use;
  std::size_t component_count = 0;

  static std::size_t no_function()
  {
    return static_cast<std::size_t>(-1);
  }
};

InlineCallGraph analyze_inline_call_graph(
  const lowir_model::LowirProgram & program, Stats * stats = 0);

PartialInlinePrefix analyze_partial_inline_prefix(
  const lowir_model::Function & function);

bool partial_inline_actual_is_constant(const lowir_model::Operand & operand);

// Collect final retained-body telemetry after reachability pruning.  This is
// deliberately separate from production policy and is called only when the
// optimizer was given a Stats sink.
void collect_retained_inline_census(
  const lowir_model::LowirProgram & program, Stats * stats);

// Inspect retained O1 direct calls for an acyclic, side-effect-free entry path
// that can return before a bailout to the original call.  This is P31's
// diagnostic-only seam: it records facts in Stats and never mutates LowIR.
void collect_partial_inline_census(
  const lowir_model::LowirProgram & program,
  const InlineCallGraph & graph, Stats * stats);

}  // namespace lowir_opt
