#pragma once

#include "native/mir/construction.h"
#include "native/lowering/values.h"

#include <vector>

namespace lowir_native {
namespace address_replay_detail {

template <class Derived>
class AddressReplay
{
protected:
  bool address_depends_on_register(const lowir_model::Operand & operand,
                                   X64Register reg) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    if(operand.kind != lowir_model::Operand::OP_TEMP) return false;
    const std::uint32_t id = operand.value;
    if(id >= lowerer.value_known_.size() ||
       !lowerer.value_known_[id]) return false;
    const ValueFact & value = lowerer.values_[id];
    if(build::operand_uses_register(value.location, reg)) return true;
    if(!value.deferred_address) return false;
    return address_depends_on_register(value.deferred_address_base, reg) ||
      address_depends_on_register(value.deferred_address_index, reg);
  }

  bool is_frame_address(const lowir_model::Operand & operand) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return operand.kind == lowir_model::Operand::OP_TEMP &&
      lowerer.value_known_[operand.value] &&
      lowerer.values_[operand.value].frame_address;
  }

  void append_address(std::vector<mir_model::MirInstruction> & out,
                      X64Register destination,
                      const mir_model::MirOperand & source)
  {
    using namespace build;
    mir_model::MirInstruction lea =
      machine_instruction(mir_model::MirInstruction::MI_LEA);
    append_operand(lea, reg_operand(destination));
    append_operand(lea, source);
    out.push_back(lea);
  }

  void emit_rematerialized_index_base(
      std::vector<mir_model::MirInstruction> & out,
      X64Register destination, const ValueFact & value)
  {
    using namespace build;
    Derived & lowerer = static_cast<Derived &>(*this);
    const lowir_model::Operand & base_operand = value.deferred_address_base;
    if(base_operand.kind == lowir_model::Operand::OP_TEMP &&
       lowerer.value_known_[base_operand.value]) {
      const ValueFact & base_value = lowerer.values_[base_operand.value];
      if(base_value.rematerialized_constant_index ||
         base_value.deferred_address || base_value.frame_address) {
        lowerer.emit_operand_address(out, destination, base_operand);
        return;
      }
    }
    const mir_model::MirOperand base = lowerer.resolve(base_operand);
    if(base.kind == mir_model::MirOperand::OP_REG) {
      if(base.reg != destination)
        append_move(out, reg_operand(destination), base);
      return;
    }
    lowerer.move_value_to_register(out, destination, base,
      lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
  }
};

}  // namespace address_replay_detail
}  // namespace lowir_native
