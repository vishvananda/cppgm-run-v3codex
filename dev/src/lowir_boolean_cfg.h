#pragma once

#include "lowir_model.h"

namespace lowir_opt {

struct Stats;

bool fold_boolean_phi_branch(
  lowir_model::Function * function, Stats * stats);
bool fold_edge_known_branches(
  lowir_model::Function * function, Stats * stats);
// Terminal folding, unreachable-block removal with dead-edge value
// rematerialization, jump bypassing, and forward block merging.  Functions
// containing phis stop after the two folds above; their CFG stays stable
// until an edge-aware transform requests repair.
bool cleanup_cfg(lowir_model::Function * function, Stats * stats);

}  // namespace lowir_opt
