#pragma once

#include "lowir/analysis/inline.h"
#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct InlineCleanup;
struct Stats;

struct StablePrefixSpecializationIndex
{
  std::vector<unsigned char> known;
  std::vector<lowir_model::SymbolId> family;
  std::vector<std::uint64_t> index;
};

// Propagate arguments agreed by every direct caller into non-address-observable
// internal functions and remove scalar parameters that become unused.
std::size_t specialize_interprocedural_arguments(
    lowir_model::LowirProgram & program,
    const InlineCallGraph & call_graph,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats = 0);

// Split a repeated constant group from an otherwise mixed internal target.
// The caller supplies ordinary local cleanup so the generic and specialized
// bodies can be costed on comparable simplified shapes before redirection.
std::size_t specialize_o3_constant_groups(
    lowir_model::LowirProgram & program,
    const InlineCallGraph & call_graph,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats,
    const InlineCleanup * cleanup,
    StablePrefixSpecializationIndex * stable_prefix_specializations = 0);

// Reuse a normally returned result from a structurally repeat-stable query
// until an intervening operation may change memory.
std::size_t eliminate_repeated_stable_calls(
    lowir_model::LowirProgram & program,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats = 0,
    const StablePrefixSpecializationIndex * stable_prefix_specializations = 0);

// Keep one bounded, call-free entry-to-return corridor in the original
// function and send pre-side-effect exits to a complete private clone.
std::size_t split_o3_fast_function_path(
    lowir_model::LowirProgram & program,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats = 0);

bool thread_fast_split_phi_comparison(lowir_model::Function * function);

}  // namespace lowir_opt
