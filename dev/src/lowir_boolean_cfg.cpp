#include "lowir_boolean_cfg.h"

#include "lowir_function_analysis.h"
#include "lowir_opt.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_analysis::Graph;
using lowir_analysis::build_graph;
using lowir_model::Block;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::Operand;

bool has_candidate_merge(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.size() == 3 &&
       instructions[0].kind == Instruction::IK_PHI &&
       instructions[1].kind == Instruction::IK_CMP &&
       instructions[2].kind == Instruction::IK_BRANCH)
      return true;
  }
  return false;
}

std::vector<std::size_t> value_uses(const Function & function)
{
  std::vector<std::size_t> result(function.value_names.size(), 0);
  const auto count = [&result](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP && operand.value < result.size())
      ++result[operand.value];
  };
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      count(instruction.first);
      count(instruction.second);
      count(instruction.third);
      for(std::size_t argument = 0;
          argument < instruction.args.size(); ++argument)
        count(instruction.args[argument]);
    }
  return result;
}

bool selected_target(const Instruction & phi, const Instruction & compare,
                     long long constant, std::uint32_t predecessor,
                     const Instruction & branch, Operand * selected)
{
  for(std::size_t incoming = 0; incoming < phi.args.size(); incoming += 2) {
    if(phi.args[incoming].kind != Operand::OP_LABEL ||
       phi.args[incoming].block != predecessor ||
       phi.args[incoming + 1].kind != Operand::OP_INTEGER ||
       !phi.args[incoming + 1].has_int_value)
      continue;
    const bool equal = phi.args[incoming + 1].int_value == constant;
    const bool condition = compare.op.kind == LowOperation::LOP_EQ ?
      equal : !equal;
    *selected = condition ? branch.second : branch.third;
    return true;
  }
  return false;
}

void remove_unreachable_blocks(Function * function, Stats * stats)
{
  const Graph graph = build_graph(*function, stats);
  std::vector<unsigned char> reachable(function->blocks.size(), 0);
  std::deque<std::size_t> work(1, 0);
  reachable[0] = 1;
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    for(std::size_t edge = 0; edge < graph.successors[block].size(); ++edge) {
      const std::size_t successor = graph.successors[block][edge];
      if(reachable[successor]) continue;
      reachable[successor] = 1;
      work.push_back(successor);
    }
  }
  std::vector<Block> kept;
  kept.reserve(function->blocks.size());
  std::size_t removed = 0;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    if(reachable[block]) kept.push_back(std::move(function->blocks[block]));
    else ++removed;
  }
  function->blocks.swap(kept);
  if(stats) stats->rewrites += removed + 1;
}

}  // namespace

bool fold_boolean_phi_branch(Function * function, Stats * stats)
{
  if(!has_candidate_merge(*function)) return false;
  const std::vector<std::size_t> uses = value_uses(*function);
  const Graph graph = build_graph(*function, stats);
  for(std::size_t merge = 0; merge < function->blocks.size(); ++merge) {
    const Block & merge_block = function->blocks[merge];
    if(merge_block.instructions.size() != 3 ||
       merge_block.instructions[0].kind != Instruction::IK_PHI ||
       merge_block.instructions[1].kind != Instruction::IK_CMP ||
       merge_block.instructions[2].kind != Instruction::IK_BRANCH)
      continue;
    const Instruction & phi = merge_block.instructions[0];
    const Instruction & compare = merge_block.instructions[1];
    const Instruction & branch = merge_block.instructions[2];
    if(!phi.dest.valid() || !compare.dest.valid() || phi.args.size() != 4 ||
       uses[phi.dest] != 1 || uses[compare.dest] != 1 ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.first.value != compare.dest ||
       (compare.op.kind != LowOperation::LOP_EQ &&
        compare.op.kind != LowOperation::LOP_NE))
      continue;

    const Operand * constant = 0;
    if(compare.first.kind == Operand::OP_TEMP &&
       compare.first.value == phi.dest &&
       compare.second.kind == Operand::OP_INTEGER &&
       compare.second.has_int_value)
      constant = &compare.second;
    else if(compare.second.kind == Operand::OP_TEMP &&
            compare.second.value == phi.dest &&
            compare.first.kind == Operand::OP_INTEGER &&
            compare.first.has_int_value)
      constant = &compare.first;
    if(!constant || graph.predecessors[merge].size() != 2) continue;

    const std::size_t left = graph.predecessors[merge][0];
    const std::size_t right = graph.predecessors[merge][1];
    if(left == right || graph.predecessors[left].size() != 1 ||
       graph.predecessors[right].size() != 1 ||
       graph.predecessors[left][0] != graph.predecessors[right][0])
      continue;
    const std::size_t parent = graph.predecessors[left][0];
    if(parent == merge) continue;
    const Block & left_block = function->blocks[left];
    const Block & right_block = function->blocks[right];
    if(left_block.instructions.size() != 1 ||
       right_block.instructions.size() != 1 ||
       left_block.instructions[0].kind != Instruction::IK_JUMP ||
       right_block.instructions[0].kind != Instruction::IK_JUMP ||
       left_block.instructions[0].first.block != merge_block.id ||
       right_block.instructions[0].first.block != merge_block.id ||
       function->blocks[parent].instructions.empty())
      continue;
    Instruction & parent_branch = function->blocks[parent].instructions.back();
    if(parent_branch.kind != Instruction::IK_BRANCH) continue;
    const std::uint32_t left_id = left_block.id;
    const std::uint32_t right_id = right_block.id;
    if(!((parent_branch.second.block == left_id &&
          parent_branch.third.block == right_id) ||
         (parent_branch.second.block == right_id &&
          parent_branch.third.block == left_id)) ||
       graph.eh_targets[left_id] || graph.eh_targets[right_id] ||
       graph.eh_targets[static_cast<std::uint32_t>(merge_block.id)])
      continue;

    Operand selected_true, selected_false;
    if(!selected_target(phi, compare, constant->int_value,
         parent_branch.second.block, branch, &selected_true) ||
       !selected_target(phi, compare, constant->int_value,
         parent_branch.third.block, branch, &selected_false))
      continue;
    parent_branch.second = selected_true;
    parent_branch.third = selected_false;
    remove_unreachable_blocks(function, stats);
    return true;
  }
  return false;
}

}  // namespace lowir_opt
