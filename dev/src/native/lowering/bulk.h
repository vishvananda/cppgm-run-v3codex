#ifndef CPPGM_LOWIR_NATIVE_BULK_LOWERING_H
#define CPPGM_LOWIR_NATIVE_BULK_LOWERING_H

#include "native/mir/construction.h"
#include "native/lowering/memcpy.h"
#include "lowir/model/program.h"

#include <vector>

namespace lowir_native
{
namespace bulk_detail
{

using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using mir_model::MirInstruction;
using mir_model::MirOperand;
using namespace build;

template <class Derived>
class BulkLowering
{
protected:
  bool try_emit_preserving_dynamic_copy(
      const Instruction& instruction,
      lowir_model::SymbolId memcpy_symbol,
      std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    if(derived.optimization_level_ < 3 ||
       !derived.facts_.has_small_direct_parameter_copy ||
       !memcpy_detail::is_inline_unused_call(
         instruction, derived.facts_.uses, memcpy_symbol)) return false;
    MirInstruction copy = machine_instruction(MirInstruction::MI_COPY_BYTES);
    copy.copy_preserves_pointers = true;
    for(std::size_t i = 0; i < instruction.args.size(); ++i) {
      MirOperand argument =
        derived.resolve_gpr_call_argument(instruction.args[i]);
      if(instruction.args[i].kind == Operand::OP_TEMP) {
        const auto& value = derived.values_[instruction.args[i].value];
        if(value.forwarded_parameter.valid())
          argument = derived.selected_value_location(
            value.forwarded_parameter);
      }
      if(derived.is_frame_address(instruction.args[i]))
        copy.copy_address_operand_mask |= 1u << i;
      append_operand(copy, argument);
    }
    out.push_back(copy);
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      derived.consume(instruction.args[i]);
    derived.active_instruction_ = 0;
    derived.consume(instruction.first);
    return true;
  }

  bool can_preserve_parameter_pointers(const Operand& source,
                                       const Operand& destination,
                                       std::size_t bytes) const
  {
    const Derived& derived = static_cast<const Derived&>(*this);
    return derived.optimization_level_ >= 3 && bytes && bytes <= 64 &&
      source.kind == Operand::OP_TEMP &&
      destination.kind == Operand::OP_TEMP &&
      derived.facts_.has(source.value,
                         analysis::FunctionFacts::VF_PARAMETER) &&
      derived.facts_.has(destination.value,
                         analysis::FunctionFacts::VF_PARAMETER);
  }

  void append_object_copy(std::size_t bytes, std::size_t alignment,
                          X64Register destination, X64Register source,
                          bool preserve_pointers,
                          std::vector<MirInstruction>& out)
  {
    MirInstruction copy = machine_instruction(MirInstruction::MI_COPY_BYTES);
    copy.byte_count = bytes;
    copy.byte_alignment = alignment;
    copy.copy_preserves_pointers = preserve_pointers;
    append_operand(copy, reg_operand(destination));
    append_operand(copy, reg_operand(source));
    out.push_back(copy);
  }

  bool direct_address_register(const Operand& operand,
                               X64Register* result) const
  {
    if (operand.kind != Operand::OP_TEMP) return false;
    const Derived& derived = static_cast<const Derived&>(*this);
    const MirOperand address = derived.resolve(operand);
    if (address.kind != MirOperand::OP_REG) return false;
    *result = address.reg;
    return true;
  }

  bool copy_side_operand(const Operand& operand, MirOperand* result)
  {
    Derived& derived = static_cast<Derived&>(*this);
    X64Register address_register = XR_RSI;
    if (direct_address_register(operand, &address_register))
    {
      *result = reg_operand(address_register);
      return true;
    }
    long long frame_offset = 0;
    if (derived.frame_provenance(operand, frame_offset))
    {
      result->kind = MirOperand::OP_FRAME;
      result->frame_binding = 0;
      result->offset = frame_offset;
      return true;
    }
    return false;
  }

