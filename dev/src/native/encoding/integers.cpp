#include "native/encoding/integers.h"

#include "native/analysis/data_layout.h"
#include "native/errors.h"
#include "native/encoding/instructions.h"
#include "native/encoding/scalar_memory.h"

#include <climits>
#include <cstdint>

namespace lowir_native {
namespace {

void require_operands(const mir_model::MirInstruction & instruction,
                      std::size_t count)
{
  if(instruction.operands.size() != count)
    native_errors::ThrowInternal("invalid MIR operand count for integer encoding");
}

X64Register require_register(const mir_model::MirOperand & operand)
{
  if(operand.kind != mir_model::MirOperand::OP_REG)
    native_errors::ThrowInternal("integer encoder expected a register operand");
  return operand.reg;
}

struct MemoryAddress
{
  X64Register base = XR_RBP;
  X64Register index = XR_RAX;
  unsigned scale = 1;
  long long displacement = 0;
  bool indexed = false;
};

MemoryAddress prepare_memory_address(
    elf_detail::CodeBuffer & out, const mir_model::MirOperand & operand,
    const mir_model::MirFunction & function)
{
  MemoryAddress address;
  if(operand.kind == mir_model::MirOperand::OP_FRAME) {
    address.displacement = actual_frame_offset(function, operand.offset);
  } else if(operand.kind == mir_model::MirOperand::OP_DEREF) {
    address.base = operand.reg;
    address.index = operand.index;
    address.scale = operand.scale;
    address.displacement = operand.offset;
    address.indexed = operand.has_index;
  } else if(operand.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, operand.symbol, operand.address_binding);
    address.base = XR_R11;
  } else {
    native_errors::ThrowInternal("integer memory operand is not address-shaped");
  }
  return address;
}

void emit_register_memory_operation(
    elf_detail::CodeBuffer & out, X64Register destination,
    const mir_model::MirOperand & source, unsigned width,
    unsigned byte_opcode, unsigned wide_opcode,
    const mir_model::MirFunction & function, bool escaped = false)
{
  const MemoryAddress address =
    prepare_memory_address(out, source, function);
  emit_size_prefix(out, width);
  if(address.indexed)
    emit_indexed_rex(
      out, width == 64, destination, address.base, address.index,
      width == 8);
  else emit_rex(
    out, width == 64, destination, address.base, width == 8);
  if(escaped) out.byte(0x0f);
  out.byte(width == 8 ? byte_opcode : wide_opcode);
  if(address.indexed)
    emit_indexed_memory_modrm(out, destination, address.base, address.index,
      address.scale, address.displacement);
  else emit_memory_modrm(
    out, destination, address.base, address.displacement);
}

}  // namespace

void emit_integer_alu(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    unsigned register_opcode, unsigned immediate_extension,
    const mir_model::MirFunction * function, unsigned width)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  const mir_model::MirOperand & source = instruction.operands[1];
  if(source.kind == mir_model::MirOperand::OP_FRAME ||
     source.kind == mir_model::MirOperand::OP_GLOBAL ||
     source.kind == mir_model::MirOperand::OP_DEREF) {
    if(!function)
      native_errors::ThrowInternal("integer memory operation outside function");
    const unsigned memory_width = data_layout::type_width(instruction.type);
    emit_register_memory_operation(out, destination, source, memory_width,
      register_opcode + 1, register_opcode + 2, *function);
  } else if(source.kind == mir_model::MirOperand::OP_REG) {
    emit_size_prefix(out, width);
    emit_rex(out, width == 64, source.reg, destination, width == 8);
    out.byte(width == 8 ? register_opcode - 1 : register_opcode);
    emit_modrm(out, 3, source.reg, destination);
  } else if(source.kind == mir_model::MirOperand::OP_IMM && width <= 32) {
    emit_size_prefix(out, width);
    emit_rex(out, false, XR_RAX, destination, width == 8);
    out.byte(width == 8 ? 0x80 : 0x81);
    emit_modrm(out, 3, immediate_extension, destination);
    out.little(static_cast<std::uint32_t>(source.imm),
               width == 8 ? 1 : width / 8);
  } else if(source.kind == mir_model::MirOperand::OP_IMM &&
            source.imm >= INT32_MIN && source.imm <= INT32_MAX) {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0x81);
    emit_modrm(out, 3, immediate_extension, destination);
    out.little(static_cast<std::uint32_t>(source.imm), 4);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_immediate_move(out, XR_R11, static_cast<std::uint64_t>(source.imm));
    mir_model::MirInstruction register_form = instruction;
    register_form.operands[1].kind = mir_model::MirOperand::OP_REG;
    register_form.operands[1].reg = XR_R11;
    emit_integer_alu(out, register_form, register_opcode,
                     immediate_extension, function, width);
  } else native_errors::ThrowInternal("unsupported native ALU operand");
}

