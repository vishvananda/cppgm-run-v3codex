#include "lowir_loop_opt.h"

#include "lowir_opt.h"
#include "lowir_optimizer_support.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::Operand;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);

struct Location
{
  std::size_t block;
  std::size_t instruction;
};

bool speculatable(const Instruction & ins)
{
  if(ins.debug_location.present()) return false;
  if(ins.kind == Instruction::IK_CONST || ins.kind == Instruction::IK_COPY ||
     ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX ||
     ins.kind == Instruction::IK_UNARY || ins.kind == Instruction::IK_CMP ||
     ins.kind == Instruction::IK_CONVERT)
    return true;
  if(ins.kind != Instruction::IK_BINARY) return false;
  return ins.op.kind != lowir_model::LowOperation::LOP_DIV &&
    ins.op.kind != lowir_model::LowOperation::LOP_UDIV &&
    ins.op.kind != lowir_model::LowOperation::LOP_MOD &&
    ins.op.kind != lowir_model::LowOperation::LOP_UMOD;
}

bool unsupported_slot_operand(const Instruction & ins)
{
  const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
  for(std::size_t i = 0; i < 3; ++i)
    if(operands[i]->kind == Operand::OP_SLOT &&
       !(ins.kind == Instruction::IK_ADDR && i == 0))
      return true;
  for(std::size_t i = 0; i < ins.args.size(); ++i)
    if(ins.args[i].kind == Operand::OP_SLOT) return true;
  return false;
}

bool call_may_write_memory(const Instruction & ins)
{
  return ins.kind == Instruction::IK_CALL &&
    ins.call_boundary.effects != lowir_model::CFXM_READNONE &&
    ins.call_boundary.effects != lowir_model::CFXM_READONLY;
}

void collect_temp_operands(const Instruction & ins,
                           std::vector<lowir_model::ValueId> * values)
{
  const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
  for(std::size_t i = 0; i < 3; ++i)
    if(operands[i]->kind == Operand::OP_TEMP)
      values->push_back(operands[i]->value);
  for(std::size_t i = 0; i < ins.args.size(); ++i)
    if(ins.args[i].kind == Operand::OP_TEMP)
      values->push_back(ins.args[i].value);
}

void rewrite_block_target(Instruction * instruction,
                          lowir_model::BlockId old_target,
                          lowir_model::BlockId new_target)
{
  Operand * operands[] = {
    &instruction->first, &instruction->second, &instruction->third};
  for(std::size_t i = 0; i < 3; ++i)
    if(operands[i]->kind == Operand::OP_LABEL &&
       operands[i]->block == old_target)
      operands[i]->block = new_target;
  for(std::size_t i = 1; i < instruction->args.size(); i += 2)
    if(instruction->args[i].kind == Operand::OP_LABEL &&
       instruction->args[i].block == old_target)
      instruction->args[i].block = new_target;
}

bool has_two_root_invariants(
    const Function & function, const lowir_analysis::NaturalLoop & loop,
    const std::vector<std::size_t> & definition_block)
{
  std::size_t count = 0;
  std::vector<lowir_model::ValueId> operands;
  for(std::size_t member = 0; member < loop.blocks.size(); ++member) {
    const std::vector<Instruction> & instructions =
      function.blocks[loop.blocks[member]].instructions;
    for(std::size_t instruction = 0;
        instruction < instructions.size(); ++instruction) {
      const Instruction & candidate = instructions[instruction];
      if(!candidate.dest.valid() || !speculatable(candidate) ||
         unsupported_slot_operand(candidate)) continue;
      operands.clear();
      collect_temp_operands(candidate, &operands);
      bool invariant = true;
      for(std::size_t operand = 0; operand < operands.size(); ++operand) {
        const std::uint32_t value = operands[operand];
        if(value < definition_block.size() &&
           definition_block[value] != kNoIndex &&
           loop.contains(definition_block[value])) {
          invariant = false;
          break;
        }
      }
      if(invariant && ++count == 2) return true;
    }
  }
  return false;
}

lowir_model::StringId fresh_preheader_label(lowir_model::Program * program,
                                             const Function & function)
{
  if(program->presentation_policy == lowir_model::PRESENTATION_OBJECT_ONLY)
    return lowir_model::StringId();
  std::size_t ordinal = function.next_block_id;
  for(;;) {
    const lowir_model::StringId candidate = program->strings.intern(
      "__loop_preheader_" + std::to_string(ordinal++));
    if(std::find(function.block_labels.begin(), function.block_labels.end(),
                 candidate) == function.block_labels.end())
      return candidate;
  }
}

