#ifndef CPPGM_LOWIR_NATIVE_BULK_LOWERING_H
#define CPPGM_LOWIR_NATIVE_BULK_LOWERING_H

#include "lowir_native_mir.h"
#include "lowir_model.h"

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
  void append_object_copy(std::size_t bytes, std::size_t alignment,
                          std::vector<MirInstruction>& out)
  {
    MirInstruction copy = machine_instruction(MirInstruction::MI_COPY_BYTES);
    copy.byte_count = bytes;
    copy.byte_alignment = alignment;
    append_operand(copy, reg_operand(XR_RDI));
    append_operand(copy, reg_operand(XR_RSI));
    out.push_back(copy);
  }

  void emit_object_copy(const Operand& source, const Operand& destination,
                        std::size_t bytes, std::size_t alignment,
                        std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    if (derived.aliases_same_object(source, destination))
    {
      derived.consume(source);
      derived.consume(destination);
      return;
    }
    X64Register saved_source = XR_RSP;
    bool allocated_saved_source = false;
    if (source.kind == Operand::OP_TEMP)
    {
      const MirOperand resolved_source = derived.resolve(source);
      if (resolved_source.kind == MirOperand::OP_REG &&
          resolved_source.reg == XR_RDI)
      {
        allocated_saved_source =
          derived.registers_.try_allocate(false, saved_source);
        if (!allocated_saved_source) saved_source = XR_R11;
        append_move(out, reg_operand(saved_source), resolved_source);
      }
    }
    derived.emit_operand_address(out, XR_RDI, destination);
    if (saved_source != XR_RSP)
      append_move(out, reg_operand(XR_RSI), reg_operand(saved_source));
    else
      derived.emit_operand_address(out, XR_RSI, source);
    append_object_copy(bytes, alignment, out);
    if (allocated_saved_source) derived.registers_.release(saved_source);
    derived.consume(source);
    derived.consume(destination);
  }

  void emit_object_zero(const Operand& destination,
                        std::size_t bytes, std::size_t alignment,
                        std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    derived.emit_operand_address(out, XR_RDI, destination);
    MirInstruction zero = machine_instruction(MirInstruction::MI_ZERO_BYTES);
    zero.byte_count = bytes;
    zero.byte_alignment = alignment;
    append_operand(zero, reg_operand(XR_RDI));
    out.push_back(zero);
    derived.consume(destination);
  }

  void emit_object_value(const std::string& name, const LowType& type,
                         const Operand& source,
                         std::vector<MirInstruction>& out)
  {
    Derived& derived = static_cast<Derived&>(*this);
    const MirOperand home = derived.allocate_temp_home(name, type);
    // Resolve an incoming object address before assigning its possible RDI
    // carrier to the frame destination.
    derived.emit_operand_address(out, XR_RSI, source);
    derived.append_address(out, XR_RDI, home);
    append_object_copy(type.storage_size, type.alignment, out);
    derived.consume(source);
    derived.define_object_result(name, type, 0, home);
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
                       instruction.type.alignment, out);
    return true;
  }

  void emit_bulk(const Instruction& instruction,
                 std::vector<MirInstruction>& out)
  {
    if (instruction.kind == Instruction::IK_COPYOBJ)
      emit_object_copy(instruction.first, instruction.second,
                       instruction.byte_count,
                       instruction.byte_alignment, out);
    else
      emit_object_zero(instruction.first, instruction.byte_count,
                       instruction.byte_alignment, out);
  }
};

}
}

#endif
