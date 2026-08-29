#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;
struct InlineCallGraph;
struct InlinePolicyOverrides;

// A direct callback used by the optimized-body wave to publish a callee's
// locally simplified shape before its reverse callers are considered.  The
// opaque context avoids a heap-owning callable in this hot path.
struct InlineCleanup
{
  typedef void (*Run)(lowir_model::Function *, Stats *, void *);

  Run run = 0;
  void * context = 0;
};

// Inline conservative, typed direct-call candidates.  Returns the number of
// rewritten call sites and explicit no-unwind EH regions.
std::size_t inline_o1_calls(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  const std::vector<unsigned char> & prepared_oversized_symbols,
  const std::vector<std::size_t> & original_instruction_counts,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0,
  const InlinePolicyOverrides * limit_overrides = 0);

// Revisit small acyclic bodies after the selected optimization level's local
// passes have exposed their final compact shape.  The late wave has its own
// bounded caller budget and charges the optimized body size.
std::size_t inline_optimized_calls(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0,
  const InlineCleanup * cleanup = 0,
  const InlinePolicyOverrides * limit_overrides = 0);

// After reachability pruning, revisit only definition-removing single-call
// candidates.  Callee-first order lets transfer chains cascade in one graph
// wave without enabling ordinary positive-growth inlining.
std::size_t inline_post_prune_single_calls(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0,
  const InlinePolicyOverrides * limit_overrides = 0);

// Revisit one bounded, inline-preferred body/caller pair with exactly two
// calls.  Only the pair's unique natural-loop call is expanded; the callable
// body remains available for the non-loop call site.
std::size_t inline_o3_loop_priority_call(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0,
  const InlineCleanup * cleanup = 0);

}  // namespace lowir_opt
