#include "native/encoding/address_folding.h"
#include "native/analysis/data_layout.h"
#include "native/errors.h"
#include "native/encoding/instructions.h"
#include "native/encoding/scalar_memory.h"

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
    if(operand.kind == MirOperand::OP_DEREF && operand.has_index &&
       operand.index == reg)
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

// Emit the scalar store shared by the dead-copy/address reducers after each
// reducer has proved its own sequence and liveness rules.  Unlike the generic
// address emitter, this path deliberately materializes even a local global in
// R11; a value already held in R11 therefore remains ineligible.
bool emit_folded_store(
    elf_detail::CodeBuffer & out, const MirOperand & address,
    X64Register source, const lowir_model::LowType & type,
    const mir_model::MirFunction & function)
{
  long long offset = address.offset;
  X64Register base = address.reg;
  if(address.kind == MirOperand::OP_FRAME) {
    base = XR_RBP;
    offset = actual_frame_offset(function, offset);
  } else if(address.kind == MirOperand::OP_GLOBAL) {
    if(source == XR_R11) return false;
    emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
    base = XR_R11;
    offset = 0;
  } else if(address.kind != MirOperand::OP_DEREF) {
    native_errors::ThrowInternal("folded native store address is not memory-shaped");
  }
  emit_store(out, base, offset, source, data_layout::type_width(type));
  return true;
}

std::size_t plan_dead_setup_load(
    const std::vector<MirInstruction> & instructions, std::size_t start,
    MirOperand * folded_address,
    const TransientScratchUsePlan & scratch_uses)
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
     load.operands[1].has_index ||
     load.operands[1].reg != setup.operands[0].reg)
    return 0;

  const X64Register setup_reg = setup.operands[0].reg;
  if(address && (address->operands.size() != 2 ||
     address->operands[0].kind != MirOperand::OP_REG ||
     address->operands[0].reg != setup_reg ||
     address->operands[1].kind != MirOperand::OP_DEREF ||
     address->operands[1].has_index ||
     address->operands[1].reg != setup_reg)) return 0;
  const bool self_copy = !address && setup.opcode == MirInstruction::MI_MOV &&
    setup.operands[1].reg == setup_reg;
  if(!self_copy && load.operands[0].reg != setup_reg &&
     !scratch_uses.dead_after(load_index + 1, setup_reg) &&
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

TransientScratchUsePlan::TransientScratchUsePlan(
    const std::vector<MirInstruction> & instructions)
  : r11_read_before_definition_(instructions.size() + 1, 0)
{
  bool read_before_definition = false;
  for(std::size_t i = instructions.size(); i != 0; --i) {
    const MirInstruction & instruction = instructions[i - 1];
    const bool simple_definition = simple_register_definition(instruction) &&
      instruction.operands[0].reg == XR_R11;
    bool reads_r11 = false;
    for(std::size_t operand = simple_definition ? 1 : 0;
        operand < instruction.operands.size(); ++operand)
      if(((instruction.operands[operand].kind == MirOperand::OP_REG ||
           instruction.operands[operand].kind == MirOperand::OP_DEREF) &&
          instruction.operands[operand].reg == XR_R11) ||
         (instruction.operands[operand].kind == MirOperand::OP_DEREF &&
          instruction.operands[operand].has_index &&
          instruction.operands[operand].index == XR_R11))
        reads_r11 = true;
    if(simple_definition && !reads_r11) read_before_definition = false;
    else if(reads_r11) read_before_definition = true;
    r11_read_before_definition_[i - 1] = read_before_definition ? 1 : 0;
  }
}

bool TransientScratchUsePlan::dead_after(
    std::size_t start, X64Register reg) const
{
  return reg == XR_R11 && start < r11_read_before_definition_.size() &&
    !r11_read_before_definition_[start];
}

std::size_t emit_dead_setup_load(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses)
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
    plan_dead_setup_load(instructions, start, &address, scratch_uses);
  if(!count) return 0;
  const MirInstruction & load = instructions[start + count - 1];
  if(address.kind != MirOperand::OP_FRAME &&
     address.kind != MirOperand::OP_DEREF) {
    native_errors::ThrowInternal("folded native load address is not memory-shaped");
  }
  emit_address_normalized_load(
    out, load.operands[0].reg, address, load.type, function);
  return count;
}

std::size_t emit_dead_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses)
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
     (store.operands[0].kind == MirOperand::OP_DEREF &&
      store.operands[0].has_index) ||
     store.operands[1].reg != copy.operands[0].reg) return 0;
  const X64Register copied = copy.operands[0].reg;
  const X64Register source = copy.operands[1].reg;
  const MirOperand * frame = store.operands[0].kind == MirOperand::OP_FRAME ?
    &store.operands[0] : 0;
  const bool transient_dead = !frame &&
    scratch_uses.dead_after(start + 2, copied);
  if(copied != source && !transient_dead &&
     !overwritten_without_read(instructions, start + 2, copied, frame))
    return 0;

  MirOperand address = store.operands[0];
  if(address.kind == MirOperand::OP_DEREF && address.reg == copied)
    address.reg = source;
  if(!emit_folded_store(out, address, source, store.type, function)) return 0;
  return 2;
}

