#include "lowir/optimize/pre.h"

#include "lowir/optimize/expression_key.h"
#include "lowir/optimize/pipeline.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::Operand;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);
const std::size_t kMaximumFunctionInstructions = 32768;
const std::size_t kMaximumInsertedExpressions = 64;
const std::size_t kMaximumInsertedPhis = 64;
const std::size_t kMaximumAvailabilityProbes = 65536;

struct Occurrence
{
  std::size_t block;
  lowir_model::ValueId value;
  std::size_t next;
};

struct OccurrenceList
{
  std::size_t head = kNoIndex;
  std::size_t count = 0;
};

typedef std::unordered_map<ExpressionKey, OccurrenceList, ExpressionKeyHash>
  OccurrenceMap;

Operand value_operand(lowir_model::ValueId value)
{
  Operand result;
  result.kind = Operand::OP_TEMP;
  result.value = value;
  return result;
}

Operand label_operand(lowir_model::BlockId block)
{
  Operand result;
  result.kind = Operand::OP_LABEL;
  result.block = block;
  return result;
}

bool has_exception_structure(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction::Kind kind =
        function.blocks[block].instructions[index].kind;
      if(kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_RESUME)
        return true;
    }
  return false;
}

bool safely_insertable(const Instruction & instruction)
{
  if(instruction.kind == Instruction::IK_ADDR) return true;
  if(instruction.kind != Instruction::IK_BINARY) return false;
  return instruction.op.kind == lowir_model::LowOperation::LOP_AND ||
    instruction.op.kind == lowir_model::LowOperation::LOP_OR ||
    instruction.op.kind == lowir_model::LowOperation::LOP_XOR;
}

void collect_definitions(const Function & function,
                         std::vector<std::size_t> * definition_blocks,
                         std::vector<unsigned char> * parameters)
{
  definition_blocks->assign(function.value_names.size(), kNoIndex);
  parameters->assign(function.value_names.size(), 0);
  for(std::size_t index = 0; index < function.params.size(); ++index)
    (*parameters)[function.params[index].value] = 1;
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.dest.valid())
        (*definition_blocks)[instruction.dest] = block;
    }
}

bool operand_available(
    const Operand & operand, std::size_t predecessor,
    const std::vector<std::size_t> & definition_blocks,
    const std::vector<unsigned char> & parameters,
    const lowir_analysis::DominatorTree & dominators)
{
  if(operand.kind != Operand::OP_TEMP) return true;
  const std::uint32_t value = operand.value;
  if(value >= definition_blocks.size()) return false;
  if(parameters[value]) return true;
  return definition_blocks[value] != kNoIndex &&
    dominators.dominates(definition_blocks[value], predecessor);
}

bool expression_available(
    const Instruction & instruction, std::size_t predecessor,
    const std::vector<std::size_t> & definition_blocks,
    const std::vector<unsigned char> & parameters,
    const lowir_analysis::DominatorTree & dominators)
{
  const Operand * operands[] = {
    &instruction.first, &instruction.second, &instruction.third};
  for(std::size_t index = 0; index < 3; ++index)
    if(!operand_available(*operands[index], predecessor,
         definition_blocks, parameters, dominators)) return false;
  for(std::size_t index = 0; index < instruction.args.size(); ++index)
    if(!operand_available(instruction.args[index], predecessor,
         definition_blocks, parameters, dominators)) return false;
  return true;
}

void add_occurrence(const ExpressionKey & key, std::size_t block,
                    lowir_model::ValueId value,
                    OccurrenceMap * lists,
                    std::vector<Occurrence> * occurrences)
{
  OccurrenceList & list = (*lists)[key];
  occurrences->push_back(Occurrence{block, value, list.head});
  list.head = occurrences->size() - 1;
  ++list.count;
}

