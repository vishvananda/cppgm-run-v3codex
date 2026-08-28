#pragma once

#include "lowir/model/program.h"
#include "lowir_native_abi.h"
#include "mir_model.h"

#include <vector>

namespace lowir_native {
struct Stats;
namespace allocation { class AllocationDecisionLog; }
namespace session_detail {

mir_model::MirFunction lower_native_function(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    const std::vector<unsigned char> & pointer_globals,
    const std::vector<lowir_model::SymbolId> & tls_wrappers,
    const abi::FunctionSignatureIndex & signatures,
    int optimization_level,
    Stats * stats,
    allocation::AllocationDecisionLog * decisions);

}  // namespace session_detail
}  // namespace lowir_native
