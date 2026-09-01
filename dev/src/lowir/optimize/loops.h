#pragma once

#include "lowir/analysis/function.h"

namespace lowir_opt {

struct Stats;

bool hoist_loop_invariants(lowir_model::Program * program,
                           lowir_model::Function * function,
                           lowir_analysis::FunctionAnalysis * analysis,
                           int optimization_level, Stats * stats);

bool forward_loop_carried_store_loads(
  lowir_model::Function * function,
  lowir_analysis::FunctionAnalysis * analysis, Stats * stats,
  bool require_inline_hint = true);

}  // namespace lowir_opt
