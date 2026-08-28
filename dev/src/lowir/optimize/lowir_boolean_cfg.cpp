#include "lowir/optimize/lowir_boolean_cfg.h"

#include "lowir/analysis/lowir_function_analysis.h"
#include "lowir/optimize/lowir_opt.h"
#include "lowir/optimize/lowir_optimizer_support.h"
#include "lowir/analysis/lowir_phi_edges.h"
#include "lowir/optimize/lowir_scalar_rules.h"

#include <algorithm>
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

bool cleanup_is_eh_instruction(Instruction::Kind kind);

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

bool has_direct_boolean_phi_branch(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.size() == 2 &&
       instructions[0].kind == Instruction::IK_PHI &&
       instructions[1].kind == Instruction::IK_BRANCH)
      return true;
  }
  return false;
}

bool has_edge_known_branch_candidate(
    const Function & function, std::vector<unsigned char> * seen)
{
  seen->assign(function.value_names.size(), 0);
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(!instructions.empty() &&
       instructions.back().kind == Instruction::IK_BRANCH &&
       instructions.back().first.kind == Operand::OP_TEMP) {
      const std::uint32_t value = instructions.back().first.value;
      if(value >= seen->size()) continue;
      if((*seen)[value]) return true;
      (*seen)[value] = 1;
    }
  }
  return false;
}

bool block_has_phi(const Function & function, const Graph & graph,
                   const Operand & target)
{
  if(target.kind != Operand::OP_LABEL || !target.block.valid()) return true;
  const std::size_t block = graph.find(target.block);
  if(block == static_cast<std::size_t>(-1)) return true;
  const std::vector<Instruction> & instructions =
    function.blocks[block].instructions;
  for(std::size_t i = 0; i < instructions.size(); ++i)
    if(instructions[i].kind == Instruction::IK_PHI) return true;
  return false;
}

bool predicate_keeps_memory(const Instruction & instruction)
{
  switch(instruction.kind) {
  case Instruction::IK_CONST:
  case Instruction::IK_COPY:
  case Instruction::IK_ADDR:
  case Instruction::IK_INDEX:
  case Instruction::IK_UNARY:
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
  case Instruction::IK_CONVERT:
  case Instruction::IK_PHI:
  case Instruction::IK_JUMP:
  case Instruction::IK_BRANCH:
  case Instruction::IK_SWITCH:
  case Instruction::IK_RETURN:
  case Instruction::IK_UNREACHABLE:
    return true;
  case Instruction::IK_LOAD:
    return !instruction.volatile_access;
  default:
    return false;
  }
}

const Instruction * local_definition(const Block & block,
                                     lowir_model::ValueId value)
{
  for(std::size_t i = block.instructions.size(); i-- > 0;)
    if(block.instructions[i].dest.valid() &&
       block.instructions[i].dest == value)
      return &block.instructions[i];
  return 0;
}

bool block_memory_stable_from(const Block & block,
                              const Instruction * first)
{
  bool found = false;
  for(std::size_t i = 0; i < block.instructions.size(); ++i) {
    if(&block.instructions[i] == first) found = true;
    if(found && !predicate_keeps_memory(block.instructions[i])) return false;
  }
  return found;
}

bool block_memory_stable(const Block & block)
{
  for(std::size_t i = 0; i < block.instructions.size(); ++i)
    if(!predicate_keeps_memory(block.instructions[i])) return false;
  return true;
}

