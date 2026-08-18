#pragma once

#include "lowir_native_abi.h"
#include "lowir_native_mir.h"
#include "lowir_native_value.h"
#include "lowir_native_wide.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native {
namespace call_detail {

template <class Derived>
class CallLowering
{
protected:
  void stabilize_extended_register_sources(
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
    if(!copies_stack_object) return;

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
      typename std::unordered_map<std::string, ValueFact>::iterator value =
        lowerer.values_.find(argument.text);
      if(value == lowerer.values_.end() ||
         value->second.location.kind != mir_model::MirOperand::OP_REG ||
         (value->second.location.reg != XR_RDI &&
          value->second.location.reg != XR_RSI))
        continue;
      const mir_model::MirOperand location = value->second.location;
      const bool shared = lowerer.has_live_location_alias(argument.text, location);
      const mir_model::MirOperand home = value->second.has_spill_home ?
        value->second.spill_home :
        lowerer.allocate_temp_home(argument.text, value->second.type);
      build::append_store(out, home, location, value->second.type.text);
      lowerer.set_value_location(argument.text, home);
      if(!shared && lowerer.registers_.is_used(location.reg))
        lowerer.registers_.release(location.reg);
    }
  }
};

}  // namespace call_detail
}  // namespace lowir_native
