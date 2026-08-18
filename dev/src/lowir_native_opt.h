#pragma once

#include <cstddef>
#include <cstdint>

#include "mir_model.h"

namespace lowir_native {
namespace machine_opt {

struct Stats
{
  std::size_t functions = 0;
  std::size_t input_instructions = 0;
  std::size_t output_instructions = 0;
  std::size_t instruction_visits = 0;
  std::size_t cfg_edge_visits = 0;
  std::size_t worklist_pushes = 0;
  std::size_t rewrites = 0;
  std::size_t operand_rewrites = 0;
  std::size_t dead_definitions = 0;
  std::size_t frame_rewrites = 0;
  std::size_t implicit_return_rewrites = 0;
  std::size_t peak_analysis_bytes = 0;
  std::uint64_t elapsed_nanoseconds = 0;
};

void optimize_function(mir_model::MirFunction & function, int level,
                       Stats * stats = 0);
void prepare_for_encoding(mir_model::MirFunction & function,
                          Stats * stats = 0);
void optimize(mir_model::MirProgram & program, int level, Stats * stats = 0);

}  // namespace machine_opt
}  // namespace lowir_native
