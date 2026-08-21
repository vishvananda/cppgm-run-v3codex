#include "lowir_loop_simplify.h"

#include "lowir_opt.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::Operand;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);

struct Induction
{
  std::size_t phi_instruction;
  std::size_t update_block;
  std::size_t update_instruction;
  lowir_model::ValueId value;
  long long initial;
  long long step;
};

bool pure_loop_instruction(const Instruction & instruction)
{
  const Instruction::Kind kind = instruction.kind;
  if(kind == Instruction::IK_BINARY &&
     (instruction.op.kind == LowOperation::LOP_DIV ||
      instruction.op.kind == LowOperation::LOP_UDIV ||
      instruction.op.kind == LowOperation::LOP_MOD ||
      instruction.op.kind == LowOperation::LOP_UMOD)) return false;
  return kind == Instruction::IK_CONST || kind == Instruction::IK_COPY ||
    kind == Instruction::IK_PHI || kind == Instruction::IK_ADDR ||
    kind == Instruction::IK_INDEX || kind == Instruction::IK_UNARY ||
    kind == Instruction::IK_BINARY || kind == Instruction::IK_CMP ||
    kind == Instruction::IK_CONVERT || kind == Instruction::IK_JUMP ||
    kind == Instruction::IK_BRANCH;
}

void count_temp_use(const Operand & operand, std::vector<std::size_t> * uses)
{
  if(operand.kind == Operand::OP_TEMP) ++(*uses)[operand.value];
}

Instruction * definition(Function * function,
                         const std::vector<std::size_t> & definition_block,
                         const std::vector<std::size_t> & definition_index,
                         lowir_model::ValueId value)
{
  const std::uint32_t id = value;
  if(id >= definition_block.size() || definition_block[id] == kNoIndex)
    return 0;
  return &function->blocks[definition_block[id]].instructions[
    definition_index[id]];
}

