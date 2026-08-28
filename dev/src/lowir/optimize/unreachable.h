#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

// Dense program fact used by the O1 CFG pass.  Symbol spellings are not part
// of the optimization identity.
std::vector<unsigned char> noreturn_symbol_index(
    const lowir_model::LowirProgram & program);

bool truncate_noreturn_continuations(
    lowir_model::Function * function,
    const std::vector<unsigned char> & noreturn_symbols,
    Stats * stats = 0);

bool eliminate_unreachable_edges(lowir_model::Function * function,
                                 Stats * stats = 0);

}  // namespace lowir_opt
