#pragma once

#include "lowir_model.h"

namespace lowir_opt {

struct Stats;

// Rewrite a complete, nonescaping one-scalar object slot as an ordinary
// scalar slot.  Subsequent scalar-slot promotion owns SSA construction.
bool promote_small_objects(lowir_model::Function * function, Stats * stats);

}  // namespace lowir_opt
