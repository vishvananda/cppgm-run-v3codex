#pragma once

#include "native/mir/construction.h"
#include "native/allocation/registers.h"
#include "native/lowering/selection.h"
#include "native/lowering/values.h"
#include "native/lowering/wide.h"

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native {
namespace index_detail {

template <class Derived>
class IndexLowering
{
protected:
  bool add_address_offset(long long left, long long right,
                          long long * result) const
  {
    if((right > 0 && left > std::numeric_limits<long long>::max() - right) ||
       (right < 0 && left < std::numeric_limits<long long>::min() - right))
      return false;
    *result = left + right;
    return true;
  }

  bool index_has_direct_memory_use(
      const lowir_model::LowirBlock & block,
      std::size_t instruction_index,
      const lowir_model::Instruction & instruction) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    if(lowerer.facts_.uses[instruction.dest] != 1 ||
       lowerer.facts_.has(instruction.dest,
                          analysis::FunctionFacts::VF_EDGE_LIVE) ||
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
       consumer.first.value == instruction.dest)
      return !lowerer.facts_.has(
        consumer.dest, analysis::FunctionFacts::VF_DIRECT_COMPARE_STORAGE);
    return consumer.kind == lowir_model::Instruction::IK_STORE &&
      consumer.second.kind == lowir_model::Operand::OP_TEMP &&
      consumer.second.value == instruction.dest;
  }

  void emit_index(const lowir_model::LowirBlock & block,
                  std::size_t instruction_index,
                  const lowir_model::Instruction & instruction,
                  std::vector<mir_model::MirInstruction> & out)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    if(lowerer.facts_.uses[instruction.dest] == 0) {
      lowerer.consume(instruction.first);
      lowerer.consume(instruction.second);
      return;
    }
    const bool constant_index =
      instruction.second.kind == lowir_model::Operand::OP_INTEGER;
    const long long offset = constant_index ?
      selection::integer_value(instruction.second) *
        static_cast<long long>(instruction.type.storage_size) : 0;
    mir_model::MirOperand base = lowerer.resolve(instruction.first);
    const bool deferred_base =
      instruction.first.kind == lowir_model::Operand::OP_TEMP &&
      lowerer.value_known_[instruction.first.value] &&
      lowerer.values_[instruction.first.value].deferred_address;
    const bool stable_register_base =
      instruction.first.kind == lowir_model::Operand::OP_TEMP &&
      lowerer.value_known_[instruction.first.value] &&
      lowerer.values_[instruction.first.value].parameter;
    const bool stable_deferred_base = deferred_base &&
      lowerer.values_[instruction.first.value].deferred_address_stable;
    bool require_selected_parameter_home = false;
    if(constant_index && stable_register_base) {
      const mir_model::MirOperand selected =
        lowerer.values_[instruction.first.value].location;
      if(selected.kind == mir_model::MirOperand::OP_REG &&
         (base.kind != mir_model::MirOperand::OP_REG ||
          selected.reg != base.reg) &&
         !lowerer.crosses_register_clobber(instruction.dest, selected.reg)) {
        base = selected;
        require_selected_parameter_home = true;
      }
    }
    if(require_selected_parameter_home)
      base = lowerer.selected_value_location(instruction.first.value);
    // The union flag extends deferral only to the FRAME form: a pure
    // rbp-relative address replays anywhere, while register-carried
    // forms need the carrier alive at every consumer, which only the
    // all-storage analysis guarantees.
    const bool storage_only_uses = lowerer.facts_.has(
      instruction.dest, analysis::FunctionFacts::VF_ONLY_STORAGE_ADDRESS);
    if(constant_index &&
       (storage_only_uses ||
        lowerer.facts_.has(
          instruction.dest,
          analysis::FunctionFacts::VF_ADDRESS_UNION_SAFE))) {
      mir_model::MirOperand address;
      bool encodable = false;
      if(stable_register_base && base.kind == mir_model::MirOperand::OP_REG &&
         storage_only_uses &&
         !lowerer.crosses_register_clobber(instruction.dest, base.reg)) {
        address = dereference(base.reg, offset);
        encodable = true;
      } else if((stable_deferred_base ||
                 lowerer.is_frame_address(instruction.first)) &&
                (base.kind == mir_model::MirOperand::OP_DEREF ||
                 base.kind == mir_model::MirOperand::OP_FRAME)) {
        long long combined = 0;
        encodable = add_address_offset(base.offset, offset, &combined);
        if(encodable && base.kind == mir_model::MirOperand::OP_DEREF) {
          encodable = storage_only_uses &&
            !lowerer.crosses_register_clobber(
              instruction.dest, base.reg) &&
            (!base.has_index || !lowerer.crosses_register_clobber(
              instruction.dest, base.index));
        }
        if(encodable) {
          address = base;
          address.offset = combined;
        }
      }
      if(encodable) {
        lowerer.reserve_deferred_address_carriers(address);
        ValueFact value;
        value.location = address;
        value.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
        value.deferred_address = true;
        value.deferred_address_stable = true;
        value.deferred_address_base = instruction.first;
        value.deferred_address_index = instruction.second;
        if(base.kind == mir_model::MirOperand::OP_FRAME &&
           lowerer.is_frame_address(instruction.first)) {
          value.frame_address = true;
          value.has_frame_provenance = true;
          value.frame_provenance = address.offset;
        }
        lowerer.set_value(instruction.dest, value);
        return;
      }
    }
    // A pointer value already held in stable storage is cheaper to reload at
    // the eventual memory operation than to build, spill, and reload a second
    // pointer for base+constant.  Keep the semantic base alive and replay the
    // displacement only for the all-storage consumer class.
    if(lowerer.optimization_level_ >= 1 && constant_index &&
       storage_only_uses &&
       (base.kind == mir_model::MirOperand::OP_FRAME ||
        base.kind == mir_model::MirOperand::OP_SYMBOL ||
        base.kind == mir_model::MirOperand::OP_GLOBAL ||
        (instruction.first.kind == lowir_model::Operand::OP_TEMP &&
         lowerer.value_known_[instruction.first.value] &&
         lowerer.values_[instruction.first.value].rematerialized_constant_index))) {
      ValueFact value;
      value.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
      value.deferred_address = true;
      value.rematerialized_constant_index = true;
      value.rematerialized_index_offset = offset;
      value.deferred_address_base = instruction.first;
      value.deferred_address_index = instruction.second;
      lowerer.set_value(instruction.dest, value);
      if(lowerer.stats_)
        ++lowerer.stats_->planned_rematerialized_constant_indexes;
      return;
    }
    if(index_has_direct_memory_use(block, instruction_index, instruction) &&
       (base.kind == mir_model::MirOperand::OP_REG ||
        (constant_index &&
         base.kind == mir_model::MirOperand::OP_FRAME &&
         lowerer.is_frame_address(instruction.first)))) {
      mir_model::MirOperand address;
      if(constant_index) {
        if(base.kind == mir_model::MirOperand::OP_FRAME) {
          address = base;
          address.offset += offset;
        } else address = dereference(base.reg, offset);
      } else {
        const mir_model::MirOperand index =
          lowerer.resolve(instruction.second);
        if(index.kind != mir_model::MirOperand::OP_REG || index.reg == XR_RSP)
          goto materialize_index;
        address = indexed_dereference(
          base.reg, index.reg,
          static_cast<unsigned>(instruction.type.storage_size));
      }
      lowerer.reserve_deferred_address_carriers(address);
      ValueFact value;
      value.location = address;
      value.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
      value.deferred_address = true;
      value.deferred_address_stable = constant_index &&
        (stable_register_base || lowerer.is_frame_address(instruction.first));
      value.deferred_address_base = instruction.first;
      value.deferred_address_index = instruction.second;
      lowerer.set_value(instruction.dest, value);
      return;
    }
    if(instruction.first.kind == lowir_model::Operand::OP_TEMP &&
       lowerer.facts_.first_use[instruction.first.value] == lowerer.position_ &&
       !lowerer.result_crosses_call(instruction.dest) &&
       lowerer.incoming_parameter_register_known_[instruction.first.value]) {
      const X64Register incoming =
        lowerer.incoming_parameter_registers_[instruction.first.value];
      if(lowerer.incoming_parameter_register_is_intact(
           instruction.first.value, incoming))
        base = reg_operand(incoming);
    }
materialize_index:
    if(constant_index && offset == 0 &&
       instruction.first.kind == lowir_model::Operand::OP_TEMP &&
       base.kind == mir_model::MirOperand::OP_REG &&
       !lowerer.crosses_register_clobber(instruction.dest, base.reg)) {
      ValueFact alias = lowerer.values_[instruction.first.value];
      alias.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
      alias.parameter = false;
      alias.fixed_register_home = false;
      lowerer.set_value(instruction.dest, alias);
      lowerer.consume(instruction.first);
      lowerer.consume(instruction.second);
      return;
    }
    mir_model::MirOperand destination;
    const bool forwarded_alias = offset == 0 &&
      instruction.first.kind == lowir_model::Operand::OP_TEMP &&
      lowerer.facts_.has(
        instruction.first.value,
        analysis::FunctionFacts::VF_FORWARDED_PARAMETER_ACROSS_CALL);
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
        lowerer.values_[instruction.first.value].parameter &&
        offset != 0 &&
        !lowerer.facts_.has(
          instruction.first.value,
          analysis::FunctionFacts::VF_ZERO_INDEX_PARAMETER);
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
      if(base.kind == mir_model::MirOperand::OP_FRAME &&
         lowerer.is_frame_address(instruction.first)) {
        base.offset += offset;
        mir_model::MirInstruction lea =
          machine_instruction(mir_model::MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, base);
        out.push_back(lea);
        address_emitted = true;
      } else if(deferred_base &&
                base.kind == mir_model::MirOperand::OP_DEREF) {
        long long combined = 0;
        if(add_address_offset(base.offset, offset, &combined)) {
          base.offset = combined;
          mir_model::MirInstruction lea =
            machine_instruction(mir_model::MirInstruction::MI_LEA);
          append_operand(lea, destination);
          append_operand(lea, base);
          out.push_back(lea);
          address_emitted = true;
        }
      } else if(base.kind == mir_model::MirOperand::OP_REG && offset != 0) {
        mir_model::MirInstruction lea =
          machine_instruction(mir_model::MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, dereference(base.reg, offset));
        out.push_back(lea);
        address_emitted = true;
      }
    } else {
      mir_model::MirOperand index = lowerer.resolve(instruction.second);
      const bool encodable_scale = instruction.type.storage_size == 1 ||
        instruction.type.storage_size == 2 ||
        instruction.type.storage_size == 4 ||
        instruction.type.storage_size == 8;
      if(deferred_base && base.kind == mir_model::MirOperand::OP_DEREF &&
         !base.has_index && encodable_scale &&
         index.kind != mir_model::MirOperand::OP_REG &&
         destination.reg != base.reg) {
        lowerer.move_value_to_register(
          out, destination.reg, index, lowerer.operand_type(instruction.second));
        index = destination;
      }
      if(deferred_base && base.kind == mir_model::MirOperand::OP_DEREF &&
         !base.has_index && encodable_scale &&
         index.kind == mir_model::MirOperand::OP_REG && index.reg != XR_RSP) {
        mir_model::MirOperand address = base;
        address.has_index = true;
        address.index = index.reg;
        address.scale = static_cast<unsigned>(instruction.type.storage_size);
        mir_model::MirInstruction lea =
          machine_instruction(mir_model::MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, address);
        out.push_back(lea);
        address_emitted = true;
      } else if(base.kind == mir_model::MirOperand::OP_REG &&
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
          if(instruction.first.kind == lowir_model::Operand::OP_TEMP &&
             lowerer.values_[instruction.first.value].rematerialized_constant_index)
            lowerer.emit_operand_address(
              out, destination.reg, instruction.first);
          else if(deferred_base || lowerer.is_frame_address(instruction.first))
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
          mir_model::MirInstruction::MI_IMUL, machine_type(lowir_model::LTK_I64));
        append_operand(scale, reg_operand(XR_RDX));
        append_operand(scale, immediate(
          static_cast<long long>(instruction.type.storage_size)));
        out.push_back(scale);
        index = reg_operand(XR_RDX);
      }
      mir_model::MirInstruction add = machine_instruction(
        mir_model::MirInstruction::MI_ADD, machine_type(lowir_model::LTK_PTR));
      append_operand(add, destination);
      append_operand(add, index);
      out.push_back(add);
      }
    }
    if(!address_emitted && constant_index) {
      if(base.kind != mir_model::MirOperand::OP_REG ||
         destination.reg != base.reg) {
        if(instruction.first.kind == lowir_model::Operand::OP_TEMP &&
           lowerer.values_[instruction.first.value].rematerialized_constant_index)
          lowerer.emit_operand_address(out, destination.reg, instruction.first);
        else if(deferred_base || lowerer.is_frame_address(instruction.first))
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
      append_store(out, pressure_home, destination, pointer_type);
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
