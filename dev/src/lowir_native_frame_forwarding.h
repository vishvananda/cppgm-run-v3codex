#ifndef CPPGM_LOWIR_NATIVE_FRAME_FORWARDING_H
#define CPPGM_LOWIR_NATIVE_FRAME_FORWARDING_H

#include "mir_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lowir_native {
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
      IA_DROP_ADJACENT_STORE
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

}  // namespace frame_forwarding
}  // namespace lowir_native

#endif