bool find_induction(Function * function,
                    const lowir_analysis::NaturalLoop & loop,
                    const std::vector<std::size_t> & definition_block,
                    const std::vector<std::size_t> & definition_index,
                    lowir_model::ValueId value, Induction * result)
{
  const std::uint32_t id = value;
  if(id >= definition_block.size() ||
     definition_block[id] != loop.header) return false;
  const std::size_t phi_index = definition_index[id];
  Instruction & phi =
    function->blocks[loop.header].instructions[phi_index];
  if(phi.kind != Instruction::IK_PHI ||
     phi.type.kind != lowir_model::LTK_I64 || loop.latches.size() != 1)
    return false;
  Operand initial;
  Operand updated;
  bool has_initial = false;
  bool has_updated = false;
  const lowir_model::BlockId preheader_id =
    function->blocks[loop.preheader].id;
  const lowir_model::BlockId latch_id =
    function->blocks[loop.latches[0]].id;
  for(std::size_t incoming = 0;
      incoming + 1 < phi.args.size(); incoming += 2) {
    if(phi.args[incoming].block == preheader_id) {
      initial = phi.args[incoming + 1];
      has_initial = true;
    } else if(phi.args[incoming].block == latch_id) {
      updated = phi.args[incoming + 1];
      has_updated = true;
    }
  }
  if(!has_initial || !initial.has_int_value ||
     initial.kind != Operand::OP_INTEGER || !has_updated ||
     updated.kind != Operand::OP_TEMP) return false;
  if(initial.int_high !=
     (initial.int_value < 0 ? ~UINT64_C(0) : UINT64_C(0))) return false;
  Instruction * update = definition(
    function, definition_block, definition_index, updated.value);
  if(!update || update->kind != Instruction::IK_BINARY ||
     update->type.kind != lowir_model::LTK_I64 ||
     definition_block[updated.value] != loop.latches[0]) return false;
  long long step = 0;
  if(update->op.kind == LowOperation::LOP_ADD &&
     update->first.kind == Operand::OP_TEMP &&
     update->first.value == value &&
     update->second.kind == Operand::OP_INTEGER &&
     update->second.has_int_value &&
     update->second.int_high ==
       (update->second.int_value < 0 ? ~UINT64_C(0) : UINT64_C(0)))
    step = update->second.int_value;
  else if(update->op.kind == LowOperation::LOP_SUB &&
          update->first.kind == Operand::OP_TEMP &&
          update->first.value == value &&
          update->second.kind == Operand::OP_INTEGER &&
          update->second.has_int_value &&
          update->second.int_high ==
            (update->second.int_value < 0 ? ~UINT64_C(0) : UINT64_C(0)) &&
          update->second.int_value != std::numeric_limits<long long>::min())
    step = -update->second.int_value;
  else return false;
  if(step == 0) return false;
  result->phi_instruction = phi_index;
  result->update_block = definition_block[updated.value];
  result->update_instruction = definition_index[updated.value];
  result->value = value;
  result->initial = initial.int_value;
  result->step = step;
  return true;
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

bool proves_termination(long long initial, long long step, long long bound,
                        LowOperation::Kind condition)
{
  typedef __int128 Wide;
  const Wide first = initial;
  const Wide increment = step;
  const Wide limit = bound;
  Wide trips = 0;
  if(condition == LowOperation::LOP_LT && increment > 0) {
    if(first >= limit) return true;
    trips = (limit - first + increment - 1) / increment;
  } else if(condition == LowOperation::LOP_LE && increment > 0) {
    if(first > limit) return true;
    trips = (limit - first) / increment + 1;
  } else if(condition == LowOperation::LOP_GT && increment < 0) {
    if(first <= limit) return true;
    const Wide positive = -increment;
    trips = (first - limit + positive - 1) / positive;
  } else if(condition == LowOperation::LOP_GE && increment < 0) {
    if(first < limit) return true;
    trips = (first - limit) / (-increment) + 1;
  } else if(condition == LowOperation::LOP_NE) {
    const Wide difference = limit - first;
    if(difference == 0) return true;
    if((difference > 0) != (increment > 0) ||
       difference % increment != 0) return false;
    trips = difference / increment;
  } else return false;
  if(trips < 0 || trips > static_cast<Wide>(UINT64_C(1000000000)))
    return false;
  const Wide final_value = first + trips * increment;
  return final_value >= std::numeric_limits<long long>::min() &&
    final_value <= std::numeric_limits<long long>::max();
}

bool power_of_two(long long value, unsigned * shift)
{
  if(value <= 1) return false;
  const std::uint64_t bits = static_cast<std::uint64_t>(value);
  if((bits & (bits - 1)) != 0) return false;
  unsigned result = 0;
  for(std::uint64_t cursor = bits; cursor > 1; cursor >>= 1) ++result;
  *shift = result;
  return true;
}

bool has_outside_value_use(const Function & function,
                           const lowir_analysis::NaturalLoop & loop)
{
  std::vector<unsigned char> defined(function.value_names.size(), 0);
  for(std::size_t member = 0; member < loop.blocks.size(); ++member) {
    const std::vector<Instruction> & instructions =
      function.blocks[loop.blocks[member]].instructions;
    for(std::size_t i = 0; i < instructions.size(); ++i)
      if(instructions[i].dest.valid()) defined[instructions[i].dest] = 1;
  }
  const auto outside_use = [&defined](const Operand & operand) {
    return operand.kind == Operand::OP_TEMP && defined[operand.value];
  };
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    if(loop.contains(block)) continue;
    for(std::size_t i = 0; i < function.blocks[block].instructions.size(); ++i) {
      const Instruction & ins = function.blocks[block].instructions[i];
      if(outside_use(ins.first) || outside_use(ins.second) ||
         outside_use(ins.third)) return true;
      for(std::size_t arg = 0; arg < ins.args.size(); ++arg)
        if(outside_use(ins.args[arg])) return true;
    }
  }
  return false;
}

void erase_loop_blocks(Function * function,
                       const lowir_analysis::NaturalLoop & loop)
{
  std::vector<unsigned char> erase(function->blocks.size(), 0);
  for(std::size_t i = 0; i < loop.blocks.size(); ++i)
    erase[loop.blocks[i]] = 1;
  std::size_t kept = 0;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    if(!erase[block]) {
      if(kept != block)
        function->blocks[kept] = std::move(function->blocks[block]);
      ++kept;
    }
  function->blocks.resize(kept);
}

}  // namespace

