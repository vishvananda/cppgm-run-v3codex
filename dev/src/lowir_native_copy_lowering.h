#pragma once

#include "lowir_native_mir.h"
#include "lowir_native_selection.h"
#include "lowir_native_value.h"
#include "lowir_native_wide.h"

#include <vector>

namespace lowir_native {
namespace copy_detail {

template <class Derived>
class CopyLowering
{
protected:
  void emit_copy(const lowir_model::Instruction & instruction,
                 std::vector<mir_model::MirInstruction> & out,
                 bool direct_return = false)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    if(instruction.type.kind == lowir_model::LTK_OBJECT) {
      lowerer.emit_object_value(instruction.dest, instruction.type,
                                instruction.first, out);
      return;
    }
    if(wide::is_integer(instruction.type)) {
      const mir_model::MirOperand destination =
        lowerer.allocate_temp_home(instruction.dest, instruction.type);
      wide::append_copy(destination, lowerer.wide_value(instruction.first), out);
      lowerer.consume(instruction.first);
      lowerer.define(instruction.dest, instruction.type, destination);
      return;
    }
    if(selection::is_floating(instruction.type)) {
      lowerer.emit_float_copy(instruction, out);
      return;
    }
    const mir_model::MirOperand source = lowerer.resolve(instruction.first);
    if(instruction.first.kind == lowir_model::Operand::OP_TEMP) {
      const ValueFact & original =
        lowerer.values_.find(instruction.first.text)->second;
      const bool stable = source.kind == mir_model::MirOperand::OP_IMM ||
        source.kind == mir_model::MirOperand::OP_SYMBOL ||
        (source.kind == mir_model::MirOperand::OP_FRAME &&
         (original.frame_address ||
          (original.has_spill_home &&
           original.spill_home.offset == source.offset &&
           original.spill_home.frame_binding == source.frame_binding))) ||
        (source.kind == mir_model::MirOperand::OP_REG &&
         !lowerer.crosses_register_clobber(instruction.dest, source.reg));
      if(!original.parameter && stable &&
         lowir_model::same_lowir_type(original.type, instruction.type)) {
        ValueFact alias = original;
        alias.parameter = false;
        alias.fixed_register_home = false;
        lowerer.set_value(instruction.dest, alias);
        lowerer.consume(instruction.first);
        return;
      }
    }
    if(source.kind == mir_model::MirOperand::OP_IMM &&
       selection::is_integer_or_pointer(instruction.type)) {
      lowerer.consume(instruction.first);
      lowerer.define(instruction.dest, instruction.type, source);
      return;
    }
    mir_model::MirOperand destination;
    mir_model::MirOperand pressure_home;
    const bool safe_reuse = lowerer.can_reuse(instruction.first) &&
      !lowerer.crosses_register_clobber(instruction.dest, source.reg);
    if(direct_return && source.kind != mir_model::MirOperand::OP_REG) {
      destination = reg_operand(XR_RAX);
      lowerer.move_value_to_register(
        out, destination.reg, source, instruction.type);
    } else if(safe_reuse) {
      destination = source;
    } else {
      X64Register result = XR_RSP;
      if(lowerer.try_allocate_result(instruction.dest, out, &result))
        destination = reg_operand(result);
      else {
        pressure_home =
          lowerer.allocate_temp_home(instruction.dest, instruction.type);
        destination = reg_operand(XR_RAX);
      }
      lowerer.move_value_to_register(
        out, destination.reg, source, instruction.type);
    }
    if(selection::is_integer_or_pointer(instruction.type))
      append_integer_normalization(out, instruction.type, destination);
    lowerer.consume(instruction.first, destination.reg);
    if(pressure_home.kind == mir_model::MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, instruction.type);
    lowerer.define(instruction.dest, instruction.type,
      pressure_home.kind == mir_model::MirOperand::OP_FRAME ?
        pressure_home : destination);
  }
};

}  // namespace copy_detail
}  // namespace lowir_native
