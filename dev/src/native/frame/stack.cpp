#include "native/frame/stack.h"

#include "native/mir/construction.h"

namespace lowir_native {
namespace stack {

void append_dynamic_allocation(
    std::vector<mir_model::MirInstruction> & out)
{
  using namespace build;
  mir_model::MirInstruction round =
    machine_instruction(mir_model::MirInstruction::MI_ADD);
  append_operand(round, reg_operand(XR_RAX));
  append_operand(round, immediate(15));
  out.push_back(round);
  mir_model::MirInstruction align =
    machine_instruction(mir_model::MirInstruction::MI_AND);
  append_operand(align, reg_operand(XR_RAX));
  append_operand(align, immediate(-16));
  out.push_back(align);
  mir_model::MirInstruction allocate =
    machine_instruction(mir_model::MirInstruction::MI_SUB);
  append_operand(allocate, reg_operand(XR_RSP));
  append_operand(allocate, reg_operand(XR_RAX));
  out.push_back(allocate);
}

}  // namespace stack
}  // namespace lowir_native
