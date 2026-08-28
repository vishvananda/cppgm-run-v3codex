#pragma once

#include "lowir/analysis/lowir_inline_analysis.h"
#include "lowir/model/lowir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

// Propagate arguments agreed by every direct caller into non-address-observable
// internal functions and remove scalar parameters that become unused.
std::size_t specialize_interprocedural_arguments(
    lowir_model::LowirProgram & program,
    const InlineCallGraph & call_graph,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats = 0);

}  // namespace lowir_opt
