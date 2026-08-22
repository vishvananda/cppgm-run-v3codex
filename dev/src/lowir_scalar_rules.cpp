#include "lowir_scalar_rules.h"

namespace lowir_opt {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::LowType;
using lowir_model::Operand;

// Rewrite an unsigned divide, remainder, or multiply whose right operand is
// a positive power of two into the equivalent shift or mask.  Signed division
// keeps its rounding semantics and is not rewritten.
bool strength_reduce_binary(Instruction * ins, const LowType & type)
{
  if(ins->kind != Instruction::IK_BINARY ||
     ins->second.kind != Operand::OP_INTEGER ||
     ins->second.int_high != 0 ||
     type.kind == lowir_model::LTK_I128) return false;
  const unsigned long long value = ins->second.int_value;
  if(value == 0 || (value & (value - 1)) != 0 || value == 1) return false;
  unsigned shift = 0;
  while((value >> shift) != 1) ++shift;
  if(ins->op.kind == LowOperation::LOP_UDIV) {
    ins->op.kind = LowOperation::LOP_USHR;
    ins->second.int_value = shift;
  } else if(ins->op.kind == LowOperation::LOP_UMOD) {
    ins->op.kind = LowOperation::LOP_AND;
    ins->second.int_value = value - 1;
  } else if(ins->op.kind == LowOperation::LOP_MUL) {
    ins->op.kind = LowOperation::LOP_SHL;
    ins->second.int_value = shift;
  } else return false;
  ins->second.int_high = 0;
  ins->second.has_spelling = false;
  return true;
}

// A readonly scalar global with a literal initializer always observes that
// literal, so a typed direct load of it is the constant.
ReadonlyGlobalIndex::ReadonlyGlobalIndex(
    const lowir_model::LowirProgram & program, bool populate)
  : known(program.symbol_names.size(), 0),
    constants(program.symbol_names.size()),
    types(program.symbol_names.size())
{
  if(!populate) return;
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const lowir_model::GlobalDefinition & global = program.globals[i];
    if(global.structured ||
       global.storage != lowir_model::GSM_READONLY ||
       global.init_kind != lowir_model::GlobalDefinition::INIT_INTEGER ||
       global.init_operand.kind != Operand::OP_INTEGER) continue;
    known[global.symbol] = 1;
    constants[global.symbol] = global.init_operand;
    types[global.symbol] = global.type;
  }
}

bool fold_readonly_global_loads(Function * function,
    const std::vector<unsigned char> & readonly_known,
    const std::vector<Operand> & readonly_constants,
    const std::vector<LowType> & readonly_types, Stats * stats)
{
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind != Instruction::IK_LOAD ||
         ins.first.kind != Operand::OP_GLOBAL ||
         static_cast<std::uint32_t>(ins.first.symbol) >=
           readonly_known.size() ||
         !readonly_known[ins.first.symbol] ||
         !lowir_model::same_lowir_type(
           ins.type, readonly_types[ins.first.symbol]))
        continue;
      ins.kind = Instruction::IK_CONST;
      ins.first = readonly_constants[ins.first.symbol];
      ins.second = Operand();
      changed = true;
      if(stats) ++stats->rewrites;
    }
  return changed;
}


bool same_operand(const Operand & a, const Operand & b)
{
  if(a.kind != b.kind) return false;
  if(a.kind == Operand::OP_LABEL) return a.block == b.block;
  if(a.kind == Operand::OP_SLOT) return a.slot == b.slot;
  if(a.kind == Operand::OP_TEMP) return a.value == b.value;
  if(a.kind == Operand::OP_GLOBAL) return a.symbol == b.symbol;
  if(a.kind == Operand::OP_INTEGER) {
    return a.has_int_value == b.has_int_value &&
      a.int_value == b.int_value && a.int_high == b.int_high;
  }
  if(a.kind == Operand::OP_FLOAT) {
    return a.has_float_bits == b.has_float_bits &&
      a.literal_low == b.literal_low && a.literal_high == b.literal_high &&
      lowir_model::same_lowir_type(a.literal_type, b.literal_type);
  }
  return true;
}

