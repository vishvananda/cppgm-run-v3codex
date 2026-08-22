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

}
