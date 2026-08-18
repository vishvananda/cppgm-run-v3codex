#pragma once

#include "lowir_native_analysis.h"
#include "lowir_native_mir.h"
#include "lowir_native_value.h"

#include <algorithm>
#include <cstddef>

namespace lowir_native {
namespace parameter_detail {

template <class Derived>
class ParameterRegisterState
{
protected:
  ParameterRegisterState()
  {
    std::fill(first_selected_definition_, first_selected_definition_ + 16,
              analysis::FunctionFacts::missing_position());
  }

  bool incoming_register_is_intact(lowir_model::ValueId value,
                                   X64Register reg) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return incoming_register_is_available(reg) &&
      !lowerer.crosses_register_clobber(value, reg);
  }

  bool incoming_register_is_available(X64Register reg) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return !analysis::register_was_clobbered_before(
        lowerer.facts_, reg, lowerer.position_) &&
      !parameter_setup_clobbers_[static_cast<std::size_t>(reg)] &&
      first_selected_definition_[static_cast<std::size_t>(reg)] >=
        lowerer.position_;
  }

  void record_parameter_setup_clobbers()
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    for(std::size_t i = 0; i < lowerer.parameter_moves_.size(); ++i) {
      const mir_model::MirInstruction & instruction =
        lowerer.parameter_moves_[i];
      if(instruction.operands.empty() ||
         instruction.operands[0].kind != mir_model::MirOperand::OP_REG)
        continue;
      const mir_model::MirOperand & destination = instruction.operands[0];
      if(instruction.opcode == mir_model::MirInstruction::MI_MOV &&
         instruction.operands.size() > 1 &&
         instruction.operands[1].kind == mir_model::MirOperand::OP_REG &&
         instruction.operands[1].reg == destination.reg)
        continue;
      parameter_setup_clobbers_[static_cast<std::size_t>(destination.reg)] = 1;
    }
  }

  void remember_selected_register_definition(
      const mir_model::MirOperand & location, std::size_t position)
  {
    if(location.kind != mir_model::MirOperand::OP_REG) return;
    std::size_t & first = first_selected_definition_[
      static_cast<std::size_t>(location.reg)];
    if(first == analysis::FunctionFacts::missing_position()) first = position;
  }

  void reserve_direct_parameter_register(
      const mir_model::MirParamBinding & binding,
      const ValueFact & value, std::size_t uses)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(!uses || binding.location != mir_model::MirParamBinding::PL_REG ||
       value.location.kind != mir_model::MirOperand::OP_REG ||
       value.location.reg != binding.reg ||
       !lowerer.managed_register(binding.reg) ||
       lowerer.registers_.is_used(binding.reg)) return;
    lowerer.registers_.reserve(binding.reg);
  }

  unsigned active_setup_register_clobbers_ = 0;

private:
  unsigned char parameter_setup_clobbers_[16] = {};
  std::size_t first_selected_definition_[16];
};

}  // namespace parameter_detail
}  // namespace lowir_native
