#include "lowir_native_encoding.h"
#include "lowir_native_data_layout.h"

#include <climits>
#include <cstdint>
#include <stdexcept>

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

void emit_integer_extension(
    CodeBuffer & out, const mir_model::MirInstruction & instruction,
    bool sign_extend)
{
  if(instruction.operands.size() != 1 ||
     instruction.operands[0].kind != mir_model::MirOperand::OP_REG)
    throw std::logic_error("invalid integer-extension operands");
  const X64Register reg = instruction.operands[0].reg;
  const unsigned width = data_layout::type_width(instruction.type);
  if(width == 64) return;
  if(!sign_extend && width == 32) {
    emit_rex(out, false, reg, reg);
    out.byte(0x89);
    emit_modrm(out, 3, reg, reg);
    return;
  }
  emit_rex(out, sign_extend, reg, reg,
           !sign_extend && width == 8 && reg >= XR_RSP && reg < XR_R8);
  if(sign_extend && width == 32) {
    out.byte(0x63);
  } else {
    out.byte(0x0f);
    out.byte(sign_extend ? (width == 8 ? 0xbe : 0xbf) :
                           (width == 8 ? 0xb6 : 0xb7));
  }
  emit_modrm(out, 3, reg, reg);
}

void emit_move_zero_extended_byte(CodeBuffer & out, X64Register destination,
                                  X64Register source)
{
  emit_rex(out, false, destination, source,
           source >= XR_RSP && source < XR_R8);
  out.byte(0x0f);
  out.byte(0xb6);
  emit_modrm(out, 3, destination, source);
}

void emit_test_register(CodeBuffer & out, X64Register reg, unsigned width)
{
  emit_size_prefix(out, width);
  emit_rex(out, width == 64, reg, reg, width == 8);
  out.byte(width == 8 ? 0x84 : 0x85);
  emit_modrm(out, 3, reg, reg);
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

struct SignedDivisionMagic
{
  std::uint64_t multiplier;
  unsigned shift;
};

SignedDivisionMagic signed_division_magic(long long divisor)
{
  const std::uint64_t divisor_bits =
    static_cast<std::uint64_t>(divisor);
  const std::uint64_t absolute_divisor = divisor < 0 ?
    UINT64_C(0) - divisor_bits : divisor_bits;
  const std::uint64_t signed_minimum = UINT64_C(1) << 63;
  const std::uint64_t adjusted_minimum =
    signed_minimum + (divisor_bits >> 63);
  const std::uint64_t negative_anc = adjusted_minimum - 1 -
    adjusted_minimum % absolute_divisor;

  unsigned exponent = 63;
  std::uint64_t quotient1 = signed_minimum / negative_anc;
  std::uint64_t remainder1 =
    signed_minimum - quotient1 * negative_anc;
  std::uint64_t quotient2 = signed_minimum / absolute_divisor;
  std::uint64_t remainder2 =
    signed_minimum - quotient2 * absolute_divisor;
  std::uint64_t delta = 0;
  do {
    ++exponent;
    quotient1 <<= 1;
    remainder1 <<= 1;
    if(remainder1 >= negative_anc) {
      ++quotient1;
      remainder1 -= negative_anc;
    }
    quotient2 <<= 1;
    remainder2 <<= 1;
    if(remainder2 >= absolute_divisor) {
      ++quotient2;
      remainder2 -= absolute_divisor;
    }
    delta = absolute_divisor - remainder2;
  } while(quotient1 < delta ||
          (quotient1 == delta && remainder1 == 0));

  std::uint64_t multiplier = quotient2 + 1;
  if(divisor < 0) multiplier = UINT64_C(0) - multiplier;
  SignedDivisionMagic result = { multiplier, exponent - 64 };
  return result;
}

void emit_signed_multiply_high(CodeBuffer & out, X64Register source,
                               std::uint64_t multiplier)
{
  emit_register_move(out, XR_RAX, source);
  emit_immediate_move(out, XR_R10, multiplier);
  emit_rex(out, true, XR_RAX, XR_R10);
  out.byte(0xf7);
  emit_modrm(out, 3, 5, XR_R10);
}

bool preserves_condition_flags(mir_model::MirInstruction::Opcode opcode)
{
  using mir_model::MirInstruction;
  switch(opcode) {
  case MirInstruction::MI_MOV:
  case MirInstruction::MI_LOAD:
  case MirInstruction::MI_STORE:
  case MirInstruction::MI_MFENCE:
  case MirInstruction::MI_XCHG:
  case MirInstruction::MI_LEA:
  case MirInstruction::MI_FMOV:
  case MirInstruction::MI_FNEG:
  case MirInstruction::MI_FADD:
  case MirInstruction::MI_FSUB:
  case MirInstruction::MI_FMUL:
  case MirInstruction::MI_FDIV:
  case MirInstruction::MI_FSTP:
  case MirInstruction::MI_FPOP:
  case MirInstruction::MI_SITOFP:
  case MirInstruction::MI_UITOFP:
  case MirInstruction::MI_FPTOSI:
  case MirInstruction::MI_FPTOUI:
  case MirInstruction::MI_FPEXT:
  case MirInstruction::MI_FPTRUNC:
  case MirInstruction::MI_NOT:
  case MirInstruction::MI_BSWAP:
  case MirInstruction::MI_MOVZX:
  case MirInstruction::MI_SEXT:
  case MirInstruction::MI_ZEXT:
  case MirInstruction::MI_CQO:
  case MirInstruction::MI_COPY_BYTES:
  case MirInstruction::MI_ZERO_BYTES:
  case MirInstruction::MI_EH_FILTER:
  case MirInstruction::MI_EH_CLEANUP_CLAUSE:
    return true;
  default:
    return false;
  }
}

}  // namespace

