#include "lowir/optimize/full_unroll_o3.h"

#include "lowir/optimize/pipeline.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::BlockId;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::Operand;
using lowir_model::ValueId;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);
const std::size_t kMaximumTrips = 4;
const std::size_t kMaximumLoopClones = 64;
const std::size_t kMaximumTranslationUnitClones = 4096;

struct ValueReplacements
{
  std::vector<Operand> values;
  std::vector<unsigned char> valid;

  explicit ValueReplacements(std::size_t count)
    : values(count), valid(count, 0)
  {}

  void set(ValueId value, const Operand & replacement)
  {
    const std::uint32_t id = value;
    values[id] = replacement;
    valid[id] = 1;
  }
};

Operand temp_operand(ValueId value, const lowir_model::LowType & type)
{
  Operand result;
  result.kind = Operand::OP_TEMP;
  result.value = value;
  result.literal_type = type;
  return result;
}

Operand integer_operand(long long value)
{
  Operand result;
  result.kind = Operand::OP_INTEGER;
  result.has_int_value = true;
  result.int_value = value;
  result.int_high = value < 0 ? ~UINT64_C(0) : UINT64_C(0);
  result.literal_type =
    lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  return result;
}

void replace_operand(Operand * operand,
                     const ValueReplacements & replacements)
{
  if(operand->kind != Operand::OP_TEMP) return;
  const std::uint32_t id = operand->value;
  if(id < replacements.valid.size() && replacements.valid[id])
    *operand = replacements.values[id];
}

void replace_instruction_operands(
    Instruction * instruction,
    const ValueReplacements & replacements)
{
  replace_operand(&instruction->first, replacements);
  replace_operand(&instruction->second, replacements);
  replace_operand(&instruction->third, replacements);
  for(std::size_t arg = 0; arg < instruction->args.size(); ++arg)
    replace_operand(&instruction->args[arg], replacements);
}

LowOperation::Kind negate_compare(LowOperation::Kind kind)
{
  switch(kind) {
  case LowOperation::LOP_EQ: return LowOperation::LOP_NE;
  case LowOperation::LOP_NE: return LowOperation::LOP_EQ;
  case LowOperation::LOP_LT: return LowOperation::LOP_GE;
  case LowOperation::LOP_LE: return LowOperation::LOP_GT;
  case LowOperation::LOP_GT: return LowOperation::LOP_LE;
  case LowOperation::LOP_GE: return LowOperation::LOP_LT;
  case LowOperation::LOP_ULT: return LowOperation::LOP_UGE;
  case LowOperation::LOP_ULE: return LowOperation::LOP_UGT;
  case LowOperation::LOP_UGT: return LowOperation::LOP_ULE;
  case LowOperation::LOP_UGE: return LowOperation::LOP_ULT;
  default: return LowOperation::LOP_NONE;
  }
}

bool constant_trip_count(long long initial, long long step, long long bound,
                         LowOperation::Kind condition,
                         long long minimum_value,
                         long long maximum_value,
                         std::size_t * trip_count)
{
  typedef __int128 Wide;
  const Wide first = initial;
  const Wide increment = step;
  const Wide limit = bound;
  Wide trips = 0;
  if((condition == LowOperation::LOP_LT ||
      condition == LowOperation::LOP_ULT) && increment > 0) {
    if(first < limit)
      trips = (limit - first + increment - 1) / increment;
  } else if((condition == LowOperation::LOP_LE ||
             condition == LowOperation::LOP_ULE) && increment > 0) {
    if(first <= limit) trips = (limit - first) / increment + 1;
  } else if((condition == LowOperation::LOP_GT ||
             condition == LowOperation::LOP_UGT) && increment < 0) {
    if(first > limit) {
      const Wide positive = -increment;
      trips = (first - limit + positive - 1) / positive;
    }
  } else if((condition == LowOperation::LOP_GE ||
             condition == LowOperation::LOP_UGE) && increment < 0) {
    if(first >= limit) trips = (first - limit) / (-increment) + 1;
  } else if(condition == LowOperation::LOP_NE) {
    const Wide difference = limit - first;
    if(difference != 0) {
      if((difference > 0) != (increment > 0) ||
         difference % increment != 0) return false;
      trips = difference / increment;
    }
  } else return false;
  if(trips < 0 || trips > static_cast<Wide>(kMaximumTrips)) return false;
  const Wide final_value = first + trips * increment;
  if(final_value < minimum_value || final_value > maximum_value) return false;
  *trip_count = static_cast<std::size_t>(trips);
  return true;
}

