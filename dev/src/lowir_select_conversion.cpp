#include "lowir_select_conversion.h"

#include "lowir_opt.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::BlockId;
using lowir_model::Instruction;
using lowir_model::LowirFunction;
using lowir_model::LowOperation;
using lowir_model::Operand;

const std::size_t kNoBlock = static_cast<std::size_t>(-1);
const std::size_t kArmInstructionLimit = 3;

bool speculatable(const Instruction & ins)
{
  switch(ins.kind) {
  case Instruction::IK_CONST:
  case Instruction::IK_COPY:
  case Instruction::IK_ADDR:
  case Instruction::IK_INDEX:
  case Instruction::IK_UNARY:
  case Instruction::IK_CMP:
  case Instruction::IK_CONVERT:
    return true;
  case Instruction::IK_BINARY:
    return ins.op.kind != LowOperation::LOP_DIV &&
      ins.op.kind != LowOperation::LOP_UDIV &&
      ins.op.kind != LowOperation::LOP_MOD &&
      ins.op.kind != LowOperation::LOP_UMOD;
  default:
    return false;
  }
}

bool selectable_type(const lowir_model::LowType & type)
{
  switch(type.kind) {
  case lowir_model::LTK_PTR:
  case lowir_model::LTK_I1:
  case lowir_model::LTK_I8:
  case lowir_model::LTK_U8:
  case lowir_model::LTK_I16:
  case lowir_model::LTK_U16:
  case lowir_model::LTK_I32:
  case lowir_model::LTK_U32:
  case lowir_model::LTK_I64:
    return true;
  default:
    return false;
  }
}

// An arm participates when it only stages speculatable pure values and
// falls through to the join.
bool arm_shape(const lowir_model::LowirBlock & block, BlockId * join)
{
  if(block.instructions.empty()) return false;
  const Instruction & terminator = block.instructions.back();
  if(terminator.kind != Instruction::IK_JUMP ||
     terminator.first.kind != Operand::OP_LABEL) return false;
  if(block.instructions.size() > kArmInstructionLimit + 1) return false;
  for(std::size_t i = 0; i + 1 < block.instructions.size(); ++i)
    if(!speculatable(block.instructions[i])) return false;
  *join = terminator.first.block;
  return true;
}

}  // namespace

