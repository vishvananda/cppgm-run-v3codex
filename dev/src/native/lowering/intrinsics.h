#pragma once

#include "native/errors.h"
#include "native/mir/construction.h"
#include "native/lowering/selection.h"
#include "native/frame/stack.h"
#include "native/lowering/varargs.h"
#include "native/lowering/values.h"

#include <vector>

namespace lowir_native {

template <class Derived>
class IntrinsicLowering
{
protected:
  bool TryEmitIntrinsic(const lowir_model::Instruction & instruction,
                        std::vector<mir_model::MirInstruction> & out)
  {
    if(instruction.kind == lowir_model::Instruction::IK_VA_START)
      EmitVaStart(instruction, out);
    else if(instruction.kind == lowir_model::Instruction::IK_VA_ARG)
      EmitVaArg(instruction, out);
    else if(instruction.kind == lowir_model::Instruction::IK_STACK_ALLOC)
      EmitStackAlloc(instruction, out);
    else return false;
    return true;
  }

  void EmitVaStart(const lowir_model::Instruction & instruction,
                   std::vector<mir_model::MirInstruction> & out)
  {
    Derived & derived = static_cast<Derived &>(*this);
    if(!derived.variadic_register_save_offset_)
      native_errors::ThrowInternal("va_start register-save area is unavailable");
    derived.emit_operand_address(out, XR_RCX, instruction.first);
    varargs::append_va_start(derived.variadic_state_,
                             derived.variadic_register_save_offset_, out);
    derived.consume(instruction.first);
  }

  void EmitStackAlloc(const lowir_model::Instruction & instruction,
                      std::vector<mir_model::MirInstruction> & out)
  {
    Derived & derived = static_cast<Derived &>(*this);
    derived.move_value_to_register(out, XR_RAX, derived.resolve(instruction.first),
                                   derived.operand_type(instruction.first));
    stack::append_dynamic_allocation(out);
    derived.consume(instruction.first);
    const mir_model::MirOperand destination = build::reg_operand(
      derived.allocate_result(instruction.dest, out));
    build::append_move(out, destination, build::reg_operand(XR_RSP));
    derived.define(instruction.dest,
      lowir_model::builtin_lowir_type(lowir_model::LTK_PTR), destination);
  }

  void EmitVaArg(const lowir_model::Instruction & instruction,
                 std::vector<mir_model::MirInstruction> & out)
  {
    Derived & derived = static_cast<Derived &>(*this);
    const bool floating = instruction.type.kind == lowir_model::LTK_F64;
    if(!floating && !selection::is_integer_or_pointer(instruction.type))
      native_errors::ThrowSource("va_arg scalar class is not implemented");
    derived.emit_operand_address(out, XR_RCX, instruction.first);
    varargs::append_va_arg_address(floating, out);
    derived.consume(instruction.first);
    if(floating) {
      const mir_model::MirOperand destination =
        derived.allocate_float_result(instruction.dest, instruction.type);
      build::append_float_move(out, destination, build::dereference(XR_R8),
        lowir_model::builtin_lowir_type(lowir_model::LTK_F64));
      derived.define(instruction.dest, instruction.type, destination);
      return;
    }
    const mir_model::MirOperand destination = build::reg_operand(
      derived.allocate_result(instruction.dest, out));
    const lowir_model::LowType & scalar_type =
      lowir_model::builtin_lowir_type(instruction.type.kind);
    build::append_load(out, destination, build::dereference(XR_R8),
                       scalar_type);
    build::append_integer_normalization(out, instruction.type, destination);
    derived.define(instruction.dest, instruction.type, destination);
  }
};

}  // namespace lowir_native
