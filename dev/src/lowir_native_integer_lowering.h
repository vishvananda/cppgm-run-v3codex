#pragma once

#include "lowir_native_division_lowering.h"
#include "lowir_native_mir.h"
#include "lowir_native_selection.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace lowir_native {
namespace integer_detail {

inline bool memory_operand(const mir_model::MirOperand & operand)
{
  return operand.kind == mir_model::MirOperand::OP_FRAME ||
    operand.kind == mir_model::MirOperand::OP_GLOBAL ||
    operand.kind == mir_model::MirOperand::OP_DEREF;
}

inline bool memory_rhs_operation(const lowir_model::LowOperation & operation,
                                 const lowir_model::LowType & type)
{
  using lowir_model::LowOperation;
  if(operation.kind == LowOperation::LOP_ADD ||
     operation.kind == LowOperation::LOP_SUB ||
     operation.kind == LowOperation::LOP_AND ||
     operation.kind == LowOperation::LOP_OR ||
     operation.kind == LowOperation::LOP_XOR)
    return true;
  return operation.kind == LowOperation::LOP_MUL &&
    lowir_model::lowir_type_bit_width(type) != 8;
}

template <class Derived>
class IntegerLowering
{
protected:
  void emit_shift(const lowir_model::Instruction & instruction,
                  const mir_model::MirOperand & destination,
                  const mir_model::MirOperand & right,
                  std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    lowerer.move_value_to_register(
      out, XR_RCX, right, lowerer.operand_type(instruction.second));
    mir_model::MirInstruction::Opcode opcode =
      mir_model::MirInstruction::MI_SHL_CL;
    if(instruction.op.kind == lowir_model::LowOperation::LOP_SHR)
      opcode = mir_model::MirInstruction::MI_SAR_CL;
    else if(instruction.op.kind == lowir_model::LowOperation::LOP_USHR)
      opcode = mir_model::MirInstruction::MI_SHR_CL;
    mir_model::MirInstruction shift = machine_instruction(opcode);
    append_operand(shift, destination);
    out.push_back(shift);
  }