bool convert_select_diamonds(LowirFunction * function, Stats * stats)
{
  if(function->blocks.size() < 4) return false;
  std::vector<std::size_t> block_by_id(function->next_block_id, kNoBlock);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    block_by_id[function->blocks[i].id] = i;

  // Count every incoming normal edge and pin every exception target: a
  // converted arm or join may only be reached by the diamond's own edges.
  std::vector<std::size_t> predecessors(function->next_block_id, 0);
  std::vector<unsigned char> eh_target(function->next_block_id, 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const bool eh_marker = ins.kind == Instruction::IK_EH_TRY ||
        ins.kind == Instruction::IK_EH_CLEANUP ||
        ins.kind == Instruction::IK_EH_CATCH ||
        ins.kind == Instruction::IK_EH_FILTER;
      const Operand * fixed[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(fixed[k]->kind == Operand::OP_LABEL) {
          if(eh_marker) eh_target[fixed[k]->block] = 1;
          else ++predecessors[fixed[k]->block];
        }
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_LABEL) {
          if(eh_marker) eh_target[ins.args[k].block] = 1;
          else if(ins.kind != Instruction::IK_PHI)
            ++predecessors[ins.args[k].block];
        }
    }

  bool changed = false;
  for(std::size_t a = 0; a < function->blocks.size(); ++a) {
    lowir_model::LowirBlock & head = function->blocks[a];
    if(head.instructions.empty()) continue;
    Instruction & branch = head.instructions.back();
    if(branch.kind != Instruction::IK_BRANCH ||
       branch.first.kind != Operand::OP_TEMP ||
       branch.second.kind != Operand::OP_LABEL ||
       branch.third.kind != Operand::OP_LABEL ||
       branch.second.block == branch.third.block) continue;
    const std::size_t t = block_by_id[branch.second.block];
    const std::size_t f = block_by_id[branch.third.block];
    if(t == kNoBlock || f == kNoBlock || t == a || f == a) continue;
    if(eh_target[branch.second.block] || eh_target[branch.third.block] ||
       predecessors[branch.second.block] != 1 ||
       predecessors[branch.third.block] != 1) continue;
    BlockId true_join = 0;
    BlockId false_join = 0;
    if(!arm_shape(function->blocks[t], &true_join) ||
       !arm_shape(function->blocks[f], &false_join) ||
       true_join != false_join) continue;
    const std::size_t j = block_by_id[true_join];
    if(j == kNoBlock || j == a || j == t || j == f) continue;
    if(eh_target[true_join] || predecessors[true_join] != 2) continue;
    lowir_model::LowirBlock & join = function->blocks[j];

    // Every leading phi must be a two-entry arm merge of a selectable
    // scalar; a later phi never appears before an earlier one.
    std::size_t phi_count = 0;
    bool convertible = true;
    for(; phi_count < join.instructions.size(); ++phi_count) {
      const Instruction & phi = join.instructions[phi_count];
      if(phi.kind != Instruction::IK_PHI) break;
      if(phi.args.size() != 4 || !selectable_type(phi.type)) {
        convertible = false;
        break;
      }
    }
    if(!convertible) continue;

    // Validate every leading phi before mutating anything: each must merge
    // exactly the two arm edges.  Hoisting keeps arm instruction order, so
    // operand domination is preserved.
    std::vector<Operand> true_values(phi_count);
    std::vector<Operand> false_values(phi_count);
    for(std::size_t phi = 0; convertible && phi < phi_count; ++phi) {
      const Instruction & merge = join.instructions[phi];
      bool has_true = false;
      bool has_false = false;
      for(std::size_t arg = 0; arg + 1 < merge.args.size(); arg += 2) {
        if(merge.args[arg].kind != Operand::OP_LABEL) break;
        if(merge.args[arg].block == function->blocks[t].id) {
          true_values[phi] = merge.args[arg + 1];
          has_true = true;
        } else if(merge.args[arg].block == function->blocks[f].id) {
          false_values[phi] = merge.args[arg + 1];
          has_false = true;
        }
      }
      convertible = has_true && has_false;
    }
    if(!convertible) continue;

    for(std::size_t phi = 0; phi < phi_count; ++phi) {
      Instruction & merge = join.instructions[phi];
      merge.kind = Instruction::IK_SELECT;
      merge.first = branch.first;
      merge.second = true_values[phi];
      merge.third = false_values[phi];
      merge.args.clear();
    }

    // Hoist the staged arm work into the head, then fall through.  The arm
    // blocks keep their terminators: they become valid unreachable blocks
    // that later CFG cleanup removes.
    std::vector<Instruction> staged;
    std::vector<Instruction> & true_arm = function->blocks[t].instructions;
    std::vector<Instruction> & false_arm = function->blocks[f].instructions;
    staged.insert(staged.end(), true_arm.begin(), true_arm.end() - 1);
    staged.insert(staged.end(), false_arm.begin(), false_arm.end() - 1);
    true_arm.erase(true_arm.begin(), true_arm.end() - 1);
    false_arm.erase(false_arm.begin(), false_arm.end() - 1);
    head.instructions.insert(head.instructions.end() - 1,
                             staged.begin(), staged.end());
    Instruction & terminator = head.instructions.back();
    terminator.kind = Instruction::IK_JUMP;
    terminator.first = Operand();
    terminator.first.kind = Operand::OP_LABEL;
    terminator.first.block = true_join;
    terminator.second = Operand();
    terminator.third = Operand();
    // The stale static count keeps this join out of later diamonds in the
    // same run; a fresh run recounts.
    predecessors[true_join] += 1;
    changed = true;
    if(stats) ++stats->select_diamonds_converted;
  }
  return changed;
}

}  // namespace lowir_opt
