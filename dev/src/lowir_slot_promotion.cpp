#include "lowir_slot_promotion.h"

namespace lowir_opt {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;

bool slot_is_phi_scalar_type(const LowType & type)
{
  return type.kind == lowir_model::LTK_I1 ||
    type.kind == lowir_model::LTK_I8 ||
    type.kind == lowir_model::LTK_U8 ||
    type.kind == lowir_model::LTK_I16 ||
    type.kind == lowir_model::LTK_U16 ||
    type.kind == lowir_model::LTK_I32 ||
    type.kind == lowir_model::LTK_U32 ||
    type.kind == lowir_model::LTK_I64 ||
    type.kind == lowir_model::LTK_F32 ||
    type.kind == lowir_model::LTK_F64 ||
    type.kind == lowir_model::LTK_PTR;
}

std::vector<unsigned char> find_promotable_slots(
    const Function & function, std::size_t * count)
{
  std::vector<unsigned char> eligible(function.slot_names.size(), 0);
  *count = 0;
  for(std::size_t i = 0; i < function.slots.size(); ++i) {
    const lowir_model::SlotId slot = function.slots[i];
    if(lowir_model::lowir_slot_type(function, slot).kind !=
       lowir_model::LTK_OBJECT) {
      eligible[slot] = 1;
      ++*count;
    }
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function.blocks[i].instructions[j];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           (ins.volatile_access ||
            !((ins.kind == Instruction::IK_LOAD && k == 0) ||
              (ins.kind == Instruction::IK_STORE && k == 1))) &&
           eligible[values[k]->slot]) {
          eligible[values[k]->slot] = 0;
          --*count;
        }
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT && eligible[ins.args[k].slot]) {
          eligible[ins.args[k].slot] = 0;
          --*count;
        }
    }
  return eligible;
}

}  // namespace lowir_opt
