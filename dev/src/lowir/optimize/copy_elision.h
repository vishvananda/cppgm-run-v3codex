#pragma once

#include "lowir/model/program.h"

namespace lowir_opt {

struct Stats;

// Consume one or more serialized source-language copy-elision permissions.
// The pass is deliberately narrow: every incoming edge must construct the
// same private temporary, and every use of that temporary address must belong
// to the marked transfer/lifetime shape.
bool coalesce_copy_elision_candidates(lowir_model::Function * function,
                                      Stats * stats);

}  // namespace lowir_opt