std::size_t create_bounded_preheaders(
    lowir_model::Program * program, Function * function,
    const lowir_analysis::Graph & graph,
    const lowir_analysis::LoopForest & forest,
    const std::vector<std::size_t> & definition_block, Stats * stats)
{
  if(function->blocks.size() >= 16384) return 0;
  const std::size_t budget = std::min<std::size_t>(
    8, 16384 - function->blocks.size());
  const std::size_t original_block_count = function->blocks.size();
  std::vector<lowir_model::BlockId> preheader_headers;
  std::size_t created = 0;
  for(std::size_t loop_index = 0;
      loop_index < forest.loops.size() && created < budget; ++loop_index) {
    const lowir_analysis::NaturalLoop & loop = forest.loops[loop_index];
    if(loop.preheader != kNoIndex || loop.has_eh ||
       !has_two_root_invariants(*function, loop, definition_block)) continue;
    std::size_t predecessor = kNoIndex;
    bool multiple = false;
    for(std::size_t edge = 0;
        edge < graph.predecessors[loop.header].size(); ++edge) {
      const std::size_t candidate = graph.predecessors[loop.header][edge];
      if(loop.contains(candidate)) continue;
      if(predecessor != kNoIndex) multiple = true;
      predecessor = candidate;
    }
    if(multiple || predecessor == kNoIndex ||
       graph.successors[predecessor].size() < 2 ||
       function->blocks[predecessor].instructions.empty()) continue;

    const lowir_model::BlockId header_id = function->blocks[loop.header].id;
    lowir_model::Block preheader;
    preheader.id = lowir_model::allocate_lowir_block_id(
      *function, fresh_preheader_label(program, *function));
    Instruction jump;
    jump.kind = Instruction::IK_JUMP;
    jump.first.kind = Operand::OP_LABEL;
    jump.first.block = header_id;
    preheader.instructions.push_back(jump);

    rewrite_block_target(
      &function->blocks[predecessor].instructions.back(),
      header_id, preheader.id);
    std::vector<Instruction> & header =
      function->blocks[loop.header].instructions;
    for(std::size_t instruction = 0;
        instruction < header.size() &&
        header[instruction].kind == Instruction::IK_PHI; ++instruction)
      for(std::size_t incoming = 0;
          incoming + 1 < header[instruction].args.size(); incoming += 2)
        if(header[instruction].args[incoming].block ==
           function->blocks[predecessor].id)
          header[instruction].args[incoming].block = preheader.id;
    function->blocks.push_back(std::move(preheader));
    preheader_headers.push_back(header_id);
    ++created;
  }
  if(created) {
    std::vector<lowir_model::Block> ordered;
    ordered.reserve(function->blocks.size());
    for(std::size_t block = 0; block < original_block_count; ++block) {
      for(std::size_t generated = 0;
          generated < preheader_headers.size(); ++generated)
        if(preheader_headers[generated] == function->blocks[block].id)
          ordered.push_back(std::move(
            function->blocks[original_block_count + generated]));
      ordered.push_back(std::move(function->blocks[block]));
    }
    function->blocks.swap(ordered);
  }
  if(stats) stats->licm_preheaders_created += created;
  return created;
}

void collect_function_facts(const Function & function,
                            std::vector<std::size_t> * definition_block,
                            std::vector<unsigned char> * escaped_slots)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function.blocks[block].instructions.size();
        ++instruction) {
      const Instruction & ins =
        function.blocks[block].instructions[instruction];
      if(ins.dest.valid()) (*definition_block)[ins.dest] = block;
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t operand = 0; operand < 3; ++operand)
        if(operands[operand]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && operand == 0) ||
             (ins.kind == Instruction::IK_STORE && operand == 1)))
          (*escaped_slots)[operands[operand]->slot] = 1;
      for(std::size_t operand = 0; operand < ins.args.size(); ++operand)
        if(ins.args[operand].kind == Operand::OP_SLOT)
          (*escaped_slots)[ins.args[operand].slot] = 1;
    }
}

