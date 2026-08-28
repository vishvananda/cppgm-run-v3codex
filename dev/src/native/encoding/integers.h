#pragma once

#include "native/object/code_buffer.h"

namespace lowir_native {

void emit_integer_alu(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    unsigned register_opcode, unsigned immediate_extension,
    const mir_model::MirFunction * function, unsigned width = 64);
void emit_integer_multiply(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction * function);
void emit_integer_memory_compare(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction & function);
void emit_integer_register_memory_compare(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction & function);

}  // namespace lowir_native
