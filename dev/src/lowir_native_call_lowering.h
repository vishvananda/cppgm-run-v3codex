#pragma once

#include "lowir_native_abi.h"
#include "lowir_native_mir.h"
#include "lowir_native_value.h"
#include "lowir_native_wide.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace call_detail {

template <class Derived>
class CallLowering
{
protected:
  bool scalar_result_can_remain_in_return_register(
      lowir_model::ValueId result) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    if(lowerer.source_.blocks.size() != 1 ||
       lowerer.crosses_register_clobber(result, XR_RAX)) return false;
    const std::size_t last_use = lowerer.facts_.last_use[result];
    if(last_use == analysis::FunctionFacts::missing_position() ||
       last_use >= lowerer.source_.blocks[0].instructions.size()) return false;
    const lowir_model::Instruction & consumer =
      lowerer.source_.blocks[0].instructions[last_use];
    // Scalar-store lowering may materialize the stored value in rax before it
    // consumes the address.  Retaining an address result in rax would then
    // replace the address with the value being stored.
    return consumer.kind != lowir_model::Instruction::IK_STORE ||
      consumer.second.kind != lowir_model::Operand::OP_TEMP ||
      consumer.second.value != result;
  }

  bool stabilize_extended_register_sources(
      const lowir_model::Instruction & instruction,
      const std::vector<lowir_model::LowirParameter> & parameters,
      const abi::Plan & plan,
      std::vector<mir_model::MirInstruction> & out)
  {
    bool copies_stack_object = false;
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const abi::Piece & piece = plan.pieces[i];
      if(piece.location == abi::PL_STACK && piece.chunk_offset == 0 &&
         parameters[piece.parameter_index].type.kind == lowir_model::LTK_OBJECT) {
        copies_stack_object = true;
        break;
      }
    }
    if(!copies_stack_object) return false;

    Derived & lowerer = static_cast<Derived &>(*this);
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const abi::Piece & piece = plan.pieces[i];
      const std::size_t parameter = piece.parameter_index;
      if(piece.location != abi::PL_GPR ||
         parameters[parameter].type.kind == lowir_model::LTK_OBJECT ||
         wide::is_integer(parameters[parameter].type))
        continue;
      const lowir_model::Operand & argument = instruction.args[parameter];
      if(argument.kind != lowir_model::Operand::OP_TEMP) continue;
      ValueFact & value = lowerer.values_[argument.value];
      if(!lowerer.value_known_[argument.value] ||
         value.location.kind != mir_model::MirOperand::OP_REG ||
         (value.location.reg != XR_RDI && value.location.reg != XR_RSI))
        continue;
      const mir_model::MirOperand location = value.location;
      const bool shared =
        lowerer.has_live_location_alias(argument.value, location);
      const mir_model::MirOperand home = value.has_spill_home ?
        value.spill_home :
        lowerer.allocate_temp_home(argument.value, value.type);
      build::append_store(out, home, location, value.type);
      lowerer.set_value_location(argument.value, home);
      if(!shared && lowerer.registers_.is_used(location.reg))
        lowerer.registers_.release(location.reg);
    }
    return true;
  }
};

}  // namespace call_detail
}  // namespace lowir_native
