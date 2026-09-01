#pragma once

#include "native/errors.h"
#include "native/lowering/division.h"
#include "native/mir/construction.h"
#include "native/lowering/selection.h"

#include <cstddef>
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
    mir_model::MirInstruction::Opcode opcode =
      mir_model::MirInstruction::MI_SHL_CL;
    if(instruction.op.kind == lowir_model::LowOperation::LOP_SHR)
      opcode = mir_model::MirInstruction::MI_SAR_CL;
    else if(instruction.op.kind == lowir_model::LowOperation::LOP_USHR)
      opcode = mir_model::MirInstruction::MI_SHR_CL;
    if(right.kind == mir_model::MirOperand::OP_IMM) {
      if(opcode == mir_model::MirInstruction::MI_SHL_CL)
        opcode = mir_model::MirInstruction::MI_SHL_IMM;
      else if(opcode == mir_model::MirInstruction::MI_SHR_CL)
        opcode = mir_model::MirInstruction::MI_SHR_IMM;
      else
        opcode = mir_model::MirInstruction::MI_SAR_IMM;
      mir_model::MirInstruction shift = machine_instruction(opcode);
      append_operand(shift, destination);
      append_operand(shift, right);
      out.push_back(shift);
      return;
    }
    lowerer.move_value_to_register(
      out, XR_RCX, right, lowerer.operand_type(instruction.second));
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
      native_errors::ThrowLowirInput(
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
    const std::size_t destination_start = out.size();
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
      lowerer.finalize_integer_result(instruction.dest, instruction.type,
        destination, pressure_home, out);
      return;
    } else if(shift) {
      emit_shift(instruction, destination, right, out);
      append_integer_normalization(out, instruction.type, destination);
      lowerer.consume(instruction.first, destination.reg);
      lowerer.consume(instruction.second, destination.reg);
      lowerer.finalize_integer_result(instruction.dest, instruction.type,
        destination, pressure_home, out);
      return;
    } else if(instruction.op.kind != LowOperation::LOP_ADD) {
      native_errors::ThrowSource(
		std::string("integer binary operation is not implemented: ") +
        lowir_model::lowir_operation_text(instruction.op));
    }

    const bool zero_extending_and =
      instruction.op.kind == LowOperation::LOP_AND &&
      instruction.type.kind == lowir_model::LTK_I64 &&
      ((left.kind == MirOperand::OP_IMM && left.imm >= 0 &&
        left.imm <= 0xffffffffLL) ||
       (right.kind == MirOperand::OP_IMM && right.imm >= 0 &&
        right.imm <= 0xffffffffLL));
    const lowir_model::LowType & operation_type = zero_extending_and ?
      machine_type(lowir_model::LTK_I32) : instruction.type;

    const bool three_operand_add = lowerer.optimization_level_ >= 1 &&
      instruction.op.kind == LowOperation::LOP_ADD &&
      (instruction.type.kind == lowir_model::LTK_I64 ||
       instruction.type.kind == lowir_model::LTK_PTR) &&
      left.kind == MirOperand::OP_REG && destination.kind == MirOperand::OP_REG &&
      ((right.kind == MirOperand::OP_REG && right.reg != XR_RSP) ||
       (right.kind == MirOperand::OP_IMM &&
        right.imm >= INT32_MIN && right.imm <= INT32_MAX)) &&
      out.size() > destination_start &&
      out.back().opcode == MirInstruction::MI_MOV &&
      out.back().operands.size() == 2 &&
      out.back().operands[0].kind == MirOperand::OP_REG &&
      out.back().operands[0].reg == destination.reg &&
      out.back().operands[1].kind == MirOperand::OP_REG &&
      out.back().operands[1].reg == left.reg;
    if(three_operand_add) {
      const MirInstruction setup = out.back();
      out.pop_back();
      MirInstruction operation = machine_instruction(
        MirInstruction::MI_LEA, instruction.type);
      operation.debug_location = setup.debug_location;
      operation.has_source_position = setup.has_source_position;
      operation.source_position = setup.source_position;
      append_operand(operation, destination);
      append_operand(operation, right.kind == MirOperand::OP_REG ?
        indexed_dereference(left.reg, right.reg, 1) :
        dereference(left.reg, right.imm));
      out.push_back(operation);
      if(lowerer.stats_) ++lowerer.stats_->three_operand_adds_selected;
    } else {
      MirInstruction operation = machine_instruction(opcode, operation_type);
      append_operand(operation, destination);
      append_operand(operation, right);
      out.push_back(operation);
    }
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
