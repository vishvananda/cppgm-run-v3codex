#include "lowir_native_encoding.h"

#include <climits>
#include <cstdint>

namespace lowir_native {

using elf_detail::CodeBuffer;

void emit_rex(CodeBuffer & out, bool wide, X64Register reg, X64Register rm,
              bool force)
{
  const unsigned value = 0x40 | (wide ? 8 : 0) |
    ((static_cast<unsigned>(reg) >> 3) << 2) |
    (static_cast<unsigned>(rm) >> 3);
  if(value != 0x40 || force) out.byte(value);
}

void emit_modrm(CodeBuffer & out, unsigned mod, unsigned reg, unsigned rm)
{
  out.byte((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

void emit_register_move(CodeBuffer & out, X64Register destination,
                        X64Register source)
{
  emit_rex(out, true, source, destination);
  out.byte(0x89);
  emit_modrm(out, 3, source, destination);
}

void emit_symbol_move(
    CodeBuffer & out, X64Register destination, const std::string & symbol,
    mir_model::MirOperand::AddressBinding address_binding)
{
  // Keep symbol-address intent distinct from calls and stored pointers.  The
  // relocatable writer leaves this RIP-relative LEA for a definition in this
  // object and changes it to a GOT load for an import.
  if(out.relocatable_addresses()) {
    emit_rex(out, true, destination, XR_RBP);
    out.byte(0x8d);
    emit_modrm(out, 0, destination, 5);
    out.address32(symbol, address_binding);
  } else {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0xb8 + (static_cast<unsigned>(destination) & 7));
    out.absolute64(symbol);
  }
}

void emit_tls_address(CodeBuffer & out, X64Register destination,
                      const std::string & symbol)
{
  // PA32's non-PIE host link permits one local-exec TPOFF32 displacement.
  out.byte(0x64);
  emit_rex(out, true, destination, XR_RSP);
  out.byte(0x8b);
  emit_modrm(out, 0, destination, 4);
  out.byte(0x25);
  out.zeros(4);
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0x81);
  emit_modrm(out, 3, 0, destination);
  out.tls_offset32(symbol);
}

void emit_memory_modrm(CodeBuffer & out, unsigned reg, X64Register base,
                       long long displacement)
{
  const unsigned base_code = static_cast<unsigned>(base) & 7;
  unsigned mode = 2;
  unsigned displacement_bytes = 4;
  if(displacement == 0 && base_code != 5) {
    mode = 0;
    displacement_bytes = 0;
  } else if(displacement >= -128 && displacement <= 127) {
    mode = 1;
    displacement_bytes = 1;
  }
  emit_modrm(out, mode, reg, base);
  if(base_code == 4)
    out.byte((0 << 6) | (4 << 3) | base_code);
  if(displacement_bytes)
    out.little(static_cast<std::uint32_t>(displacement), displacement_bytes);
}

void emit_size_prefix(CodeBuffer & out, unsigned width)
{
  if(width == 16) out.byte(0x66);
}

void emit_sized_register_move(CodeBuffer & out, X64Register destination,
                              X64Register source, unsigned width)
{
  if(width == 64) {
    emit_register_move(out, destination, source);
    return;
  }
  emit_size_prefix(out, width);
  emit_rex(out, false, source, destination, width == 8);
  out.byte(width == 8 ? 0x88 : 0x89);
  emit_modrm(out, 3, source, destination);
}

void emit_load(CodeBuffer & out, X64Register destination, X64Register base,
               long long displacement, unsigned width)
{
  emit_size_prefix(out, width);
  emit_rex(out, width == 64, destination, base, width == 8);
  out.byte(width == 8 ? 0x8a : 0x8b);
  emit_memory_modrm(out, destination, base, displacement);
}

void emit_store(CodeBuffer & out, X64Register base, long long displacement,
                X64Register source, unsigned width)
{
  emit_size_prefix(out, width);
  emit_rex(out, width == 64, source, base, width == 8);
  out.byte(width == 8 ? 0x88 : 0x89);
  emit_memory_modrm(out, source, base, displacement);
}

void emit_lea(CodeBuffer & out, X64Register destination, X64Register base,
              long long displacement)
{
  emit_rex(out, true, destination, base);
  out.byte(0x8d);
  emit_memory_modrm(out, destination, base, displacement);
}

void emit_push(CodeBuffer & out, X64Register reg)
{
  if(reg >= XR_R8) out.byte(0x41);
  out.byte(0x50 + (static_cast<unsigned>(reg) & 7));
}

void emit_pop(CodeBuffer & out, X64Register reg)
{
  if(reg >= XR_R8) out.byte(0x41);
  out.byte(0x58 + (static_cast<unsigned>(reg) & 7));
}

void emit_stack_adjust(CodeBuffer & out, bool subtract, unsigned bytes)
{
  if(!bytes) return;
  out.byte(0x48);
  out.byte(bytes <= 127 ? 0x83 : 0x81);
  emit_modrm(out, 3, subtract ? 5 : 0, XR_RSP);
  if(bytes <= 127) out.byte(bytes);
  else out.little(bytes, 4);
}

void emit_immediate_move(elf_detail::CodeBuffer & out,
                         X64Register destination,
                         std::uint64_t value)
{
  const unsigned reg = static_cast<unsigned>(destination);
  if(value <= UINT64_C(0xffffffff)) {
    if(reg >= 8) out.byte(0x41);
    out.byte(0xb8 + (reg & 7));
    out.little(value, 4);
    return;
  }
  if(value >= UINT64_C(0xffffffff80000000)) {
    out.byte(0x48 | (reg >> 3));
    out.byte(0xc7);
    out.byte(0xc0 | (reg & 7));
    out.little(value, 4);
    return;
  }
  out.byte(0x48 | (reg >> 3));
  out.byte(0xb8 + (reg & 7));
  out.little(value, 8);
}

void emit_register_alu(CodeBuffer & out, unsigned opcode,
                       X64Register destination, X64Register source)
{
  emit_rex(out, true, source, destination);
  out.byte(opcode);
  emit_modrm(out, 3, source, destination);
}

void emit_condition_jump(CodeBuffer & out, X86Condition condition,
                         const std::string & target)
{
  if(out.short_relative(
       0x70 + static_cast<unsigned>(condition), target)) return;
  out.byte(0x0f);
  out.byte(0x80 + static_cast<unsigned>(condition));
  out.relative32(target);
}

namespace {

bool is_register_move(const mir_model::MirInstruction & instruction,
                      X64Register destination, X64Register source)
{
  return instruction.opcode == mir_model::MirInstruction::MI_MOV &&
    instruction.operands.size() == 2 &&
    instruction.operands[0].kind == mir_model::MirOperand::OP_REG &&
    instruction.operands[0].reg == destination &&
    instruction.operands[1].kind == mir_model::MirOperand::OP_REG &&
    instruction.operands[1].reg == source;
}

bool is_immediate_move(const mir_model::MirInstruction & instruction,
                       X64Register destination, long long * value)
{
  if(instruction.opcode != mir_model::MirInstruction::MI_MOV ||
     instruction.operands.size() != 2 ||
     instruction.operands[0].kind != mir_model::MirOperand::OP_REG ||
     instruction.operands[0].reg != destination ||
     instruction.operands[1].kind != mir_model::MirOperand::OP_IMM)
    return false;
  *value = instruction.operands[1].imm;
  return true;
}

void emit_immediate_shift(CodeBuffer & out, X64Register destination,
                          unsigned extension, unsigned amount)
{
  if(!amount) return;
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0xc1);
  emit_modrm(out, 3, extension, destination);
  out.byte(amount);
}

void emit_register_negate(CodeBuffer & out, X64Register destination)
{
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0xf7);
  emit_modrm(out, 3, 3, destination);
}

void emit_immediate_and(CodeBuffer & out, X64Register destination,
                        std::uint64_t mask, X64Register scratch)
{
  if(mask <= 127) {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0x83);
    emit_modrm(out, 3, 4, destination);
    out.byte(static_cast<unsigned>(mask));
    return;
  }
  if(mask <= static_cast<std::uint64_t>(INT32_MAX)) {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0x81);
    emit_modrm(out, 3, 4, destination);
    out.little(mask, 4);
    return;
  }
  emit_immediate_move(out, scratch, mask);
  emit_register_alu(out, 0x21, destination, scratch);
}

}  // namespace

