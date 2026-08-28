#pragma once

#include <vector>

#include "lowir/model/program.h"
#include "native/mir/model.h"

namespace lowir_native {
namespace program_lowering {

mir_model::MirGlobalDefinition lower_global(
    const lowir_model::LowirGlobalDefinition & source);
std::vector<lowir_model::SymbolId> tls_wrapper_index(
    const lowir_model::LowirProgram & source);
void lower_startup(const lowir_model::LowirProgram & source,
                   mir_model::MirProgram & target);

}  // namespace program_lowering
}  // namespace lowir_native