bool edge_establishes_nonzero(const Function & function,
                              std::size_t predecessor,
                              std::size_t successor,
                              const Operand & checked_value,
                              const Instruction * reload,
                              bool stable_reload_path)
{
  const Block & incoming_block = function.blocks[predecessor];
  if(incoming_block.instructions.empty()) return false;
  const Instruction & incoming = incoming_block.instructions.back();
  if(incoming.kind != Instruction::IK_BRANCH ||
     incoming.first.kind != Operand::OP_TEMP) return false;
  const bool true_edge = incoming.second.kind == Operand::OP_LABEL &&
    incoming.second.block == function.blocks[successor].id;
  const bool false_edge = incoming.third.kind == Operand::OP_LABEL &&
    incoming.third.block == function.blocks[successor].id;
  if(true_edge == false_edge) return false;
  const Instruction * predicate =
    local_definition(incoming_block, incoming.first.value);
  if(!predicate || predicate->kind != Instruction::IK_CMP ||
     (predicate->op.kind != LowOperation::LOP_EQ &&
      predicate->op.kind != LowOperation::LOP_NE)) return false;
  Operand tested;
  if(predicate->first.kind == Operand::OP_TEMP &&
     lowir_opt::is_zero(predicate->second)) tested = predicate->first;
  else if(predicate->second.kind == Operand::OP_TEMP &&
          lowir_opt::is_zero(predicate->first)) tested = predicate->second;
  else return false;
  const bool nonzero_edge = predicate->op.kind == LowOperation::LOP_NE ?
    true_edge : false_edge;
  if(!nonzero_edge) return false;
  if(tested.kind == Operand::OP_TEMP &&
     checked_value.kind == Operand::OP_TEMP &&
     tested.value == checked_value.value)
    return true;
  if(!stable_reload_path || !reload) return false;
  const Instruction * original_load =
    local_definition(incoming_block, tested.value);
  return original_load && original_load->kind == Instruction::IK_LOAD &&
    !original_load->volatile_access &&
    optimizer_support::same_storage_location(
      original_load->first, reload->first) &&
    lowir_model::same_lowir_type(original_load->type, reload->type) &&
    block_memory_stable_from(incoming_block, original_load);
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

bool direct_phi_incoming(const Instruction & phi,
                         lowir_model::BlockId predecessor,
                         Operand * value)
{
  bool found = false;
  for(std::size_t incoming = 0;
      incoming + 1 < phi.args.size(); incoming += 2) {
    if(phi.args[incoming].kind != Operand::OP_LABEL ||
       phi.args[incoming].block != predecessor)
      continue;
    if(found) return false;
    *value = phi.args[incoming + 1];
    found = true;
  }
  return found;
}

bool fold_direct_boolean_phi_branches(Function * function, Stats * stats)
{
  if(!has_direct_boolean_phi_branch(*function)) return false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function->blocks[block].instructions.size();
        ++instruction)
      if(cleanup_is_eh_instruction(
           function->blocks[block].instructions[instruction].kind))
        return false;
  const std::vector<std::size_t> uses = value_uses(*function);
  const Graph graph = build_graph(*function, stats);
  const lowir_analysis::DominatorTree dom =
    lowir_analysis::dominators(graph, stats);
  const lowir_analysis::LoopForest loops =
    lowir_analysis::discover_loops(*function, graph, dom, stats);
  bool changed = false;
  for(std::size_t merge = 0; merge < function->blocks.size(); ++merge) {
    const Block & merge_block = function->blocks[merge];
    if(merge_block.instructions.empty() ||
       merge_block.instructions[0].kind != Instruction::IK_PHI ||
       graph.eh_targets[static_cast<std::uint32_t>(merge_block.id)])
      continue;
    if(merge_block.instructions.size() != 2 ||
       merge_block.instructions[1].kind != Instruction::IK_BRANCH)
      continue;
    const Instruction phi = merge_block.instructions[0];
    const Instruction branch = merge_block.instructions[1];
    if(!phi.dest.valid() || phi.type.kind != lowir_model::LTK_I64 ||
       phi.args.size() < 4 || phi.args.size() % 2 != 0 ||
       phi.dest >= uses.size() || uses[phi.dest] != 1 ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.first.value != phi.dest ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL ||
       branch.second.block == merge_block.id ||
       branch.third.block == merge_block.id ||
       merge >= loops.innermost_loop.size() ||
       loops.innermost_loop[merge] >= loops.loops.size() ||
       graph.predecessors[merge].size() != phi.args.size() / 2 ||
       block_has_phi(*function, graph, branch.second) ||
       block_has_phi(*function, graph, branch.third))
      continue;

    struct IncomingRewrite
    {
      std::size_t predecessor;
      Operand value;
    };
    std::vector<IncomingRewrite> rewrites;
    rewrites.reserve(graph.predecessors[merge].size());
    bool safe = true;
    for(std::size_t incoming = 0;
        incoming < graph.predecessors[merge].size(); ++incoming) {
      const std::size_t predecessor = graph.predecessors[merge][incoming];
      const Block & predecessor_block = function->blocks[predecessor];
      Operand value;
      if(predecessor_block.instructions.empty() ||
         graph.predecessors[predecessor].size() != 1 ||
         dom.dominates(merge, predecessor) ||
         graph.eh_targets[static_cast<std::uint32_t>(predecessor_block.id)] ||
         !direct_phi_incoming(phi, predecessor_block.id, &value) ||
         (value.kind != Operand::OP_TEMP &&
          !(value.kind == Operand::OP_INTEGER && value.has_int_value))) {
        safe = false;
        break;
      }
      for(std::size_t instruction = 0;
          instruction < predecessor_block.instructions.size();
          ++instruction)
        if(predecessor_block.instructions[instruction].kind ==
             Instruction::IK_PHI ||
           predecessor_block.instructions[instruction].kind ==
             Instruction::IK_CALL) {
          safe = false;
          break;
        }
      if(!safe) break;
      const Instruction & terminal = predecessor_block.instructions.back();
      if(terminal.kind != Instruction::IK_JUMP ||
         terminal.first.kind != Operand::OP_LABEL ||
         terminal.first.block != merge_block.id) {
        safe = false;
        break;
      }
      rewrites.push_back(IncomingRewrite{predecessor, value});
    }
    if(!safe) continue;

    for(std::size_t rewrite = 0; rewrite < rewrites.size(); ++rewrite) {
      Instruction & terminal =
        function->blocks[rewrites[rewrite].predecessor].instructions.back();
      const lowir_model::InstructionDebugLocation old_debug =
        terminal.debug_location;
      const Operand & value = rewrites[rewrite].value;
      terminal = Instruction();
      if(value.kind == Operand::OP_INTEGER) {
        terminal.kind = Instruction::IK_JUMP;
        terminal.first = value.int_value ? branch.second : branch.third;
        terminal.debug_location = old_debug;
      } else {
        terminal.kind = Instruction::IK_BRANCH;
        terminal.first = value;
        terminal.second = branch.second;
        terminal.third = branch.third;
        terminal.debug_location = branch.debug_location;
      }
      if(stats) ++stats->rewrites;
    }
    changed = true;
  }
  if(changed) remove_unreachable_blocks(function, stats);
  return changed;
}