void emit_integer_multiply(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction * function)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  mir_model::MirOperand source = instruction.operands[1];
  if(source.kind == mir_model::MirOperand::OP_FRAME ||
     source.kind == mir_model::MirOperand::OP_GLOBAL ||
     source.kind == mir_model::MirOperand::OP_DEREF) {
    if(!function)
      native_errors::ThrowInternal("integer memory multiply outside function");
    const unsigned width = data_layout::type_width(instruction.type);
    if(width == 8)
      native_errors::ThrowInternal("two-operand memory multiply requires 16+ bits");
    emit_register_memory_operation(
      out, destination, source, width, 0xaf, 0xaf, *function, true);
    return;
  }
  if(source.kind == mir_model::MirOperand::OP_IMM &&
     (source.imm < INT32_MIN || source.imm > INT32_MAX)) {
    emit_immediate_move(out, XR_R11, static_cast<std::uint64_t>(source.imm));
    source.kind = mir_model::MirOperand::OP_REG;
    source.reg = XR_R11;
  }
  if(source.kind == mir_model::MirOperand::OP_REG) {
    emit_rex(out, true, destination, source.reg);
    out.byte(0x0f);
    out.byte(0xaf);
    emit_modrm(out, 3, destination, source.reg);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    // Strength-reduce the common struct strides: powers of two become a
    // shift and {3, 5, 9} times a power of two an lea plus a shift.  The
    // arithmetic is on the full 64-bit register either way, matching the
    // truncating imul.
    if(source.imm > 1) {
      unsigned long long magnitude =
        static_cast<unsigned long long>(source.imm);
      unsigned shift = 0;
      while((magnitude & 1) == 0) {
        magnitude >>= 1;
        ++shift;
      }
      const bool lea_factor =
        magnitude == 3 || magnitude == 5 || magnitude == 9;
      if(magnitude == 1 || lea_factor) {
        if(lea_factor)
          emit_indexed_lea(out, destination, destination, destination,
                           static_cast<unsigned>(magnitude - 1), 0);
        if(shift != 0) {
          emit_rex(out, true, XR_RAX, destination);
          out.byte(shift == 1 ? 0xd1 : 0xc1);
          emit_modrm(out, 3, 4, destination);
          if(shift != 1) out.byte(shift);
        }
        return;
      }
    }
    emit_rex(out, true, destination, destination);
    out.byte(0x69);
    emit_modrm(out, 3, destination, destination);
    out.little(static_cast<std::uint32_t>(source.imm), 4);
  } else native_errors::ThrowInternal("unsupported native multiply operand");
}

void emit_integer_memory_compare(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const mir_model::MirOperand & address_operand = instruction.operands[0];
  const MemoryAddress address =
    prepare_memory_address(out, address_operand, function);
  const unsigned width = data_layout::type_width(instruction.type);
  const mir_model::MirOperand & source = instruction.operands[1];
  emit_size_prefix(out, width);
  if(source.kind == mir_model::MirOperand::OP_REG) {
    if(address.indexed)
      emit_indexed_rex(
        out, width == 64, source.reg, address.base, address.index,
        width == 8);
    else emit_rex(out, width == 64, source.reg, address.base, width == 8);
    out.byte(width == 8 ? 0x38 : 0x39);
    if(address.indexed)
      emit_indexed_memory_modrm(out, source.reg, address.base, address.index,
        address.scale, address.displacement);
    else emit_memory_modrm(
      out, source.reg, address.base, address.displacement);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    if(address.indexed)
      emit_indexed_rex(out, width == 64, XR_RAX,
                       address.base, address.index);
    else emit_rex(out, width == 64, XR_RAX, address.base, width == 8);
    out.byte(width == 8 ? 0x80 : 0x81);
    if(address.indexed)
      emit_indexed_memory_modrm(out, 7, address.base, address.index,
        address.scale, address.displacement);
    else emit_memory_modrm(out, 7, address.base, address.displacement);
    const unsigned immediate_bytes =
      width == 8 ? 1 : (width == 16 ? 2 : 4);
    out.little(static_cast<std::uint64_t>(source.imm), immediate_bytes);
  } else native_errors::ThrowInternal("unsupported memory compare source");
}

void emit_integer_register_memory_compare(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  emit_register_memory_operation(out, destination, instruction.operands[1],
    data_layout::type_width(instruction.type), 0x3a, 0x3b, function);
}

}  // namespace lowir_native