bool loop_load_prefix_safe(const std::vector<Instruction> & instructions,
                           std::size_t stop)
{
  for(std::size_t index = 0; index < stop; ++index) {
    const Instruction::Kind kind = instructions[index].kind;
    if(kind != Instruction::IK_PHI && kind != Instruction::IK_CONST &&
       kind != Instruction::IK_COPY && kind != Instruction::IK_ADDR &&
       kind != Instruction::IK_INDEX && kind != Instruction::IK_UNARY &&
       kind != Instruction::IK_BINARY && kind != Instruction::IK_CMP &&
       kind != Instruction::IK_CONVERT)
      return false;
  }
  return true;
}

bool local_value_defined_before(const std::vector<Instruction> & instructions,
                                lowir_model::ValueId value,
                                std::size_t stop)
{
  for(std::size_t index = 0; index < stop; ++index)
    if(instructions[index].dest.valid() &&
       instructions[index].dest == value)
      return true;
  return false;
}

bool has_loop_store_latch_shape(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.size() < 2 ||
       instructions.back().kind != Instruction::IK_JUMP)
      continue;
    const Instruction & store = instructions[instructions.size() - 2];
    if(store.kind == Instruction::IK_STORE && !store.volatile_access &&
       store.first.kind == Operand::OP_TEMP)
      return true;
  }
  return false;
}

std::size_t forward_loop_carried_store_loads_impl(
    Function * function, lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats)
{
  if(!function->metadata.inline_hint ||
     !has_loop_store_latch_shape(*function)) return 0;
  const lowir_analysis::LoopForest & forest = analysis->loop_forest();
  if(forest.loops.empty()) return 0;
  const lowir_analysis::Graph & graph = analysis->graph();
  const lowir_analysis::DominatorTree & dominators =
    analysis->dominator_tree();
  std::vector<std::size_t> definition_block(
    function->value_names.size(), kNoIndex);
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const lowir_model::ValueId value =
        function->blocks[block].instructions[index].dest;
      if(value.valid()) definition_block[value] = block;
    }

  // One rewrite per function bounds both phi growth and the compile-time scan.
  for(std::size_t loop_index = 0;
      loop_index < forest.loops.size(); ++loop_index) {
    const lowir_analysis::NaturalLoop & loop = forest.loops[loop_index];
    if(loop.has_eh || loop.preheader == kNoIndex || loop.latches.empty() ||
       loop.header >= function->blocks.size() ||
       loop.preheader >= function->blocks.size())
      continue;
    const std::vector<Instruction> & preheader_instructions =
      function->blocks[loop.preheader].instructions;
    if(preheader_instructions.empty() ||
       preheader_instructions.back().kind != Instruction::IK_JUMP ||
       preheader_instructions.back().first.kind != Operand::OP_LABEL ||
       preheader_instructions.back().first.block !=
         function->blocks[loop.header].id)
      continue;

    std::vector<Instruction> & header =
      function->blocks[loop.header].instructions;
    for(std::size_t load_index = 0;
        load_index < header.size(); ++load_index) {
      const Instruction & load = header[load_index];
      if(load.kind != Instruction::IK_LOAD || !load.dest.valid() ||
         load.volatile_access || load.debug_location.present() ||
         load.type.kind == lowir_model::LTK_INVALID ||
         load.type.kind == lowir_model::LTK_VOID ||
         load.type.kind == lowir_model::LTK_OBJECT ||
         !loop_load_prefix_safe(header, load_index))
        continue;
      if(load.first.kind == Operand::OP_TEMP) {
        const std::uint32_t address = load.first.value;
        if(address >= definition_block.size()) continue;
        const std::size_t definition = definition_block[address];
        if(definition != kNoIndex &&
           !dominators.dominates(definition, loop.preheader))
          continue;
      } else if(load.first.kind != Operand::OP_SLOT &&
                load.first.kind != Operand::OP_GLOBAL)
        continue;

      struct Incoming
      {
        std::size_t predecessor;
        Operand value;
      };
      std::vector<Incoming> incoming;
      incoming.reserve(graph.predecessors[loop.header].size());
      bool eligible = true;
      std::size_t outside = 0;
      for(std::size_t edge = 0;
          edge < graph.predecessors[loop.header].size(); ++edge) {
        const std::size_t predecessor =
          graph.predecessors[loop.header][edge];
        if(predecessor == loop.preheader) {
          ++outside;
          incoming.push_back(Incoming{predecessor, Operand()});
          continue;
        }
        if(!loop.contains(predecessor)) { eligible = false; break; }
        const std::vector<Instruction> & latch =
          function->blocks[predecessor].instructions;
        if(latch.size() < 2 ||
           latch.back().kind != Instruction::IK_JUMP ||
           latch.back().first.kind != Operand::OP_LABEL ||
           latch.back().first.block != function->blocks[loop.header].id) {
          eligible = false;
          break;
        }
        const Instruction & store = latch[latch.size() - 2];
        if(store.kind != Instruction::IK_STORE || store.volatile_access ||
           store.first.kind != Operand::OP_TEMP ||
           !optimizer_support::same_storage_location(
             store.second, load.first) ||
           !lowir_model::same_lowir_type(store.type, load.type) ||
           !local_value_defined_before(
             latch, store.first.value, latch.size() - 2)) {
          eligible = false;
          break;
        }
        incoming.push_back(Incoming{predecessor, store.first});
      }
      if(!eligible || outside != 1 ||
         incoming.size() != graph.predecessors[loop.header].size())
        continue;

      Instruction initial_load = load;
      initial_load.dest = lowir_model::append_lowir_fresh_generated_value(
        *function, load.type);
      Instruction phi;
      phi.kind = Instruction::IK_PHI;
      phi.dest = load.dest;
      phi.type = load.type;
      phi.args.reserve(incoming.size() * 2);
      for(std::size_t edge = 0; edge < incoming.size(); ++edge) {
        Operand label;
        label.kind = Operand::OP_LABEL;
        label.block = function->blocks[incoming[edge].predecessor].id;
        phi.args.push_back(label);
        if(incoming[edge].predecessor == loop.preheader) {
          Operand initial;
          initial.kind = Operand::OP_TEMP;
          initial.value = initial_load.dest;
          phi.args.push_back(initial);
        } else
          phi.args.push_back(incoming[edge].value);
      }

      std::vector<Instruction> rebuilt;
      rebuilt.reserve(header.size());
      std::size_t index = 0;
      while(index < header.size() &&
            header[index].kind == Instruction::IK_PHI)
        rebuilt.push_back(std::move(header[index++]));
      rebuilt.push_back(std::move(phi));
      for(; index < header.size(); ++index)
        if(index != load_index)
          rebuilt.push_back(std::move(header[index]));
      header.swap(rebuilt);
      std::vector<Instruction> & preheader =
        function->blocks[loop.preheader].instructions;
      preheader.insert(preheader.end() - 1, std::move(initial_load));
      if(stats) ++stats->rewrites;
      return 1;
    }
  }
  return 0;
}

}  // namespace

