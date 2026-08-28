#pragma once

#include "lowir/analysis/function.h"

#include <cstddef>

namespace lowir_opt {

struct Stats;

struct O3UnrollBudget
{
  std::size_t cloned_instructions = 0;
};

// Fully unroll at most one canonical constant-trip loop in a function.  The
// translation-unit budget is shared by every function in one optimizer run.
bool fully_unroll_small_loop(
    lowir_model::Function * function,
    lowir_analysis::FunctionAnalysis * analysis,
    O3UnrollBudget * budget,
    Stats * stats);

}  // namespace lowir_opt