std::size_t find_definition(const Function & function,
                            const std::vector<std::size_t> & path,
                            ValueId value, std::size_t * instruction_index)
{
  for(std::size_t member = 0; member < path.size(); ++member) {
    const std::vector<Instruction> & instructions =
      function.blocks[path[member]].instructions;
    for(std::size_t index = 0; index + 1 < instructions.size(); ++index)
      if(instructions[index].dest == value) {
        *instruction_index = index;
        return path[member];
      }
  }
  return kNoIndex;
}

bool signed_literal(const Operand & operand, long long * value)
{
  if(operand.kind != Operand::OP_INTEGER || !operand.has_int_value ||
     operand.int_high !=
       (operand.int_value < 0 ? ~UINT64_C(0) : UINT64_C(0)))
    return false;
  *value = operand.int_value;
  return true;
}

bool induction_step(const Function & function,
                    const std::vector<std::size_t> & path,
                    ValueId induction,
                    const Operand & backedge,
                    lowir_model::LowTypeKind type,
                    long long * step)
{
  if(backedge.kind != Operand::OP_TEMP) return false;
  std::size_t instruction_index = 0;
  const std::size_t block = find_definition(
    function, path, backedge.value, &instruction_index);
  if(block == kNoIndex) return false;
  const Instruction & update =
    function.blocks[block].instructions[instruction_index];
  if(update.kind != Instruction::IK_BINARY || update.type.kind != type)
    return false;
  long long amount = 0;
  if(update.op.kind == LowOperation::LOP_ADD) {
    if(update.first.kind == Operand::OP_TEMP &&
       update.first.value == induction &&
       signed_literal(update.second, &amount)) {
      *step = amount;
      return amount != 0;
    }
    if(update.second.kind == Operand::OP_TEMP &&
       update.second.value == induction &&
       signed_literal(update.first, &amount)) {
      *step = amount;
      return amount != 0;
    }
  }
  if(update.op.kind == LowOperation::LOP_SUB &&
     update.first.kind == Operand::OP_TEMP &&
     update.first.value == induction &&
     signed_literal(update.second, &amount) &&
     amount != std::numeric_limits<long long>::min()) {
    *step = -amount;
    return *step != 0;
  }
  return false;
}

bool unsigned_compare(LowOperation::Kind condition)
{
  return condition == LowOperation::LOP_ULT ||
    condition == LowOperation::LOP_ULE ||
    condition == LowOperation::LOP_UGT ||
    condition == LowOperation::LOP_UGE;
}

bool integer_type_range(lowir_model::LowTypeKind type,
                        bool unsigned_condition,
                        long long * minimum_value,
                        long long * maximum_value)
{
  unsigned bits = 0;
  switch(type) {
  case lowir_model::LTK_I8: case lowir_model::LTK_U8: bits = 8; break;
  case lowir_model::LTK_I16: case lowir_model::LTK_U16: bits = 16; break;
  case lowir_model::LTK_I32: case lowir_model::LTK_U32: bits = 32; break;
  case lowir_model::LTK_I64: bits = 64; break;
  default: return false;
  }
  if(unsigned_condition) {
    *minimum_value = 0;
    *maximum_value = bits == 64 ?
      std::numeric_limits<long long>::max() :
      static_cast<long long>((UINT64_C(1) << bits) - 1);
    return true;
  }
  if(type == lowir_model::LTK_U8 || type == lowir_model::LTK_U16 ||
     type == lowir_model::LTK_U32) return false;
  if(bits == 64) {
    *minimum_value = std::numeric_limits<long long>::min();
    *maximum_value = std::numeric_limits<long long>::max();
    return true;
  }
  const long long limit = static_cast<long long>(UINT64_C(1) << (bits - 1));
  *minimum_value = -limit;
  *maximum_value = limit - 1;
  return true;
}

