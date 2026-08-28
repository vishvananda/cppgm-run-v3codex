#pragma once

#include "native/object/code_buffer.h"
#include "native/mir/model.h"

namespace lowir_native {
namespace global_encoding {

void emit_global(elf_detail::CodeBuffer & out,
                 const mir_model::MirGlobalDefinition & global);

}  // namespace global_encoding
}  // namespace lowir_native
