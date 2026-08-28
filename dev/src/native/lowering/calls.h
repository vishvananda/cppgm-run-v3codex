#pragma once

#include "native/lowering/abi.h"
#include "native/mir/construction.h"
#include "native/mir/optimize.h"
#include "native/lowering/selection.h"
#include "native/lowering/values.h"
#include "native/lowering/wide.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace call_detail {

template <class Derived>
class CallLowering
{
protected:
  bool gpr_call_argument_can_read_rax(
      const lowir_model::Operand & operand) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return operand.kind == lowir_model::Operand::OP_TEMP &&
      rax_first_use_active_ && rax_first_use_value_ == operand.value &&
      lowerer.facts_.first_use[operand.value] == lowerer.position_ &&
      lowerer.facts_.uses[operand.value] > 1 &&
      lowerer.facts_.has(operand.value,
        analysis::FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE);
  }

  void record_rax_first_use_carrier(
      const lowir_model::Instruction & instruction,
      const std::vector<mir_model::MirInstruction> & emitted,
      std::size_t first)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(rax_first_use_active_) {
      for(std::size_t i = first; i < emitted.size(); ++i)
        if(machine_opt::instruction_definition_mask(emitted[i]) &
           analysis::register_mask(XR_RAX)) {
          rax_first_use_active_ = false;
          break;
        }
      if(rax_first_use_active_ &&
         lowerer.position_ >= lowerer.facts_.first_use[rax_first_use_value_])
        rax_first_use_active_ = false;
    }
    if(instruction.kind == lowir_model::Instruction::IK_CALL &&
       instruction.dest.valid() &&
       lowerer.facts_.has(instruction.dest,
         analysis::FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE)) {
      rax_first_use_value_ = instruction.dest;
      rax_first_use_active_ = true;
    }
  }

  mir_model::MirOperand resolve_gpr_call_argument(
      const lowir_model::Operand & operand) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return gpr_call_argument_can_read_rax(operand) ?
      build::reg_operand(XR_RAX) : lowerer.resolve(operand);
  }

  bool indirect_target_can_keep_register(
      X64Register target,
      const std::vector<lowir_model::LowirParameter> & parameters) const
  {
    if(target != XR_RBX && target != XR_R12 && target != XR_R13 &&
       target != XR_R14 && target != XR_R15 &&
       target != XR_R8 && target != XR_R9)
      return false;
    std::size_t gpr = 0;
    for(std::size_t i = 0; i < parameters.size() && gpr < 6; ++i) {
      if(selection::is_scalar_float(parameters[i].type)) continue;
      if(abi::argument_register(gpr++) == target) return false;
    }
    return true;
  }

  lowir_model::ValueId rax_first_use_value_;
  bool rax_first_use_active_ = false;

  bool indirect_target_can_keep_register(
      X64Register target, const abi::Plan & plan) const
  {
    if(target != XR_RBX && target != XR_R12 && target != XR_R13 &&
       target != XR_R14 && target != XR_R15 &&
       target != XR_R8 && target != XR_R9)
      return false;
    for(std::size_t i = 0; i < plan.pieces.size(); ++i)
      if(plan.pieces[i].location == abi::PL_GPR &&
         plan.pieces[i].reg == target)
        return false;
    return true;
  }

  bool result_is_next_call_address_argument(
      const lowir_model::LowirBlock & block,
      std::size_t instruction_index,
      const lowir_model::Instruction & producer) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       producer.type.kind == lowir_model::LTK_PTR) return false;
    const lowir_model::Instruction & call =
      block.instructions[instruction_index + 1];
    if(call.kind != lowir_model::Instruction::IK_CALL) return false;
    const Derived & lowerer = static_cast<const Derived &>(*this);
    std::vector<lowir_model::LowirParameter> parameters;
    if(call.has_call_signature) parameters = call.call_params;
    else if(call.first.kind == lowir_model::Operand::OP_GLOBAL) {
      const abi::FunctionSignature & found =
        lowerer.signatures_[call.first.symbol];
      if(found.params) parameters = *found.params;
    }
    for(std::size_t i = 0; i < call.args.size() && i < parameters.size(); ++i)
      if(call.args[i].kind == lowir_model::Operand::OP_TEMP &&
         call.args[i].value == producer.dest &&
         parameters[i].metadata.passing != lowir_model::PPM_DIRECT)
        return true;
    return false;
  }

  bool move_destination_is_safe(const std::vector<GprMove> & moves,
                                std::size_t candidate) const
  {
    for(std::size_t i = 0; i < moves.size(); ++i) {
      if(i == candidate || !moves[i].pending ||
         moves[i].source.kind != mir_model::MirOperand::OP_REG) continue;
      if(moves[i].source.reg == moves[candidate].destination) return false;
    }
    return true;
  }

  bool direct_object_chunk_storage(const lowir_model::Operand & operand,
                                   std::size_t chunk_offset,
                                   mir_model::MirOperand * result) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    mir_model::MirOperand location;
    if(operand.kind == lowir_model::Operand::OP_SLOT)
      location = lowerer.storage(operand);
    else if(operand.kind == lowir_model::Operand::OP_TEMP &&
            lowerer.value_known_[operand.value]) {
      const ValueFact & value = lowerer.values_[operand.value];
      if(value.type.kind != lowir_model::LTK_OBJECT &&
         !wide::is_integer(value.type) && !value.frame_address)
        return false;
      location = value.location;
    } else return false;
    if(location.kind != mir_model::MirOperand::OP_FRAME &&
       location.kind != mir_model::MirOperand::OP_DEREF)
      return false;
    location.offset += static_cast<long long>(chunk_offset);
    *result = location;
    return true;
  }

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

  bool scalar_call_result_stages_directly_from_rax(
      lowir_model::ValueId result) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return lowerer.optimization_level_ >= 1 && lowerer.facts_.has_eh &&
      lowerer.facts_.has(result, analysis::FunctionFacts::VF_EDGE_LIVE) &&
      (lowerer.facts_.has(
         result, analysis::FunctionFacts::VF_LOOP_INVARIANT) ||
       lowerer.result_crosses_call(result)) &&
      !lowerer.facts_.has(
        result, analysis::FunctionFacts::VF_EXACT_FORWARD_EDGE) &&
      lowerer.planned_register_entry(result) == 0;
  }

  mir_model::MirOperand place_allocated_scalar_call_result(
      lowir_model::ValueId value, X64Register result,
      std::vector<mir_model::MirInstruction> & out)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(scalar_call_result_stages_directly_from_rax(value)) {
      lowerer.registers_.release(result);
      if(lowerer.stats_) ++lowerer.stats_->planned_direct_call_edge_homes;
      return build::reg_operand(XR_RAX);
    }
    const mir_model::MirOperand location = build::reg_operand(result);
    build::append_move(out, location, build::reg_operand(XR_RAX));
    return location;
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
      if(piece.location == abi::PL_XMM ||
         parameters[parameter].type.kind == lowir_model::LTK_OBJECT ||
         wide::is_integer(parameters[parameter].type))
        continue;
      const lowir_model::Operand & argument = instruction.args[parameter];
      if(argument.kind != lowir_model::Operand::OP_TEMP) continue;
      if(!lowerer.value_known_[argument.value]) continue;
      ValueFact & value = lowerer.values_[argument.value];
      if(value.location.kind != mir_model::MirOperand::OP_REG ||
         (value.location.reg != XR_RDI && value.location.reg != XR_RSI))
        continue;
      const mir_model::MirOperand location =
        lowerer.selected_value_location(argument.value);
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
