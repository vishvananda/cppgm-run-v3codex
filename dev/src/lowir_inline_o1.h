#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace lowir_opt {

struct Stats;

// Inline conservative, typed direct-call candidates.  Returns the number of
// rewritten call sites and explicit no-unwind EH regions.
std::size_t inline_o1_calls(
  lowir_model::LowirProgram & program,
  const std::unordered_set<std::string> & prepared_oversized_functions,
  const std::vector<std::size_t> & original_instruction_counts,
  std::unordered_set<std::string> * rewritten_functions = 0,
  Stats * stats = 0);

}  // namespace lowir_opt
