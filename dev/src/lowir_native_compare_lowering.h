#pragma once

#include "lowir_native_mir.h"
#include "lowir_native_selection.h"

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

  // A select stages the true value in the RDX scratch and the false value
  // in the freshly allocated result, then tests the condition
  // and completes the choice with one conditional move.  Value staging runs
  // before the test so a value transiently living in RAX is never lost.
  void emit_select(const lowir_model::Instruction & instruction,
                   std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    X64Register result = XR_RSP;
    mir_model::MirOperand pressure_home;
    if(!lowerer.try_allocate_result(instruction.dest, out, &result)) {
      pressure_home = lowerer.allocate_temp_home(
        instruction.dest, instruction.type);
      result = XR_R10;
    }
    if(lowerer.is_frame_address(instruction.second))
      lowerer.emit_operand_address(out, XR_RDX, instruction.second);
    else
      lowerer.move_value_to_register(out, XR_RDX,
        lowerer.resolve(instruction.second), instruction.type);
    if(lowerer.is_frame_address(instruction.third))
      lowerer.emit_operand_address(out, result, instruction.third);
    else
      lowerer.move_value_to_register(out, result,
        lowerer.resolve(instruction.third), instruction.type);
    const lowir_model::LowType condition_type =
      lowerer.operand_type(instruction.first);
    lowerer.move_value_to_register(out, XR_RAX,
      lowerer.resolve(instruction.first), condition_type);
    mir_model::MirInstruction test = machine_instruction(
      mir_model::MirInstruction::MI_TEST, condition_type);
    append_operand(test, reg_operand(XR_RAX));
    append_operand(test, reg_operand(XR_RAX));
    out.push_back(test);
    mir_model::MirInstruction choose = machine_instruction(
      mir_model::MirInstruction::MI_CMOV, instruction.type);
    choose.condition = XC_NE;
    append_operand(choose, reg_operand(result));
    append_operand(choose, reg_operand(XR_RDX));
    out.push_back(choose);
    if(selection::is_narrow_integer(instruction.type))
      append_integer_normalization(out, instruction.type,
                                   reg_operand(result));
    lowerer.consume(instruction.first);
    lowerer.consume(instruction.second);
    lowerer.consume(instruction.third, result);
    if(pressure_home.kind == mir_model::MirOperand::OP_FRAME) {
      append_store(out, pressure_home, reg_operand(result),
                   instruction.type);
      lowerer.define(instruction.dest, instruction.type, pressure_home);
    } else {
      lowerer.define(instruction.dest, instruction.type,
                     reg_operand(result));
    }
  }
};

}  // namespace comparison_detail
}  // namespace lowir_native
