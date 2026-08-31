#include "lowir/optimize/copy_elision.h"

#include "lowir/analysis/function.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/scalar_rules.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::Operand;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);

struct Location
{
  std::size_t block;
  std::size_t instruction;

  Location() : block(kNoIndex), instruction(kNoIndex) {}
  Location(std::size_t block_value, std::size_t instruction_value)
    : block(block_value), instruction(instruction_value) {}

  bool matches(std::size_t block_value, std::size_t instruction_value) const
  {
    return block == block_value && instruction == instruction_value;
  }
};

struct Plan
{
  Location transfer;
  Location normal_cleanup;
  Location exceptional_cleanup;
  std::vector<Location> producers;
  Operand source;
  Operand destination;
  bool eh_region = false;
};

bool operand_is(const Operand & operand, const Operand & expected)
{
  return same_operand(operand, expected);
}

std::size_t operand_uses(const Instruction & instruction,
                         const Operand & operand)
{
  std::size_t result = 0;
  if(operand_is(instruction.first, operand)) ++result;
  if(operand_is(instruction.second, operand)) ++result;
  if(operand_is(instruction.third, operand)) ++result;
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    if(operand_is(instruction.args[i], operand)) ++result;
  return result;
}

bool direct_void_call_on(const Instruction & instruction,
                         const Operand & object)
{
  return instruction.kind == Instruction::IK_CALL &&
    instruction.call_returns_void && !instruction.has_call_signature &&
    instruction.first.kind == Operand::OP_GLOBAL &&
    instruction.args.size() == 1 &&
    operand_is(instruction.args[0], object);
}

bool direct_constructor_into(const Instruction & instruction,
                             const Operand & object)
{
  return instruction.kind == Instruction::IK_CALL &&
    !instruction.copy_elision_candidate &&
    instruction.call_returns_void && !instruction.has_call_signature &&
    instruction.first.kind == Operand::OP_GLOBAL &&
    !instruction.args.empty() &&
    operand_is(instruction.args[0], object) &&
    operand_uses(instruction, object) == 1;
}

bool definition_dominates(const Operand & operand, std::size_t use_block,
                          std::size_t use_instruction,
                          const lowir_analysis::ValueIndex & values,
                          const lowir_analysis::DominatorTree & dominators)
{
  if(operand.kind != Operand::OP_TEMP) return true;
  const lowir_analysis::ValueDefinition definition =
    values.definition(operand.value);
  if(definition.kind == lowir_analysis::ValueDefinition::PARAMETER)
    return true;
  if(definition.kind != lowir_analysis::ValueDefinition::INSTRUCTION)
    return false;
  if(definition.block == use_block)
    return definition.instruction < use_instruction;
  return dominators.dominates(definition.block, use_block);
}

bool only_private_slot_address(const Function & function,
                               const Location & source_definition,
                               lowir_model::SlotId source_slot)
{
  Operand slot;
  slot.kind = Operand::OP_SLOT;
  slot.slot = source_slot;
  slot.literal_type = lowir_model::lowir_slot_type(function, source_slot);
  for(std::size_t b = 0; b < function.blocks.size(); ++b)
    for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
      const std::size_t uses = operand_uses(function.blocks[b].instructions[i], slot);
      if(!uses) continue;
      if(!source_definition.matches(b, i) || uses != 1 ||
         !operand_is(function.blocks[b].instructions[i].first, slot))
        return false;
    }
  return true;
}

