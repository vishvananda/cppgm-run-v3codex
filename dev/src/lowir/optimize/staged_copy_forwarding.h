#pragma once

#include "lowir/analysis/function.h"
#include "lowir/model/program.h"
#include "lowir/optimize/pipeline.h"

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

// Remove a bounded bulk zero when straight-line scalar stores overwrite every
// byte before the destination can be observed.
bool eliminate_fully_overwritten_zero_inits(
  lowir_model::Function * function, Stats * stats);

// Coalesce an exact straight-line run of adjacent scalar member copies into
// one byte copy.  The run must have no gaps and all address/load temporaries
// must be private to the four-instruction copy groups.
bool coalesce_adjacent_scalar_copies(
  lowir_model::Function * function, Stats * stats);

// Replace a complete terminal object swap that stages the old first object in
// one private slot with ordinary scalar exchanges.  The proof accepts the
// aggregate copy itself or the field-wise form exposed by inlining, requires
// every staged byte to come from the first object before that object is
// overwritten, and rejects escaping staging storage or observable work.
bool lower_terminal_staged_object_swap(
  lowir_model::Function * function, Stats * stats);

}
