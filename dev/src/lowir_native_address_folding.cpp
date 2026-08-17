#include "lowir_native_address_folding.h"
#include "lowir_native_data_layout.h"
#include "lowir_native_encoding.h"

#include <limits>
#include <stdexcept>

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

std::size_t plan_dead_setup_load(
    const std::vector<MirInstruction> & instructions, std::size_t start,
    MirOperand * folded_address)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return 0;
  const MirInstruction & setup = instructions[start];
  std::size_t load_index = start + 1;
  const MirInstruction * address = 0;
  if(setup.opcode == MirInstruction::MI_MOV &&
     instructions.size() - start >= 3 &&
     instructions[start + 1].opcode == MirInstruction::MI_LEA) {
    address = &instructions[start + 1];
    load_index = start + 2;
  }
  const MirInstruction & load = instructions[load_index];
  if((setup.opcode != MirInstruction::MI_LEA &&
      setup.opcode != MirInstruction::MI_MOV) ||
     load.opcode != MirInstruction::MI_LOAD || load.operands.size() != 2 ||
     setup.operands.size() != 2 ||
     setup.operands[0].kind != MirOperand::OP_REG ||
     ((!address && setup.opcode == MirInstruction::MI_LEA &&
       setup.operands[1].kind != MirOperand::OP_FRAME &&
       setup.operands[1].kind != MirOperand::OP_DEREF) ||
      (setup.opcode == MirInstruction::MI_MOV &&
       setup.operands[1].kind != MirOperand::OP_REG)) ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_DEREF ||
     load.operands[1].reg != setup.operands[0].reg)
    return 0;

  const X64Register setup_reg = setup.operands[0].reg;
  if(address && (address->operands.size() != 2 ||
     address->operands[0].kind != MirOperand::OP_REG ||
     address->operands[0].reg != setup_reg ||
     address->operands[1].kind != MirOperand::OP_DEREF ||
     address->operands[1].reg != setup_reg)) return 0;
  const bool self_copy = !address && setup.opcode == MirInstruction::MI_MOV &&
    setup.operands[1].reg == setup_reg;
  if(!self_copy && load.operands[0].reg != setup_reg &&
     !overwritten_without_read(instructions, load_index + 1, setup_reg))
    return 0;

  if(address) {
    long long offset = 0;
    if(!add_offsets(address->operands[1].offset, load.operands[1].offset,
                    &offset)) return 0;
    *folded_address = load.operands[1];
    folded_address->reg = setup.operands[1].reg;
    folded_address->offset = offset;
  } else if(setup.opcode == MirInstruction::MI_MOV) {
    *folded_address = load.operands[1];
    folded_address->reg = setup.operands[1].reg;
  } else {
    long long offset = 0;
    if(!add_offsets(setup.operands[1].offset, load.operands[1].offset,
                    &offset)) return 0;
    if(setup.operands[1].kind == MirOperand::OP_FRAME &&
       (setup.operands[1].offset < 0) != (offset < 0)) return 0;
    *folded_address = setup.operands[1];
    folded_address->offset = offset;
  }
  return load_index - start + 1;
}

}  // namespace

std::size_t emit_dead_setup_load(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function)
{
  if(start >= instructions.size() ||
     (instructions[start].opcode != MirInstruction::MI_LEA &&
      instructions[start].opcode != MirInstruction::MI_MOV)) return 0;
  const bool pair = start + 1 < instructions.size() &&
    instructions[start + 1].opcode == MirInstruction::MI_LOAD;
  const bool chain = start + 2 < instructions.size() &&
    instructions[start].opcode == MirInstruction::MI_MOV &&
    instructions[start + 1].opcode == MirInstruction::MI_LEA &&
    instructions[start + 2].opcode == MirInstruction::MI_LOAD;
  if(!pair && !chain) return 0;
  MirOperand address;
  const std::size_t count =
    plan_dead_setup_load(instructions, start, &address);
  if(!count) return 0;
  const MirInstruction & load = instructions[start + count - 1];
  long long offset = address.offset;
  X64Register base = address.reg;
  if(address.kind == MirOperand::OP_FRAME) {
    base = XR_RBP;
    if(offset < 0) offset -= static_cast<long long>(
      function.callee_saved_regs.size() * 8);
  } else if(address.kind != MirOperand::OP_DEREF) {
    throw std::logic_error("folded native load address is not memory-shaped");
  }
  emit_load(out, load.operands[0].reg, base, offset,
            data_layout::type_width(load.type));
  return count;
}

}  // namespace address_folding
}  // namespace lowir_native
