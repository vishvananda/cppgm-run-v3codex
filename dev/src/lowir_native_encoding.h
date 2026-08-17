#pragma once

#include "lowir_native_code_buffer.h"
#include "lowir_native_registers.h"

#include <cstdint>
#include <string>

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
void emit_tls_address(elf_detail::CodeBuffer & out,
                      X64Register destination, const std::string & symbol);
void emit_memory_modrm(elf_detail::CodeBuffer & out, unsigned reg,
                       X64Register base, long long displacement);
void emit_size_prefix(elf_detail::CodeBuffer & out, unsigned width);
void emit_sized_register_move(elf_detail::CodeBuffer & out,
                              X64Register destination, X64Register source,
                              unsigned width);
void emit_load(elf_detail::CodeBuffer & out, X64Register destination,
               X64Register base, long long displacement, unsigned width);
void emit_store(elf_detail::CodeBuffer & out, X64Register base,
                long long displacement, X64Register source, unsigned width);
void emit_lea(elf_detail::CodeBuffer & out, X64Register destination,
              X64Register base, long long displacement);
void emit_push(elf_detail::CodeBuffer & out, X64Register reg);
void emit_pop(elf_detail::CodeBuffer & out, X64Register reg);
void emit_stack_adjust(elf_detail::CodeBuffer & out, bool subtract,
                       unsigned bytes);
void emit_register_alu(elf_detail::CodeBuffer & out, unsigned opcode,
                       X64Register destination, X64Register source);
void emit_condition_jump(elf_detail::CodeBuffer & out,
                         X86Condition condition,
                         const std::string & target);
void emit_i128_shift(elf_detail::CodeBuffer & out,
                     mir_model::MirInstruction::Opcode opcode);
void emit_i128_division(elf_detail::CodeBuffer & out,
                        mir_model::MirInstruction::Opcode opcode);

}  // namespace lowir_native