bool linear_body_path(const Function & function,
                      const lowir_analysis::NaturalLoop & loop,
                      const lowir_analysis::Graph & graph,
                      std::size_t first,
                      std::vector<std::size_t> * path,
                      Stats * stats)
{
  std::vector<unsigned char> seen(function.blocks.size(), 0);
  std::size_t block = first;
  while(block != loop.header) {
    if(block >= function.blocks.size() || !loop.contains(block) || seen[block])
      return false;
    seen[block] = 1;
    path->push_back(block);
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    if(instructions.empty() ||
       instructions.back().kind != Instruction::IK_JUMP ||
       graph.successors[block].size() != 1) return false;
    for(std::size_t index = 0; index + 1 < instructions.size(); ++index) {
      if(stats) ++stats->o3_unroll_instruction_visits;
      if(instructions[index].kind >= Instruction::IK_EH_TRY) return false;
    }
    block = graph.successors[block][0];
  }
  return !path->empty() && path->size() + 1 == loop.blocks.size();
}

bool collect_phi_states(const Function & function,
                        const lowir_analysis::NaturalLoop & loop,
                        std::vector<ValueId> * destinations,
                        std::vector<Operand> * initial,
                        std::vector<Operand> * backedge)
{
  const std::vector<Instruction> & header =
    function.blocks[loop.header].instructions;
  const BlockId preheader_id = function.blocks[loop.preheader].id;
  const BlockId latch_id = function.blocks[loop.latches[0]].id;
  for(std::size_t index = 0;
      index + 2 < header.size(); ++index) {
    const Instruction & phi = header[index];
    if(phi.kind != Instruction::IK_PHI || phi.args.size() != 4) return false;
    Operand entry;
    Operand next;
    bool has_entry = false;
    bool has_next = false;
    for(std::size_t incoming = 0; incoming < phi.args.size(); incoming += 2) {
      if(phi.args[incoming].kind != Operand::OP_LABEL) return false;
      if(phi.args[incoming].block == preheader_id) {
        entry = phi.args[incoming + 1];
        has_entry = true;
      } else if(phi.args[incoming].block == latch_id) {
        next = phi.args[incoming + 1];
        has_next = true;
      }
    }
    if(!has_entry || !has_next) return false;
    destinations->push_back(phi.dest);
    initial->push_back(entry);
    backedge->push_back(next);
  }
  return !destinations->empty();
}

bool replacement_covers_outside_uses(
    const Function & function,
    const lowir_analysis::NaturalLoop & loop,
    const std::vector<std::size_t> & path,
    const std::vector<ValueId> & phi_destinations,
    ValueId comparison,
    std::size_t trips)
{
  std::vector<unsigned char> defined(function.value_names.size(), 0);
  std::vector<unsigned char> replaceable(function.value_names.size(), 0);
  for(std::size_t index = 0; index < phi_destinations.size(); ++index) {
    defined[phi_destinations[index]] = 1;
    replaceable[phi_destinations[index]] = 1;
  }
  defined[comparison] = 1;
  replaceable[comparison] = 1;
  for(std::size_t member = 0; member < path.size(); ++member) {
    const std::vector<Instruction> & instructions =
      function.blocks[path[member]].instructions;
    for(std::size_t index = 0; index + 1 < instructions.size(); ++index)
      if(instructions[index].dest.valid()) {
        defined[instructions[index].dest] = 1;
        if(trips != 0) replaceable[instructions[index].dest] = 1;
      }
  }
  const auto unsupported = [&defined, &replaceable](const Operand & operand) {
    if(operand.kind != Operand::OP_TEMP) return false;
    const std::uint32_t value = operand.value;
    return value < defined.size() && defined[value] && !replaceable[value];
  };
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    if(loop.contains(block)) continue;
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    for(std::size_t index = 0; index < instructions.size(); ++index) {
      const Instruction & instruction = instructions[index];
      if(unsupported(instruction.first) || unsupported(instruction.second) ||
         unsupported(instruction.third)) return false;
      for(std::size_t arg = 0; arg < instruction.args.size(); ++arg)
        if(unsupported(instruction.args[arg])) return false;
    }
  }
  return true;
}

std::size_t path_instruction_count(const Function & function,
                                   const std::vector<std::size_t> & path)
{
  std::size_t result = 0;
  for(std::size_t member = 0; member < path.size(); ++member)
    result += function.blocks[path[member]].instructions.size() - 1;
  return result;
}