std::size_t emit_dead_address_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses)
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
     setup.operands[1].has_index || store.operands[0].has_index ||
     store.operands[0].reg != setup.operands[0].reg ||
     store.operands[1].kind != MirOperand::OP_REG ||
     store.operands[1].reg == setup.operands[0].reg) return 0;
  const X64Register address_reg = setup.operands[0].reg;
  if(!scratch_uses.dead_after(start + 2, address_reg) &&
     !overwritten_without_read(instructions, start + 2, address_reg)) return 0;
  long long offset = 0;
  if(!add_offsets(setup.operands[1].offset, store.operands[0].offset,
                  &offset)) return 0;
  if(setup.operands[1].kind == MirOperand::OP_FRAME &&
     (setup.operands[1].offset < 0) != (offset < 0)) return 0;
  MirOperand address = setup.operands[1];
  address.offset = offset;
  if(!emit_folded_store(
       out, address, store.operands[1].reg, store.type, function)) return 0;
  return 2;
}

std::size_t emit_dead_address_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses)
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
     store.operands[0].has_index ||
     store.operands[0].reg != copy.operands[0].reg ||
     store.operands[1].kind != MirOperand::OP_REG) return 0;
  const X64Register copied = copy.operands[0].reg;
  const X64Register source = copy.operands[1].reg;
  if(copied != source && !scratch_uses.dead_after(start + 2, copied) &&
     !overwritten_without_read(instructions, start + 2, copied)) return 0;
  const X64Register value = store.operands[1].reg == copied ?
    source : store.operands[1].reg;
  emit_store(out, source, store.operands[0].offset, value,
             data_layout::type_width(store.type));
  return 2;
}

std::size_t emit_dead_copy_address_store(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const TransientScratchUsePlan & scratch_uses)
{
  if(start > instructions.size() || instructions.size() - start < 3)
    return 0;
  const MirInstruction & copy = instructions[start];
  const MirInstruction & setup = instructions[start + 1];
  const MirInstruction & store = instructions[start + 2];
  if(copy.opcode != MirInstruction::MI_MOV || copy.operands.size() != 2 ||
     copy.operands[0].kind != MirOperand::OP_REG ||
     copy.operands[1].kind != MirOperand::OP_REG ||
     setup.opcode != MirInstruction::MI_LEA || setup.operands.size() != 2 ||
     setup.operands[0].kind != MirOperand::OP_REG ||
     setup.operands[0].reg != copy.operands[0].reg ||
     setup.operands[1].kind != MirOperand::OP_DEREF ||
     setup.operands[1].has_index ||
     setup.operands[1].reg != copy.operands[0].reg ||
     store.opcode != MirInstruction::MI_STORE || store.operands.size() != 2 ||
     store.operands[0].kind != MirOperand::OP_DEREF ||
     store.operands[0].has_index ||
     store.operands[0].reg != copy.operands[0].reg ||
     store.operands[1].kind != MirOperand::OP_REG ||
     store.operands[1].reg == copy.operands[0].reg) return 0;
  const X64Register address_reg = copy.operands[0].reg;
  if(!scratch_uses.dead_after(start + 3, address_reg) &&
     !overwritten_without_read(instructions, start + 3, address_reg)) return 0;
  long long offset = 0;
  if(!add_offsets(setup.operands[1].offset, store.operands[0].offset,
                  &offset)) return 0;
  emit_store(out, copy.operands[1].reg, offset, store.operands[1].reg,
             data_layout::type_width(store.type));
  return 3;
}

std::size_t emit_memory_fold(
    elf_detail::CodeBuffer & out,
    const std::vector<MirInstruction> & instructions, std::size_t start,
    const mir_model::MirFunction & function, MemoryFoldKind kind,
    const TransientScratchUsePlan & scratch_uses)
{
  std::size_t folded = 0;
  if(kind == MFK_SETUP_LOAD)
    folded = emit_dead_setup_load(
      out, instructions, start, function, scratch_uses);
  if(kind == MFK_COPY_STORE) {
    folded = emit_dead_copy_store(
      out, instructions, start, function, scratch_uses);
    if(!folded)
      folded = emit_dead_address_copy_store(
        out, instructions, start, function, scratch_uses);
  }
  if(kind == MFK_ADDRESS_STORE)
    folded = emit_dead_address_store(
      out, instructions, start, function, scratch_uses);
  if(kind == MFK_COPY_ADDRESS_STORE)
    folded = emit_dead_copy_address_store(
      out, instructions, start, scratch_uses);
  return folded;
}

}  // namespace address_folding
}  // namespace lowir_native
