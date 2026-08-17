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

bool plan_dead_address_load(
    const std::vector<MirInstruction> & instructions, std::size_t start,
    MirOperand * folded_address)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return false;
  const MirInstruction & address = instructions[start];
  const MirInstruction & load = instructions[start + 1];
  if(address.opcode != MirInstruction::MI_LEA ||
     address.operands.size() != 2 ||
     address.operands[0].kind != MirOperand::OP_REG ||
     (address.operands[1].kind != MirOperand::OP_FRAME &&
      address.operands[1].kind != MirOperand::OP_DEREF) ||
     load.opcode != MirInstruction::MI_LOAD || load.operands.size() != 2 ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_DEREF ||
     load.operands[1].reg != address.operands[0].reg)
    return false;

  const X64Register address_reg = address.operands[0].reg;
  if(load.operands[0].reg != address_reg &&
     !overwritten_without_read(instructions, start + 2, address_reg))
    return false;

  long long offset = 0;
  if(!add_offsets(address.operands[1].offset, load.operands[1].offset,
                  &offset)) return false;
  if(address.operands[1].kind == MirOperand::OP_FRAME &&
     (address.operands[1].offset < 0) != (offset < 0)) return false;

  *folded_address = address.operands[1];
  folded_address->offset = offset;
  return true;
}

}  // namespace address_folding
}  // namespace lowir_native
