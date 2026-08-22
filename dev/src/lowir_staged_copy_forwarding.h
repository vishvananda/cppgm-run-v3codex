#pragma once

#include "lowir_function_analysis.h"
#include "lowir_model.h"
#include "lowir_opt.h"

namespace lowir_opt {

// A staging slot that is only ever written field-by-field and then copied
// whole into a destination behaves as a member-wise copy: the copyobj is
// replaced with direct stores of the staged values to the destination and
// the staging traffic dies.  Padding bytes are not copied, matching the
// member-wise object-copy contract the frontend already relies on.
bool forward_staged_object_copies(
    lowir_model::Function * function,
    lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats);

}