std::size_t emit_power_of_two_division(
    CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(start > instructions.size() || instructions.size() - start < 5)
    return 0;
  long long signed_divisor = 0;
  if(!is_immediate_move(instructions[start], XR_RDX, &signed_divisor) ||
     !is_register_move(instructions[start + 1], XR_RCX, XR_RDX))
    return 0;
  std::size_t cursor = start + 2;
  X64Register dividend = XR_RAX;
  if(cursor < instructions.size() &&
     instructions[cursor].opcode == MirInstruction::MI_MOV &&
     instructions[cursor].operands.size() == 2 &&
     instructions[cursor].operands[0].kind == MirOperand::OP_REG &&
     instructions[cursor].operands[0].reg == XR_RAX &&
     instructions[cursor].operands[1].kind == MirOperand::OP_REG) {
    dividend = instructions[cursor].operands[1].reg;
    ++cursor;
  }
  bool unsigned_operation = false;
  long long high_word = -1;
  if(cursor < instructions.size() &&
     is_immediate_move(instructions[cursor], XR_RDX, &high_word) &&
     high_word == 0) {
    unsigned_operation = true;
  } else if(cursor >= instructions.size() ||
            instructions[cursor].opcode != MirInstruction::MI_CQO ||
            !instructions[cursor].operands.empty()) {
    return 0;
  }
  ++cursor;
  if(cursor >= instructions.size() ||
     instructions[cursor].opcode != (unsigned_operation ?
       MirInstruction::MI_DIV : MirInstruction::MI_IDIV) ||
     instructions[cursor].operands.size() != 1 ||
     instructions[cursor].operands[0].kind != MirOperand::OP_REG ||
     instructions[cursor].operands[0].reg != XR_RCX)
    return 0;
  ++cursor;
  if(cursor >= instructions.size() ||
     instructions[cursor].opcode != MirInstruction::MI_MOV ||
     instructions[cursor].operands.size() != 2 ||
     instructions[cursor].operands[0].kind != MirOperand::OP_REG ||
     instructions[cursor].operands[1].kind != MirOperand::OP_REG ||
     instructions[cursor].operands[0].reg != dividend)
    return 0;
  const X64Register result_source = instructions[cursor].operands[1].reg;
  const bool remainder = result_source == XR_RDX;
  if(!remainder && result_source != XR_RAX) return 0;

  std::uint64_t divisor = static_cast<std::uint64_t>(signed_divisor);
  bool negate_quotient = false;
  if(unsigned_operation) {
    if(!divisor || (divisor & (divisor - 1))) return 0;
  } else {
    if(!signed_divisor) return 0;
    negate_quotient = signed_divisor < 0;
    if(negate_quotient) divisor = UINT64_C(0) - divisor;
    if((divisor & (divisor - 1)) || (negate_quotient && divisor == 1))
      return 0;
  }

  unsigned shift = 0;
  for(std::uint64_t value = divisor; value > 1; value >>= 1) ++shift;
  if(unsigned_operation) {
    if(remainder)
      emit_immediate_and(out, dividend, divisor - 1, XR_R11);
    else
      emit_immediate_shift(out, dividend, 5, shift);
  } else if(divisor == 1) {
    if(remainder) emit_register_alu(out, 0x31, dividend, dividend);
  } else {
    emit_register_move(out, XR_R11, dividend);
    emit_immediate_shift(out, XR_R11, 7, 63);
    emit_immediate_and(out, XR_R11, divisor - 1, XR_R10);
    emit_register_alu(out, 0x01, dividend, XR_R11);
    if(remainder) {
      emit_immediate_and(out, dividend, divisor - 1, XR_R10);
      emit_register_alu(out, 0x29, dividend, XR_R11);
    } else {
      emit_immediate_shift(out, dividend, 7, shift);
      if(negate_quotient) emit_register_negate(out, dividend);
    }
  }
  return cursor - start + 1;
}

