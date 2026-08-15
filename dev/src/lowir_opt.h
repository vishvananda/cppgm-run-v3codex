#pragma once

#include "lowir_model.h"

#include <cstddef>

namespace lowir_opt {

struct Stats
{
  std::size_t functions = 0;
  std::size_t input_instructions = 0;
  std::size_t output_instructions = 0;
  std::size_t instruction_visits = 0;
  std::size_t cfg_edge_visits = 0;
  std::size_t worklist_pushes = 0;
  std::size_t rewrites = 0;
};

// Optimize one already-typed LowIR program in place. Level zero deliberately
// performs no transform; parsing and serialization provide canonicalization.
void optimize(lowir_model::LowirProgram & program, int level,
              Stats * stats = 0);

}  // namespace lowir_opt
