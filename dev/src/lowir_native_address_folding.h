#pragma once

#include "lowir_native_code_buffer.h"
#include "mir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace address_folding {

inline bool is_setup_load_sequence(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  if(start >= instructions.size() ||
     (instructions[start].opcode != MirInstruction::MI_LEA &&
      instructions[start].opcode != MirInstruction::MI_MOV)) return false;
  if(start + 1 < instructions.size() &&
     instructions[start + 1].opcode == MirInstruction::MI_LOAD) return true;
  return start + 2 < instructions.size() &&
    instructions[start].opcode == MirInstruction::MI_MOV &&
    instructions[start + 1].opcode == MirInstruction::MI_LEA &&
    instructions[start + 2].opcode == MirInstruction::MI_LOAD;
}

inline bool is_copy_store_sequence(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  return start + 1 < instructions.size() &&
    instructions[start].opcode == MirInstruction::MI_MOV &&
    instructions[start + 1].opcode == MirInstruction::MI_STORE;
}

std::size_t emit_dead_setup_load(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function);
std::size_t emit_dead_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function);

}  // namespace address_folding
}  // namespace lowir_native