bool forward_loop_carried_store_loads(
    Function * function, lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats)
{
  return forward_loop_carried_store_loads_impl(function, analysis, stats) != 0;
}

bool hoist_loop_invariants(lowir_model::Program * program,
                           Function * function,
                           lowir_analysis::FunctionAnalysis * analysis,
                           int optimization_level, Stats * stats)
{
  if(function->blocks.empty()) return false;
  const std::chrono::steady_clock::time_point started =
    stats ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point();
  const lowir_analysis::LoopForest * forest = &analysis->loop_forest();
  if(forest->loops.empty()) {
    if(stats) stats->licm_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
    return false;
  }

  std::vector<std::size_t> definition_block(
    function->value_names.size(), kNoIndex);
  std::vector<unsigned char> escaped_slots(function->slot_names.size(), 0);
  collect_function_facts(*function, &definition_block, &escaped_slots);

  if(create_bounded_preheaders(program, function, analysis->graph(), *forest,
                               definition_block, stats)) {
    analysis->invalidate_cfg();
    forest = &analysis->loop_forest();
    std::fill(definition_block.begin(), definition_block.end(), kNoIndex);
    for(std::size_t block = 0; block < function->blocks.size(); ++block)
      for(std::size_t instruction = 0;
          instruction < function->blocks[block].instructions.size();
          ++instruction) {
        const lowir_model::ValueId destination =
          function->blocks[block].instructions[instruction].dest;
        if(destination.valid()) definition_block[destination] = block;
      }
  }

  std::vector<std::size_t> order(forest->loops.size());
  for(std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
    [forest](std::size_t left, std::size_t right) {
      return forest->loops[left].blocks.size() <
        forest->loops[right].blocks.size();
    });
  bool changed = false;
  const std::size_t function_budget = std::min<std::size_t>(
    4096, function->value_names.size() / 4 + 16);
  std::size_t total_hoisted = 0;
  for(std::size_t order_index = 0;
      order_index < order.size(); ++order_index) {
    const lowir_analysis::NaturalLoop & loop =
      forest->loops[order[order_index]];
    if(loop.preheader == kNoIndex) {
      if(stats) ++stats->licm_no_preheader;
      continue;
    }
    if(loop.has_eh) {
      if(stats) ++stats->licm_eh_skips;
      continue;
    }
    std::size_t loop_instruction_count = 0;
    for(std::size_t i = 0; i < loop.blocks.size(); ++i)
      loop_instruction_count +=
        function->blocks[loop.blocks[i]].instructions.size();
    if(loop_instruction_count > 16384 || total_hoisted == function_budget) {
      if(stats) { ++stats->licm_budget_skips; ++stats->budget_skips; }
      continue;
    }

    bool unknown_memory_write = false;
    std::vector<lowir_model::SymbolId> written_globals;
    std::vector<unsigned char> written_slots(function->slot_names.size(), 0);
    for(std::size_t member = 0; member < loop.blocks.size(); ++member) {
      const std::vector<Instruction> & instructions =
        function->blocks[loop.blocks[member]].instructions;
      for(std::size_t instruction = 0;
          instruction < instructions.size(); ++instruction) {
        const Instruction & ins = instructions[instruction];
        if(ins.kind == Instruction::IK_STORE) {
          if(ins.second.kind == Operand::OP_GLOBAL)
            written_globals.push_back(ins.second.symbol);
          else if(ins.second.kind == Operand::OP_SLOT)
            written_slots[ins.second.slot] = 1;
          else
            unknown_memory_write = true;
        } else if(call_may_write_memory(ins) ||
                  ins.kind == Instruction::IK_ATOMIC_LOAD ||
                  ins.kind == Instruction::IK_ATOMIC_STORE ||
                  ins.kind == Instruction::IK_ATOMIC_EXCHANGE ||
                  ins.kind == Instruction::IK_ATOMIC_ADD_FETCH ||
                  ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
                  ins.kind == Instruction::IK_ATOMIC_THREAD_FENCE ||
                  ins.kind == Instruction::IK_ATOMIC_SIGNAL_FENCE ||
                  ins.kind == Instruction::IK_COPYOBJ ||
                  ins.kind == Instruction::IK_ZEROINIT)
          unknown_memory_write = true;
      }
    }
    std::sort(written_globals.begin(), written_globals.end());
    written_globals.erase(
      std::unique(written_globals.begin(), written_globals.end()),
      written_globals.end());

    std::vector<Location> candidates;
    std::vector<std::size_t> candidate_by_value(
      function->value_names.size(), kNoIndex);
    for(std::size_t member = 0; member < loop.blocks.size(); ++member) {
      const std::size_t block = loop.blocks[member];
      const std::vector<Instruction> & instructions =
        function->blocks[block].instructions;
      for(std::size_t instruction = 0;
          instruction < instructions.size(); ++instruction) {
        const Instruction & ins = instructions[instruction];
        if(stats) ++stats->instruction_visits;
        bool eligible = loop.preheader < block && ins.dest.valid() &&
          speculatable(ins) &&
          !unsupported_slot_operand(ins);
        if(optimization_level >= 2 && ins.kind == Instruction::IK_LOAD) {
          eligible = false;
          if(ins.volatile_access) { /* volatile loads never hoist */ }
          else if(ins.first.kind == Operand::OP_GLOBAL && !unknown_memory_write)
            eligible = !std::binary_search(
              written_globals.begin(), written_globals.end(),
              ins.first.symbol);
          else if(ins.first.kind == Operand::OP_SLOT &&
                  !escaped_slots[ins.first.slot])
            eligible = !written_slots[ins.first.slot];
          if(eligible && ins.debug_location.present()) eligible = false;
        }
        if(eligible && loop.preheader >= block) eligible = false;
        if(!eligible) continue;
        candidate_by_value[ins.dest] = candidates.size();
        candidates.push_back(Location{block, instruction});
      }
    }
    if(stats) stats->licm_candidates += candidates.size();
    if(candidates.empty()) continue;

    struct Dependency { std::size_t user; std::size_t next; };
    std::vector<std::size_t> first_user(candidates.size(), kNoIndex);
    std::vector<std::size_t> blockers(candidates.size(), 0);
    std::vector<unsigned char> permanently_blocked(candidates.size(), 0);
    std::vector<Dependency> dependencies;
    std::vector<lowir_model::ValueId> operands;
    for(std::size_t candidate = 0;
        candidate < candidates.size(); ++candidate) {
      operands.clear();
      const Location location = candidates[candidate];
      collect_temp_operands(
        function->blocks[location.block].instructions[location.instruction],
        &operands);
      for(std::size_t operand = 0; operand < operands.size(); ++operand) {
        const std::uint32_t value = operands[operand];
        if(value >= definition_block.size() ||
           definition_block[value] == kNoIndex ||
           !loop.contains(definition_block[value])) continue;
        const std::size_t producer = candidate_by_value[value];
        if(producer == kNoIndex) {
          permanently_blocked[candidate] = 1;
          continue;
        }
        dependencies.push_back(Dependency{candidate, first_user[producer]});
        first_user[producer] = dependencies.size() - 1;
        ++blockers[candidate];
      }
    }

    std::vector<std::size_t> ready;
    ready.reserve(candidates.size());
    for(std::size_t candidate = 0;
        candidate < candidates.size(); ++candidate)
      if(!permanently_blocked[candidate] && blockers[candidate] == 0)
        ready.push_back(candidate);
    std::vector<unsigned char> hoist(candidates.size(), 0);
    for(std::size_t cursor = 0;
        cursor < ready.size() && total_hoisted < function_budget; ++cursor) {
      const std::size_t producer = ready[cursor];
      hoist[producer] = 1;
      ++total_hoisted;
      if(stats) ++stats->worklist_pushes;
      for(std::size_t edge = first_user[producer]; edge != kNoIndex;
          edge = dependencies[edge].next) {
        const std::size_t user = dependencies[edge].user;
        if(blockers[user]) --blockers[user];
        if(!permanently_blocked[user] && blockers[user] == 0)
          ready.push_back(user);
      }
    }
    if(ready.empty()) continue;

    std::vector<std::vector<unsigned char> > remove(function->blocks.size());
    std::vector<Instruction> moved;
    moved.reserve(ready.size());
    for(std::size_t cursor = 0; cursor < ready.size(); ++cursor) {
      const std::size_t candidate = ready[cursor];
      if(!hoist[candidate]) continue;
      const Location location = candidates[candidate];
      if(remove[location.block].empty())
        remove[location.block].assign(
          function->blocks[location.block].instructions.size(), 0);
      remove[location.block][location.instruction] = 1;
      Instruction & ins =
        function->blocks[location.block].instructions[location.instruction];
      definition_block[ins.dest] = loop.preheader;
      moved.push_back(std::move(ins));
    }
    for(std::size_t member = 0; member < loop.blocks.size(); ++member) {
      const std::size_t block = loop.blocks[member];
      if(remove[block].empty()) continue;
      std::vector<Instruction> & instructions =
        function->blocks[block].instructions;
      std::size_t kept = 0;
      for(std::size_t instruction = 0;
          instruction < instructions.size(); ++instruction)
        if(!remove[block][instruction]) {
          if(kept != instruction)
            instructions[kept] = std::move(instructions[instruction]);
          ++kept;
        }
      instructions.resize(kept);
    }
    std::vector<Instruction> & preheader =
      function->blocks[loop.preheader].instructions;
    const std::size_t insertion = preheader.empty() ? 0 : preheader.size() - 1;
    preheader.insert(preheader.begin() + insertion,
      std::make_move_iterator(moved.begin()),
      std::make_move_iterator(moved.end()));
    changed = true;
    if(stats) {
      stats->licm_hoisted += moved.size();
      for(std::size_t i = 0; i < moved.size(); ++i)
        if(moved[i].kind == Instruction::IK_LOAD)
          ++stats->licm_loads_hoisted;
      stats->rewrites += moved.size();
    }
  }
  if(stats) stats->licm_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
  return changed;
}

}  // namespace lowir_opt