std::vector<bool> condition_flags_live_before(
    const std::vector<mir_model::MirInstruction> & instructions)
{
  using mir_model::MirInstruction;
  std::vector<bool> result(instructions.size(), false);
  bool live = false;
  for(std::size_t i = 0; i < instructions.size(); ++i) {
    result[i] = live;
    const MirInstruction::Opcode opcode = instructions[i].opcode;
    if(opcode == MirInstruction::MI_CMP ||
       opcode == MirInstruction::MI_TEST ||
       opcode == MirInstruction::MI_FCMP) {
      live = true;
    } else if(opcode == MirInstruction::MI_JCC ||
              opcode == MirInstruction::MI_JNE ||
              opcode == MirInstruction::MI_SETCC ||
              !preserves_condition_flags(opcode)) {
      live = false;
    }
  }
  return result;
}

bool emit_flag_safe_zero_move(
    CodeBuffer & out, const mir_model::MirInstruction & instruction,
    bool flags_live)
{
  if(flags_live || instruction.opcode != mir_model::MirInstruction::MI_MOV ||
     instruction.operands.size() != 2 ||
     instruction.operands[0].kind != mir_model::MirOperand::OP_REG ||
     instruction.operands[1].kind != mir_model::MirOperand::OP_IMM ||
     instruction.operands[1].imm != 0)
    return false;
  const X64Register destination = instruction.operands[0].reg;
  emit_rex(out, false, destination, destination);
  out.byte(0x31);
  emit_modrm(out, 3, destination, destination);
  return true;
}

bool is_redundant_u32_normalization(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, bool frame_load_zero_extends)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(start == 0 || start >= instructions.size()) return false;
  const MirInstruction & normalization = instructions[start];
  if(normalization.opcode != MirInstruction::MI_ZEXT ||
     normalization.operands.size() != 1 ||
     normalization.operands[0].kind != MirOperand::OP_REG ||
     data_layout::type_width(normalization.type) != 32)
    return false;

  const X64Register reg = normalization.operands[0].reg;
  const MirInstruction & producer = instructions[start - 1];
  if(producer.operands.empty() ||
     producer.operands[0].kind != MirOperand::OP_REG ||
     producer.operands[0].reg != reg)
    return false;

  if(producer.opcode == MirInstruction::MI_LOAD &&
     producer.operands.size() == 2)
    return data_layout::type_width(producer.type) == 32 &&
      (producer.operands[1].kind != MirOperand::OP_FRAME ||
       frame_load_zero_extends);
  if(producer.opcode == MirInstruction::MI_MOVZX)
    return producer.operands.size() == 2;
  if((producer.opcode == MirInstruction::MI_ZEXT ||
      producer.opcode == MirInstruction::MI_BSWAP) &&
     data_layout::type_width(producer.type) == 32)
    return true;
  if(producer.opcode != MirInstruction::MI_MOV ||
     producer.operands.size() != 2 ||
     producer.operands[1].kind != MirOperand::OP_IMM)
    return false;
  return static_cast<std::uint64_t>(producer.operands[1].imm) <=
    UINT64_C(0xffffffff);
}

