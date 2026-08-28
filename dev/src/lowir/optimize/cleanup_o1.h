#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

namespace lowir_opt {

struct Stats;

struct FunctionBoundaries
{
  std::vector<lowir_model::FunctionBoundaryMetadata> values;
  std::vector<unsigned char> known;
};

typedef std::pair<std::size_t, std::size_t> DceLocation;

struct DceValueLiveness
{
  DceLocation definition = DceLocation(0, 0);
  std::size_t uses = 0;
  bool defined = false;
};

struct DceScratch
{
  std::vector<DceValueLiveness> values;
  std::vector<std::vector<unsigned char> > dead;
  std::deque<DceLocation> work;

  void reset(const lowir_model::Function & function);
};

inline bool local_value_definition_is_pure(
    lowir_model::Instruction::Kind kind)
{
  using lowir_model::Instruction;
  return kind == Instruction::IK_CONST || kind == Instruction::IK_COPY ||
    kind == Instruction::IK_PHI || kind == Instruction::IK_ADDR ||
    kind == Instruction::IK_INDEX || kind == Instruction::IK_UNARY ||
    kind == Instruction::IK_BINARY || kind == Instruction::IK_CMP ||
    kind == Instruction::IK_CONVERT;
}

bool eliminate_dead_code(lowir_model::Function * function,
    const FunctionBoundaries & boundaries, Stats * stats,
    DceScratch * reusable_scratch);

bool share_terminal_resume_blocks(lowir_model::Function * function,
                                  Stats * stats = 0);
bool share_exact_cleanup_tails(lowir_model::Function * function,
                               Stats * stats = 0);
// Serialize blocks that cannot return -- throw, resume, and noreturn-call
// paths, and blocks reaching only such paths -- after every ordinary block,
// so the hot path stays dense.  Cold regions have no ordinary successors, so
// definition-before-use order is preserved by construction.
bool sink_cold_blocks(lowir_model::Function * function,
                      const std::vector<unsigned char> & noreturn_symbols,
                      Stats * stats = 0);

// Rehome pure operand-free definitions (constants and addresses) whose only
// uses sit in a single raising-cold block into that block, so the hot path
// stops carrying them across its calls.
bool sink_cold_only_definitions(
    lowir_model::Function * function,
    const std::vector<unsigned char> & noreturn_symbols,
    Stats * stats = 0);

}  // namespace lowir_opt
