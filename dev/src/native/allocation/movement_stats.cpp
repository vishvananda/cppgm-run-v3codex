#include "native/allocation/movement_stats.h"

#include <algorithm>

namespace lowir_native {
namespace movement_stats {
namespace {

using lowir_model::Instruction;
using lowir_model::Operand;
using mir_model::MirInstruction;
using mir_model::MirOperand;

bool is_memory_operand(const MirOperand & operand)
{
  return operand.kind == MirOperand::OP_FRAME ||
    operand.kind == MirOperand::OP_GLOBAL ||
    operand.kind == MirOperand::OP_DEREF;
}

bool has_source_slot(const Instruction & instruction)
{
  if(instruction.first.kind == Operand::OP_SLOT ||
     instruction.second.kind == Operand::OP_SLOT ||
     instruction.third.kind == Operand::OP_SLOT) return true;
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    if(instruction.args[i].kind == Operand::OP_SLOT) return true;
  return false;
}

}  // namespace

NativeMovementReason classify(const Instruction & instruction)
{
  if(instruction.kind == Instruction::IK_CALL)
    return NMR_CALL_BOUNDARY;
  if((instruction.kind >= Instruction::IK_EH_TRY &&
      instruction.kind <= Instruction::IK_RESUME) ||
     instruction.kind == Instruction::IK_THROW)
    return NMR_CLEANUP_EH;
  if(instruction.kind == Instruction::IK_CONVERT)
    return NMR_WIDTH_NORMALIZATION;
  if(instruction.kind == Instruction::IK_ADDR ||
     instruction.kind == Instruction::IK_INDEX ||
     instruction.kind == Instruction::IK_STACK_ALLOC)
    return NMR_ADDRESS_MATERIALIZATION;
  if(instruction.kind == Instruction::IK_COPYOBJ ||
     instruction.kind == Instruction::IK_ZEROINIT ||
     instruction.type.kind == lowir_model::LTK_OBJECT ||
     instruction.source_type.kind == lowir_model::LTK_OBJECT)
    return NMR_OBJECT_TEMPORARY;
  if(has_source_slot(instruction)) return NMR_SOURCE_SLOT;
  return NMR_SCALAR_TEMPORARY;
}

void record(Stats * stats, NativeMovementReason reason,
            const std::vector<MirInstruction> & instructions,
            std::size_t begin, std::size_t end)
{
  if(!stats) return;
  end = std::min(end, instructions.size());
  for(std::size_t i = begin; i < end; ++i) {
    const MirInstruction & instruction = instructions[i];
    bool movement = true;
    switch(instruction.opcode) {
    case MirInstruction::MI_LOAD:
      ++stats->movement_loads_by_reason[reason];
      break;
    case MirInstruction::MI_STORE:
      ++stats->movement_stores_by_reason[reason];
      break;
    case MirInstruction::MI_MOV:
      if(instruction.operands.size() == 2) {
        const MirOperand & destination = instruction.operands[0];
        const MirOperand & source = instruction.operands[1];
        if(is_memory_operand(destination))
          ++stats->movement_stores_by_reason[reason];
        else if(is_memory_operand(source))
          ++stats->movement_loads_by_reason[reason];
        else if(destination.kind == MirOperand::OP_REG &&
                source.kind == MirOperand::OP_REG)
          ++stats->movement_register_copies_by_reason[reason];
      }
      break;
    case MirInstruction::MI_LEA:
      ++stats->movement_addresses_by_reason[reason];
      break;
    case MirInstruction::MI_MOVZX:
    case MirInstruction::MI_ZEXT:
    case MirInstruction::MI_SEXT:
      ++stats->movement_normalizations_by_reason[reason];
      break;
    default:
      movement = false;
      break;
    }
    if(movement) ++stats->movement_instructions_by_reason[reason];
  }
}

}  // namespace movement_stats
}  // namespace lowir_native