std::size_t emit_fused_u32_register_move(
    CodeBuffer & out, const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(start > instructions.size() || instructions.size() - start < 2)
    return 0;
  const MirInstruction & move = instructions[start];
  const MirInstruction & normalization = instructions[start + 1];
  if(move.opcode != MirInstruction::MI_MOV || move.operands.size() != 2 ||
     move.operands[0].kind != MirOperand::OP_REG ||
     move.operands[1].kind != MirOperand::OP_REG ||
     normalization.opcode != MirInstruction::MI_ZEXT ||
     normalization.operands.size() != 1 ||
     normalization.operands[0].kind != MirOperand::OP_REG ||
     normalization.operands[0].reg != move.operands[0].reg ||
     data_layout::type_width(normalization.type) != 32) return 0;
  emit_sized_register_move(out, move.operands[0].reg,
    move.operands[1].reg, 32);
  return 2;
}

std::size_t emit_constant_division(
    CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(start > instructions.size() || instructions.size() - start < 4)
    return 0;
  long long signed_divisor = 0;
  std::size_t cursor = start;
  if(is_immediate_move(instructions[cursor], XR_RCX, &signed_divisor)) {
    ++cursor;
  } else if(is_immediate_move(
              instructions[cursor], XR_RDX, &signed_divisor) &&
            cursor + 1 < instructions.size() &&
            is_register_move(instructions[cursor + 1], XR_RCX, XR_RDX)) {
    cursor += 2;
  } else {
    return 0;
  }
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
     instructions[cursor].operands[1].kind != MirOperand::OP_REG)
    return 0;
  const X64Register destination = instructions[cursor].operands[0].reg;
  const X64Register result_source = instructions[cursor].operands[1].reg;
  const bool remainder = result_source == XR_RDX;
  if(!remainder && result_source != XR_RAX) return 0;

  std::uint64_t divisor = static_cast<std::uint64_t>(signed_divisor);
  bool negate_quotient = false;
  if(!signed_divisor) return 0;
  if(!unsigned_operation) {
    negate_quotient = signed_divisor < 0;
    if(negate_quotient) divisor = UINT64_C(0) - divisor;
  }

  const bool power_of_two = !(divisor & (divisor - 1));
  if(!power_of_two) {
    if(unsigned_operation || remainder) return 0;
    const SignedDivisionMagic magic =
      signed_division_magic(signed_divisor);
    emit_register_move(out, XR_R11, dividend);
    emit_signed_multiply_high(out, dividend, magic.multiplier);
    emit_register_move(out, result_source, XR_RDX);
    const bool negative_magic = (magic.multiplier >> 63) != 0;
    if(signed_divisor > 0 && negative_magic)
      emit_register_alu(out, 0x01, result_source, XR_R11);
    else if(signed_divisor < 0 && !negative_magic)
      emit_register_alu(out, 0x29, result_source, XR_R11);
    emit_immediate_shift(out, result_source, 7, magic.shift);
    if(signed_divisor > 0) {
      emit_immediate_shift(out, XR_R11, 7, 63);
      emit_register_alu(out, 0x29, result_source, XR_R11);
    } else {
      emit_register_move(out, XR_R10, result_source);
      emit_immediate_shift(out, XR_R10, 5, 63);
      emit_register_alu(out, 0x01, result_source, XR_R10);
    }
    if(destination != result_source)
      emit_register_move(out, destination, result_source);
    return cursor - start + 1;
  }
  if(!unsigned_operation && negate_quotient && divisor == 1) return 0;

  unsigned shift = 0;
  for(std::uint64_t value = divisor; value > 1; value >>= 1) ++shift;
  if(unsigned_operation) {
    if(!remainder || divisor != 1) {
      if(result_source != dividend)
        emit_register_move(out, result_source, dividend);
    }
    if(remainder)
      emit_immediate_and(out, result_source, divisor - 1, XR_R11);
    else
      emit_immediate_shift(out, result_source, 5, shift);
  } else if(divisor == 1) {
    if(remainder)
      emit_register_alu(out, 0x31, result_source, result_source);
    else if(result_source != dividend)
      emit_register_move(out, result_source, dividend);
  } else {
    emit_register_move(out, XR_R11, dividend);
    if(result_source != dividend)
      emit_register_move(out, result_source, dividend);
    emit_immediate_shift(out, XR_R11, 7, 63);
    emit_immediate_and(out, XR_R11, divisor - 1, XR_R10);
    emit_register_alu(out, 0x01, result_source, XR_R11);
    if(remainder) {
      emit_immediate_and(out, result_source, divisor - 1, XR_R10);
      emit_register_alu(out, 0x29, result_source, XR_R11);
    } else {
      emit_immediate_shift(out, result_source, 7, shift);
      if(negate_quotient) emit_register_negate(out, result_source);
    }
  }
  if(destination != result_source)
    emit_register_move(out, destination, result_source);
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
