#pragma once

#include "lowir/model/lowir_model.h"

#include <vector>

namespace lowir_opt {

struct Stats;

struct CleanupCfgScratch
{
  std::vector<unsigned char> branch_values;
};

bool fold_boolean_phi_branch(
  lowir_model::Function * function, Stats * stats);
bool fold_edge_known_branches(
  lowir_model::Function * function, Stats * stats);
bool fold_edge_known_branches(
  lowir_model::Function * function, Stats * stats,
  CleanupCfgScratch * scratch);
// Fold the unsigned x-1 underflow test on an edge where a preceding branch
// establishes that the same SSA value, or a stable reload of it, is nonzero.
bool fold_nonzero_underflow_branches(
  lowir_model::Function * function, Stats * stats);
// Terminal folding, unreachable-block removal with dead-edge value
// rematerialization, jump bypassing, and forward block merging.  Functions
// containing phis stop after the two folds above; their CFG stays stable
// until an edge-aware transform requests repair.
bool cleanup_cfg(lowir_model::Function * function, Stats * stats);
bool cleanup_cfg(lowir_model::Function * function, Stats * stats,
                 CleanupCfgScratch * scratch);

}  // namespace lowir_opt
