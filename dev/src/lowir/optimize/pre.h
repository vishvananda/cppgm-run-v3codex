#pragma once

#include "lowir/analysis/function.h"
#include "lowir/model/program.h"

namespace lowir_opt {

struct Stats;

bool eliminate_partial_redundancies(
  lowir_model::Function * function,
  lowir_analysis::FunctionAnalysis * analysis, Stats * stats);

}  // namespace lowir_opt
