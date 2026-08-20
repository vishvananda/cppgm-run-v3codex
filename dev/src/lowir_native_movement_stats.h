#pragma once

#include "lowir_model.h"
#include "lowir_native.h"
#include "mir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace movement_stats {

NativeMovementReason classify(const lowir_model::Instruction & instruction);

void record(Stats * stats, NativeMovementReason reason,
            const std::vector<mir_model::MirInstruction> & instructions,
            std::size_t begin, std::size_t end);

}  // namespace movement_stats
}  // namespace lowir_native