  void emit_object_copy(const Operand& source, const Operand& destination,
                        std::size_t bytes, std::size_t alignment,
                        bool admit_parameter_preservation,
                        std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    const bool preserve_pointers = admit_parameter_preservation &&
      can_preserve_parameter_pointers(source, destination, bytes);
    if (derived.aliases_same_object(source, destination))
    {
      derived.consume(source);
      derived.consume(destination);
      return;
    }
    // A small copy encodes as direct load/store chunks, so a frame-resident
    // side needs no address materialization at all.
    if (bytes != 0 && bytes <= 32)
    {
      MirOperand source_side;
      MirOperand destination_side;
      if (copy_side_operand(source, &source_side) &&
          copy_side_operand(destination, &destination_side))
      {
        MirInstruction copy =
          machine_instruction(MirInstruction::MI_COPY_BYTES);
        copy.byte_count = bytes;
        copy.byte_alignment = alignment;
        copy.copy_preserves_pointers = preserve_pointers;
        append_operand(copy, destination_side);
        append_operand(copy, source_side);
        out.push_back(copy);
        derived.consume(source);
        derived.consume(destination);
        return;
      }
    }
    X64Register source_register = XR_RSI;
    const bool direct_source =
      direct_address_register(source, &source_register);
    X64Register destination_register = XR_RDI;
    const bool direct_destination =
      direct_address_register(destination, &destination_register);
    bool materialized_source = false;
    if (!direct_destination)
    {
      destination_register = direct_source && source_register == XR_RDI ?
        XR_R11 : XR_RDI;
      // Address setup is destructive: materializing the destination in rdi
      // must not overwrite a parameter or deferred carrier still needed to
      // form the source address.  Reverse the setup when that is sufficient;
      // use the reserved r10 scratch when both logical addresses depend on
      // the other's copy register.
      if (!direct_source &&
          derived.address_depends_on_register(source,
                                              destination_register))
      {
        source_register =
          derived.address_depends_on_register(destination, XR_RSI) ?
            XR_R10 : XR_RSI;
        derived.emit_operand_address(out, source_register, source);
        materialized_source = true;
      }
      derived.emit_operand_address(out, destination_register, destination);
    }
    if (!direct_source && !materialized_source)
    {
      source_register = destination_register == XR_RSI ? XR_R11 : XR_RSI;
      derived.emit_operand_address(out, source_register, source);
    }
    else if(materialized_source && source_register == XR_R10)
    {
      append_move(out, reg_operand(XR_RSI), reg_operand(XR_R10));
      source_register = XR_RSI;
    }
    append_object_copy(bytes, alignment, destination_register,
                       source_register, preserve_pointers, out);
    derived.consume(source);
    derived.consume(destination);
  }

  void emit_object_zero(const Operand& destination,
                        std::size_t bytes, std::size_t alignment,
                        std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    X64Register destination_register = XR_RDI;
    if (!direct_address_register(destination, &destination_register))
      derived.emit_operand_address(out, destination_register, destination);
    MirInstruction zero = machine_instruction(MirInstruction::MI_ZERO_BYTES);
    zero.byte_count = bytes;
    zero.byte_alignment = alignment;
    append_operand(zero, reg_operand(destination_register));
    out.push_back(zero);
    derived.consume(destination);
  }

  void emit_object_value(lowir_model::ValueId value, const LowType& type,
                         const Operand& source,
                         std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    const MirOperand home = derived.allocate_temp_home(value, type);
    X64Register source_register = XR_RSI;
    const bool direct_source =
      direct_address_register(source, &source_register);
    const X64Register destination_register =
      direct_source && source_register == XR_RDI ? XR_R11 : XR_RDI;
    derived.append_address(out, destination_register, home);
    if (!direct_source)
      derived.emit_operand_address(out, XR_RSI, source);
    append_object_copy(type.storage_size, type.alignment,
                       destination_register, source_register, false, out);
    derived.consume(source);
    derived.define_object_result(value, type, 0, home);
  }

  void emit_object_load(const Instruction& instruction,
                        std::vector<MirInstruction>& out)
  {
    emit_object_value(instruction.dest, instruction.type,
                      instruction.first, out);
  }

  bool emit_object_store(const Instruction& instruction,
                         std::vector<MirInstruction>& out)
  {
    if (instruction.type.kind != lowir_model::LTK_OBJECT) return false;
    if (instruction.first.kind == Operand::OP_INTEGER &&
        instruction.first.has_int_value && instruction.first.int_value == 0)
      emit_object_zero(instruction.second, instruction.type.storage_size,
                       instruction.type.alignment, out);
    else
      emit_object_copy(instruction.first, instruction.second,
                       instruction.type.storage_size,
                       instruction.type.alignment, false, out);
    return true;
  }

  void emit_bulk(const Instruction& instruction,
                 std::vector<MirInstruction>& out)
  {
    if (instruction.kind == Instruction::IK_COPYOBJ)
      emit_object_copy(instruction.first, instruction.second,
                       instruction.byte_count,
                       instruction.byte_alignment, true, out);
    else
      emit_object_zero(instruction.first, instruction.byte_count,
                       instruction.byte_alignment, out);
  }
};

}
}

#endif