bool find_plan(Function & function, std::size_t transfer_block,
               std::size_t transfer_instruction,
               lowir_analysis::FunctionAnalysis * analysis, Plan * plan)
{
  const Instruction & transfer =
    function.blocks[transfer_block].instructions[transfer_instruction];
  if(transfer.kind != Instruction::IK_CALL ||
     !transfer.copy_elision_candidate || !transfer.call_returns_void ||
     transfer.has_call_signature || transfer.first.kind != Operand::OP_GLOBAL ||
     transfer.args.size() != 2 ||
     operand_is(transfer.args[0], transfer.args[1]))
    return false;

  plan->transfer = Location(transfer_block, transfer_instruction);
  plan->source = transfer.args[1];
  plan->destination = transfer.args[0];

  if(plan->source.kind != Operand::OP_TEMP ||
     plan->destination.kind != Operand::OP_TEMP)
    return false;
  const lowir_analysis::ValueIndex & values = analysis->value_index();
  const lowir_analysis::ValueDefinition source_definition =
    values.definition(plan->source.value);
  if(source_definition.kind != lowir_analysis::ValueDefinition::INSTRUCTION)
    return false;
  const Instruction & address = function.blocks[source_definition.block]
    .instructions[source_definition.instruction];
  if(address.kind != Instruction::IK_ADDR ||
     address.first.kind != Operand::OP_SLOT)
    return false;
  const Location source_address(
    source_definition.block, source_definition.instruction);
  if(!only_private_slot_address(function, source_address, address.first.slot))
    return false;

  Block & body = function.blocks[transfer_block];
  const bool begins_eh = transfer_instruction != 0 &&
    body.instructions[transfer_instruction - 1].kind ==
      Instruction::IK_EH_TRY;
  if(begins_eh) {
    if(transfer_instruction + 3 >= body.instructions.size() ||
       transfer_instruction + 4 != body.instructions.size() ||
       !direct_void_call_on(body.instructions[transfer_instruction + 1],
                            plan->source) ||
       body.instructions[transfer_instruction + 2].kind !=
         Instruction::IK_EH_END ||
       body.instructions[transfer_instruction + 3].kind !=
         Instruction::IK_JUMP)
      return false;
    const Instruction & begin = body.instructions[transfer_instruction - 1];
    if(transfer_instruction != 1 || begin.first.kind != Operand::OP_LABEL)
      return false;
    const lowir_analysis::Graph & graph = analysis->graph();
    const std::size_t cleanup = graph.find(begin.first.block);
    if(cleanup == kNoIndex || cleanup >= function.blocks.size() ||
       graph.predecessors[cleanup].size() != 1 ||
       graph.predecessors[cleanup][0] != transfer_block)
      return false;
    const Block & cleanup_block = function.blocks[cleanup];
    if(cleanup_block.instructions.size() != 2 ||
       !direct_void_call_on(cleanup_block.instructions[0], plan->source) ||
       cleanup_block.instructions[1].kind != Instruction::IK_JUMP ||
       !operand_is(cleanup_block.instructions[0].first,
                   body.instructions[transfer_instruction + 1].first))
      return false;
    plan->eh_region = true;
    plan->normal_cleanup = Location(
      transfer_block, transfer_instruction + 1);
    plan->exceptional_cleanup = Location(cleanup, 0);
  } else if(transfer_instruction + 1 < body.instructions.size() &&
            direct_void_call_on(body.instructions[transfer_instruction + 1],
                                plan->source)) {
    plan->normal_cleanup = Location(
      transfer_block, transfer_instruction + 1);
  }

  const lowir_analysis::Graph & graph = analysis->graph();
  const lowir_analysis::EdgeList & predecessors =
    graph.predecessors[transfer_block];
  if(predecessors.size() < 2) return false;

  for(std::size_t b = 0; b < function.blocks.size(); ++b)
    for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
      const Instruction & instruction = function.blocks[b].instructions[i];
      const std::size_t uses = operand_uses(instruction, plan->source);
      if(!uses) continue;
      if(plan->transfer.matches(b, i)) {
        if(uses != 1 || !operand_is(instruction.args[1], plan->source))
          return false;
        continue;
      }
      if(plan->normal_cleanup.matches(b, i) ||
         plan->exceptional_cleanup.matches(b, i)) {
        if(uses != 1) return false;
        continue;
      }
      if(!direct_constructor_into(instruction, plan->source)) return false;
      plan->producers.push_back(Location(b, i));
    }

  if(plan->producers.size() != predecessors.size()) return false;
  const lowir_analysis::DominatorTree & dominators =
    analysis->dominator_tree();
  for(std::size_t b = 0; b < function.blocks.size(); ++b)
    for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
      const std::size_t uses = operand_uses(
        function.blocks[b].instructions[i], plan->destination);
      if(!uses) continue;
      if(plan->transfer.matches(b, i)) {
        if(uses != 1 ||
           !operand_is(function.blocks[b].instructions[i].args[0],
                       plan->destination))
          return false;
        continue;
      }
      if(b == plan->exceptional_cleanup.block ||
         (b == transfer_block && i <= transfer_instruction) ||
         (b != transfer_block &&
          !dominators.dominates(transfer_block, b)))
        return false;
    }

  std::vector<unsigned char> producer_blocks(function.blocks.size(), 0);
  for(std::size_t i = 0; i < plan->producers.size(); ++i) {
    const Location & producer = plan->producers[i];
    if(producer_blocks[producer.block]) return false;
    producer_blocks[producer.block] = 1;
    const Block & predecessor = function.blocks[producer.block];
    if(predecessor.instructions.empty() ||
       predecessor.instructions.back().kind != Instruction::IK_JUMP ||
       predecessor.instructions.back().first.kind != Operand::OP_LABEL ||
       predecessor.instructions.back().first.block != body.id ||
       !definition_dominates(plan->destination, producer.block,
                             producer.instruction, values, dominators) ||
       !definition_dominates(plan->source, producer.block,
                             producer.instruction, values, dominators))
      return false;
  }
  for(std::size_t i = 0; i < predecessors.size(); ++i)
    if(!producer_blocks[predecessors[i]]) return false;
  return true;
}

