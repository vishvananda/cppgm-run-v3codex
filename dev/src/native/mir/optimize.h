#pragma once

#include <cstddef>
#include <cstdint>

#include "native/mir/model.h"

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
  std::size_t identity_moves = 0;
  std::size_t medium_copy_chunks = 0;
  std::size_t block_recolor_candidates = 0;
  std::size_t block_recolor_registers = 0;
  std::size_t block_recolor_blocks = 0;
  std::size_t terminal_call_results = 0;
  std::size_t sibling_parameter_retains = 0;
  std::size_t frame_rewrites = 0;
  std::size_t implicit_return_rewrites = 0;
  std::size_t frameless_functions = 0;
  std::size_t frameless_call_functions = 0;
  std::size_t frameless_saved_registers = 0;
  std::size_t peak_analysis_bytes = 0;
  std::uint64_t elapsed_nanoseconds = 0;
};

// Physical registers explicitly or implicitly defined by one MIR instruction.
// GPR bits occupy 0..15 and XMM bits occupy 16..23.
std::uint64_t instruction_definition_mask(
  const mir_model::MirInstruction & instruction);
void optimize_function(mir_model::MirFunction & function, int level,
                       Stats * stats = 0);
void optimize(mir_model::MirProgram & program, int level, Stats * stats = 0);

}  // namespace machine_opt
}  // namespace lowir_native
