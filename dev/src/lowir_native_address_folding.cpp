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
    X64Register reg, const MirOperand * forwarded_frame = 0)
{
  const std::size_t limit = instructions.size() - start > 5 ?
    start + 5 : instructions.size();
  for(std::size_t i = start; i < limit; ++i) {
    const MirInstruction & instruction = instructions[i];
    if(forwarded_frame && instruction.opcode == MirInstruction::MI_LOAD &&
       instruction.operands.size() == 2 &&
       instruction.operands[1].kind == MirOperand::OP_FRAME &&
       instruction.operands[1].offset == forwarded_frame->offset) return false;
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

std::size_t emit_dead_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return 0;
  const MirInstruction & copy = instructions[start];
  const MirInstruction & store = instructions[start + 1];
  if(copy.opcode != MirInstruction::MI_MOV || copy.operands.size() != 2 ||
     copy.operands[0].kind != MirOperand::OP_REG ||
     copy.operands[1].kind != MirOperand::OP_REG ||
     store.opcode != MirInstruction::MI_STORE || store.operands.size() != 2 ||
     (store.operands[0].kind != MirOperand::OP_FRAME &&
      store.operands[0].kind != MirOperand::OP_GLOBAL &&
      store.operands[0].kind != MirOperand::OP_DEREF) ||
     store.operands[1].kind != MirOperand::OP_REG ||
     store.operands[1].reg != copy.operands[0].reg) return 0;
  const X64Register copied = copy.operands[0].reg;
  const X64Register source = copy.operands[1].reg;
  const MirOperand * frame = store.operands[0].kind == MirOperand::OP_FRAME ?
    &store.operands[0] : 0;
  if(copied != source &&
     !overwritten_without_read(instructions, start + 2, copied, frame))
    return 0;

  MirOperand address = store.operands[0];
  if(address.kind == MirOperand::OP_DEREF && address.reg == copied)
    address.reg = source;
  long long offset = address.offset;
  X64Register base = address.reg;
  if(address.kind == MirOperand::OP_FRAME) {
    base = XR_RBP;
    if(offset < 0) offset -= static_cast<long long>(
      function.callee_saved_regs.size() * 8);
  } else if(address.kind == MirOperand::OP_GLOBAL) {
    if(source == XR_R11) return 0;
    emit_symbol_move(out, XR_R11, address.text, address.address_binding);
    base = XR_R11;
    offset = 0;
  }
  emit_store(out, base, offset, source, data_layout::type_width(store.type));
  return 2;
}

std::size_t emit_dead_address_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return 0;
  const MirInstruction & setup = instructions[start];
  const MirInstruction & store = instructions[start + 1];
  if(setup.opcode != MirInstruction::MI_LEA || setup.operands.size() != 2 ||
     setup.operands[0].kind != MirOperand::OP_REG ||
     (setup.operands[1].kind != MirOperand::OP_FRAME &&
      setup.operands[1].kind != MirOperand::OP_DEREF) ||
     store.opcode != MirInstruction::MI_STORE || store.operands.size() != 2 ||
     store.operands[0].kind != MirOperand::OP_DEREF ||
     store.operands[0].reg != setup.operands[0].reg ||
     store.operands[1].kind != MirOperand::OP_REG ||
     store.operands[1].reg == setup.operands[0].reg) return 0;
  const X64Register address_reg = setup.operands[0].reg;
  if(!overwritten_without_read(instructions, start + 2, address_reg)) return 0;
  long long offset = 0;
  if(!add_offsets(setup.operands[1].offset, store.operands[0].offset,
                  &offset)) return 0;
  if(setup.operands[1].kind == MirOperand::OP_FRAME &&
     (setup.operands[1].offset < 0) != (offset < 0)) return 0;
  X64Register base = setup.operands[1].reg;
  if(setup.operands[1].kind == MirOperand::OP_FRAME) {
    base = XR_RBP;
    if(offset < 0) offset -= static_cast<long long>(
      function.callee_saved_regs.size() * 8);
  }
  emit_store(out, base, offset, store.operands[1].reg,
             data_layout::type_width(store.type));
  return 2;
}

std::size_t emit_dead_address_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return 0;
  const MirInstruction & copy = instructions[start];
  const MirInstruction & store = instructions[start + 1];
  if(copy.opcode != MirInstruction::MI_MOV || copy.operands.size() != 2 ||
     copy.operands[0].kind != MirOperand::OP_REG ||
     copy.operands[1].kind != MirOperand::OP_REG ||
     store.opcode != MirInstruction::MI_STORE || store.operands.size() != 2 ||
     store.operands[0].kind != MirOperand::OP_DEREF ||
     store.operands[0].reg != copy.operands[0].reg ||
     store.operands[1].kind != MirOperand::OP_REG) return 0;
  const X64Register copied = copy.operands[0].reg;
  const X64Register source = copy.operands[1].reg;
  if(copied != source &&
     !overwritten_without_read(instructions, start + 2, copied)) return 0;
  const X64Register value = store.operands[1].reg == copied ?
    source : store.operands[1].reg;
  emit_store(out, source, store.operands[0].offset, value,
             data_layout::type_width(store.type));
  return 2;
}

}  // namespace address_folding
}  // namespace lowir_native
