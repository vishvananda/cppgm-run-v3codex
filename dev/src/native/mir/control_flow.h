#pragma once

#include "native/mir/model.h"

namespace lowir_native {
namespace mir_control_flow {

inline bool ends_unconditional_control_flow(
    mir_model::MirInstruction::Opcode opcode)
{
  return opcode == mir_model::MirInstruction::MI_JMP ||
    opcode == mir_model::MirInstruction::MI_JMP_INDIRECT ||
    opcode == mir_model::MirInstruction::MI_SIBLING_CALL ||
    opcode == mir_model::MirInstruction::MI_RET ||
    opcode == mir_model::MirInstruction::MI_FRET ||
    opcode == mir_model::MirInstruction::MI_RESUME ||
    opcode == mir_model::MirInstruction::MI_THROW ||
    opcode == mir_model::MirInstruction::MI_EXIT;
}

}  // namespace mir_control_flow
}  // namespace lowir_native