void clone_body_iteration(Function * function,
                          const std::vector<std::size_t> & path,
                          ValueReplacements * replacements,
                          std::vector<Instruction> * output)
{
  for(std::size_t member = 0; member < path.size(); ++member) {
    const std::vector<Instruction> & instructions =
      function->blocks[path[member]].instructions;
    for(std::size_t index = 0; index + 1 < instructions.size(); ++index) {
      Instruction clone = instructions[index];
      replace_instruction_operands(&clone, *replacements);
      if(clone.dest.valid()) {
        const ValueId original = clone.dest;
        clone.dest = lowir_model::append_lowir_fresh_generated_value(
          *function, clone.type);
        replacements->set(
          original, temp_operand(clone.dest, clone.type));
      }
      output->push_back(std::move(clone));
    }
  }
}

void rewrite_outside_uses(Function * function,
                          const std::vector<unsigned char> & loop_blocks,
                          const ValueReplacements & replacements)
{
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    const std::uint32_t id = function->blocks[block].id;
    if(id < loop_blocks.size() && loop_blocks[id]) continue;
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index)
      replace_instruction_operands(
        &function->blocks[block].instructions[index], replacements);
  }
}

void erase_loop_blocks(Function * function,
                       const std::vector<unsigned char> & loop_blocks)
{
  std::size_t output = 0;
  for(std::size_t input = 0; input < function->blocks.size(); ++input) {
    const std::uint32_t id = function->blocks[input].id;
    if(id < loop_blocks.size() && loop_blocks[id]) continue;
    if(output != input)
      function->blocks[output] = std::move(function->blocks[input]);
    ++output;
  }
  function->blocks.resize(output);
}

