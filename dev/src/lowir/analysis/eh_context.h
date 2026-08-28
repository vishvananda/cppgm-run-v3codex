#pragma once

#include "lowir/analysis/function.h"
#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_eh_context {

struct Context
{
  std::vector<unsigned char> entry_barriers;
  std::size_t barrier_count = 0;
  bool has_eh = false;
  bool conflicting = false;
};

bool is_eh_instruction(lowir_model::Instruction::Kind kind);

Context analyze(const lowir_model::Function & function,
                const lowir_analysis::Graph & graph);

}  // namespace lowir_eh_context
