#pragma once

#include "lowir_native_mir.h"
#include "lowir_native_registers.h"
#include "lowir_native_selection.h"
#include "lowir_native_value.h"
#include "lowir_native_wide.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native {
namespace index_detail {

template <class Derived>
class IndexLowering
{
protected:
  bool index_has_direct_memory_use(
      const lowir_model::LowirBlock & block,
      std::size_t instruction_index,
      const lowir_model::Instruction & instruction) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    const std::unordered_map<std::string, std::size_t>::const_iterator uses =
      lowerer.facts_.uses.find(instruction.dest);
    if(uses == lowerer.facts_.uses.end() || uses->second != 1 ||
       lowerer.facts_.edge_live.count(instruction.dest) ||
       instruction_index + 1 >= block.instructions.size()) return false;
    const std::size_t scale = instruction.type.storage_size;
    if(scale != 1 && scale != 2 && scale != 4 && scale != 8) return false;
    const lowir_model::Instruction & consumer =
      block.instructions[instruction_index + 1];
    const bool scalar = !selection::is_floating(consumer.type) &&
      !wide::is_integer(consumer.type) &&
      consumer.type.kind != lowir_model::LTK_OBJECT;
    if(!scalar) return false;
    if(consumer.kind == lowir_model::Instruction::IK_LOAD &&
       consumer.first.kind == lowir_model::Operand::OP_TEMP &&
       consumer.first.text == instruction.dest)
      return !lowerer.facts_.direct_compare_storage_values.count(
        consumer.dest);
    return consumer.kind == lowir_model::Instruction::IK_STORE &&
      consumer.second.kind == lowir_model::Operand::OP_TEMP &&
      consumer.second.text == instruction.dest;
  }

