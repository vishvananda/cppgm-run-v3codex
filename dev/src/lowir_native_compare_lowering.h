#pragma once

#include "lowir_native_mir.h"

#include <climits>
#include <vector>

namespace lowir_native {
namespace comparison_detail {

template <class Derived>
class CompareLowering
{
protected:
  void select_direct_return_compare_operands(
      const lowir_model::Instruction & instruction,
      mir_model::MirOperand & left,
      mir_model::MirOperand & right,
      std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    left = lowerer.direct_compare_left(instruction.first, out);
    right = lowerer.resolve(instruction.second);
    if(right.kind == mir_model::MirOperand::OP_IMM &&
       lowir_model::lowir_type_bit_width(instruction.type) == 64 &&
       (right.imm < INT32_MIN || right.imm > INT32_MAX) &&
       (left.kind != mir_model::MirOperand::OP_REG || left.reg != XR_RAX)) {
      lowerer.move_value_to_register(out, XR_RAX, left, instruction.type);
      left = reg_operand(XR_RAX);
    }
    right = direct_compare_right(
      instruction.second, instruction.type, left, out);
    const bool left_memory = left.kind == mir_model::MirOperand::OP_FRAME ||
      left.kind == mir_model::MirOperand::OP_GLOBAL ||
      left.kind == mir_model::MirOperand::OP_DEREF;
    const bool right_memory = right.kind == mir_model::MirOperand::OP_FRAME ||
      right.kind == mir_model::MirOperand::OP_GLOBAL ||
      right.kind == mir_model::MirOperand::OP_DEREF;
    if(left_memory && right_memory) {
      lowerer.move_value_to_register(out, XR_RDX, right, instruction.type);
      right = reg_operand(XR_RDX);
    }
  }

  mir_model::MirOperand direct_compare_right(
      const lowir_model::Operand & operand,
      const lowir_model::LowType & type,
      const mir_model::MirOperand & left,
      std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    const mir_model::MirOperand source = lowerer.resolve(operand);
    const std::size_t width = lowir_model::lowir_type_bit_width(type);
    if(source.kind == mir_model::MirOperand::OP_IMM && width >= 32 &&
       (width < 64 ||
        (source.imm >= INT32_MIN && source.imm <= INT32_MAX)))
      return source;
    if(source.kind == mir_model::MirOperand::OP_IMM && width < 32 &&
       operand_uses_register(left, XR_RDX))
      return source;
    if(source.kind == mir_model::MirOperand::OP_REG ||
       source.kind == mir_model::MirOperand::OP_FRAME ||
       source.kind == mir_model::MirOperand::OP_GLOBAL ||
       source.kind == mir_model::MirOperand::OP_DEREF)
      return source;
    lowerer.move_value_to_register(
      out, XR_RDX, source, lowerer.operand_type(operand));
    return reg_operand(XR_RDX);
  }
};

}  // namespace comparison_detail
}  // namespace lowir_native
