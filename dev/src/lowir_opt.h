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
  std::size_t simplify_runs = 0;
  std::size_t simplify_changes = 0;
  std::size_t simplify_candidate_skips = 0;
  std::size_t dce_runs = 0;
  std::size_t dce_changes = 0;
  std::size_t dce_candidate_skips = 0;
  std::size_t cfg_runs = 0;
  std::size_t cfg_changes = 0;
  std::size_t slot_runs = 0;
  std::size_t slot_changes = 0;
  std::size_t forward_slot_runs = 0;
  std::size_t forward_slot_changes = 0;
  std::size_t local_slot_runs = 0;
  std::size_t local_slot_changes = 0;
  std::size_t remove_slot_runs = 0;
  std::size_t remove_slot_changes = 0;
  std::size_t promote_slot_runs = 0;
  std::size_t promote_slot_changes = 0;
  std::size_t dead_store_runs = 0;
  std::size_t dead_store_changes = 0;
  std::uint64_t inline_nanoseconds = 0;
  std::uint64_t simplify_nanoseconds = 0;
  std::uint64_t dce_nanoseconds = 0;
  std::uint64_t cfg_nanoseconds = 0;
  std::uint64_t slot_nanoseconds = 0;
  std::uint64_t forward_slot_nanoseconds = 0;
  std::uint64_t local_slot_nanoseconds = 0;
  std::uint64_t remove_slot_nanoseconds = 0;
  std::uint64_t promote_slot_nanoseconds = 0;
  std::uint64_t dead_store_nanoseconds = 0;
  std::uint64_t elapsed_nanoseconds = 0;
};

// Optimize one already-typed LowIR program in place. Level zero deliberately
// performs no transform; parsing and serialization provide canonicalization.
void optimize(lowir_model::LowirProgram & program, int level,
              Stats * stats = 0);

}  // namespace lowir_opt
