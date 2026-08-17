#ifndef CPPGM_LOWIR_NATIVE_FRAME_FORWARDING_H
#define CPPGM_LOWIR_NATIVE_FRAME_FORWARDING_H

#include "mir_model.h"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lowir_native {
namespace frame_forwarding {

struct FrameReloadPlan
{
  std::unordered_set<long long> adjacent;
  std::unordered_map<long long, X64Register> delayed;
};

bool parse_reload(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, long long * frame_offset,
    X64Register * source, X64Register * destination);

FrameReloadPlan find_single_use_reloads(
    const mir_model::MirFunction & function);

bool load_zero_extends(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const FrameReloadPlan & plan);

}  // namespace frame_forwarding
}  // namespace lowir_native

#endif
