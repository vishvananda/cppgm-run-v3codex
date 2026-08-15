#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <string>
#include <unordered_set>

namespace lowir_opt {

// Inline conservative, typed direct-call candidates.  Returns the number of
// rewritten call sites and explicit no-unwind EH regions.
std::size_t inline_o1_calls(
  lowir_model::LowirProgram & program,
  std::unordered_set<std::string> * rewritten_functions = 0);

}  // namespace lowir_opt