bool fold_edge_known_branches_with_scratch(
    Function * function, Stats * stats,
    std::vector<unsigned char> * branch_values)
{
  if(!has_edge_known_branch_candidate(*function, branch_values)) return false;
  const Graph graph = build_graph(*function, stats);
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    Block & current = function->blocks[block];
    if(current.instructions.empty() ||
       graph.predecessors[block].size() != 1 ||
       (static_cast<std::uint32_t>(current.id) < graph.eh_targets.size() &&
        graph.eh_targets[static_cast<std::uint32_t>(current.id)]))
      continue;
    Instruction & branch = current.instructions.back();
    if(branch.kind != Instruction::IK_BRANCH ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL)
      continue;
    const std::size_t predecessor = graph.predecessors[block][0];
    if(function->blocks[predecessor].instructions.empty()) continue;
    const Instruction & incoming =
      function->blocks[predecessor].instructions.back();
    if(incoming.kind != Instruction::IK_BRANCH ||
       incoming.first.kind != Operand::OP_TEMP ||
       incoming.first.value != branch.first.value)
      continue;
    const bool true_edge = incoming.second.kind == Operand::OP_LABEL &&
      incoming.second.block == current.id;
    const bool false_edge = incoming.third.kind == Operand::OP_LABEL &&
      incoming.third.block == current.id;
    if(true_edge == false_edge) continue;
    const Operand selected = true_edge ? branch.second : branch.third;
    const Operand removed = true_edge ? branch.third : branch.second;
    // Removing a direct predecessor edge requires phi repair.  Keep this
    // inexpensive fold on the edge-local case; the general edge editor owns
    // phi-changing rewrites.
    if(block_has_phi(*function, graph, removed)) continue;
    const lowir_model::InstructionDebugLocation debug =
      branch.debug_location;
    branch = Instruction();
    branch.kind = Instruction::IK_JUMP;
    branch.first = selected;
    branch.debug_location = debug;
    changed = true;
    if(stats) ++stats->rewrites;
  }
  return changed;
}

}  // namespace

