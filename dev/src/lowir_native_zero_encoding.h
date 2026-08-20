#pragma once

#include "lowir_native_code_buffer.h"
#include "lowir_native_registers.h"

#include <cstddef>

namespace lowir_native {

void emit_zero_bytes(elf_detail::CodeBuffer & out, X64Register destination,
	std::size_t byte_count);

}
