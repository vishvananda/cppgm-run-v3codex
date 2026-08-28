#pragma once

#include "native/object/code_buffer.h"
#include "native/mir/model.h"

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

inline bool is_address_store_sequence(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  return start + 1 < instructions.size() &&
    instructions[start].opcode == MirInstruction::MI_LEA &&
    instructions[start + 1].opcode == MirInstruction::MI_STORE;
}

inline bool is_copy_address_store_sequence(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  using mir_model::MirInstruction;
  return start + 2 < instructions.size() &&
    instructions[start].opcode == MirInstruction::MI_MOV &&
    instructions[start + 1].opcode == MirInstruction::MI_LEA &&
    instructions[start + 2].opcode == MirInstruction::MI_STORE;
}

enum MemoryFoldKind {
  MFK_NONE,
  MFK_SETUP_LOAD,
  MFK_COPY_STORE,
  MFK_ADDRESS_STORE,
  MFK_COPY_ADDRESS_STORE
};

class TransientScratchUsePlan
{
public:
  explicit TransientScratchUsePlan(
      const std::vector<mir_model::MirInstruction> & instructions);
  bool dead_after(std::size_t start, X64Register reg) const;

private:
  std::vector<unsigned char> r11_read_before_definition_;
};

inline MemoryFoldKind classify_memory_fold(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start)
{
  if(is_setup_load_sequence(instructions, start)) return MFK_SETUP_LOAD;
  if(is_copy_store_sequence(instructions, start)) return MFK_COPY_STORE;
  if(is_address_store_sequence(instructions, start)) return MFK_ADDRESS_STORE;
  if(is_copy_address_store_sequence(instructions, start))
    return MFK_COPY_ADDRESS_STORE;
  return MFK_NONE;
}

std::size_t emit_dead_setup_load(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses);
std::size_t emit_dead_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses);
std::size_t emit_dead_address_store(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses);
std::size_t emit_dead_address_copy_store(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function,
    const TransientScratchUsePlan & scratch_uses);
std::size_t emit_dead_copy_address_store(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const TransientScratchUsePlan & scratch_uses);
std::size_t emit_memory_fold(
    elf_detail::CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function,
    MemoryFoldKind kind, const TransientScratchUsePlan & scratch_uses);

}  // namespace address_folding
}  // namespace lowir_native