bool fold_nonzero_underflow_branches(Function * function, Stats * stats)
{
  const Graph graph = build_graph(*function, stats);
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    Block & current = function->blocks[block];
    if(current.instructions.empty() ||
       graph.predecessors[block].size() != 1) continue;
    Instruction & branch = current.instructions.back();
    if(branch.kind != Instruction::IK_BRANCH ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL) continue;

    const Instruction * underflow =
      local_definition(current, branch.first.value);
    if(!underflow || underflow->kind != Instruction::IK_CMP ||
       (underflow->op.kind != LowOperation::LOP_UGE &&
        underflow->op.kind != LowOperation::LOP_ULT) ||
       underflow->first.kind != Operand::OP_TEMP ||
       underflow->second.kind != Operand::OP_TEMP) continue;
    const Instruction * subtract =
      local_definition(current, underflow->first.value);
    if(!subtract || subtract->kind != Instruction::IK_BINARY ||
       subtract->op.kind != LowOperation::LOP_SUB ||
       subtract->first.kind != Operand::OP_TEMP ||
       !lowir_opt::is_one(subtract->second) ||
       subtract->first.value != underflow->second.value) continue;
    const Instruction * reload =
      local_definition(current, subtract->first.value);
    const bool stable_reload = reload &&
      reload->kind == Instruction::IK_LOAD &&
      !reload->volatile_access &&
      block_memory_stable_from(current, reload);

    bool established = false;
    bool stable_reload_path = stable_reload && block_memory_stable(current);
    std::size_t successor = block;
    std::vector<unsigned char> seen(function->blocks.size(), 0);
    seen[successor] = 1;
    for(std::size_t depth = 0; depth < 8 && !established; ++depth) {
      if(graph.predecessors[successor].size() != 1) break;
      const std::size_t predecessor = graph.predecessors[successor][0];
      if(seen[predecessor]) break;
      seen[predecessor] = 1;
      established = edge_establishes_nonzero(
        *function, predecessor, successor, subtract->first,
        stable_reload ? reload : 0, stable_reload_path);
      if(established) break;
      stable_reload_path = stable_reload_path &&
        block_memory_stable(function->blocks[predecessor]);
      successor = predecessor;
    }
    if(!established) continue;

    const Operand selected = underflow->op.kind == LowOperation::LOP_ULT ?
      branch.second : branch.third;
    const Operand removed = underflow->op.kind == LowOperation::LOP_ULT ?
      branch.third : branch.second;
    if(block_has_phi(*function, graph, removed)) continue;
    const lowir_model::InstructionDebugLocation debug = branch.debug_location;
    branch = Instruction();
    branch.kind = Instruction::IK_JUMP;
    branch.first = selected;
    branch.debug_location = debug;
    changed = true;
    if(stats) {
      ++stats->predicate_range_folds;
      ++stats->rewrites;
    }
  }
  return changed;
}

bool fold_edge_known_branches(Function * function, Stats * stats)
{
  return fold_edge_known_branches(function, stats, 0);
}

bool fold_edge_known_branches(Function * function, Stats * stats,
                              CleanupCfgScratch * reusable_scratch)
{
  CleanupCfgScratch owned_scratch;
  CleanupCfgScratch & scratch = reusable_scratch ?
    *reusable_scratch : owned_scratch;
  return fold_edge_known_branches_with_scratch(
    function, stats, &scratch.branch_values);
}

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
    // The merge block's targets may hold phis naming it as a predecessor;
    // the parent now owns those edges.
    lowir_phi_edges::rewrite_moved_phi_edges(
      function, parent_branch, merge_block.id, function->blocks[parent].id);
    remove_unreachable_blocks(function, stats);
    return true;
  }
  return false;
}


