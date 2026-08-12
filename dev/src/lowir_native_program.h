#pragma once

#include "lowir_model.h"
#include "mir_model.h"

namespace lowir_native {
namespace program_lowering {

mir_model::MirGlobalDefinition lower_global(
    const lowir_model::LowirGlobalDefinition & source);
void lower_startup(const lowir_model::LowirProgram & source,
                   mir_model::MirProgram & target);

}  // namespace program_lowering
}  // namespace lowir_native
