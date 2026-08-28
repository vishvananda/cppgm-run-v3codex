#pragma once

#include "native/object/code_buffer.h"
#include "native/object/elf_format.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace eh_reference_detail {

void emit_host_eh_reference_data(
  const lowir_model::LowirProgram & source,
  const object_elf_detail::DeclarationObjectSymbols & declarations,
  std::vector<object_elf_detail::HostFunctionLayout> & functions,
  elf_detail::CodeBuffer & data,
  std::size_t & data_alignment);

}  // namespace eh_reference_detail
}  // namespace lowir_native