namespace {

using lowir_model::BlockId;
using lowir_model::LowType;

const std::size_t kNoBlock = static_cast<std::size_t>(-1);
const std::size_t kNoBlockIndex = static_cast<std::size_t>(-1);

bool cleanup_is_eh_instruction(Instruction::Kind kind)
{
  return kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_EH_END;
}

bool is_pure(Instruction::Kind kind)
{
  return kind == Instruction::IK_CONST || kind == Instruction::IK_COPY ||
    kind == Instruction::IK_PHI ||
    kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

}  // namespace

std::vector<BlockId> bypass_targets(const Function & function,
                                    const Graph & graph)
{
  const std::size_t count = function.blocks.size();
  std::vector<std::size_t> next(count, kNoBlock);
  for(std::size_t i = 0; i < count; ++i) {
    const Block & block = function.blocks[i];
    if(graph.eh_targets[static_cast<std::uint32_t>(block.id)] ||
       block.instructions.size() != 1 ||
       block.instructions[0].kind != Instruction::IK_JUMP) continue;
    const std::size_t found = graph.find(block.instructions[0].first.block);
    if(found != kNoBlockIndex) next[i] = found;
  }
  std::vector<BlockId> result(count);
  std::vector<unsigned char> state(count, 0);
  for(std::size_t start = 0; start < count; ++start) {
    if(state[start] == 2) continue;
    std::vector<std::size_t> path;
    std::size_t cursor = start;
    while(state[cursor] == 0 && next[cursor] != kNoBlock) {
      state[cursor] = 1;
      path.push_back(cursor);
      cursor = next[cursor];
    }
    if(state[cursor] == 0) {
      state[cursor] = 2;
      result[cursor] = function.blocks[cursor].id;
    }
    if(state[cursor] == 1) {
      std::size_t cycle = 0;
      while(cycle < path.size() && path[cycle] != cursor) ++cycle;
      for(std::size_t i = cycle; i < path.size(); ++i) {
        result[path[i]] = function.blocks[path[i]].id;
        state[path[i]] = 2;
      }
      for(std::size_t i = cycle; i > 0; --i) {
        result[path[i - 1]] = function.blocks[cursor].id;
        state[path[i - 1]] = 2;
      }
      continue;
    }
    const BlockId target = result[cursor];
    for(std::size_t i = path.size(); i > 0; --i) {
      result[path[i - 1]] = target;
      state[path[i - 1]] = 2;
    }
  }
  for(std::size_t i = 0; i < count; ++i)
    if(!result[i].valid()) result[i] = function.blocks[i].id;
  return result;
}

void rewrite_as_jump(Instruction * terminal, Operand target)
{
  const lowir_model::InstructionDebugLocation debug =
    terminal->debug_location;
  *terminal = Instruction();
  terminal->kind = Instruction::IK_JUMP;
  terminal->first = std::move(target);
  terminal->debug_location = debug;
}

bool fold_terminal_control(Function * function)
{
  bool changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    Block & block = function->blocks[i];
    if(block.instructions.empty()) continue;
    Instruction & term = block.instructions.back();
    if(term.kind == Instruction::IK_BRANCH) {
      if(term.first.kind == Operand::OP_INTEGER && term.first.has_int_value) {
        const Operand selected = term.first.int_value ? term.second : term.third;
        rewrite_as_jump(&term, selected);
        changed = true;
      } else if(term.second.block == term.third.block) {
        rewrite_as_jump(&term, term.second);
        changed = true;
      }
    } else if(term.kind == Instruction::IK_SWITCH &&
              term.first.kind == Operand::OP_INTEGER &&
              term.first.has_int_value) {
      Operand selected = term.second;
      for(std::size_t j = 0; j + 1 < term.args.size(); j += 2)
        if(term.args[j].kind == Operand::OP_INTEGER &&
           term.args[j].has_int_value &&
           term.args[j].int_value == term.first.int_value) {
          selected = term.args[j + 1];
          break;
        }
      rewrite_as_jump(&term, selected);
      changed = true;
    }
  }
  return changed;
}

bool function_has_phi(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function.blocks[block].instructions.size();
        ++instruction)
      if(function.blocks[block].instructions[instruction].kind ==
         Instruction::IK_PHI) return true;
  return false;
}

bool cleanup_cfg(Function * function, Stats * stats)
{
  return cleanup_cfg(function, stats, 0);
}

