#pragma once

#include "lowir/analysis/function.h"

namespace lowir_opt {

struct Stats;

bool simplify_counted_loops(lowir_model::Function * function,
                            lowir_analysis::FunctionAnalysis * analysis,
                            Stats * stats);

}  // namespace lowir_opt
