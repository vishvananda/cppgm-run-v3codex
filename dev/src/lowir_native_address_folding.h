#pragma once

#include "mir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_native {
namespace address_folding {

bool plan_dead_setup_load(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, mir_model::MirOperand * folded_address);

}  // namespace address_folding
}  // namespace lowir_native