lowir_model::ValueId find_available_value(
    const OccurrenceList & list,
    const std::vector<Occurrence> & occurrences,
    std::size_t predecessor, std::size_t join,
    lowir_model::ValueId excluded,
    const lowir_analysis::DominatorTree & dominators,
    std::size_t * probes, bool * exhausted)
{
  std::size_t best = kNoIndex;
  for(std::size_t index = list.head; index != kNoIndex;
      index = occurrences[index].next) {
    if(++*probes > kMaximumAvailabilityProbes) {
      *exhausted = true;
      return lowir_model::ValueId();
    }
    const Occurrence & occurrence = occurrences[index];
    if(occurrence.value == excluded || occurrence.block == join ||
       !dominators.dominates(occurrence.block, predecessor)) continue;
    if(best == kNoIndex ||
       dominators.preorder[occurrence.block] >
         dominators.preorder[occurrences[best].block])
      best = index;
  }
  return best == kNoIndex ? lowir_model::ValueId() :
    occurrences[best].value;
}

Instruction make_phi(
    const Instruction & expression,
    const lowir_model::LowType & result_type,
    const std::vector<std::size_t> & predecessors,
    const std::vector<lowir_model::ValueId> & values,
    const Function & function)
{
  Instruction result;
  result.kind = Instruction::IK_PHI;
  result.dest = expression.dest;
  result.type = result_type;
  result.args.reserve(predecessors.size() * 2);
  for(std::size_t index = 0; index < predecessors.size(); ++index) {
    result.args.push_back(label_operand(function.blocks[predecessors[index]].id));
    result.args.push_back(value_operand(values[index]));
  }
  return result;
}

void apply_pre_rewrites(
    Function * function,
    std::vector<std::vector<unsigned char> > * remove,
    std::vector<std::vector<Instruction> > * phis,
    std::vector<std::vector<Instruction> > * insertions)
{
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    if(!(*phis)[block].empty() || !(*remove)[block].empty()) {
      std::vector<Instruction> rebuilt;
      rebuilt.reserve(instructions.size() + (*phis)[block].size());
      std::size_t index = 0;
      while(index < instructions.size() &&
            instructions[index].kind == Instruction::IK_PHI)
        rebuilt.push_back(std::move(instructions[index++]));
      rebuilt.insert(rebuilt.end(),
        std::make_move_iterator((*phis)[block].begin()),
        std::make_move_iterator((*phis)[block].end()));
      for(; index < instructions.size(); ++index)
        if((*remove)[block].empty() || !(*remove)[block][index])
          rebuilt.push_back(std::move(instructions[index]));
      instructions.swap(rebuilt);
    }
    if((*insertions)[block].empty()) continue;
    const std::size_t position = instructions.empty() ?
      0 : instructions.size() - 1;
    instructions.insert(instructions.begin() + position,
      std::make_move_iterator((*insertions)[block].begin()),
      std::make_move_iterator((*insertions)[block].end()));
  }
}

}  // namespace

