#include "lowir/analysis/phi_edges.h"

#include "lowir/optimize/errors.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace lowir_phi_edges {
using lowir_model::BlockId;
using lowir_model::Instruction;
using lowir_model::LowirBlock;
using lowir_model::LowirFunction;
using lowir_model::Operand;

namespace {

struct Edge
{
  BlockId predecessor;
  BlockId target;
};

void append_unique(std::vector<BlockId> * values, BlockId value)
{
  if(std::find(values->begin(), values->end(), value) == values->end())
    values->push_back(value);
}

bool terminal_targets(const Instruction & terminal, BlockId target)
{
  if(terminal.kind == Instruction::IK_JUMP)
    return terminal.first.kind == Operand::OP_LABEL &&
      terminal.first.block == target;
  if(terminal.kind == Instruction::IK_BRANCH)
    return (terminal.second.kind == Operand::OP_LABEL &&
            terminal.second.block == target) ||
      (terminal.third.kind == Operand::OP_LABEL &&
       terminal.third.block == target);
  if(terminal.kind != Instruction::IK_SWITCH) return false;
  if(terminal.second.kind == Operand::OP_LABEL &&
     terminal.second.block == target) return true;
  for(std::size_t i = 1; i < terminal.args.size(); i += 2)
    if(terminal.args[i].kind == Operand::OP_LABEL &&
       terminal.args[i].block == target) return true;
  return false;
}

std::vector<std::vector<BlockId> > ordinary_successors(
    const LowirFunction & function)
{
  std::vector<std::vector<BlockId> > result(function.next_block_id);
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const LowirBlock & source = function.blocks[block];
    if(source.instructions.empty()) continue;
    const Instruction & terminal = source.instructions.back();
    std::vector<BlockId> & targets = result[source.id];
    if(terminal.kind == Instruction::IK_JUMP)
      append_unique(&targets, terminal.first.block);
    else if(terminal.kind == Instruction::IK_BRANCH) {
      append_unique(&targets, terminal.second.block);
      append_unique(&targets, terminal.third.block);
    } else if(terminal.kind == Instruction::IK_SWITCH) {
      append_unique(&targets, terminal.second.block);
      for(std::size_t i = 1; i < terminal.args.size(); i += 2)
        append_unique(&targets, terminal.args[i].block);
    }
  }
  return result;
}

std::vector<Edge> critical_phi_edges(const LowirFunction & function)
{
  const std::vector<std::vector<BlockId> > successors =
    ordinary_successors(function);
  std::vector<Edge> result;
  std::unordered_set<std::uint64_t> seen;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const LowirBlock & target = function.blocks[block];
    for(std::size_t instruction = 0;
        instruction < target.instructions.size(); ++instruction) {
      const Instruction & phi = target.instructions[instruction];
      if(phi.kind != Instruction::IK_PHI) break;
      for(std::size_t incoming = 0;
          incoming + 1 < phi.args.size(); incoming += 2) {
        const BlockId predecessor = phi.args[incoming].block;
        const std::uint32_t predecessor_id = predecessor;
        if(predecessor_id >= successors.size() ||
           successors[predecessor_id].size() <= 1) continue;
        const std::uint64_t key =
          (static_cast<std::uint64_t>(predecessor_id) << 32) |
          static_cast<std::uint32_t>(target.id);
        if(seen.insert(key).second)
          result.push_back(Edge{predecessor, target.id});
      }
    }
  }
  return result;
}

void rewrite_target(Instruction * terminal, BlockId old_target,
                    BlockId replacement)
{
  const auto rewrite = [old_target, replacement](Operand * operand) {
    if(operand->kind == Operand::OP_LABEL && operand->block == old_target)
      operand->block = replacement;
  };
  if(terminal->kind == Instruction::IK_JUMP)
    rewrite(&terminal->first);
  else if(terminal->kind == Instruction::IK_BRANCH) {
    rewrite(&terminal->second);
    rewrite(&terminal->third);
  } else if(terminal->kind == Instruction::IK_SWITCH) {
    rewrite(&terminal->second);
    for(std::size_t i = 1; i < terminal->args.size(); i += 2)
      rewrite(&terminal->args[i]);
  }
}

