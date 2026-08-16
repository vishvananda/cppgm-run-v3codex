#pragma once

#include "lowir_model.h"
#include "lowir_native_abi.h"
#include "mir_model.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace lowir_native {
struct Stats;
namespace session_detail {

mir_model::MirFunction lower_native_function(
    const lowir_model::LowirFunction & function,
    const std::unordered_set<std::string> & pointer_globals,
    const std::unordered_map<std::string, std::string> & tls_wrappers,
    const abi::FunctionSignatureIndex & signatures,
    Stats * stats);

}  // namespace session_detail
}  // namespace lowir_native
