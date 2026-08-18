#pragma once

#include "lowir_native_code_buffer.h"
#include "lowir_native_registers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lowir_native {

void emit_immediate_move(elf_detail::CodeBuffer & out,
                         X64Register destination,
                         std::uint64_t value);
void emit_rex(elf_detail::CodeBuffer & out, bool wide, X64Register reg,
              X64Register rm, bool force = false);
void emit_modrm(elf_detail::CodeBuffer & out, unsigned mod, unsigned reg,
                unsigned rm);
void emit_register_move(elf_detail::CodeBuffer & out,
                        X64Register destination, X64Register source);
void emit_symbol_move(
    elf_detail::CodeBuffer & out, X64Register destination,
    const std::string & symbol,
    mir_model::MirOperand::AddressBinding address_binding =
      mir_model::MirOperand::ADDRESS_LOCAL);
void emit_symbol_move(elf_detail::CodeBuffer & out,
                      X64Register destination,
                      lowir_model::LocalLabelId label);
void emit_tls_address(elf_detail::CodeBuffer & out,
                      X64Register destination, const std::string & symbol);
void emit_memory_modrm(elf_detail::CodeBuffer & out, unsigned reg,
                       X64Register base, long long displacement);
void emit_indexed_memory_modrm(elf_detail::CodeBuffer & out, unsigned reg,
                               X64Register base, X64Register index,
                               unsigned scale, long long displacement);
void emit_size_prefix(elf_detail::CodeBuffer & out, unsigned width);
void emit_sized_register_move(elf_detail::CodeBuffer & out,
                              X64Register destination, X64Register source,
                              unsigned width);
void emit_integer_extension(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction, bool sign_extend);
void emit_move_zero_extended_byte(elf_detail::CodeBuffer & out,
                                  X64Register destination,
                                  X64Register source);
inline void emit_set_condition(elf_detail::CodeBuffer & out,
                               X86Condition condition,
                               X64Register destination)
{
  emit_rex(out, false, XR_RAX, destination, destination >= XR_RSP);
  out.byte(0x0f);
  out.byte(0x90 + static_cast<unsigned>(condition));
  emit_modrm(out, 3, 0, destination);
}
void emit_test_register(elf_detail::CodeBuffer & out, X64Register reg,
                        unsigned width = 64);
void emit_load(elf_detail::CodeBuffer & out, X64Register destination,
               X64Register base, long long displacement, unsigned width);
void emit_indexed_load(elf_detail::CodeBuffer & out, X64Register destination,
                       X64Register base, X64Register index, unsigned scale,
                       long long displacement, unsigned width);
void emit_store(elf_detail::CodeBuffer & out, X64Register base,
                long long displacement, X64Register source, unsigned width);
void emit_indexed_store(elf_detail::CodeBuffer & out, X64Register base,
                        X64Register index, unsigned scale,
                        long long displacement, X64Register source,
                        unsigned width);
void emit_lea(elf_detail::CodeBuffer & out, X64Register destination,
              X64Register base, long long displacement);
void emit_indexed_lea(elf_detail::CodeBuffer & out, X64Register destination,
                      X64Register base, X64Register index, unsigned scale,
                      long long displacement);
void emit_push(elf_detail::CodeBuffer & out, X64Register reg);
void emit_pop(elf_detail::CodeBuffer & out, X64Register reg);
void emit_stack_adjust(elf_detail::CodeBuffer & out, bool subtract,
                       unsigned bytes);
void emit_register_alu(elf_detail::CodeBuffer & out, unsigned opcode,
                       X64Register destination, X64Register source);
void emit_condition_jump(elf_detail::CodeBuffer & out,
                         X86Condition condition,
                         const std::string & target);
void emit_condition_jump(elf_detail::CodeBuffer & out,
                         X86Condition condition,
                         lowir_model::LocalLabelId target);
std::size_t emit_constant_division(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start);
std::vector<bool> condition_flags_live_before(
    const std::vector<mir_model::MirInstruction> & instructions);
bool emit_flag_safe_zero_move(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction, bool flags_live);
bool is_redundant_u32_normalization(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, bool frame_load_zero_extends = false);
std::size_t emit_fused_u32_register_move(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start);
void emit_i128_shift(elf_detail::CodeBuffer & out,
                     mir_model::MirInstruction::Opcode opcode);
void emit_i128_division(elf_detail::CodeBuffer & out,
                        mir_model::MirInstruction::Opcode opcode);

}  // namespace lowir_native