bool is_zero(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == 0;
}

bool is_one(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == 1;
}

bool is_minus_one(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == -1;
}

bool rewrite_inverted_compare(Instruction * ins,
    LowOperation::Kind inner_compare,
    lowir_model::LowTypeKind inner_type,
    const Operand & inner_first,
    const Operand & inner_second)
{
  if(inner_type == lowir_model::LTK_F32 ||
     inner_type == lowir_model::LTK_F64 ||
     inner_type == lowir_model::LTK_F80 ||
     inner_type == lowir_model::LTK_OBJECT ||
     inner_type == lowir_model::LTK_VOID ||
     inner_type == lowir_model::LTK_INVALID)
    return false;
  LowOperation::Kind inverted;
  switch(inner_compare) {
  case LowOperation::LOP_EQ: inverted = LowOperation::LOP_NE; break;
  case LowOperation::LOP_NE: inverted = LowOperation::LOP_EQ; break;
  case LowOperation::LOP_LT: inverted = LowOperation::LOP_GE; break;
  case LowOperation::LOP_GE: inverted = LowOperation::LOP_LT; break;
  case LowOperation::LOP_LE: inverted = LowOperation::LOP_GT; break;
  case LowOperation::LOP_GT: inverted = LowOperation::LOP_LE; break;
  case LowOperation::LOP_ULT: inverted = LowOperation::LOP_UGE; break;
  case LowOperation::LOP_UGE: inverted = LowOperation::LOP_ULT; break;
  case LowOperation::LOP_ULE: inverted = LowOperation::LOP_UGT; break;
  case LowOperation::LOP_UGT: inverted = LowOperation::LOP_ULE; break;
  default: return false;
  }
  ins->op.kind = inverted;
  ins->first = inner_first;
  ins->second = inner_second;
  ins->type = lowir_model::builtin_lowir_type(inner_type);
  return true;
}

namespace {

bool same_load_address(const Operand & left, const Operand & right)
{
  if(left.kind != right.kind) return false;
  if(left.kind == Operand::OP_TEMP) return left.value == right.value;
  if(left.kind == Operand::OP_SLOT) return left.slot == right.slot;
  if(left.kind == Operand::OP_GLOBAL)
    return left.symbol == right.symbol &&
      left.address_binding == right.address_binding;
  return false;
}

bool keeps_memory(const Instruction & ins)
{
  switch(ins.kind) {
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
    return true;
  default:
    return false;
  }
}

}  // namespace

bool eliminate_duplicate_block_loads(Function * function, Stats * stats)
{
  struct TrackedLoad
  {
    Operand address;
    LowType type;
    lowir_model::ValueId dest;
  };
  bool changed = false;
  std::vector<TrackedLoad> live;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    live.clear();
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind == Instruction::IK_LOAD) {
        bool matched = false;
        for(std::size_t i = 0; i < live.size() && !matched; ++i) {
          if(!same_load_address(live[i].address, ins.first) ||
             !lowir_model::same_lowir_type(live[i].type, ins.type))
            continue;
          ins.kind = Instruction::IK_COPY;
          Operand prior;
          prior.kind = Operand::OP_TEMP;
          prior.value = live[i].dest;
          ins.first = prior;
          ins.second = Operand();
          changed = true;
          matched = true;
          if(stats) ++stats->duplicate_block_loads_removed;
        }
        if(!matched && ins.dest.valid()) {
          TrackedLoad tracked;
          tracked.address = ins.first;
          tracked.type = ins.type;
          tracked.dest = ins.dest;
          live.push_back(tracked);
        }
        continue;
      }
      if(!keeps_memory(ins)) live.clear();
    }
  }
  return changed;
}

}
