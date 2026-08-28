#ifndef CPPGM_LOWIR_NATIVE_FRAME_FORWARDING_H
#define CPPGM_LOWIR_NATIVE_FRAME_FORWARDING_H

#include "native/object/code_buffer.h"
#include "native/mir/model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lowir_native {

struct Stats;

namespace frame_forwarding {

struct FrameReloadPlan
{
  struct InstructionAction
  {
    enum Kind : std::uint8_t
    {
      IA_NONE,
      IA_SKIP_DELAYED_STORE,
      IA_FORWARD_DELAYED_LOAD,
      IA_DROP_ADJACENT_STORE,
      // The store's value rides r10 (or r11) across a window proven free
      // of every touch of that register, including the encoder-inserted
      // ones; the paired loads forward from it via
      // IA_FORWARD_DELAYED_LOAD.  Applied before the encode-time
      // peepholes so a fold cannot consume the store and orphan the
      // carry.
      IA_CARRY_SCRATCH_STORE,
      IA_CARRY_SCRATCH_STORE_R11
    } kind = IA_NONE;

    std::uint8_t source = 0;

    X64Register source_register() const
    {
      return static_cast<X64Register>(source);
    }
  };

  std::vector<std::size_t> block_starts;
  std::vector<InstructionAction> actions;

  InstructionAction action(std::size_t block,
                           std::size_t instruction) const;
};

bool parse_reload(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, X64Register * source, X64Register * destination);

FrameReloadPlan find_single_use_reloads(
    const mir_model::MirFunction & function);

// Emitters consulted by the encoder's per-instruction loop: the carry
// replacement (applied before the peepholes), the delayed store skip, and
// the delayed load forward.
bool emit_carry_scratch_store(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const FrameReloadPlan::InstructionAction & action, Stats * stats);
bool emit_delayed_frame_forwarding(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const FrameReloadPlan::InstructionAction & action);

}  // namespace frame_forwarding
}  // namespace lowir_native

#endif
