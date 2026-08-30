#pragma once

#include "lowir/model/program.h"

namespace lowir_opt {

struct Stats;

// Rewrite a complete, nonescaping one-scalar object slot as an ordinary
// scalar slot.  Subsequent scalar-slot promotion owns SSA construction.
bool promote_small_objects(lowir_model::Function * function, Stats * stats);

// At O3, also recover scalar reference temporaries whose derived address is
// used only by complete, type-compatible nonvolatile memory operations.
bool promote_small_objects_with_addressed_scalars(
  lowir_model::Function * function, Stats * stats);

// Split a complete, nonescaping multi-field object slot into one scalar
// slot per accessed field (padding gaps synthesize integer fields so
// whole-object copies keep exact memcpy semantics).  Subsequent
// scalar-slot promotion owns SSA construction.
bool scalar_replace_aggregate_slots(lowir_model::LowirProgram * program,
                                    lowir_model::Function * function,
                                    Stats * stats);

}  // namespace lowir_opt
