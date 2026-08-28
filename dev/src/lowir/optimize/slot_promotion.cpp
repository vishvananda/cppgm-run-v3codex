#include "lowir/optimize/slot_promotion.h"

#include "lowir/analysis/eh_context.h"
#include "lowir/analysis/function.h"
#include "lowir/optimize/pipeline.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>

namespace lowir_opt {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using lowir_analysis::Graph;
using lowir_analysis::build_graph;

const std::size_t kNoBlockIndex = static_cast<std::size_t>(-1);

bool slot_is_phi_scalar_type(const LowType & type)
{
  return type.kind == lowir_model::LTK_I1 ||
    type.kind == lowir_model::LTK_I8 ||
    type.kind == lowir_model::LTK_U8 ||
    type.kind == lowir_model::LTK_I16 ||
    type.kind == lowir_model::LTK_U16 ||
    type.kind == lowir_model::LTK_I32 ||
    type.kind == lowir_model::LTK_U32 ||
    type.kind == lowir_model::LTK_I64 ||
    type.kind == lowir_model::LTK_F32 ||
    type.kind == lowir_model::LTK_F64 ||
    type.kind == lowir_model::LTK_PTR;
}

std::vector<unsigned char> find_promotable_slots(
    const Function & function, std::size_t * count)
{
  std::vector<unsigned char> eligible(function.slot_names.size(), 0);
  *count = 0;
  for(std::size_t i = 0; i < function.slots.size(); ++i) {
    const lowir_model::SlotId slot = function.slots[i];
    if(lowir_model::lowir_slot_type(function, slot).kind !=
       lowir_model::LTK_OBJECT) {
      eligible[slot] = 1;
      ++*count;
    }
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function.blocks[i].instructions[j];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           (ins.volatile_access ||
            !((ins.kind == Instruction::IK_LOAD && k == 0) ||
              (ins.kind == Instruction::IK_STORE && k == 1))) &&
           eligible[values[k]->slot]) {
          eligible[values[k]->slot] = 0;
          --*count;
        }
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT && eligible[ins.args[k].slot]) {
          eligible[ins.args[k].slot] = 0;
          --*count;
        }
    }
  return eligible;
}

namespace {
void normal_successors(const Function & function, const Graph & graph,
                       std::size_t block, std::vector<std::size_t> * out)
{
  if(function.blocks[block].instructions.empty()) return;
  const Instruction & term = function.blocks[block].instructions.back();
  const Operand * targets[2] = {0, 0};
  if(term.kind == Instruction::IK_JUMP) targets[0] = &term.first;
  else if(term.kind == Instruction::IK_BRANCH) {
    targets[0] = &term.second; targets[1] = &term.third;
  }
  for(std::size_t i = 0; i < 2; ++i)
    if(targets[i] && graph.find(targets[i]->block) != kNoBlockIndex)
      out->push_back(graph.find(targets[i]->block));
  if(term.kind == Instruction::IK_SWITCH) {
    if(graph.find(term.second.block) != kNoBlockIndex)
      out->push_back(graph.find(term.second.block));
    for(std::size_t i = 1; i < term.args.size(); i += 2)
      if(graph.find(term.args[i].block) != kNoBlockIndex)
        out->push_back(graph.find(term.args[i].block));
  }
}

bool exceeds_state_budget(std::size_t blocks, std::size_t facts,
                          std::size_t instructions)
{
  const std::size_t scale = blocks + facts + instructions + 1;
  const std::size_t budget = scale >
      std::numeric_limits<std::size_t>::max() / 16 ?
    std::numeric_limits<std::size_t>::max() : scale * 16;
  return facts != 0 && blocks > budget / facts;
}
}  // namespace

