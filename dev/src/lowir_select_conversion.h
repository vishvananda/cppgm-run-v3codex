#pragma once

#include "lowir_model.h"

namespace lowir_opt {

struct Stats;

// Collapse two-arm branch diamonds whose arms only stage pure speculatable
// values into `select` instructions, removing the branch and both arm
// blocks from the hot path.
bool convert_select_diamonds(lowir_model::LowirFunction * function,
                             Stats * stats);

}  // namespace lowir_opt