bool simplify_counted_loops(Function * function,
                            lowir_analysis::FunctionAnalysis * analysis,
                            Stats * stats)
{
  const lowir_analysis::LoopForest & forest = analysis->loop_forest();
  if(forest.loops.empty()) return false;
  std::vector<std::size_t> definition_block(
    function->value_names.size(), kNoIndex);
  std::vector<std::size_t> definition_index(
    function->value_names.size(), kNoIndex);
  std::vector<std::size_t> uses(function->value_names.size(), 0);
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function->blocks[block].instructions.size();
        ++instruction) {
      const Instruction & ins =
        function->blocks[block].instructions[instruction];
      if(ins.dest.valid()) {
        definition_block[ins.dest] = block;
        definition_index[ins.dest] = instruction;
      }
      count_temp_use(ins.first, &uses);
      count_temp_use(ins.second, &uses);
      count_temp_use(ins.third, &uses);
      for(std::size_t arg = 0; arg < ins.args.size(); ++arg)
        count_temp_use(ins.args[arg], &uses);
    }

  bool changed = false;
  bool cfg_changed = false;
  for(std::size_t loop_index = 0;
      loop_index < forest.loops.size(); ++loop_index) {
    const lowir_analysis::NaturalLoop & loop = forest.loops[loop_index];
    if(loop.preheader == kNoIndex || loop.has_eh || loop.latches.size() != 1)
      continue;
    std::vector<Instruction> & header =
      function->blocks[loop.header].instructions;
    if(header.empty() || header.back().kind != Instruction::IK_BRANCH ||
       header.back().first.kind != Operand::OP_TEMP) continue;
    Instruction & branch = header.back();
    Instruction * compare = definition(function, definition_block,
      definition_index, branch.first.value);
    if(!compare || compare->kind != Instruction::IK_CMP ||
       definition_block[branch.first.value] != loop.header ||
       compare->first.kind != Operand::OP_TEMP ||
       compare->second.kind != Operand::OP_INTEGER ||
       !compare->second.has_int_value ||
       compare->type.kind != lowir_model::LTK_I64 ||
       compare->second.int_high !=
         (compare->second.int_value < 0 ? ~UINT64_C(0) : UINT64_C(0)))
      continue;
    Induction induction;
    if(!find_induction(function, loop, definition_block, definition_index,
                       compare->first.value, &induction)) continue;
    if(stats) ++stats->induction_variables;

    const std::size_t true_target = analysis->graph().find(branch.second.block);
    const std::size_t false_target = analysis->graph().find(branch.third.block);
    const bool true_inside = loop.contains(true_target);
    const bool false_inside = loop.contains(false_target);
    if(true_inside == false_inside) continue;
    LowOperation::Kind condition = compare->op.kind;
    std::size_t exit = false_target;
    if(!true_inside) {
      condition = negate_compare(condition);
      exit = true_target;
      if(condition != LowOperation::LOP_NONE && uses[compare->dest] == 1) {
        compare->op.kind = condition;
        std::swap(branch.second, branch.third);
        changed = true;
        if(stats) { ++stats->loop_exits_canonicalized; ++stats->rewrites; }
      }
    }

    for(std::size_t member = 0; member < loop.blocks.size(); ++member) {
      std::vector<Instruction> & instructions =
        function->blocks[loop.blocks[member]].instructions;
      for(std::size_t instruction = 0;
          instruction < instructions.size(); ++instruction) {
        Instruction & candidate = instructions[instruction];
        if(candidate.kind != Instruction::IK_BINARY ||
           candidate.op.kind != LowOperation::LOP_MUL) continue;
        Operand * induction_operand = &candidate.first;
        Operand * factor = &candidate.second;
        if(induction_operand->kind != Operand::OP_TEMP ||
           induction_operand->value != induction.value)
          std::swap(induction_operand, factor);
        unsigned shift = 0;
        if(induction_operand->kind != Operand::OP_TEMP ||
           induction_operand->value != induction.value ||
           factor->kind != Operand::OP_INTEGER || !factor->has_int_value ||
           factor->int_high != 0 ||
           !power_of_two(factor->int_value, &shift)) continue;
        const Operand induction_value = *induction_operand;
        const Operand factor_value = *factor;
        candidate.first = induction_value;
        candidate.second = factor_value;
        candidate.second.int_value = shift;
        candidate.second.int_high = 0;
        candidate.second.has_spelling = false;
        candidate.op.kind = LowOperation::LOP_SHL;
        changed = true;
        if(stats) { ++stats->induction_strength_reductions; ++stats->rewrites; }
      }
    }

    if(exit == kNoIndex || loop.exits.size() != 1 || loop.exits[0] != exit ||
       !proves_termination(induction.initial, induction.step,
                           compare->second.int_value, condition) ||
       has_outside_value_use(*function, loop)) continue;
    bool pure = true;
    for(std::size_t member = 0; member < loop.blocks.size() && pure; ++member)
      for(std::size_t instruction = 0;
          instruction < function->blocks[loop.blocks[member]].instructions.size();
          ++instruction)
        pure = pure && pure_loop_instruction(
          function->blocks[loop.blocks[member]].instructions[instruction]);
    if(!pure || (!function->blocks[exit].instructions.empty() &&
       function->blocks[exit].instructions.front().kind == Instruction::IK_PHI))
      continue;
    Instruction & preheader =
      function->blocks[loop.preheader].instructions.back();
    if(preheader.kind != Instruction::IK_JUMP ||
       preheader.first.block != function->blocks[loop.header].id) continue;
    preheader.first.block = function->blocks[exit].id;
    erase_loop_blocks(function, loop);
    changed = true;
    cfg_changed = true;
    if(stats) { ++stats->dead_loops_removed; ++stats->rewrites; }
    // The cached loop forest describes the old CFG.  Later cleanup removes
    // this loop; leave any other loop for the next optimizer invocation.
    break;
  }
  if(cfg_changed) analysis->invalidate_cfg();
  return changed;
}

}  // namespace lowir_opt