bool try_unroll(Function * function,
                const lowir_analysis::NaturalLoop & loop,
                const lowir_analysis::Graph & graph,
                O3UnrollBudget * budget,
                Stats * stats)
{
  if(loop.preheader == kNoIndex || loop.has_eh || loop.latches.size() != 1 ||
     loop.exits.size() != 1) return false;
  const std::vector<Instruction> & header =
    function->blocks[loop.header].instructions;
  if(header.size() < 3 ||
     header[header.size() - 2].kind != Instruction::IK_CMP ||
     header.back().kind != Instruction::IK_BRANCH ||
     header.back().first.kind != Operand::OP_TEMP ||
     header.back().first.value != header[header.size() - 2].dest)
    return false;
  const Instruction & comparison = header[header.size() - 2];
  if(comparison.first.kind != Operand::OP_TEMP) return false;
  long long bound = 0;
  if(!signed_literal(comparison.second, &bound)) return false;

  const std::size_t true_target = graph.find(header.back().second.block);
  const std::size_t false_target = graph.find(header.back().third.block);
  const bool true_inside = loop.contains(true_target);
  const bool false_inside = loop.contains(false_target);
  if(true_inside == false_inside) return false;
  LowOperation::Kind condition = comparison.op.kind;
  const std::size_t body = true_inside ? true_target : false_target;
  const std::size_t exit = true_inside ? false_target : true_target;
  if(!true_inside) condition = negate_compare(condition);
  long long minimum_value = 0;
  long long maximum_value = 0;
  if(!integer_type_range(comparison.type.kind, unsigned_compare(condition),
                         &minimum_value, &maximum_value)) return false;
  if(condition == LowOperation::LOP_NONE || exit != loop.exits[0] ||
     (!function->blocks[exit].instructions.empty() &&
      function->blocks[exit].instructions.front().kind == Instruction::IK_PHI))
    return false;

  std::vector<std::size_t> path;
  if(!linear_body_path(*function, loop, graph, body, &path, stats)) return false;
  std::vector<ValueId> phi_destinations;
  std::vector<Operand> initial;
  std::vector<Operand> backedge;
  if(!collect_phi_states(*function, loop, &phi_destinations,
                         &initial, &backedge)) return false;
  std::size_t induction_index = phi_destinations.size();
  for(std::size_t index = 0; index < phi_destinations.size(); ++index)
    if(phi_destinations[index] == comparison.first.value) induction_index = index;
  if(induction_index == phi_destinations.size()) return false;
  long long initial_value = 0;
  long long step = 0;
  if(!signed_literal(initial[induction_index], &initial_value) ||
     !induction_step(*function, path, phi_destinations[induction_index],
                     backedge[induction_index], comparison.type.kind,
                     &step)) return false;
  std::size_t trips = 0;
  if(initial_value < minimum_value || initial_value > maximum_value ||
     bound < minimum_value || bound > maximum_value ||
     !constant_trip_count(initial_value, step, bound, condition,
                          minimum_value, maximum_value, &trips)) {
    if(stats) ++stats->o3_unroll_trip_skips;
    return false;
  }
  if(!replacement_covers_outside_uses(
       *function, loop, path, phi_destinations, comparison.dest, trips))
    return false;
  const std::size_t cloned = path_instruction_count(*function, path) * trips;
  if(cloned > kMaximumLoopClones ||
     budget->cloned_instructions >
       kMaximumTranslationUnitClones - cloned) {
    if(stats) ++stats->o3_unroll_budget_skips;
    return false;
  }

  ValueReplacements replacements(function->value_names.size());
  std::vector<Operand> state = initial;
  std::vector<Operand> next_state(initial.size());
  std::vector<Instruction> unrolled;
  unrolled.reserve(cloned + 1);
  for(std::size_t iteration = 0; iteration < trips; ++iteration) {
    for(std::size_t phi = 0; phi < phi_destinations.size(); ++phi)
      replacements.set(phi_destinations[phi], state[phi]);
    replacements.set(comparison.dest, integer_operand(true_inside ? 1 : 0));
    clone_body_iteration(function, path, &replacements, &unrolled);
    for(std::size_t phi = 0; phi < phi_destinations.size(); ++phi) {
      next_state[phi] = backedge[phi];
      replace_operand(&next_state[phi], replacements);
    }
    state.swap(next_state);
  }
  for(std::size_t phi = 0; phi < phi_destinations.size(); ++phi)
    replacements.set(phi_destinations[phi], state[phi]);
  replacements.set(comparison.dest, integer_operand(true_inside ? 0 : 1));

  std::vector<unsigned char> loop_blocks(function->next_block_id, 0);
  for(std::size_t member = 0; member < loop.blocks.size(); ++member)
    loop_blocks[function->blocks[loop.blocks[member]].id] = 1;
  Instruction jump = function->blocks[loop.preheader].instructions.back();
  jump.kind = Instruction::IK_JUMP;
  jump.first.kind = Operand::OP_LABEL;
  jump.first.block = function->blocks[exit].id;
  function->blocks[loop.preheader].instructions.pop_back();
  std::vector<Instruction> & preheader =
    function->blocks[loop.preheader].instructions;
  preheader.insert(preheader.end(),
                   std::make_move_iterator(unrolled.begin()),
                   std::make_move_iterator(unrolled.end()));
  preheader.push_back(std::move(jump));
  rewrite_outside_uses(function, loop_blocks, replacements);
  erase_loop_blocks(function, loop_blocks);

  budget->cloned_instructions += cloned;
  if(stats) {
    ++stats->o3_loops_unrolled;
    stats->o3_unroll_iterations += trips;
    stats->o3_unroll_cloned_instructions += cloned;
    stats->o3_unroll_peak_scratch_bytes = std::max(
      stats->o3_unroll_peak_scratch_bytes,
      replacements.values.capacity() * sizeof(Operand) +
      replacements.valid.capacity() +
      state.capacity() * sizeof(Operand) +
      next_state.capacity() * sizeof(Operand) +
      unrolled.capacity() * sizeof(Instruction) +
      loop_blocks.capacity());
    ++stats->rewrites;
  }
  return true;
}

}  // namespace

bool fully_unroll_small_loop(
    Function * function,
    lowir_analysis::FunctionAnalysis * analysis,
    O3UnrollBudget * budget,
    Stats * stats)
{
  const lowir_analysis::Graph & graph = analysis->graph();
  const lowir_analysis::LoopForest & forest = analysis->loop_forest();
  for(std::size_t index = 0; index < forest.loops.size(); ++index) {
    if(stats) ++stats->o3_loops_considered;
    if(try_unroll(function, forest.loops[index], graph, budget, stats)) {
      analysis->invalidate_cfg();
      return true;
    }
    if(stats) ++stats->o3_unroll_candidate_skips;
  }
  return false;
}

}  // namespace lowir_opt