  void emit_binary(const lowir_model::Instruction & instruction,
                   const lowir_model::LowirBlock & block,
                   std::size_t instruction_index,
                   std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    using lowir_model::LowOperation;
    using mir_model::MirInstruction;
    using mir_model::MirOperand;
    Derived & lowerer = static_cast<Derived &>(*this);
    if(wide::is_integer(instruction.type)) {
      const MirOperand destination =
        lowerer.allocate_temp_home(instruction.dest, instruction.type);
      wide::append_binary(destination,
        lowerer.wide_value(instruction.first),
        lowerer.wide_value(instruction.second), instruction.op, out);
      lowerer.consume(instruction.first);
      lowerer.consume(instruction.second);
      lowerer.define(instruction.dest, instruction.type, destination);
      return;
    }
    if(selection::is_floating(instruction.type)) {
      lowerer.emit_float_binary(instruction, out);
      return;
    }
    if(!selection::is_integer_or_pointer(instruction.type))
      throw std::runtime_error(
        "integer selector received non-integer binary operation");

    const MirOperand left = lowerer.resolve(instruction.first);
    MirOperand right = lowerer.resolve(instruction.second);
    const division_detail::Classification division =
      division_detail::classify(instruction.op);
    bool direct_division_return = false;
    if(division.valid && lowerer.result_is_immediate_return(
         block, instruction_index, instruction.dest))
      direct_division_return =
        division_detail::direct_return_setup_is_safe(left, right);
    const bool pressure_leaf = lowerer.constrained_wide_pressure() &&
      instruction.first.kind == lowir_model::Operand::OP_TEMP &&
      lowerer.facts_.has(instruction.first.value,
                         analysis::FunctionFacts::VF_PARAMETER);

    const bool legal_memory_rhs = memory_operand(right) &&
      memory_rhs_operation(instruction.op, instruction.type) &&
      !division.valid;
    X64Register fixed_destination = XR_RSP;
    if(pressure_leaf)
      fixed_destination = instruction.first.value ==
        lowerer.source_.params.front().value ? XR_R15 : XR_RAX;
    const bool address_would_be_clobbered =
      fixed_destination != XR_RSP &&
      build::operand_uses_register(right, fixed_destination);
    if(memory_operand(right) &&
       (!legal_memory_rhs || address_would_be_clobbered)) {
      lowerer.move_value_to_register(
        out, XR_RDX, right, lowerer.operand_type(instruction.second));
      right = reg_operand(XR_RDX);
      append_integer_normalization(
        out, lowerer.operand_type(instruction.second), right);
    }

    MirOperand pressure_home;
    MirOperand destination;
    if(direct_division_return)
      destination = reg_operand(division.remainder ? XR_RDX : XR_RAX);
    else if(!pressure_leaf)
      destination = lowerer.binary_destination(
        instruction, left, out, true, &pressure_home);
    else if(instruction.first.value == lowerer.source_.params.front().value) {
      destination = reg_operand(XR_R15);
      if(!lowerer.registers_.is_used(XR_R15))
        lowerer.registers_.reserve(XR_R15);
      lowerer.move_value_to_register(
        out, XR_R15, left, lowerer.operand_type(instruction.first));
    } else {
      pressure_home =
        lowerer.allocate_temp_home(instruction.dest, instruction.type);
      destination = reg_operand(XR_RAX);
      lowerer.move_value_to_register(
        out, XR_RAX, left, lowerer.operand_type(instruction.first));
    }

    const bool shift = instruction.op.kind == LowOperation::LOP_SHL ||
      instruction.op.kind == LowOperation::LOP_SHR ||
      instruction.op.kind == LowOperation::LOP_USHR;
    if(shift && memory_operand(right)) {
      lowerer.move_value_to_register(
        out, XR_RDX, right, lowerer.operand_type(instruction.second));
      right = reg_operand(XR_RDX);
      append_integer_normalization(
        out, lowerer.operand_type(instruction.second), right);
    }

    MirInstruction::Opcode opcode = MirInstruction::MI_ADD;
    if(instruction.op.kind == LowOperation::LOP_SUB)
      opcode = MirInstruction::MI_SUB;
    else if(instruction.op.kind == LowOperation::LOP_MUL)
      opcode = MirInstruction::MI_IMUL;
    else if(instruction.op.kind == LowOperation::LOP_AND)
      opcode = MirInstruction::MI_AND;
    else if(instruction.op.kind == LowOperation::LOP_OR)
      opcode = MirInstruction::MI_OR;
    else if(instruction.op.kind == LowOperation::LOP_XOR)
      opcode = MirInstruction::MI_XOR;
    else if(division.valid) {
      division_detail::emit_division(division, destination,
        direct_division_return ? left : destination, right, out);
      append_integer_normalization(out, instruction.type, destination);
      lowerer.consume(instruction.first, destination.reg);
      lowerer.consume(instruction.second, destination.reg);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      lowerer.define(instruction.dest, instruction.type,
        pressure_home.kind == MirOperand::OP_FRAME ? pressure_home :
        destination);
      return;
    } else if(shift) {
      emit_shift(instruction, destination, right, out);
      append_integer_normalization(out, instruction.type, destination);
      lowerer.consume(instruction.first, destination.reg);
      lowerer.consume(instruction.second, destination.reg);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      lowerer.define(instruction.dest, instruction.type,
        pressure_home.kind == MirOperand::OP_FRAME ? pressure_home :
        destination);
      return;
    } else if(instruction.op.kind != LowOperation::LOP_ADD) {
      throw std::runtime_error(
        std::string("integer binary operation is not implemented: ") +
        lowir_model::lowir_operation_text(instruction.op));
    }

    MirInstruction operation = machine_instruction(opcode, instruction.type);
    append_operand(operation, destination);
    append_operand(operation, right);
    out.push_back(operation);
    if(memory_operand(right) && lowerer.stats_)
      ++lowerer.stats_->memory_rhs_operations_selected;
    append_integer_normalization(out, instruction.type, destination);
    lowerer.consume(instruction.first, destination.reg);
    lowerer.consume(instruction.second, destination.reg);
    if(pressure_home.kind == MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, instruction.type);
    lowerer.define(instruction.dest, instruction.type,
      pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
  }
};

}  // namespace integer_detail
}  // namespace lowir_native