bool cleanup_cfg(Function * function, Stats * stats, CleanupCfgScratch * scratch)
{
  if(function->blocks.empty()) return false;
  CleanupCfgScratch owned_scratch;
  CleanupCfgScratch & active_scratch = scratch ? *scratch : owned_scratch;
  bool changed = fold_edge_known_branches_with_scratch(
    function, stats, &active_scratch.branch_values);
  changed = fold_direct_boolean_phi_branches(function, stats) || changed;
  changed = fold_boolean_phi_branch(function, stats) || changed;
  // Phi predecessor identities are part of the instruction contract.  Phi
  // construction runs after CFG cleanup; a later optimizer round trip keeps
  // that CFG stable until edge-aware repair is requested by a transform.
  if(function_has_phi(*function)) return changed;
  changed = fold_terminal_control(function) || changed;

  // There are no unreachable blocks, bypass chains, or merge candidates in a
  // one-block function.  Terminal folding above is the complete CFG cleanup.
  if(function->blocks.size() == 1) return changed;

  Graph graph = build_graph(*function, stats);
  const std::vector<BlockId> bypass = bypass_targets(*function, graph);
  bool graph_targets_changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction & ins = function->blocks[i].instructions[j];
      Operand * targets[3] = {0, 0, 0};
      std::size_t count = 0;
      if(ins.kind == Instruction::IK_JUMP || ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) targets[count++] = &ins.first;
      else if(ins.kind == Instruction::IK_BRANCH) {
        targets[count++] = &ins.second; targets[count++] = &ins.third;
      } else if(ins.kind == Instruction::IK_SWITCH) targets[count++] = &ins.second;
      for(std::size_t k = 0; k < count; ++k) {
        const std::size_t found = graph.find(targets[k]->block);
        const BlockId target = found == kNoBlockIndex ?
          targets[k]->block : bypass[found];
        if(target != targets[k]->block &&
           ins.kind != Instruction::IK_EH_TRY &&
           ins.kind != Instruction::IK_EH_CLEANUP) {
          targets[k]->block = target;
          changed = true;
          graph_targets_changed = true;
        }
      }
      if(ins.kind == Instruction::IK_SWITCH)
        for(std::size_t k = 1; k < ins.args.size(); k += 2) {
          const std::size_t found = graph.find(ins.args[k].block);
          const BlockId target = found == kNoBlockIndex ?
            ins.args[k].block : bypass[found];
          if(target != ins.args[k].block) {
            ins.args[k].block = target;
            changed = true;
            graph_targets_changed = true;
          }
        }
      if(ins.kind == Instruction::IK_BRANCH &&
         ins.second.block == ins.third.block) {
        const Operand selected = ins.second;
        rewrite_as_jump(&ins, selected);
        changed = true;
      }
    }
  }

  if(graph_targets_changed) graph = build_graph(*function, stats);
  std::vector<unsigned char> reachable(function->blocks.size(), 0);
  std::deque<std::size_t> work;
  reachable[0] = 1;
  work.push_back(0);
  while(!work.empty()) {
    const std::size_t block = work.front(); work.pop_front();
    for(std::size_t i = 0; i < graph.successors[block].size(); ++i) {
      const std::size_t next = graph.successors[block][i];
      if(!reachable[next]) { reachable[next] = 1; work.push_back(next); }
    }
  }

  const bool has_unreachable =
    std::find(reachable.begin(), reachable.end(), 0) != reachable.end();

  // EH cleanup code can intentionally use an address computed on a source
  // edge which constant folding proves untaken.  The address is still part of
  // the cleanup contract, so rematerialize simple dead-edge definitions at
  // the entry before pruning that edge.
  if(has_unreachable) {
    struct Definition { std::size_t block; Instruction instruction; };
    std::vector<Definition> definitions(function->value_names.size());
    std::vector<unsigned char> defined(function->value_names.size(), 0);
    std::vector<std::vector<lowir_model::ValueId> > dependencies(
      function->value_names.size());
    for(std::size_t i = 0; i < function->blocks.size(); ++i)
      for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(!ins.dest.valid()) continue;
      definitions[ins.dest] = Definition{i, ins};
      defined[ins.dest] = 1;
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(operands[k]->value);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(ins.args[k].value);
      }
    std::vector<unsigned char> available(function->value_names.size(), 0);
    for(std::size_t i = 0; i < function->params.size(); ++i)
      available[function->params[i].value] = 1;
    const std::size_t entry_end = function->blocks[0].instructions.empty() ? 0 :
      function->blocks[0].instructions.size() - 1;
    for(std::size_t i = 0; i < entry_end; ++i)
      if(function->blocks[0].instructions[i].dest.valid())
        available[function->blocks[0].instructions[i].dest] = 1;
    std::vector<Instruction> rematerialized;
    const auto eligible_definition = [&](lowir_model::ValueId value) {
      return defined[value] && !reachable[definitions[value].block] &&
        is_pure(definitions[value].instruction.kind);
    };
    const auto rematerialize = [&](lowir_model::ValueId value) {
      if(available[value]) return true;
      if(!eligible_definition(value)) return false;
      struct Frame { lowir_model::ValueId value; std::size_t dependency; };
      std::vector<Frame> stack(1, Frame{value, 0});
      std::vector<unsigned char> active(function->value_names.size(), 0);
      active[value] = 1;
      while(!stack.empty()) {
        Frame & frame = stack.back();
        const std::vector<lowir_model::ValueId> & required =
          dependencies[frame.value];
        while(frame.dependency < required.size() &&
              available[required[frame.dependency]])
          ++frame.dependency;
        if(frame.dependency < required.size()) {
          const lowir_model::ValueId dependency = required[frame.dependency++];
          if(active[dependency] || !eligible_definition(dependency))
            return false;
          active[dependency] = 1;
          stack.push_back(Frame{dependency, 0});
          continue;
        }
        rematerialized.push_back(definitions[frame.value].instruction);
        available[frame.value] = 1;
        active[frame.value] = 0;
        stack.pop_back();
      }
      return true;
    };
    for(std::size_t i = 0; i < function->blocks.size(); ++i) if(reachable[i])
      for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
        const Instruction & ins = function->blocks[i].instructions[j];
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t k = 0; k < 3; ++k)
          if(operands[k]->kind == Operand::OP_TEMP &&
             defined[operands[k]->value] &&
             !reachable[definitions[operands[k]->value].block])
            rematerialize(operands[k]->value);
        for(std::size_t k = 0; k < ins.args.size(); ++k)
          if(ins.args[k].kind == Operand::OP_TEMP &&
             defined[ins.args[k].value] &&
             !reachable[definitions[ins.args[k].value].block])
            rematerialize(ins.args[k].value);
      }
    if(!rematerialized.empty()) {
      function->blocks[0].instructions.insert(
        function->blocks[0].instructions.begin() + entry_end,
        rematerialized.begin(), rematerialized.end());
      changed = true;
      if(stats) stats->rewrites += rematerialized.size();
    }

    std::vector<Block> live;
    live.reserve(function->blocks.size());
    for(std::size_t i = 0; i < function->blocks.size(); ++i) {
      if(reachable[i]) live.push_back(std::move(function->blocks[i]));
      else changed = true;
    }
    function->blocks.swap(live);

    graph = build_graph(*function, stats);
  }
  std::vector<unsigned char> block_has_eh(function->blocks.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j)
      block_has_eh[i] = block_has_eh[i] ||
        cleanup_is_eh_instruction(block.instructions[j].kind);
  }
  std::vector<std::size_t> merge_next(function->blocks.size(), kNoBlock),
    merge_parent(function->blocks.size(), kNoBlock);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    if(block.instructions.empty() ||
       block.instructions.back().kind != Instruction::IK_JUMP) continue;
    const std::size_t target =
      graph.find(block.instructions.back().first.block);
    // A backward merge would relocate the target after blocks that may use
    // values it defines, violating LowIR's presentation-order requirement.
    if(target == kNoBlockIndex || target <= i ||
       block_has_eh[i] || block_has_eh[target] ||
       graph.eh_targets[static_cast<std::uint32_t>(block.id)] ||
       graph.eh_targets[static_cast<std::uint32_t>(
         block.instructions.back().first.block)] ||
       graph.predecessors[target].size() != 1) continue;
    merge_next[i] = target;
    merge_parent[target] = i;
  }
  std::vector<unsigned char> consumed(function->blocks.size(), 0);
  std::vector<Block> merged(function->blocks.size());
  std::size_t merged_edges = 0;
  for(std::size_t head = 0; head < function->blocks.size(); ++head) {
    if(merge_next[head] == kNoBlock || merge_parent[head] != kNoBlock) continue;
    merged[head] = std::move(function->blocks[head]);
    std::size_t cursor = head;
    while(merge_next[cursor] != kNoBlock) {
      const std::size_t target = merge_next[cursor];
      consumed[target] = 1;
      merged[head].instructions.pop_back();
      merged[head].instructions.insert(merged[head].instructions.end(),
        std::make_move_iterator(function->blocks[target].instructions.begin()),
        std::make_move_iterator(function->blocks[target].instructions.end()));
      cursor = target;
      ++merged_edges;
    }
  }
  if(merged_edges) {
    std::vector<Block> compact;
    compact.reserve(function->blocks.size() - merged_edges);
    for(std::size_t i = 0; i < function->blocks.size(); ++i) {
      if(consumed[i]) continue;
      if(!merged[i].id.valid())
        compact.push_back(std::move(function->blocks[i]));
      else compact.push_back(std::move(merged[i]));
    }
    function->blocks.swap(compact);
    changed = true;
    if(stats) stats->rewrites += merged_edges;
  }
  return changed;
}

}  // namespace lowir_opt