void split_function(lowir_model::LowirProgram * program,
                    LowirFunction * function)
{
  const std::vector<Edge> edges = critical_phi_edges(*function);
  if(edges.empty()) return;
  std::vector<std::size_t> block_index(function->next_block_id,
                                       function->blocks.size());
  bool has_presentation = true;
  std::unordered_set<std::uint32_t> labels;
  labels.reserve(function->blocks.size() + edges.size());
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    block_index[function->blocks[i].id] = i;
    const lowir_model::StringId label =
      function->block_labels[function->blocks[i].id];
    if(!label.valid()) has_presentation = false;
    else labels.insert(static_cast<std::uint32_t>(label));
  }
  for(std::size_t edge = 0; edge < edges.size(); ++edge) {
    const std::uint32_t predecessor = edges[edge].predecessor;
    const std::uint32_t target = edges[edge].target;
    if(predecessor >= block_index.size() || target >= block_index.size() ||
       block_index[predecessor] >= function->blocks.size() ||
       block_index[target] >= function->blocks.size())
      lowir_opt::ThrowOptimizerInternalError("invalid critical phi edge");
    lowir_model::StringId label;
    if(has_presentation) {
      std::size_t ordinal = function->next_block_id;
      do {
        label = program->strings.intern(
          "__phi_edge_" + std::to_string(ordinal++));
      } while(labels.count(static_cast<std::uint32_t>(label)));
      labels.insert(static_cast<std::uint32_t>(label));
    }
    const BlockId edge_block =
      lowir_model::allocate_lowir_block_id(*function, label);

    LowirBlock & source = function->blocks[block_index[predecessor]];
    rewrite_target(&source.instructions.back(), edges[edge].target,
                   edge_block);
    LowirBlock & destination = function->blocks[block_index[target]];
    for(std::size_t i = 0; i < destination.instructions.size(); ++i) {
      Instruction & phi = destination.instructions[i];
      if(phi.kind != Instruction::IK_PHI) break;
      for(std::size_t incoming = 0;
          incoming + 1 < phi.args.size(); incoming += 2)
        if(phi.args[incoming].block == edges[edge].predecessor)
          phi.args[incoming].block = edge_block;
    }

    LowirBlock inserted;
    inserted.id = edge_block;
    Instruction jump;
    jump.kind = Instruction::IK_JUMP;
    jump.first.kind = Operand::OP_LABEL;
    jump.first.block = edges[edge].target;
    inserted.instructions.push_back(jump);
    function->blocks.push_back(inserted);
  }
}

}  // namespace

void rewrite_moved_phi_edges(LowirFunction * function,
                             const Instruction & terminal,
                             BlockId old_predecessor,
                             BlockId continuation)
{
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    LowirBlock & target = function->blocks[block];
    if(!terminal_targets(terminal, target.id)) continue;
    for(std::size_t instruction = 0;
        instruction < target.instructions.size(); ++instruction) {
      Instruction & phi = target.instructions[instruction];
      if(phi.kind != Instruction::IK_PHI) break;
      for(std::size_t incoming = 0;
          incoming + 1 < phi.args.size(); incoming += 2)
        if(phi.args[incoming].kind == Operand::OP_LABEL &&
           phi.args[incoming].block == old_predecessor)
          phi.args[incoming].block = continuation;
    }
  }
}

bool has_critical_phi_edges(const lowir_model::LowirProgram & program)
{
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    if(!critical_phi_edges(program.functions[i]).empty()) return true;
  return false;
}

void split_critical_phi_edges(lowir_model::LowirProgram * program)
{
  for(std::size_t i = 0; i < program->functions.size(); ++i)
    split_function(program, &program->functions[i]);
}

}  // namespace lowir_phi_edges
