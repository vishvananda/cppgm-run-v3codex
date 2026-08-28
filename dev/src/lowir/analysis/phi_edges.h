#pragma once

#include "lowir/model/program.h"

namespace lowir_phi_edges {

// When a transform moves a block's terminal instruction into a continuation,
// update phi inputs reached by that terminal to name the new predecessor.
void rewrite_moved_phi_edges(lowir_model::LowirFunction * function,
                             const lowir_model::Instruction & terminal,
                             lowir_model::BlockId old_predecessor,
                             lowir_model::BlockId continuation);

bool has_critical_phi_edges(const lowir_model::LowirProgram & program);
void split_critical_phi_edges(lowir_model::LowirProgram * program);

}  // namespace lowir_phi_edges
