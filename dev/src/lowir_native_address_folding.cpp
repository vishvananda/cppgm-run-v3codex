#include "lowir_native_address_folding.h"

#include <limits>

namespace lowir_native {
namespace address_folding {
namespace {

using mir_model::MirInstruction;
using mir_model::MirOperand;

bool add_offsets(long long left, long long right, long long * result)
{
  if((right > 0 && left > std::numeric_limits<long long>::max() - right) ||
     (right < 0 && left < std::numeric_limits<long long>::min() - right))
    return false;
  *result = left + right;
  return true;
}

bool simple_register_definition(const MirInstruction & instruction)
{
  return (instruction.opcode == MirInstruction::MI_MOV ||
          instruction.opcode == MirInstruction::MI_LOAD ||
          instruction.opcode == MirInstruction::MI_LEA) &&
    instruction.operands.size() == 2 &&
    instruction.operands[0].kind == MirOperand::OP_REG;
}

bool source_reads_register(const MirInstruction & instruction,
                           X64Register reg)
{
  for(std::size_t i = 1; i < instruction.operands.size(); ++i) {
    const MirOperand & operand = instruction.operands[i];
    if((operand.kind == MirOperand::OP_REG ||
        operand.kind == MirOperand::OP_DEREF) && operand.reg == reg)
      return true;
  }
  return false;
}

bool overwritten_without_read(
    const std::vector<MirInstruction> & instructions, std::size_t start,
    X64Register reg)
{
  const std::size_t limit = instructions.size() - start > 5 ?
    start + 5 : instructions.size();
  for(std::size_t i = start; i < limit; ++i) {
    const MirInstruction & instruction = instructions[i];
    if(!simple_register_definition(instruction) ||
       source_reads_register(instruction, reg)) return false;
    if(instruction.operands[0].reg == reg) return true;
  }
  return false;
}

}  // namespace

bool plan_dead_setup_load(
    const std::vector<MirInstruction> & instructions, std::size_t start,
    MirOperand * folded_address)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return false;
  const MirInstruction & setup = instructions[start];
  const MirInstruction & load = instructions[start + 1];
  if((setup.opcode != MirInstruction::MI_LEA &&
      setup.opcode != MirInstruction::MI_MOV) ||
     load.opcode != MirInstruction::MI_LOAD || load.operands.size() != 2 ||
     setup.operands.size() != 2 ||
     setup.operands[0].kind != MirOperand::OP_REG ||
     ((setup.opcode == MirInstruction::MI_LEA &&
       setup.operands[1].kind != MirOperand::OP_FRAME &&
       setup.operands[1].kind != MirOperand::OP_DEREF) ||
      (setup.opcode == MirInstruction::MI_MOV &&
       setup.operands[1].kind != MirOperand::OP_REG)) ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_DEREF ||
     load.operands[1].reg != setup.operands[0].reg)
    return false;

  const X64Register setup_reg = setup.operands[0].reg;
  const bool self_copy = setup.opcode == MirInstruction::MI_MOV &&
    setup.operands[1].reg == setup_reg;
  if(!self_copy && load.operands[0].reg != setup_reg &&
     !overwritten_without_read(instructions, start + 2, setup_reg))
    return false;

  if(setup.opcode == MirInstruction::MI_MOV) {
    *folded_address = load.operands[1];
    folded_address->reg = setup.operands[1].reg;
  } else {
    long long offset = 0;
    if(!add_offsets(setup.operands[1].offset, load.operands[1].offset,
                    &offset)) return false;
    if(setup.operands[1].kind == MirOperand::OP_FRAME &&
       (setup.operands[1].offset < 0) != (offset < 0)) return false;
    *folded_address = setup.operands[1];
    folded_address->offset = offset;
  }
  return true;
}

}  // namespace address_folding
}  // namespace lowir_native
