#include "lowir/analysis/eh_context.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace lowir_eh_context {
namespace {

const std::uint32_t kUnreached = std::numeric_limits<std::uint32_t>::max();
const std::uint32_t kConflict = kUnreached - 1;

bool is_normal_target(const lowir_model::Instruction & terminator,
                      lowir_model::BlockId target)
{
  if(terminator.kind == lowir_model::Instruction::IK_JUMP)
    return terminator.first.kind == lowir_model::Operand::OP_LABEL &&
      terminator.first.block == target;
  if(terminator.kind == lowir_model::Instruction::IK_BRANCH)
    return (terminator.second.kind == lowir_model::Operand::OP_LABEL &&
            terminator.second.block == target) ||
      (terminator.third.kind == lowir_model::Operand::OP_LABEL &&
       terminator.third.block == target);
  if(terminator.kind != lowir_model::Instruction::IK_SWITCH) return false;
  if(terminator.second.kind == lowir_model::Operand::OP_LABEL &&
     terminator.second.block == target) return true;
  for(std::size_t index = 1; index < terminator.args.size(); index += 2)
    if(terminator.args[index].kind == lowir_model::Operand::OP_LABEL &&
       terminator.args[index].block == target) return true;
  return false;
}

}  // namespace

bool is_eh_instruction(lowir_model::Instruction::Kind kind)
{
  return kind >= lowir_model::Instruction::IK_EH_TRY &&
    kind <= lowir_model::Instruction::IK_RESUME;
}

Context analyze(const lowir_model::Function & function,
                const lowir_analysis::Graph & graph)
{
  Context result;
  result.entry_barriers.assign(function.blocks.size(), 0);
  if(function.blocks.empty()) return result;

  std::size_t instruction_count = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    instruction_count += function.blocks[block].instructions.size();
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index)
      if(is_eh_instruction(
           function.blocks[block].instructions[index].kind)) {
        result.has_eh = true;
        ++result.barrier_count;
      }
  }
  if(!result.has_eh) return result;

  std::vector<std::size_t> offsets(function.blocks.size() + 1, 0);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    offsets[block + 1] = offsets[block] +
      function.blocks[block].instructions.size();
  std::vector<std::uint32_t> parents(instruction_count + 1, kUnreached);
  std::vector<std::uint32_t> incoming(function.blocks.size(), kUnreached);
  std::vector<unsigned char> queued(function.blocks.size(), 0);
  std::deque<std::size_t> work;
  incoming[0] = 0;
  queued[0] = 1;
  work.push_back(0);

  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::uint32_t id = function.blocks[block].id;
    if(id >= graph.eh_targets.size() || !graph.eh_targets[id]) continue;
    result.entry_barriers[block] = 1;
    ++result.barrier_count;
  }

  const auto merge_incoming = [&incoming, &queued, &work, &result](
      std::size_t block, std::uint32_t state) {
    std::uint32_t merged = incoming[block];
    if(merged == kUnreached) merged = state;
    else if(merged != state) merged = kConflict;
    if(merged == incoming[block]) return;
    incoming[block] = merged;
    if(merged == kConflict) result.conflicting = true;
    if(!queued[block]) {
      queued[block] = 1;
      work.push_back(block);
    }
  };

  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    queued[block] = 0;
    std::uint32_t state = incoming[block];
    const std::vector<lowir_model::Instruction> & instructions =
      function.blocks[block].instructions;
    for(std::size_t index = 0; index < instructions.size(); ++index) {
      const lowir_model::Instruction::Kind kind = instructions[index].kind;
      if(kind == lowir_model::Instruction::IK_EH_TRY ||
         kind == lowir_model::Instruction::IK_EH_CLEANUP) {
        const std::size_t landing = graph.find(instructions[index].first.block);
        if(landing != static_cast<std::size_t>(-1))
          merge_incoming(landing, state);
        if(state == kConflict) continue;
        const std::uint32_t marker = static_cast<std::uint32_t>(
          offsets[block] + index + 1);
        if(parents[marker] == kUnreached) parents[marker] = state;
        else if(parents[marker] != state) state = kConflict;
        if(state != kConflict) state = marker;
      } else if(kind == lowir_model::Instruction::IK_EH_END) {
        if(state == 0 || state == kConflict || state > instruction_count ||
           parents[state] == kUnreached)
          state = kConflict;
        else state = parents[state];
      }
    }
    if(state == kConflict) result.conflicting = true;
    if(instructions.empty()) continue;
    const lowir_model::Instruction & terminator = instructions.back();
    for(std::size_t edge = 0;
        edge < graph.successors[block].size(); ++edge) {
      const std::size_t successor = graph.successors[block][edge];
      if(!is_normal_target(terminator, function.blocks[successor].id))
        continue;
      merge_incoming(successor, state);
    }
  }
  return result;
}

}  // namespace lowir_eh_context