bool eliminate_dead_slot_stores(Function * function, Stats * stats)
{
  if(function->slots.empty() || function->blocks.empty()) return false;
  const std::size_t slot_count = function->slot_names.size();
  std::vector<unsigned char> escaped(slot_count, 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_SLOT &&
           (ins.volatile_access ||
            !((ins.kind == Instruction::IK_LOAD && k == 0) ||
              (ins.kind == Instruction::IK_STORE && k == 1))))
          escaped[operands[k]->slot] = 1;
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT)
          escaped[ins.args[k].slot] = 1;
    }
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  if(exceeds_state_budget(function->blocks.size(), function->slots.size(),
                          instruction_total)) {
    if(stats) ++stats->budget_skips;
    return false;
  }
  bool linear_single_block = function->blocks.size() == 1;
  if(linear_single_block) {
    const Block & block = function->blocks[0];
    for(std::size_t i = 0; i < block.instructions.size(); ++i)
      if(lowir_eh_context::is_eh_instruction(block.instructions[i].kind)) {
        linear_single_block = false;
        break;
      }
    if(linear_single_block && !block.instructions.empty()) {
      const Instruction & term = block.instructions.back();
      if((term.kind == Instruction::IK_JUMP &&
          term.first.block == block.id) ||
         (term.kind == Instruction::IK_BRANCH &&
          (term.second.block == block.id || term.third.block == block.id)))
        linear_single_block = false;
      if(term.kind == Instruction::IK_SWITCH) {
        linear_single_block = term.second.block != block.id;
        for(std::size_t i = 1;
            linear_single_block && i < term.args.size(); i += 2)
          linear_single_block = term.args[i].block != block.id;
      }
    }
  }
  if(linear_single_block) {
    std::vector<unsigned char> live(slot_count, 0);
    std::vector<Instruction> & instructions =
      function->blocks[0].instructions;
    std::vector<unsigned char> dead(instructions.size(), 0);
    std::size_t removed = 0;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if(ins.kind == Instruction::IK_LOAD &&
         ins.first.kind == Operand::OP_SLOT)
        live[ins.first.slot] = 1;
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped[ins.second.slot]) {
        if(!live[ins.second.slot]) {
          dead[index - 1] = 1;
          ++removed;
          if(stats) ++stats->rewrites;
          continue;
        }
        live[ins.second.slot] = 0;
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live[operands[i]->slot] = 1;
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live[ins.args[i].slot] = 1;
      }
    }
    if(!removed) return false;
    std::size_t kept = 0;
    for(std::size_t index = 0; index < instructions.size(); ++index)
      if(!dead[index]) {
        if(kept != index) instructions[kept] = std::move(instructions[index]);
        ++kept;
      }
    instructions.resize(kept);
    return true;
  }
  const Graph graph = build_graph(*function, stats);
  // Word-packed live sets: the dense per-slot byte vectors dominated this
  // pass's cost on post-inline functions (merge, compare, and a heap
  // allocation per block visit).  Identical liveness, identical removals.
  const std::size_t words = (slot_count + 63) / 64;
  typedef std::vector<std::uint64_t> LiveSlots;
  std::vector<LiveSlots> live_in(
    function->blocks.size(), LiveSlots(words, 0));
  const auto live_test = [](const LiveSlots & set, std::size_t slot) {
    return ((set[slot >> 6] >> (slot & 63)) & 1) != 0;
  };
  const auto live_set = [](LiveSlots & set, std::size_t slot) {
    set[slot >> 6] |= std::uint64_t(1) << (slot & 63);
  };
  const auto live_clear = [](LiveSlots & set, std::size_t slot) {
    set[slot >> 6] &= ~(std::uint64_t(1) << (slot & 63));
  };
  const auto live_merge = [words](LiveSlots & into, const LiveSlots & from) {
    for(std::size_t w = 0; w < words; ++w) into[w] |= from[w];
  };
  LiveSlots live(words, 0);
  std::vector<std::size_t> successors;
  const auto transfer = [&](std::size_t block) {
    live.assign(words, 0);
    successors.clear();
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      live_merge(live, live_in[successors[i]]);
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.find(ins.first.block) != kNoBlockIndex)
        live_merge(live, live_in[graph.find(ins.first.block)]);
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live_set(live, ins.first.slot);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped[ins.second.slot])
        live_clear(live, ins.second.slot);
      else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live_set(live, operands[i]->slot);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live_set(live, ins.args[i].slot);
      }
      if(stats) ++stats->instruction_visits;
    }
  };
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 1);
  for(std::size_t reverse = function->blocks.size(); reverse > 0; --reverse)
    work.push_back(reverse - 1);
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    queued[block] = 0;
    transfer(block);
    if(live == live_in[block]) continue;
    live_in[block].swap(live);
    if(stats) ++stats->dataflow_updates;
    for(std::size_t i = 0; i < graph.predecessors[block].size(); ++i) {
      const std::size_t predecessor = graph.predecessors[block][i];
      if(queued[predecessor]) continue;
      queued[predecessor] = 1;
      work.push_back(predecessor);
      if(stats) ++stats->worklist_pushes;
    }
  }

  bool changed = false;
  for(std::size_t reverse = function->blocks.size(); reverse > 0; --reverse) {
    const std::size_t block = reverse - 1;
    live.assign(words, 0);
    successors.clear();
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      live_merge(live, live_in[successors[i]]);
    std::vector<Instruction> kept_reverse;
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.find(ins.first.block) != kNoBlockIndex)
        live_merge(live, live_in[graph.find(ins.first.block)]);
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live_set(live, ins.first.slot);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped[ins.second.slot]) {
        if(!live_test(live, ins.second.slot)) {
          changed = true;
          if(stats) ++stats->rewrites;
          continue;
        }
        live_clear(live, ins.second.slot);
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live_set(live, operands[i]->slot);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live_set(live, ins.args[i].slot);
      }
      kept_reverse.push_back(std::move(ins));
    }
    std::reverse(kept_reverse.begin(), kept_reverse.end());
    function->blocks[block].instructions.swap(kept_reverse);
  }
  return changed;
}

}  // namespace lowir_opt
