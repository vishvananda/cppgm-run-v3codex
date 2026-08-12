#pragma once

#include "lowir_model.h"
#include "mir_model.h"

#include <vector>

namespace lowir_native {
namespace eh {

void plan_program(const lowir_model::LowirProgram & source,
                  mir_model::MirProgram & target);
bool lower_marker(const lowir_model::Instruction & source,
                  std::vector<mir_model::MirInstruction> & target);

}  // namespace eh
}  // namespace lowir_native
