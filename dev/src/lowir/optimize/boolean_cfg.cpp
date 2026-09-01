#include "lowir/optimize/boolean_cfg.h"

#include "lowir/analysis/function.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/support.h"
#include "lowir/analysis/phi_edges.h"
#include "lowir/optimize/scalar_rules.h"

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
const Instruction * local_definition(
    const Block & block, lowir_model::ValueId value);

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

bool has_forwarded_boolean_phi_branch(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.size() == 3 &&
       instructions[0].kind == Instruction::IK_PHI &&
       instructions[1].kind == Instruction::IK_CONVERT &&
       instructions[2].kind == Instruction::IK_JUMP)
      return true;
  }
  return false;
}

bool has_zero_bounded_signed_branch_candidate(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.size() < 2) continue;
    const Instruction & compare = instructions[instructions.size() - 2];
    const Instruction & branch = instructions.back();
    if(compare.kind == Instruction::IK_CMP &&
       compare.op.kind == LowOperation::LOP_LT &&
       compare.first.kind == Operand::OP_TEMP &&
       lowir_opt::is_zero(compare.second) &&
       branch.kind == Instruction::IK_BRANCH &&
       branch.first.kind == Operand::OP_TEMP &&
       branch.first.value == compare.dest)
      return true;
  }
  return false;
}

void find_terminal_phi_candidates(const Function & function,
				  bool * return_chain,
				  bool * return_branch)
{
  *return_chain = false;
  *return_branch = false;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.size() >= 3 && instructions.size() <= 5 &&
       instructions.front().kind == Instruction::IK_PHI &&
       instructions.back().kind == Instruction::IK_RETURN)
      *return_chain = true;
    if(instructions.size() == 2 &&
       instructions.front().kind == Instruction::IK_PHI &&
       instructions.back().kind == Instruction::IK_BRANCH)
      *return_branch = true;
  }
}

bool has_secondary_temp_operand(const Instruction & instruction)
{
  return (instruction.second.kind == Operand::OP_TEMP &&
	  instruction.second.value.valid()) ||
    (instruction.third.kind == Operand::OP_TEMP &&
	  instruction.third.value.valid());
}

