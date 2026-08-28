#pragma once

#include "lowir/model/program.h"
#include "native/driver/stats.h"
#include "native/analysis/function.h"
#include "native/allocation/location_planning.h"
#include "native/allocation/spill_slots.h"
#include "native/mir/model.h"

#include <cstddef>
#include <cstdint>

namespace lowir_native {
namespace frame_home_planning {

long long allocate_binding(
    mir_model::MirFunction & target,
    std::size_t & frame_bytes,
    mir_model::MirFrameBinding::Kind kind,
    lowir_model::PresentationName name,
    const lowir_model::LowType & type);

std::uint32_t append_binding(
    mir_model::MirFunction & target,
    mir_model::MirFrameBinding::Kind kind,
    lowir_model::PresentationName name,
    const lowir_model::LowType & type,
    long long offset);

mir_model::MirOperand allocate_temporary(
    mir_model::MirFunction & target,
    std::size_t & frame_bytes,
    spill_slots::Pool & spill_slots,
    location_planning::GeneratedFrameNames & generated_names,
    const analysis::FunctionFacts & facts,
    lowir_model::ValueId value,
    const lowir_model::LowType & type,
    std::size_t position,
    TemporaryHomeReason reason,
    bool crosses_call,
    Stats * stats);

}  // namespace frame_home_planning
}  // namespace lowir_native
