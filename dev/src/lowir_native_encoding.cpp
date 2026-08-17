#include "lowir_native_encoding.h"

#include <cstdint>

namespace lowir_native {

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

}  // namespace lowir_native
