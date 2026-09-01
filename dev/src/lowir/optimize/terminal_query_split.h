#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

// Keep a pure terminal query arm in place and extract its effectful slow
// suffix after proving that the suffix can return the value it stored.
std::size_t split_o3_terminal_query_slow_suffix(
    lowir_model::LowirProgram & program,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats = 0);

}  // namespace lowir_opt
