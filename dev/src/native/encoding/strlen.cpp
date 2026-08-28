#include "native/encoding/strlen.h"

#include "native/object/code_buffer.h"
#include "native/encoding/instructions.h"

namespace lowir_native
{
namespace strlen_detail
{

bool emit_prefix16_call(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction)
{
  if(instruction.call_encoding !=
     mir_model::MirInstruction::MCE_STRLEN_PREFIX16) return false;

  const lowir_model::LocalLabelId fallback =
    out.internal_label("builtin_strlen_fallback");
  const lowir_model::LocalLabelId done =
    out.internal_label("builtin_strlen_done");

  // Most compiler strings are tiny. Probe one page-contained SSE2 word
  // before paying the external-call cost, retaining libc as the long and
  // page-edge path.
  out.byte(0x48); out.byte(0x89); out.byte(0xf8);     // mov rax, rdi
  out.byte(0x25); out.little(0xfff, 4);               // and eax, 0xfff
  out.byte(0x3d); out.little(0xff0, 4);               // cmp eax, 0xff0
  emit_condition_jump(out, XC_A, fallback);
  out.byte(0x66); out.byte(0x0f); out.byte(0xef);     // pxor xmm0, xmm0
  out.byte(0xc0);
  out.byte(0xf3); out.byte(0x0f); out.byte(0x6f);     // movdqu xmm1, [rdi]
  out.byte(0x0f);
  out.byte(0x66); out.byte(0x0f); out.byte(0x74);     // pcmpeqb xmm1, xmm0
  out.byte(0xc8);
  out.byte(0x66); out.byte(0x0f); out.byte(0xd7);     // pmovmskb eax, xmm1
  out.byte(0xc1);
  out.byte(0x85); out.byte(0xc0);                     // test eax, eax
  emit_condition_jump(out, XC_E, fallback);
  out.byte(0x0f); out.byte(0xbc); out.byte(0xc0);     // bsf eax, eax
  if(!out.short_relative(0xeb, done)) {
    out.byte(0xe9);
    out.relative32(done);
  }
  out.label(fallback);
  out.byte(0xe8);
  out.relative32(instruction.operands[0].symbol);
  out.label(done);
  return true;
}

}
}