void emit_i128_shift(CodeBuffer & out,
                     mir_model::MirInstruction::Opcode opcode)
{
  const std::string high = out.internal_label("i128_shift_high");
  const std::string done = out.internal_label("i128_shift_done");
  // cmp rcx, 64; jae high
  out.byte(0x48); out.byte(0x83); out.byte(0xf9); out.byte(0x40);
  out.byte(0x0f); out.byte(0x83); out.relative32(high);
  if(opcode == mir_model::MirInstruction::MI_I128_SHL) {
    // shld rdx, rax, cl; shl rax, cl
    out.byte(0x48); out.byte(0x0f); out.byte(0xa5); out.byte(0xc2);
    out.byte(0x48); out.byte(0xd3); out.byte(0xe0);
  } else {
    // shrd rax, rdx, cl; shr/sar rdx, cl
    out.byte(0x48); out.byte(0x0f); out.byte(0xad); out.byte(0xd0);
    out.byte(0x48); out.byte(0xd3);
    out.byte(opcode == mir_model::MirInstruction::MI_I128_SAR ? 0xfa : 0xea);
  }
  out.byte(0xe9); out.relative32(done);
  out.label(high);
  if(opcode == mir_model::MirInstruction::MI_I128_SHL) {
    emit_register_move(out, XR_RDX, XR_RAX);
    out.byte(0x48); out.byte(0xd3); out.byte(0xe2); // shl rdx, cl
    out.byte(0x48); out.byte(0x31); out.byte(0xc0); // xor rax, rax
  } else {
    emit_register_move(out, XR_RAX, XR_RDX);
    out.byte(0x48); out.byte(0xd3);
    out.byte(opcode == mir_model::MirInstruction::MI_I128_SAR ? 0xf8 : 0xe8);
    if(opcode == mir_model::MirInstruction::MI_I128_SAR) {
      out.byte(0x48); out.byte(0xc1); out.byte(0xfa); out.byte(0x3f);
    } else {
      out.byte(0x48); out.byte(0x31); out.byte(0xd2); // xor rdx, rdx
    }
  }
  out.label(done);
}