  void emit_index(const lowir_model::LowirBlock & block,
                  std::size_t instruction_index,
                  const lowir_model::Instruction & instruction,
                  std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    const bool constant_index =
      instruction.second.kind == lowir_model::Operand::OP_INTEGER;
    const long long offset = constant_index ?
      selection::integer_value(instruction.second) *
        static_cast<long long>(instruction.type.storage_size) : 0;
    mir_model::MirOperand base = lowerer.resolve(instruction.first);
    if(index_has_direct_memory_use(block, instruction_index, instruction) &&
       base.kind == mir_model::MirOperand::OP_REG) {
      mir_model::MirOperand address;
      if(constant_index) {
        address = dereference(base.reg, offset);
      } else {
        const mir_model::MirOperand index =
          lowerer.resolve(instruction.second);
        if(index.kind != mir_model::MirOperand::OP_REG || index.reg == XR_RSP)
          goto materialize_index;
        address = indexed_dereference(
          base.reg, index.reg,
          static_cast<unsigned>(instruction.type.storage_size));
      }
      ValueFact value;
      value.location = address;
      value.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
      value.deferred_address = true;
      value.deferred_address_base = instruction.first;
      value.deferred_address_index = instruction.second;
      lowerer.set_value(instruction.dest, value);
      return;
    }
    if(instruction.first.kind == lowir_model::Operand::OP_TEMP &&
       lowerer.facts_.first_use[instruction.first.text] == lowerer.position_ &&
       !lowerer.result_crosses_call(instruction.dest) &&
       lowerer.incoming_parameter_registers_.count(instruction.first.text)) {
      const X64Register incoming =
        lowerer.incoming_parameter_registers_.find(
          instruction.first.text)->second;
      if(lowerer.incoming_parameter_register_is_intact(
           instruction.first.text, incoming))
        base = reg_operand(incoming);
    }
materialize_index:
    mir_model::MirOperand destination;
    const bool forwarded_alias = offset == 0 &&
      instruction.first.kind == lowir_model::Operand::OP_TEMP &&
      lowerer.facts_.forwarded_parameters_across_call.count(
        instruction.first.text);
    const bool safe_reuse = base.kind == mir_model::MirOperand::OP_REG &&
      constant_index && (lowerer.can_reuse(instruction.first) ||
      forwarded_alias) &&
      !lowerer.crosses_register_clobber(instruction.dest, base.reg);
    bool direct_return = selection::result_is_immediate_return(
      block, instruction_index, instruction.dest, lowerer.facts_);
    if(direct_return && base.kind == mir_model::MirOperand::OP_REG &&
       base.reg == XR_RAX && !lowerer.can_reuse(instruction.first))
      direct_return = false;
    if(direct_return && !constant_index) {
      const mir_model::MirOperand index = lowerer.resolve(instruction.second);
      if(index.kind == mir_model::MirOperand::OP_REG && index.reg == XR_RAX &&
         !lowerer.can_reuse(instruction.second))
        direct_return = false;
    }
    mir_model::MirOperand pressure_home;
    if(direct_return) destination = reg_operand(XR_RAX);
    else if(safe_reuse) destination = base;
    else {
      const bool force_preserved =
        instruction.first.kind == lowir_model::Operand::OP_TEMP &&
        lowerer.values_.find(instruction.first.text)->second.parameter &&
        offset != 0 &&
        !lowerer.facts_.zero_index_parameters.count(instruction.first.text);
      X64Register result = XR_RSP;
      if(lowerer.try_allocate_result(
           instruction.dest, out, &result, force_preserved))
        destination = reg_operand(result);
      else {
        pressure_home = lowerer.allocate_temp_home(
          instruction.dest,
          lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
        destination = reg_operand(XR_RAX);
      }
    }
    bool address_emitted = false;
    if(constant_index) {
      if(base.kind == mir_model::MirOperand::OP_REG && offset != 0) {
        mir_model::MirInstruction lea =
          machine_instruction(mir_model::MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, dereference(base.reg, offset));
        out.push_back(lea);
        address_emitted = true;
      }
    } else {
      mir_model::MirOperand index = lowerer.resolve(instruction.second);
      if(base.kind == mir_model::MirOperand::OP_REG &&
         index.kind == mir_model::MirOperand::OP_REG && index.reg != XR_RSP &&
         (instruction.type.storage_size == 1 ||
          instruction.type.storage_size == 2 ||
          instruction.type.storage_size == 4 ||
          instruction.type.storage_size == 8)) {
        mir_model::MirInstruction lea =
          machine_instruction(mir_model::MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, indexed_dereference(
          base.reg, index.reg,
          static_cast<unsigned>(instruction.type.storage_size)));
        out.push_back(lea);
        address_emitted = true;
      }
      if(!address_emitted) {
        if(base.kind != mir_model::MirOperand::OP_REG ||
           destination.reg != base.reg) {
          if(lowerer.is_frame_address(instruction.first))
            lowerer.append_address(out, destination.reg, base);
          else
            lowerer.move_value_to_register(
              out, destination.reg, base,
              lowerer.operand_type(instruction.first));
        }
      if(index.kind != mir_model::MirOperand::OP_REG) {
        lowerer.move_value_to_register(
          out, XR_RDX, index, lowerer.operand_type(instruction.second));
        index = reg_operand(XR_RDX);
      }
      if(instruction.type.storage_size != 1) {
        if(index.reg != XR_RDX)
          append_move(out, reg_operand(XR_RDX), index);
        mir_model::MirInstruction scale = machine_instruction(
          mir_model::MirInstruction::MI_IMUL, "i64");
        append_operand(scale, reg_operand(XR_RDX));
        append_operand(scale, immediate(
          static_cast<long long>(instruction.type.storage_size)));
        out.push_back(scale);
        index = reg_operand(XR_RDX);
      }
      mir_model::MirInstruction add = machine_instruction(
        mir_model::MirInstruction::MI_ADD, "ptr");
      append_operand(add, destination);
      append_operand(add, index);
      out.push_back(add);
      }
    }
    if(!address_emitted && constant_index) {
      if(base.kind != mir_model::MirOperand::OP_REG ||
         destination.reg != base.reg) {
        if(lowerer.is_frame_address(instruction.first))
          lowerer.append_address(out, destination.reg, base);
        else
          lowerer.move_value_to_register(
            out, destination.reg, base,
            lowerer.operand_type(instruction.first));
      }
      if(offset != 0) {
        mir_model::MirInstruction lea =
          machine_instruction(mir_model::MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, dereference(destination.reg, offset));
        out.push_back(lea);
      }
    }
    lowerer.consume(instruction.first, destination.reg);
    lowerer.consume(instruction.second, destination.reg);
    const lowir_model::LowType pointer_type =
      lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
    if(pressure_home.kind == mir_model::MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, lowir_model::lowir_type_text(pointer_type));
    lowerer.define(
      instruction.dest, pointer_type,
      pressure_home.kind == mir_model::MirOperand::OP_FRAME ?
        pressure_home : destination);
    if(safe_reuse && forwarded_alias)
      lowerer.values_[instruction.dest].parameter = true;
  }
};

}  // namespace index_detail
}  // namespace lowir_native
