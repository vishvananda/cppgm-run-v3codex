#pragma once

#include "lowir_model.h"

namespace lowir_opt {

struct Stats;

bool fold_boolean_phi_branch(
  lowir_model::Function * function, Stats * stats);
bool fold_edge_known_branches(
  lowir_model::Function * function, Stats * stats);

}  // namespace lowir_opt