bool has_edge_known_branch_candidate(
    const Function & function, bool allow_integer_equality,
    std::vector<unsigned char> * seen)
{
  seen->assign(function.value_names.size(), 0);
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(!instructions.empty() &&
       instructions.back().kind == Instruction::IK_BRANCH &&
       instructions.back().first.kind == Operand::OP_TEMP) {
      const std::uint32_t value = instructions.back().first.value;
      if(value < seen->size()) {
        if((*seen)[value]) return true;
        (*seen)[value] = 1;
      }
      if(!allow_integer_equality) continue;
      const Instruction * predicate =
        local_definition(function.blocks[block], instructions.back().first.value);
      if(!predicate || predicate->kind != Instruction::IK_CMP)
        continue;
      lowir_model::ValueId tested;
      if(predicate->first.kind == Operand::OP_TEMP &&
         predicate->second.kind == Operand::OP_INTEGER &&
         predicate->second.has_int_value)
        tested = predicate->first.value;
      else if((predicate->op.kind == LowOperation::LOP_EQ ||
               predicate->op.kind == LowOperation::LOP_NE) &&
              predicate->second.kind == Operand::OP_TEMP &&
              predicate->first.kind == Operand::OP_INTEGER &&
              predicate->first.has_int_value)
        tested = predicate->second.value;
      else
        continue;
      if(!tested.valid() || tested >= seen->size()) continue;
      if((*seen)[tested]) return true;
      (*seen)[tested] = 1;
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

bool edge_establishes_integer_equality(
    const Block & predecessor,
    bool true_edge,
    Operand * value,
    Operand * constant)
{
  if(predecessor.instructions.empty()) return false;
  const Instruction & terminal = predecessor.instructions.back();
  if(terminal.kind != Instruction::IK_BRANCH ||
     terminal.first.kind != Operand::OP_TEMP)
    return false;
  const Instruction * predicate =
    local_definition(predecessor, terminal.first.value);
  if(!predicate || predicate->kind != Instruction::IK_CMP)
    return false;
  Operand tested;
  Operand literal;
  if(predicate->first.kind == Operand::OP_TEMP &&
     predicate->second.kind == Operand::OP_INTEGER &&
     predicate->second.has_int_value) {
    tested = predicate->first;
    literal = predicate->second;
  } else if((predicate->op.kind == LowOperation::LOP_EQ ||
             predicate->op.kind == LowOperation::LOP_NE) &&
            predicate->second.kind == Operand::OP_TEMP &&
            predicate->first.kind == Operand::OP_INTEGER &&
            predicate->first.has_int_value) {
    tested = predicate->second;
    literal = predicate->first;
  } else {
    return false;
  }
  const bool equality =
    (predicate->op.kind == LowOperation::LOP_EQ && true_edge) ||
    (predicate->op.kind == LowOperation::LOP_NE && !true_edge) ||
    (predicate->first.kind == Operand::OP_TEMP &&
     lowir_opt::is_zero(literal) &&
     ((predicate->op.kind == LowOperation::LOP_ULE && true_edge) ||
      (predicate->op.kind == LowOperation::LOP_UGT && !true_edge))) ||
    (predicate->first.kind == Operand::OP_TEMP &&
     lowir_opt::is_one(literal) &&
     ((predicate->op.kind == LowOperation::LOP_ULT && true_edge) ||
      (predicate->op.kind == LowOperation::LOP_UGE && !true_edge)));
  if(!equality) return false;
  *value = tested;
  *constant = literal;
  return true;
}

bool equality_decides_branch(const Block & block,
                             const Instruction & branch,
                             const Operand & value,
                             const Operand & constant,
                             bool * condition)
{
  const Instruction * predicate =
    local_definition(block, branch.first.value);
  if(!predicate || predicate->kind != Instruction::IK_CMP ||
     (predicate->op.kind != LowOperation::LOP_EQ &&
      predicate->op.kind != LowOperation::LOP_NE))
    return false;
  const bool direct = predicate->first.kind == Operand::OP_TEMP &&
    predicate->first.value == value.value &&
    lowir_opt::same_operand(predicate->second, constant);
  const bool reversed = predicate->second.kind == Operand::OP_TEMP &&
    predicate->second.value == value.value &&
    lowir_opt::same_operand(predicate->first, constant);
  if(!direct && !reversed) return false;
  *condition = predicate->op.kind == LowOperation::LOP_EQ;
  return true;
}

bool fold_edge_known_branches_with_scratch(
    Function * function, Stats * stats,
    bool allow_integer_equality,
    std::vector<unsigned char> * branch_values)
{
  if(!has_edge_known_branch_candidate(
       *function, allow_integer_equality, branch_values))
    return false;
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
       incoming.first.kind != Operand::OP_TEMP)
      continue;
    const bool true_edge = incoming.second.kind == Operand::OP_LABEL &&
      incoming.second.block == current.id;
    const bool false_edge = incoming.third.kind == Operand::OP_LABEL &&
      incoming.third.block == current.id;
    if(true_edge == false_edge) continue;
    bool condition = true_edge;
    bool range_fold = false;
    if(incoming.first.value != branch.first.value) {
      if(!allow_integer_equality) continue;
      Operand value;
      Operand constant;
      if(!edge_establishes_integer_equality(
           function->blocks[predecessor], true_edge, &value, &constant) ||
         !equality_decides_branch(
           current, branch, value, constant, &condition))
        continue;
      range_fold = true;
    }
    const Operand selected = condition ? branch.second : branch.third;
    const Operand removed = condition ? branch.third : branch.second;
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
    if(stats) {
      ++stats->rewrites;
      if(range_fold) ++stats->predicate_range_folds;
    }
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

bool fold_zero_bounded_signed_branch(Function * function, Stats * stats)
{
  if(!has_zero_bounded_signed_branch_candidate(*function)) return false;
  const Graph graph = build_graph(*function, stats);
  const std::vector<std::size_t> uses = value_uses(*function);
  for(std::size_t lower_block = 0;
      lower_block < function->blocks.size(); ++lower_block) {
    Block & lower = function->blocks[lower_block];
    if(lower.instructions.size() < 2) continue;
    Instruction & lower_compare =
      lower.instructions[lower.instructions.size() - 2];
    Instruction & lower_branch = lower.instructions.back();
    if(lower_compare.kind != Instruction::IK_CMP ||
       lower_compare.op.kind != LowOperation::LOP_LT ||
       lower_compare.first.kind != Operand::OP_TEMP ||
       lower_compare.second.kind != Operand::OP_INTEGER ||
       !lower_compare.second.has_int_value ||
       lower_compare.second.int_value != 0 ||
       (lower_compare.type.kind != lowir_model::LTK_I8 &&
        lower_compare.type.kind != lowir_model::LTK_I16 &&
        lower_compare.type.kind != lowir_model::LTK_I32 &&
        lower_compare.type.kind != lowir_model::LTK_I64) ||
       !lower_compare.dest.valid() || lower_compare.dest >= uses.size() ||
       uses[lower_compare.dest] != 1 ||
       lower_branch.kind != Instruction::IK_BRANCH ||
       lower_branch.first.kind != Operand::OP_TEMP ||
       lower_branch.first.value != lower_compare.dest ||
       lower_branch.second.kind != Operand::OP_LABEL ||
       lower_branch.third.kind != Operand::OP_LABEL)
      continue;

    const lowir_model::BlockId rejected = lower_branch.second.block;
    const lowir_model::BlockId upper_id = lower_branch.third.block;
    const std::size_t upper_block = graph.find(upper_id);
    if(upper_block >= function->blocks.size() ||
       graph.predecessors[upper_block].size() != 1 ||
       graph.predecessors[upper_block][0] != lower_block)
      continue;
    const Block & upper = function->blocks[upper_block];
    if(upper.instructions.size() != 2 ||
       static_cast<std::uint32_t>(upper.id) >= graph.eh_targets.size() ||
       graph.eh_targets[static_cast<std::uint32_t>(upper.id)])
      continue;
    const Instruction & upper_compare = upper.instructions[0];
    const Instruction & upper_branch = upper.instructions[1];
    if(upper_compare.kind != Instruction::IK_CMP ||
       upper_compare.op.kind != LowOperation::LOP_GT ||
       upper_compare.first.kind != Operand::OP_TEMP ||
       upper_compare.first.value != lower_compare.first.value ||
       upper_compare.second.kind != Operand::OP_INTEGER ||
       !upper_compare.second.has_int_value ||
       upper_compare.second.int_value < 0 ||
       !lowir_model::same_lowir_type(
         upper_compare.type, lower_compare.type) ||
       !upper_compare.dest.valid() || upper_compare.dest >= uses.size() ||
       uses[upper_compare.dest] != 1 ||
       upper_branch.kind != Instruction::IK_BRANCH ||
       upper_branch.first.kind != Operand::OP_TEMP ||
       upper_branch.first.value != upper_compare.dest ||
       upper_branch.second.kind != Operand::OP_LABEL ||
       upper_branch.third.kind != Operand::OP_LABEL ||
       upper_branch.second.block != rejected)
      continue;
    // Both original rejection edges would become the same predecessor.  A
    // rejection phi can distinguish negative values from values above C, so
    // it cannot in general be represented after the edge collapse.
    if(block_has_phi(*function, graph, lower_branch.second)) continue;

    lower_compare.op.kind = LowOperation::LOP_UGT;
    lower_compare.second = upper_compare.second;
    lower_branch.third = upper_branch.third;
    lowir_phi_edges::rewrite_moved_phi_edges(
      function, lower_branch, upper.id, lower.id);
    if(stats) ++stats->predicate_range_folds;
    remove_unreachable_blocks(function, stats);
    return true;
  }
  return false;
}

bool fold_edge_known_branches(Function * function, Stats * stats)
{
  return fold_edge_known_branches(function, stats, 0, false);
}

bool fold_edge_known_branches(Function * function, Stats * stats,
                              CleanupCfgScratch * reusable_scratch)
{
  return fold_edge_known_branches(
    function, stats, reusable_scratch, false);
}

bool fold_edge_known_branches(Function * function, Stats * stats,
                              CleanupCfgScratch * reusable_scratch,
                              bool allow_integer_equality)
{
  CleanupCfgScratch owned_scratch;
  CleanupCfgScratch & scratch = reusable_scratch ?
    *reusable_scratch : owned_scratch;
  return fold_edge_known_branches_with_scratch(
    function, stats, allow_integer_equality, &scratch.branch_values);
}

bool propagate_edge_integer_equalities(Function * function, Stats * stats)
{
  const Graph graph = build_graph(*function, stats);
  const lowir_analysis::DominatorTree dom =
    lowir_analysis::dominators(graph, stats);
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    if(graph.predecessors[block].size() != 1 ||
       graph.eh_targets[static_cast<std::uint32_t>(
         function->blocks[block].id)])
      continue;
    const std::size_t predecessor = graph.predecessors[block][0];
    const Block & incoming_block = function->blocks[predecessor];
    if(incoming_block.instructions.empty()) continue;
    const Instruction & terminal = incoming_block.instructions.back();
    if(terminal.kind != Instruction::IK_BRANCH ||
       terminal.second.kind != Operand::OP_LABEL ||
       terminal.third.kind != Operand::OP_LABEL)
      continue;
    const bool true_edge = terminal.second.block == function->blocks[block].id;
    const bool false_edge = terminal.third.block == function->blocks[block].id;
    if(true_edge == false_edge) continue;
    Operand value;
    Operand constant;
    if(!edge_establishes_integer_equality(
         incoming_block, true_edge, &value, &constant))
      continue;

    std::size_t replacements = 0;
    const auto replace = [&value, &constant, &replacements](Operand * operand) {
      if(operand->kind == Operand::OP_TEMP &&
         operand->value == value.value) {
        *operand = constant;
        ++replacements;
      }
    };
    for(std::size_t dominated = 0;
        dominated < function->blocks.size(); ++dominated) {
      if(!dom.dominates(block, dominated)) continue;
      std::vector<Instruction> & instructions =
        function->blocks[dominated].instructions;
      for(std::size_t index = 0; index < instructions.size(); ++index) {
        Instruction & instruction = instructions[index];
        // A call operand can denote the storage of a by-address argument,
        // even when the value in that storage is known on this edge.  The
        // function-local pass has no direct-callee signature table, so keep
        // every call operand addressable instead of replacing its lvalue by
        // the equal literal.
        if(instruction.kind == Instruction::IK_PHI ||
           instruction.kind == Instruction::IK_CALL) continue;
        replace(&instruction.first);
        replace(&instruction.second);
        replace(&instruction.third);
        for(std::size_t argument = 0;
            argument < instruction.args.size(); ++argument)
          replace(&instruction.args[argument]);
      }
    }
    if(replacements) {
      changed = true;
      if(stats) {
        ++stats->predicate_range_folds;
        ++stats->edge_integer_equalities;
        stats->rewrites += replacements;
      }
    }
  }
  return changed;
}

namespace {

std::uint64_t integer_width_mask(const lowir_model::LowType & type)
{
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  return width >= 64 ? ~std::uint64_t(0) :
    (std::uint64_t(1) << width) - 1;
}

long long signed_integer_value(long long value,
                               const lowir_model::LowType & type)
{
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  if(width >= 64) return value;
  const std::uint64_t mask = integer_width_mask(type);
  std::uint64_t normalized = static_cast<std::uint64_t>(value) & mask;
  if(normalized & (std::uint64_t(1) << (width - 1))) normalized |= ~mask;
  return static_cast<long long>(normalized);
}

bool evaluate_integer_compare(const Instruction & compare,
                              const Operand & incoming, bool * result)
{
  if(incoming.kind != Operand::OP_INTEGER || !incoming.has_int_value ||
     lowir_model::lowir_type_bit_width(compare.type) == 0 ||
     lowir_model::lowir_type_bit_width(compare.type) > 64)
    return false;
  Operand left = compare.first;
  Operand right = compare.second;
  if(left.kind == Operand::OP_TEMP) left = incoming;
  else if(right.kind == Operand::OP_TEMP) right = incoming;
  else return false;
  if(left.kind != Operand::OP_INTEGER || !left.has_int_value ||
     right.kind != Operand::OP_INTEGER || !right.has_int_value)
    return false;
  const std::uint64_t mask = integer_width_mask(compare.type);
  const std::uint64_t unsigned_left =
    static_cast<std::uint64_t>(left.int_value) & mask;
  const std::uint64_t unsigned_right =
    static_cast<std::uint64_t>(right.int_value) & mask;
  const long long signed_left =
    signed_integer_value(left.int_value, compare.type);
  const long long signed_right =
    signed_integer_value(right.int_value, compare.type);
  switch(compare.op.kind) {
  case LowOperation::LOP_EQ: *result = unsigned_left == unsigned_right; break;
  case LowOperation::LOP_NE: *result = unsigned_left != unsigned_right; break;
  case LowOperation::LOP_LT: *result = signed_left < signed_right; break;
  case LowOperation::LOP_LE: *result = signed_left <= signed_right; break;
  case LowOperation::LOP_GT: *result = signed_left > signed_right; break;
  case LowOperation::LOP_GE: *result = signed_left >= signed_right; break;
  case LowOperation::LOP_ULT: *result = unsigned_left < unsigned_right; break;
  case LowOperation::LOP_ULE: *result = unsigned_left <= unsigned_right; break;
  case LowOperation::LOP_UGT: *result = unsigned_left > unsigned_right; break;
  case LowOperation::LOP_UGE: *result = unsigned_left >= unsigned_right; break;
  default: return false;
  }
  return true;
}

bool target_starts_with_phi(const Function & function, const Graph & graph,
                            const Operand & target)
{
  if(target.kind != Operand::OP_LABEL) return true;
  const std::size_t block = graph.find(target.block);
  return block >= function.blocks.size() ||
    graph.eh_targets[static_cast<std::uint32_t>(
      function.blocks[block].id)] ||
    (!function.blocks[block].instructions.empty() &&
     function.blocks[block].instructions.front().kind ==
       Instruction::IK_PHI);
}

}  // namespace

bool thread_constant_loop_phi_edge(Function * function, Stats * stats)
{
  const Graph graph = build_graph(*function, stats);
  const lowir_analysis::DominatorTree dom =
    lowir_analysis::dominators(graph, stats);
  for(std::size_t header = 0; header < function->blocks.size(); ++header) {
    Block & header_block = function->blocks[header];
    if(header_block.instructions.size() != 3 ||
       header_block.instructions[0].kind != Instruction::IK_PHI ||
       header_block.instructions[1].kind != Instruction::IK_CMP ||
       header_block.instructions[2].kind != Instruction::IK_BRANCH ||
       graph.eh_targets[static_cast<std::uint32_t>(header_block.id)])
      continue;
    Instruction & phi = header_block.instructions[0];
    const Instruction & compare = header_block.instructions[1];
    const Instruction & branch = header_block.instructions[2];
    if(!phi.dest.valid() || !compare.dest.valid() ||
       phi.args.size() < 4 || phi.args.size() % 2 != 0 ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.first.value != compare.dest ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL ||
       !((compare.first.kind == Operand::OP_TEMP &&
          compare.first.value == phi.dest &&
          compare.second.kind == Operand::OP_INTEGER &&
          compare.second.has_int_value) ||
         (compare.second.kind == Operand::OP_TEMP &&
          compare.second.value == phi.dest &&
          compare.first.kind == Operand::OP_INTEGER &&
          compare.first.has_int_value)))
      continue;
    for(std::size_t predecessor_index = 0;
        predecessor_index < graph.predecessors[header].size();
        ++predecessor_index) {
      const std::size_t predecessor =
        graph.predecessors[header][predecessor_index];
      if(predecessor == header || !dom.dominates(header, predecessor) ||
         graph.eh_targets[static_cast<std::uint32_t>(
           function->blocks[predecessor].id)])
        continue;
      Operand incoming;
      if(!direct_phi_incoming(
           phi, function->blocks[predecessor].id, &incoming))
        continue;
      bool condition = false;
      if(!evaluate_integer_compare(compare, incoming, &condition)) continue;
      const Operand selected = condition ? branch.second : branch.third;
      if(selected.block == header_block.id ||
         target_starts_with_phi(*function, graph, selected))
        continue;
      Instruction & terminal =
        function->blocks[predecessor].instructions.back();
      if(terminal.kind != Instruction::IK_JUMP ||
         terminal.first.kind != Operand::OP_LABEL ||
         terminal.first.block != header_block.id)
        continue;
      terminal.first = selected;
      for(std::size_t incoming_index = 0;
          incoming_index + 1 < phi.args.size(); incoming_index += 2)
        if(phi.args[incoming_index].kind == Operand::OP_LABEL &&
           phi.args[incoming_index].block ==
             function->blocks[predecessor].id) {
          phi.args.erase(phi.args.begin() + incoming_index,
                         phi.args.begin() + incoming_index + 2);
          break;
        }
      if(stats) {
        ++stats->predicate_range_folds;
        ++stats->constant_loop_phi_edges;
        ++stats->rewrites;
      }
      return true;
    }
  }
  return false;
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

bool fold_trivial_boolean_phi_diamond(Function * function, Stats * stats)
{
  if(!has_direct_boolean_phi_branch(*function)) return false;
  const std::vector<std::size_t> uses = value_uses(*function);
  const Graph graph = build_graph(*function, stats);
  for(std::size_t merge = 0; merge < function->blocks.size(); ++merge) {
    const Block & merge_block = function->blocks[merge];
    if(merge_block.instructions.size() != 2 ||
       merge_block.instructions[0].kind != Instruction::IK_PHI ||
       merge_block.instructions[1].kind != Instruction::IK_BRANCH)
      continue;
    const Instruction & phi = merge_block.instructions[0];
    const Instruction & branch = merge_block.instructions[1];
    if(!phi.dest.valid() || phi.type.kind != lowir_model::LTK_U8 ||
       phi.args.size() != 4 || phi.dest >= uses.size() ||
       uses[phi.dest] != 1 || branch.first.kind != Operand::OP_TEMP ||
       branch.first.value != phi.dest ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL ||
       graph.predecessors[merge].size() != 2)
      continue;

    const std::size_t left = graph.predecessors[merge][0];
    const std::size_t right = graph.predecessors[merge][1];
    if(left == right || graph.predecessors[left].size() != 1 ||
       graph.predecessors[right].size() != 1 ||
       graph.predecessors[left][0] != graph.predecessors[right][0])
      continue;
    const std::size_t parent = graph.predecessors[left][0];
    if(parent == merge || function->blocks[parent].instructions.empty())
      continue;
    const Block & left_block = function->blocks[left];
    const Block & right_block = function->blocks[right];
    if(left_block.instructions.size() != 1 ||
       right_block.instructions.size() != 1 ||
       left_block.instructions[0].kind != Instruction::IK_JUMP ||
       right_block.instructions[0].kind != Instruction::IK_JUMP ||
       left_block.instructions[0].first.kind != Operand::OP_LABEL ||
       right_block.instructions[0].first.kind != Operand::OP_LABEL ||
       left_block.instructions[0].first.block != merge_block.id ||
       right_block.instructions[0].first.block != merge_block.id)
      continue;
    Instruction & parent_branch = function->blocks[parent].instructions.back();
    if(parent_branch.kind != Instruction::IK_BRANCH ||
       parent_branch.second.kind != Operand::OP_LABEL ||
       parent_branch.third.kind != Operand::OP_LABEL)
      continue;
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
    const auto selected_target = [&phi, &branch](
        lowir_model::BlockId predecessor, Operand * selected) {
      Operand value;
      if(!direct_phi_incoming(phi, predecessor, &value) ||
         value.kind != Operand::OP_INTEGER || !value.has_int_value ||
         (value.int_value != 0 && value.int_value != 1))
        return false;
      *selected = value.int_value ? branch.second : branch.third;
      return true;
    };
    if(!selected_target(parent_branch.second.block, &selected_true) ||
       !selected_target(parent_branch.third.block, &selected_false) ||
       selected_true.block == selected_false.block)
      continue;
    parent_branch.second = selected_true;
    parent_branch.third = selected_false;
    lowir_phi_edges::rewrite_moved_phi_edges(
      function, parent_branch, merge_block.id, function->blocks[parent].id);
    remove_unreachable_blocks(function, stats);
    return true;
  }
  return false;
}

bool fold_forwarded_boolean_phi_branch(Function * function, Stats * stats)
{
  if(!has_forwarded_boolean_phi_branch(*function)) return false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function->blocks[block].instructions.size();
        ++instruction)
      if(cleanup_is_eh_instruction(
           function->blocks[block].instructions[instruction].kind))
        return false;
  bool changed = false;
  // Every successful iteration bypasses and removes the merge and its
  // continuation, so the repeated analysis is bounded by half the original
  // block population.
  for(;;) {
    const std::vector<std::size_t> uses = value_uses(*function);
    const Graph graph = build_graph(*function, stats);
    const lowir_analysis::DominatorTree dom =
      lowir_analysis::dominators(graph, stats);
    std::vector<const Instruction *> definitions(function->value_names.size(), 0);
    for(std::size_t block = 0; block < function->blocks.size(); ++block)
      for(std::size_t instruction = 0;
          instruction < function->blocks[block].instructions.size();
          ++instruction) {
        const Instruction & candidate =
          function->blocks[block].instructions[instruction];
        if(candidate.dest.valid() && candidate.dest < definitions.size())
          definitions[candidate.dest] = &candidate;
      }

    bool rewritten = false;
    for(std::size_t merge = 0;
        merge < function->blocks.size() && !rewritten; ++merge) {
      const Block & merge_block = function->blocks[merge];
      if(merge_block.instructions.size() != 3 ||
         merge_block.instructions[0].kind != Instruction::IK_PHI ||
         merge_block.instructions[1].kind != Instruction::IK_CONVERT ||
         merge_block.instructions[2].kind != Instruction::IK_JUMP ||
         graph.eh_targets[static_cast<std::uint32_t>(merge_block.id)])
        continue;
      const Instruction phi = merge_block.instructions[0];
      const Instruction convert = merge_block.instructions[1];
      const Instruction forward = merge_block.instructions[2];
      if(!phi.dest.valid() || !convert.dest.valid() ||
         phi.type.kind != lowir_model::LTK_I64 ||
         convert.op.kind != LowOperation::LOP_TRUNC ||
         convert.type.kind != lowir_model::LTK_U8 ||
         convert.first.kind != Operand::OP_TEMP ||
         convert.first.value != phi.dest ||
         phi.dest >= uses.size() || uses[phi.dest] != 1 ||
         convert.dest >= uses.size() || uses[convert.dest] != 1 ||
         phi.args.size() < 4 || phi.args.size() % 2 != 0 ||
         forward.first.kind != Operand::OP_LABEL)
        continue;
      const std::size_t continuation = graph.find(forward.first.block);
      if(continuation == static_cast<std::size_t>(-1) ||
         continuation == merge ||
         graph.predecessors[continuation].size() != 1 ||
         graph.predecessors[continuation][0] != merge ||
         graph.eh_targets[static_cast<std::uint32_t>(
           function->blocks[continuation].id)])
        continue;
      const Block & continuation_block = function->blocks[continuation];
      if(continuation_block.instructions.size() != 1 ||
         continuation_block.instructions[0].kind != Instruction::IK_BRANCH)
        continue;
      const Instruction branch = continuation_block.instructions[0];
      if(branch.first.kind != Operand::OP_TEMP ||
         branch.first.value != convert.dest ||
         branch.second.kind != Operand::OP_LABEL ||
         branch.third.kind != Operand::OP_LABEL ||
         branch.second.block == merge_block.id ||
         branch.third.block == merge_block.id ||
         branch.second.block == continuation_block.id ||
         branch.third.block == continuation_block.id ||
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
        if(predecessor == merge || predecessor_block.instructions.empty() ||
           dom.dominates(merge, predecessor) ||
           graph.eh_targets[
             static_cast<std::uint32_t>(predecessor_block.id)] ||
           !direct_phi_incoming(phi, predecessor_block.id, &value)) {
          safe = false;
          break;
        }
        if(value.kind == Operand::OP_INTEGER) {
          if(!value.has_int_value ||
             (value.int_value != 0 && value.int_value != 1)) {
            safe = false;
            break;
          }
        } else if(value.kind == Operand::OP_TEMP) {
          const std::uint32_t id = value.value;
          if(id >= definitions.size() || !definitions[id] ||
             definitions[id]->kind != Instruction::IK_CMP) {
            safe = false;
            break;
          }
        } else {
          safe = false;
          break;
        }
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
        const Operand value = rewrites[rewrite].value;
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
      remove_unreachable_blocks(function, stats);
      changed = rewritten = true;
    }
    if(!rewritten) break;
  }
  return changed;
}

bool thread_terminal_phi_returns(Function * function, Stats * stats)
{
  if(function->blocks.empty() ||
     function->return_type.kind == lowir_model::LTK_VOID)
    return false;
  bool return_chain_candidate = false;
  bool return_branch_candidate = false;
  find_terminal_phi_candidates(
    *function, &return_chain_candidate, &return_branch_candidate);
  if(!return_chain_candidate && !return_branch_candidate) return false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function->blocks[block].instructions.size();
        ++instruction)
      if(cleanup_is_eh_instruction(
           function->blocks[block].instructions[instruction].kind))
        return false;

  struct IncomingRewrite
  {
    std::size_t predecessor;
    Operand value;
  };
  bool changed = false;
  if(return_chain_candidate) {
    const std::vector<std::size_t> uses = value_uses(*function);
    const Graph graph = build_graph(*function, stats);
    for(std::size_t merge = 0; merge < function->blocks.size(); ++merge) {
      const Block & merge_block = function->blocks[merge];
      if(merge_block.instructions.size() < 3 ||
         merge_block.instructions.size() > 5 ||
         merge_block.instructions[0].kind != Instruction::IK_PHI ||
         merge_block.instructions.back().kind != Instruction::IK_RETURN ||
         graph.eh_targets[static_cast<std::uint32_t>(merge_block.id)])
        continue;
      const Instruction phi = merge_block.instructions[0];
      const Instruction result = merge_block.instructions.back();
      if(!phi.dest.valid() || phi.args.size() != 4 ||
         phi.dest >= uses.size() || uses[phi.dest] != 1 ||
         !lowir_model::same_lowir_type(result.type, function->return_type) ||
         graph.predecessors[merge].size() != 2)
        continue;
      lowir_model::ValueId prior = phi.dest;
      bool linear = true;
      for(std::size_t chain = 1;
          chain + 1 < merge_block.instructions.size(); ++chain) {
        const Instruction & scalar = merge_block.instructions[chain];
        if((scalar.kind != Instruction::IK_CONVERT &&
            scalar.kind != Instruction::IK_CMP &&
            scalar.kind != Instruction::IK_UNARY) ||
           !scalar.dest.valid() || scalar.dest >= uses.size() ||
           uses[scalar.dest] != 1 ||
           scalar.first.kind != Operand::OP_TEMP ||
           scalar.first.value != prior ||
           has_secondary_temp_operand(scalar) || !scalar.args.empty()) {
          linear = false;
          break;
        }
        prior = scalar.dest;
      }
      if(!linear || result.first.kind != Operand::OP_TEMP ||
         result.first.value != prior)
        continue;

      std::vector<IncomingRewrite> rewrites;
      rewrites.reserve(2);
      bool safe = true;
      for(std::size_t incoming = 0;
          incoming < graph.predecessors[merge].size(); ++incoming) {
        const std::size_t predecessor = graph.predecessors[merge][incoming];
        const Block & predecessor_block = function->blocks[predecessor];
        Operand value;
        if(predecessor == merge || predecessor_block.instructions.empty() ||
           graph.eh_targets[
             static_cast<std::uint32_t>(predecessor_block.id)] ||
           !direct_phi_incoming(phi, predecessor_block.id, &value) ||
           (value.kind != Operand::OP_TEMP &&
            !(value.kind == Operand::OP_INTEGER && value.has_int_value))) {
          safe = false;
          break;
        }
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
        Block & predecessor = function->blocks[rewrites[rewrite].predecessor];
        predecessor.instructions.pop_back();
        Instruction returned = result;
        const bool direct_boolean_trunc =
          merge_block.instructions.size() == 3 &&
          phi.type.kind == lowir_model::LTK_I64 &&
          merge_block.instructions[1].kind == Instruction::IK_CONVERT &&
          merge_block.instructions[1].op.kind == LowOperation::LOP_TRUNC &&
          merge_block.instructions[1].type.kind == lowir_model::LTK_U8 &&
          rewrites[rewrite].value.kind == Operand::OP_INTEGER &&
           (rewrites[rewrite].value.int_value == 0 ||
            rewrites[rewrite].value.int_value == 1);
        if(direct_boolean_trunc) {
          returned.first = rewrites[rewrite].value;
        } else {
          Operand current = rewrites[rewrite].value;
          for(std::size_t chain = 1;
              chain + 1 < merge_block.instructions.size(); ++chain) {
            Instruction scalar = merge_block.instructions[chain];
            scalar.dest = lowir_model::append_lowir_fresh_generated_value(
              *function, scalar.type);
            scalar.first = current;
            current = Operand();
            current.kind = Operand::OP_TEMP;
            current.value = scalar.dest;
            predecessor.instructions.push_back(std::move(scalar));
            if(stats) ++stats->o3_terminal_phi_cloned_instructions;
          }
          returned.first = current;
        }
        predecessor.instructions.push_back(std::move(returned));
        if(stats) ++stats->rewrites;
      }
      if(stats) {
        ++stats->o3_terminal_phi_merges;
        stats->o3_terminal_phi_incoming_edges += rewrites.size();
      }
      remove_unreachable_blocks(function, stats);
      changed = true;
      break;
    }
  }

  if(!return_branch_candidate) return changed;

  const std::vector<std::size_t> uses = value_uses(*function);
  const Graph graph = build_graph(*function, stats);
  const lowir_analysis::DominatorTree dom =
    lowir_analysis::dominators(graph, stats);
  const lowir_analysis::LoopForest loops =
    lowir_analysis::discover_loops(*function, graph, dom, stats);
  for(std::size_t merge = 0; merge < function->blocks.size(); ++merge) {
    const Block & merge_block = function->blocks[merge];
    if(merge_block.instructions.size() != 2 ||
       merge_block.instructions[0].kind != Instruction::IK_PHI ||
       merge_block.instructions[1].kind != Instruction::IK_BRANCH ||
       graph.eh_targets[static_cast<std::uint32_t>(merge_block.id)] ||
       (merge_block.instructions[0].type.kind != lowir_model::LTK_U8 &&
        merge_block.instructions[0].type.kind != lowir_model::LTK_I64))
      continue;
    const Instruction phi = merge_block.instructions[0];
    const Instruction branch = merge_block.instructions[1];
    if(!phi.dest.valid() || phi.args.size() != 4 ||
       phi.dest >= uses.size() || uses[phi.dest] != 1 ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.first.value != phi.dest ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL ||
       merge >= loops.innermost_loop.size() ||
       loops.innermost_loop[merge] < loops.loops.size() ||
       graph.predecessors[merge].size() != 2 ||
       block_has_phi(*function, graph, branch.second) ||
       block_has_phi(*function, graph, branch.third))
      continue;
    const std::size_t true_target = graph.find(branch.second.block);
    const std::size_t false_target = graph.find(branch.third.block);
    if(true_target == static_cast<std::size_t>(-1) ||
       false_target == static_cast<std::size_t>(-1))
      continue;
    const bool true_returns =
      function->blocks[true_target].instructions.size() == 1 &&
      function->blocks[true_target].instructions[0].kind ==
        Instruction::IK_RETURN;
    const bool false_returns =
      function->blocks[false_target].instructions.size() == 1 &&
      function->blocks[false_target].instructions[0].kind ==
        Instruction::IK_RETURN;
    if(!true_returns && !false_returns) continue;

    std::vector<IncomingRewrite> rewrites;
    rewrites.reserve(2);
    bool safe = true;
    for(std::size_t incoming = 0;
        incoming < graph.predecessors[merge].size(); ++incoming) {
      const std::size_t predecessor = graph.predecessors[merge][incoming];
      const Block & predecessor_block = function->blocks[predecessor];
      Operand value;
      if(predecessor == merge || predecessor_block.instructions.empty() ||
         dom.dominates(merge, predecessor) ||
         graph.eh_targets[
           static_cast<std::uint32_t>(predecessor_block.id)] ||
         !direct_phi_incoming(phi, predecessor_block.id, &value) ||
         (value.kind != Operand::OP_TEMP &&
          !(value.kind == Operand::OP_INTEGER && value.has_int_value))) {
        safe = false;
        break;
      }
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
      const Operand value = rewrites[rewrite].value;
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
    if(stats) {
      ++stats->o3_terminal_phi_merges;
      stats->o3_terminal_phi_incoming_edges += rewrites.size();
    }
    remove_unreachable_blocks(function, stats);
    return true;
  }
  return changed;
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
    function, stats, false, &active_scratch.branch_values);
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
