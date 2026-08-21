#pragma once

#include "lowir_function_analysis.h"

namespace lowir_opt {

struct Stats;

bool hoist_loop_invariants(lowir_model::Program * program,
                           lowir_model::Function * function,
                           lowir_analysis::FunctionAnalysis * analysis,
                           int optimization_level, Stats * stats);

}  // namespace lowir_opt
