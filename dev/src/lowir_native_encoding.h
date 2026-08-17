#pragma once

#include "lowir_native_code_buffer.h"
#include "lowir_native_registers.h"

#include <cstdint>

namespace lowir_native {

void emit_immediate_move(elf_detail::CodeBuffer & out,
                         X64Register destination,
                         std::uint64_t value);

}  // namespace lowir_native