void apply_plan(Function * function, const Plan & plan, Stats * stats)
{
  for(std::size_t i = 0; i < plan.producers.size(); ++i)
    function->blocks[plan.producers[i].block]
      .instructions[plan.producers[i].instruction].args[0] = plan.destination;

  Block & transfer = function->blocks[plan.transfer.block];
  if(plan.eh_region) {
    Instruction continuation = std::move(transfer.instructions.back());
    transfer.instructions.clear();
    transfer.instructions.push_back(std::move(continuation));
    Block & cleanup = function->blocks[plan.exceptional_cleanup.block];
    Instruction resume = std::move(cleanup.instructions.back());
    cleanup.instructions.clear();
    cleanup.instructions.push_back(std::move(resume));
  } else {
    const std::size_t end = plan.normal_cleanup.block == plan.transfer.block ?
      plan.normal_cleanup.instruction + 1 : plan.transfer.instruction + 1;
    transfer.instructions.erase(
      transfer.instructions.begin() + plan.transfer.instruction,
      transfer.instructions.begin() + end);
  }
  if(stats) {
    ++stats->copy_elisions;
    stats->copy_elision_producers += plan.producers.size();
    if(plan.eh_region) ++stats->copy_elision_eh_regions;
    stats->rewrites += plan.producers.size() + 1;
  }
}

}  // namespace

bool coalesce_copy_elision_candidates(Function * function, Stats * stats)
{
  bool changed = false;
  for(std::size_t round = 0; round < 8; ++round) {
    bool found = false;
    lowir_analysis::FunctionAnalysis analysis(*function, stats);
    for(std::size_t b = 0; !found && b < function->blocks.size(); ++b)
      for(std::size_t i = 0; i < function->blocks[b].instructions.size(); ++i) {
        const Instruction & instruction =
          function->blocks[b].instructions[i];
        if(!instruction.copy_elision_candidate) continue;
        if(stats) ++stats->copy_elision_candidates;
        Plan plan;
        if(!find_plan(*function, b, i, &analysis, &plan)) continue;
        apply_plan(function, plan, stats);
        changed = found = true;
        break;
      }
    if(!found) break;
  }
  return changed;
}

}  // namespace lowir_opt
