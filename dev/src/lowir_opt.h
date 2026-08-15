#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <cstdint>

namespace lowir_opt {

struct Stats
{
  std::size_t functions = 0;
  std::size_t input_instructions = 0;
  std::size_t output_instructions = 0;
  std::size_t instruction_visits = 0;
  std::size_t block_visits = 0;
  std::size_t cfg_edge_visits = 0;
  std::size_t worklist_pushes = 0;
  std::size_t dataflow_updates = 0;
  std::size_t inline_call_visits = 0;
  std::size_t inline_calls = 0;
  std::size_t budget_skips = 0;
  std::size_t rewrites = 0;
  std::uint64_t elapsed_nanoseconds = 0;
};

// Optimize one already-typed LowIR program in place. Level zero deliberately
// performs no transform; parsing and serialization provide canonicalization.
void optimize(lowir_model::LowirProgram & program, int level,
              Stats * stats = 0);

}  // namespace lowir_opt
