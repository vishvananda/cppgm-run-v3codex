#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;
struct InlineCallGraph;

// Inline conservative, typed direct-call candidates.  Returns the number of
// rewritten call sites and explicit no-unwind EH regions.
std::size_t inline_o1_calls(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  const std::vector<unsigned char> & prepared_oversized_symbols,
  const std::vector<std::size_t> & original_instruction_counts,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0);

// Revisit small acyclic bodies after the selected optimization level's local
// passes have exposed their final compact shape.  The late wave has its own
// bounded caller budget and charges the optimized body size.
std::size_t inline_optimized_calls(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0);

// After reachability pruning, revisit only definition-removing single-call
// candidates.  Callee-first order lets transfer chains cascade in one graph
// wave without enabling ordinary positive-growth inlining.
std::size_t inline_post_prune_single_calls(
  lowir_model::LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols = 0,
  Stats * stats = 0);

}  // namespace lowir_opt
