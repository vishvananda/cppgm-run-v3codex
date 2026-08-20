#pragma once

#include "lowir_native_analysis.h"
#include "lowir_native_mir.h"
#include "lowir_native_opt.h"
#include "lowir_native_value.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lowir_native {
namespace parameter_detail {

template <class Derived>
class ParameterRegisterState
{
protected:
  struct OptionalParameterMove
  {
    std::size_t index;
    X64Register destination;
    lowir_model::ValueId parameter;
    bool required;
    bool hard_read;
    bool soft_read;
    X64Register soft_incoming;
  };

  ParameterRegisterState()
  {
    std::fill(first_selected_definition_, first_selected_definition_ + 16,
              analysis::FunctionFacts::missing_position());
  }

  void append_optional_parameter_moves(
      const std::vector<mir_model::MirInstruction> & moves,
      const std::vector<lowir_model::ValueId> & parameters)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(moves.size() != parameters.size())
      throw std::logic_error("optional parameter move identity mismatch");
    const std::size_t first = lowerer.parameter_moves_.size();
    lowerer.parameter_moves_.insert(
      lowerer.parameter_moves_.end(), moves.begin(), moves.end());
    for(std::size_t i = first; i < lowerer.parameter_moves_.size(); ++i) {
      const mir_model::MirInstruction & move = lowerer.parameter_moves_[i];
      if(move.opcode != mir_model::MirInstruction::MI_MOV ||
         move.operands.empty() ||
         move.operands[0].kind != mir_model::MirOperand::OP_REG)
        throw std::logic_error("optional parameter setup is not a GPR move");
      OptionalParameterMove optional = {
        i, move.operands[0].reg, parameters[i - first], false, false, false,
        XR_RSP
      };
      optional_parameter_moves_.push_back(optional);
    }
  }

  void append_optional_parameter_move(
      std::vector<mir_model::MirInstruction> & moves,
      std::vector<lowir_model::ValueId> & parameters,
      ValueFact & value, lowir_model::ValueId parameter,
      const mir_model::MirOperand & source)
  {
    const std::size_t first = moves.size();
    build::append_move(moves, value.location, source);
    if(moves.size() == first) return;
    value.selected_parameter_home = parameter;
    parameters.push_back(parameter);
  }

  void remember_optional_parameter_move(std::size_t first,
                                        lowir_model::ValueId parameter)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(lowerer.parameter_moves_.size() == first) return;
    if(lowerer.parameter_moves_.size() != first + 1 ||
       lowerer.parameter_moves_.back().opcode !=
         mir_model::MirInstruction::MI_MOV ||
       lowerer.parameter_moves_.back().operands.empty() ||
       lowerer.parameter_moves_.back().operands[0].kind !=
         mir_model::MirOperand::OP_REG)
      throw std::logic_error("optional parameter setup is not one GPR move");
    OptionalParameterMove optional = {
      first, lowerer.parameter_moves_.back().operands[0].reg, parameter,
      false, false, false, XR_RSP
    };
    optional_parameter_moves_.push_back(optional);
  }

  OptionalParameterMove * optional_parameter_home(
      lowir_model::ValueId parameter) const
  {
    for(std::size_t i = 0; i < optional_parameter_moves_.size(); ++i)
      if(optional_parameter_moves_[i].parameter == parameter)
        return &optional_parameter_moves_[i];
    return 0;
  }

  void require_optional_parameter_move(std::size_t index)
  {
    OptionalParameterMove & move = optional_parameter_moves_[index];
    if(move.required) return;
    move.required = true;
    for(std::size_t i = 0; i < optional_parameter_moves_.size(); ++i) {
      const OptionalParameterMove & dependent = optional_parameter_moves_[i];
      if(dependent.soft_read && dependent.soft_incoming == move.destination)
        require_optional_parameter_move(i);
    }
  }

  void discard_unread_parameter_homes()
  {
    if(optional_parameter_moves_.empty()) return;
    Derived & lowerer = static_cast<Derived &>(*this);
    unsigned mandatory_clobbers = 0;
    std::size_t optional = 0;
    for(std::size_t i = 0; i < lowerer.parameter_moves_.size(); ++i) {
      if(optional < optional_parameter_moves_.size() &&
         optional_parameter_moves_[optional].index == i) {
        ++optional;
        continue;
      }
      const mir_model::MirInstruction & instruction =
        lowerer.parameter_moves_[i];
      if(!instruction.operands.empty() &&
         instruction.operands[0].kind == mir_model::MirOperand::OP_REG)
        mandatory_clobbers |= analysis::register_mask(
          instruction.operands[0].reg);
    }
    for(std::size_t i = 0; i < optional_parameter_moves_.size(); ++i) {
      const OptionalParameterMove & move = optional_parameter_moves_[i];
      if(move.hard_read || (move.soft_read &&
         (mandatory_clobbers & analysis::register_mask(move.soft_incoming))))
        require_optional_parameter_move(i);
    }
    optional = 0;
    std::size_t output = 0;
    for(std::size_t input = 0; input < lowerer.parameter_moves_.size(); ++input) {
      bool discard = false;
      if(optional < optional_parameter_moves_.size() &&
         optional_parameter_moves_[optional].index == input) {
        const X64Register destination =
          optional_parameter_moves_[optional].destination;
        discard = !optional_parameter_moves_[optional].required;
        if(discard)
          lowerer.registers_.discard_unused_reservation(destination);
        ++optional;
      }
      if(discard) continue;
      if(output != input)
        lowerer.parameter_moves_[output] =
          std::move(lowerer.parameter_moves_[input]);
      ++output;
    }
    lowerer.parameter_moves_.resize(output);
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
    return incoming_register_is_available_without_parameter_setup(reg) &&
      !parameter_setup_clobbers_[static_cast<std::size_t>(reg)];
  }

  bool incoming_register_is_available_without_parameter_setup(
      X64Register reg) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return !(emitted_register_definitions_ &
             analysis::register_mask(reg)) &&
      first_selected_definition_[static_cast<std::size_t>(reg)] >=
        lowerer.position_;
  }

  void record_emitted_register_definitions(
      const std::vector<mir_model::MirInstruction> & instructions,
      std::size_t first)
  {
    // Only the six incoming GPRs can answer an early parameter read. Stop
    // visiting MIR once every location represented by this function is gone.
    if((emitted_register_definitions_ & tracked_incoming_registers_) ==
       tracked_incoming_registers_) return;
    for(std::size_t i = first; i < instructions.size(); ++i)
      emitted_register_definitions_ |= tracked_incoming_registers_ &
        static_cast<unsigned>(
          machine_opt::instruction_definition_mask(instructions[i]));
  }

  void record_parameter_setup_clobbers()
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    for(std::size_t i = 0;
        i < lowerer.incoming_parameter_register_known_.size(); ++i)
      if(lowerer.incoming_parameter_register_known_[i])
        tracked_incoming_registers_ |= analysis::register_mask(
          lowerer.incoming_parameter_registers_[i]);
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
  mutable std::vector<OptionalParameterMove> optional_parameter_moves_;

private:
  unsigned tracked_incoming_registers_ = 0;
  unsigned emitted_register_definitions_ = 0;
  unsigned char parameter_setup_clobbers_[16] = {};
  std::size_t first_selected_definition_[16];
};

}  // namespace parameter_detail
}  // namespace lowir_native
