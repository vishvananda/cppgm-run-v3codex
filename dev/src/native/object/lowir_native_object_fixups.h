#pragma once

#include "native/object/lowir_native_object_elf.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native {
namespace object_elf_detail {

void resolve_same_section_local_fixups(
  std::vector<EncodedSection> & text_sections,
  std::vector<EncodedSection> & data_sections,
  const lowir_model::LowirProgram & program,
  const DeclarationObjectSymbols & declarations);

}  // namespace object_elf_detail
}  // namespace lowir_native
