#pragma once

#include "lowir_function_analysis.h"
#include "lowir_model.h"

namespace lowir_opt {

struct Stats;

bool eliminate_partial_redundancies(
  lowir_model::Function * function,
  lowir_analysis::FunctionAnalysis * analysis, Stats * stats);

}  // namespace lowir_opt
