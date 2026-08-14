#pragma once

#include "lowir_model.h"

#include <memory>

namespace lowir_native {
namespace force_inline {

// Returns an owned rewritten program when forced definitions exist, otherwise null.
// The rewrite is linear in the input plus the explicitly requested expansion.
std::unique_ptr<lowir_model::LowirProgram> rewrite_program(
    const lowir_model::LowirProgram & source);

}  // namespace force_inline
}  // namespace lowir_native