namespace {

void emit_i128_abs(CodeBuffer & out, X64Register low, X64Register high,
                   X64Register mask)
{
  emit_register_alu(out, 0x31, low, mask);   // xor low, mask
  emit_register_alu(out, 0x31, high, mask);  // xor high, mask
  emit_register_alu(out, 0x29, low, mask);   // sub low, mask
  emit_register_alu(out, 0x19, high, mask);  // sbb high, mask
}

}  // namespace

void emit_i128_division(CodeBuffer & out,
                        mir_model::MirInstruction::Opcode opcode)
{
  const bool signed_division = opcode == mir_model::MirInstruction::MI_I128_SDIV ||
    opcode == mir_model::MirInstruction::MI_I128_SMOD;
  const bool remainder = opcode == mir_model::MirInstruction::MI_I128_UMOD ||
    opcode == mir_model::MirInstruction::MI_I128_SMOD;
  const unsigned scratch_bytes = signed_division ? 32 : 16;
  emit_stack_adjust(out, true, scratch_bytes);

  if(signed_division) {
    emit_register_move(out, XR_RDI, XR_RDX);
    out.byte(0x48); out.byte(0xc1); out.byte(0xff); out.byte(0x3f); // sar rdi,63
    emit_store(out, XR_RSP, 16, XR_RDI, 64);
    emit_register_move(out, XR_R10, XR_RSI);
    out.byte(0x49); out.byte(0xc1); out.byte(0xfa); out.byte(0x3f); // sar r10,63
    emit_register_move(out, XR_R11, XR_RDI);
    emit_register_alu(out, 0x31, XR_R11, XR_R10);
    emit_store(out, XR_RSP, 24, XR_R11, 64);
    emit_i128_abs(out, XR_RAX, XR_RDX, XR_RDI);
    emit_i128_abs(out, XR_RCX, XR_RSI, XR_R10);
  }

  emit_store(out, XR_RSP, 0, XR_RCX, 64);
  emit_store(out, XR_RSP, 8, XR_RSI, 64);
  emit_register_alu(out, 0x31, XR_R10, XR_R10); // xor remainder low
  emit_register_alu(out, 0x31, XR_R11, XR_R11); // xor remainder high
  emit_immediate_move(out, XR_RCX, 128);

  const std::string loop = out.internal_label("i128_div_loop");
  const std::string subtract = out.internal_label("i128_div_subtract");
  const std::string skip = out.internal_label("i128_div_skip");
  out.label(loop);
  out.byte(0x48); out.byte(0xd1); out.byte(0xe0); // shl rax,1
  out.byte(0x48); out.byte(0xd1); out.byte(0xd2); // rcl rdx,1
  out.byte(0x49); out.byte(0xd1); out.byte(0xd2); // rcl r10,1
  out.byte(0x49); out.byte(0xd1); out.byte(0xd3); // rcl r11,1

  emit_load(out, XR_RDI, XR_RSP, 8, 64);
  emit_register_alu(out, 0x39, XR_R11, XR_RDI);
  emit_condition_jump(out, XC_B, skip);
  emit_condition_jump(out, XC_A, subtract);
  emit_load(out, XR_RDI, XR_RSP, 0, 64);
  emit_register_alu(out, 0x39, XR_R10, XR_RDI);
  emit_condition_jump(out, XC_B, skip);

  out.label(subtract);
  emit_load(out, XR_RDI, XR_RSP, 0, 64);
  emit_register_alu(out, 0x29, XR_R10, XR_RDI);
  emit_load(out, XR_RDI, XR_RSP, 8, 64);
  emit_register_alu(out, 0x19, XR_R11, XR_RDI);
  out.byte(0x48); out.byte(0x83); out.byte(0xc8); out.byte(0x01); // or rax,1

  out.label(skip);
  out.byte(0x48); out.byte(0xff); out.byte(0xc9); // dec rcx
  emit_condition_jump(out, XC_NE, loop);

  if(signed_division) {
    emit_load(out, XR_RDI, XR_RSP, 24, 64);
    emit_i128_abs(out, XR_RAX, XR_RDX, XR_RDI);
    emit_load(out, XR_RDI, XR_RSP, 16, 64);
    emit_i128_abs(out, XR_R10, XR_R11, XR_RDI);
  }
  if(remainder) {
    emit_register_move(out, XR_RAX, XR_R10);
    emit_register_move(out, XR_RDX, XR_R11);
  }
  emit_stack_adjust(out, false, scratch_bytes);
}

}  // namespace lowir_native
