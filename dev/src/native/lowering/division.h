#pragma once

#include "native/mir/construction.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace lowir_native {
namespace division_detail {

struct Classification
{
  Classification(bool valid_value = false, bool unsigned_value = false,
                 bool remainder_value = false)
    : valid(valid_value), unsigned_operation(unsigned_value),
      remainder(remainder_value)
  {}
  bool valid;
  bool unsigned_operation;
  bool remainder;
};

inline Classification classify(lowir_model::LowOperation operation)
{
  switch(operation.kind) {
  case lowir_model::LowOperation::LOP_DIV:
    return Classification(true, false, false);
  case lowir_model::LowOperation::LOP_UDIV:
    return Classification(true, true, false);
  case lowir_model::LowOperation::LOP_MOD:
    return Classification(true, false, true);
  case lowir_model::LowOperation::LOP_UMOD:
    return Classification(true, true, true);
  default:
    return Classification();
  }
}

inline bool direct_return_setup_is_safe(
    const mir_model::MirOperand & dividend,
    const mir_model::MirOperand & divisor)
{
  using namespace build;
  // The direct setup uses MI_MOV rather than a typed memory load.  Keep a
  // frame- or memory-resident dividend on the ordinary materialization path.
  if(dividend.kind != mir_model::MirOperand::OP_REG &&
     dividend.kind != mir_model::MirOperand::OP_IMM)
    return false;
  const bool materializes_in_rdx =
    divisor.kind == mir_model::MirOperand::OP_FRAME ||
    divisor.kind == mir_model::MirOperand::OP_GLOBAL ||
    divisor.kind == mir_model::MirOperand::OP_DEREF ||
    (divisor.kind == mir_model::MirOperand::OP_IMM &&
     (divisor.imm < std::numeric_limits<std::int32_t>::min() ||
      divisor.imm > std::numeric_limits<std::int32_t>::max()));
  const mir_model::MirOperand setup_divisor = materializes_in_rdx ?
    reg_operand(XR_RDX) : divisor;
  return !(materializes_in_rdx && operand_uses_register(dividend, XR_RDX)) &&
    !(operand_uses_register(dividend, XR_RCX) &&
      operand_uses_register(setup_divisor, XR_RAX));
}

inline void emit_division(
    const Classification & classification,
    const mir_model::MirOperand & destination,
    const mir_model::MirOperand & dividend,
    const mir_model::MirOperand & divisor,
    std::vector<mir_model::MirInstruction> & out)
{
  using namespace build;
  const bool dividend_uses_rcx = operand_uses_register(dividend, XR_RCX);
  const bool divisor_uses_rax = operand_uses_register(divisor, XR_RAX);
  if(dividend_uses_rcx && divisor_uses_rax) {
    append_move(out, reg_operand(XR_RDX), divisor);
    append_move(out, reg_operand(XR_RAX), dividend);
    append_move(out, reg_operand(XR_RCX), reg_operand(XR_RDX));
  } else if(dividend_uses_rcx) {
    append_move(out, reg_operand(XR_RAX), dividend);
    append_move(out, reg_operand(XR_RCX), divisor);
  } else {
    append_move(out, reg_operand(XR_RCX), divisor);
    append_move(out, reg_operand(XR_RAX), dividend);
  }
  if(classification.unsigned_operation)
    append_move(out, reg_operand(XR_RDX), immediate(0));
  else out.push_back(machine_instruction(mir_model::MirInstruction::MI_CQO));
  mir_model::MirInstruction divide = machine_instruction(
    classification.unsigned_operation ?
    mir_model::MirInstruction::MI_DIV : mir_model::MirInstruction::MI_IDIV);
  append_operand(divide, reg_operand(XR_RCX));
  out.push_back(divide);
  append_move(out, destination,
    reg_operand(classification.remainder ? XR_RDX : XR_RAX));
}

}  // namespace division_detail
}  // namespace lowir_native
