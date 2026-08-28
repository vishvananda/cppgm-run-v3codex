#pragma once

#include "lowir/model/program.h"

#include <memory>

namespace lowir_native {
namespace force_inline {

// Compatibility adapter for explicit textual LowIR. Returns an owned rewrite
// only when a forced definition exists; ordinary native lowering retains its
// borrowed program and performs no speculative whole-program copy.
std::unique_ptr<lowir_model::LowirProgram> rewrite_program(
    const lowir_model::LowirProgram & source);

}  // namespace force_inline
}  // namespace lowir_native