bool eliminate_partial_redundancies(
    Function * function, lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats)
{
  const std::chrono::steady_clock::time_point started =
    stats ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point();
  if(stats) ++stats->pre_runs;
  std::size_t instruction_count = 0;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    instruction_count += function->blocks[block].instructions.size();
  if(instruction_count > kMaximumFunctionInstructions) {
    if(stats) {
      ++stats->pre_budget_skips;
      ++stats->budget_skips;
      stats->pre_nanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started).count());
    }
    return false;
  }
  if(has_exception_structure(*function)) {
    if(stats) {
      ++stats->pre_eh_skips;
      stats->pre_nanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started).count());
    }
    return false;
  }

  const lowir_analysis::Graph & graph = analysis->graph();
  const lowir_analysis::DominatorTree & dominators =
    analysis->dominator_tree();
  std::vector<std::size_t> definition_blocks;
  std::vector<unsigned char> parameters;
  collect_definitions(*function, &definition_blocks, &parameters);
  OccurrenceMap lists;
  std::vector<Occurrence> occurrences;
  lists.reserve(instruction_count / 4 + 1);
  occurrences.reserve(instruction_count / 4 + 1);
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function->blocks[block].instructions[index];
      if(cse_eligible(instruction.kind) && instruction.dest.valid())
        add_occurrence(expression_key(instruction), block, instruction.dest,
          &lists, &occurrences);
    }

  std::vector<std::vector<unsigned char> > remove(function->blocks.size());
  std::vector<std::vector<Instruction> > phis(function->blocks.size());
  std::vector<std::vector<Instruction> > insertions(function->blocks.size());
  std::size_t inserted_expressions = 0;
  std::size_t inserted_phis = 0;
  std::size_t probes = 0;
  bool exhausted = false;
  for(std::size_t block = 0;
      block < function->blocks.size() && !exhausted; ++block) {
    const lowir_analysis::EdgeList & incoming = graph.predecessors[block];
    if(incoming.size() < 2) continue;
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    const std::size_t original_size = instructions.size();
    for(std::size_t index = 0; index < original_size; ++index) {
      const Instruction & expression = instructions[index];
      if(!cse_eligible(expression.kind) || !expression.dest.valid() ||
         expression.debug_location.present()) continue;
      const lowir_model::LowType result_type =
        lowir_model::lowir_value_type(*function, expression.dest);
      const ExpressionKey key = expression_key(expression);
      auto found = lists.find(key);
      if(found == lists.end() || found->second.count < 2) continue;
      if(stats) ++stats->pre_candidates;

      std::vector<std::size_t> predecessors(incoming.size());
      std::vector<lowir_model::ValueId> values(incoming.size());
      std::vector<unsigned char> missing(incoming.size(), 0);
      std::size_t available_count = 0;
      bool valid = true;
      for(std::size_t edge = 0; edge < incoming.size(); ++edge) {
        const std::size_t predecessor = incoming[edge];
        predecessors[edge] = predecessor;
        if(predecessor == block || dominators.dominates(block, predecessor)) {
          valid = false;
          break;
        }
        values[edge] = find_available_value(found->second, occurrences,
          predecessor, block, expression.dest, dominators,
          &probes, &exhausted);
        if(exhausted) break;
        if(values[edge].valid()) ++available_count;
        else {
          missing[edge] = 1;
          if(graph.successors[predecessor].size() != 1 ||
             !safely_insertable(expression) ||
             !expression_available(expression, predecessor,
               definition_blocks, parameters, dominators)) {
            valid = false;
            if(graph.successors[predecessor].size() != 1 && stats)
              ++stats->pre_critical_edge_skips;
            break;
          }
        }
      }
      if(exhausted || !valid || available_count == 0) continue;
      const std::size_t missing_count = std::count(
        missing.begin(), missing.end(), 1);
      if(inserted_phis == kMaximumInsertedPhis ||
         inserted_expressions + missing_count >
           kMaximumInsertedExpressions) {
        if(stats) {
          ++stats->pre_budget_skips;
          ++stats->budget_skips;
        }
        continue;
      }
      for(std::size_t edge = 0; edge < predecessors.size(); ++edge)
        if(missing[edge]) {
          Instruction inserted = expression;
          inserted.dest = lowir_model::append_lowir_fresh_generated_value(
            *function, result_type);
          values[edge] = inserted.dest;
          insertions[predecessors[edge]].push_back(inserted);
          add_occurrence(key, predecessors[edge], inserted.dest,
            &lists, &occurrences);
          ++inserted_expressions;
        }
      phis[block].push_back(make_phi(
        expression, result_type, predecessors, values, *function));
      if(remove[block].empty()) remove[block].assign(original_size, 0);
      remove[block][index] = 1;
      ++inserted_phis;
      if(stats) {
        if(missing_count) ++stats->pre_partial_redundancies;
        else ++stats->pre_full_redundancies;
        stats->rewrites += 1 + missing_count;
      }
    }
  }
  if(exhausted && stats) {
    ++stats->pre_budget_skips;
    ++stats->budget_skips;
  }
  if(inserted_phis)
    apply_pre_rewrites(function, &remove, &phis, &insertions);
  if(stats) {
    stats->pre_inserted_expressions += inserted_expressions;
    stats->pre_inserted_phis += inserted_phis;
    stats->pre_availability_probes += probes;
    stats->pre_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }
  return inserted_phis != 0;
}

}  // namespace lowir_opt
