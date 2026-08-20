#pragma once

#include "lowir_native_code_buffer.h"
#include "lowir_native_registers.h"

#include <cstdint>

namespace lowir_native {

long long actual_frame_offset(const mir_model::MirFunction & function,
                              long long abstract_offset);
void emit_address_load(elf_detail::CodeBuffer & out,
                       X64Register destination,
                       const mir_model::MirOperand & address,
                       unsigned width,
                       const mir_model::MirFunction & function);
void emit_address_normalized_load(
    elf_detail::CodeBuffer & out, X64Register destination,
    const mir_model::MirOperand & address, const lowir_model::LowType & type,
    const mir_model::MirFunction & function);
void emit_normalized_register_move(
    elf_detail::CodeBuffer & out, X64Register destination,
    X64Register source, const lowir_model::LowType & type);
void emit_address_store(elf_detail::CodeBuffer & out,
                        const mir_model::MirOperand & address,
                        X64Register source, unsigned width,
                        const mir_model::MirFunction & function);
void emit_address_immediate_store(
    elf_detail::CodeBuffer & out, const mir_model::MirOperand & address,
    std::uint64_t value, unsigned width,
    const mir_model::MirFunction & function);

}  // namespace lowir_native
