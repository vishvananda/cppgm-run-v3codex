#include "lowir_unreachable_opt.h"

#include "lowir_opt.h"

#include <cstdint>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::BlockId;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowirProgram;
using lowir_model::Operand;
using lowir_model::SymbolId;

void mark_symbol(std::vector<unsigned char> * symbols, SymbolId symbol,
                 lowir_model::SymbolRole role)
{
  if(role == lowir_model::SR_UNREACHABLE && symbol.valid() &&
     static_cast<std::uint32_t>(symbol) < symbols->size())
    (*symbols)[symbol] = 1;
}

bool is_unreachable_call(const Instruction & instruction,
                         const std::vector<unsigned char> & symbols)
{
  return instruction.kind == Instruction::IK_CALL &&
    instruction.first.kind == Operand::OP_GLOBAL &&
    instruction.first.symbol.valid() &&
    static_cast<std::uint32_t>(instruction.first.symbol) < symbols.size() &&
    symbols[instruction.first.symbol] != 0;
}

}  // namespace

UnreachableRoleIndex::UnreachableRoleIndex(const LowirProgram & program)
  : symbols_(program.symbol_names.size(), 0)
{
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    mark_symbol(&symbols_, program.function_declarations[i].symbol,
                program.function_declarations[i].metadata.role);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    mark_symbol(&symbols_, program.functions[i].symbol,
                program.functions[i].metadata.role);
}

bool UnreachableRoleIndex::eliminate_conditional_edges(
    Function * function, Stats * stats) const
{
  if(function->blocks.empty()) return false;
  std::vector<unsigned char> marker_blocks;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    if(!block.instructions.empty() &&
       is_unreachable_call(block.instructions.front(), symbols_)) {
      if(marker_blocks.empty())
        marker_blocks.assign(function->next_block_id, 0);
      marker_blocks[block.id] = 1;
      if(stats) ++stats->unreachable_marker_blocks;
    }
  }
  if(marker_blocks.empty()) return false;
  bool changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    Block & block = function->blocks[i];
    if(block.instructions.empty()) continue;
    Instruction & terminator = block.instructions.back();
    if(terminator.kind != Instruction::IK_BRANCH ||
       terminator.second.kind != Operand::OP_LABEL ||
       terminator.third.kind != Operand::OP_LABEL ||
       !terminator.second.block.valid() || !terminator.third.block.valid() ||
       static_cast<std::uint32_t>(terminator.second.block) >=
         marker_blocks.size() ||
       static_cast<std::uint32_t>(terminator.third.block) >=
         marker_blocks.size())
      continue;
    const bool true_unreachable = marker_blocks[terminator.second.block] != 0;
    const bool false_unreachable = marker_blocks[terminator.third.block] != 0;
    if(true_unreachable == false_unreachable) continue;
    const Operand target = true_unreachable ? terminator.third : terminator.second;
    const lowir_model::InstructionDebugLocation debug =
      terminator.debug_location;
    terminator = Instruction();
    terminator.kind = Instruction::IK_JUMP;
    terminator.first = target;
    terminator.debug_location = debug;
    changed = true;
    if(stats) {
      ++stats->unreachable_edges_removed;
      ++stats->rewrites;
    }
  }
  return changed;
}

std::vector<unsigned char> noreturn_symbol_index(const LowirProgram & program)
{
  std::vector<unsigned char> symbols(program.symbol_names.size(), 0);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    if(program.function_declarations[i].boundary.returns ==
         lowir_model::CRM_NORETURN ||
       program.function_declarations[i].metadata.role ==
         lowir_model::SR_UNREACHABLE)
      symbols[program.function_declarations[i].symbol] = 1;
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    if(program.functions[i].boundary.returns == lowir_model::CRM_NORETURN ||
       program.functions[i].metadata.role == lowir_model::SR_UNREACHABLE)
      symbols[program.functions[i].symbol] = 1;
  return symbols;
}

bool truncate_noreturn_continuations(
    Function * function,
    const std::vector<unsigned char> & noreturn_symbols,
    Stats * stats)
{
  // A block cannot execute past a call that never returns, so its trailing
  // instructions and control edges are dead.  Replacing the tail with a
  // constant return copied from this function makes the block
  // successor-free, drops the dead phi inputs in former successors, and
  // lets cold-block sinking serialize the raising path last.
  Instruction return_template;
  return_template.kind = Instruction::IK_RETURN;
  return_template.type = function->return_type;
  if(function->return_type.kind == lowir_model::LTK_OBJECT ||
     function->return_type.kind == lowir_model::LTK_I128)
    return false;
  if(function->return_type.kind != lowir_model::LTK_VOID) {
    if(function->return_type.kind == lowir_model::LTK_F32 ||
       function->return_type.kind == lowir_model::LTK_F64 ||
       function->return_type.kind == lowir_model::LTK_F80) {
      return_template.first.kind = Operand::OP_FLOAT;
      return_template.first.literal_type = function->return_type;
    } else {
      return_template.first.kind = Operand::OP_INTEGER;
      return_template.first.int_value = 0;
      return_template.first.int_high = 0;
      return_template.first.has_int_value = true;
    }
  }
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    std::size_t raising = instructions.size();
    for(std::size_t i = 0; i < instructions.size(); ++i) {
      const Instruction & ins = instructions[i];
      if(ins.kind == Instruction::IK_CALL &&
         ins.first.kind == Operand::OP_GLOBAL &&
         ins.first.symbol.valid() &&
         static_cast<std::uint32_t>(ins.first.symbol) <
           noreturn_symbols.size() &&
         noreturn_symbols[ins.first.symbol]) {
        raising = i;
        break;
      }
    }
    if(raising + 1 >= instructions.size()) continue;
    if(instructions.back().kind == Instruction::IK_RETURN &&
       raising + 2 == instructions.size()) continue;
    bool removes_edges = false;
    for(std::size_t i = raising + 1; i < instructions.size(); ++i) {
      const Instruction & ins = instructions[i];
      if(ins.kind == Instruction::IK_JUMP ||
         ins.kind == Instruction::IK_BRANCH ||
         ins.kind == Instruction::IK_SWITCH)
        removes_edges = true;
      else if(ins.kind != Instruction::IK_COPY &&
              ins.kind != Instruction::IK_CONST &&
              ins.kind != Instruction::IK_RETURN) {
        removes_edges = false;
        raising = instructions.size();
        break;
      }
    }
    if(raising + 1 >= instructions.size()) continue;
    if(removes_edges) {
      const BlockId self = function->blocks[block].id;
      for(std::size_t other = 0; other < function->blocks.size(); ++other)
        for(std::size_t i = 0;
            i < function->blocks[other].instructions.size(); ++i) {
          Instruction & phi = function->blocks[other].instructions[i];
          if(phi.kind != Instruction::IK_PHI) continue;
          std::size_t output = 0;
          for(std::size_t input = 0; input + 1 < phi.args.size(); input += 2) {
            if(phi.args[input].kind == Operand::OP_LABEL &&
               phi.args[input].block == self) continue;
            phi.args[output] = phi.args[input];
            phi.args[output + 1] = phi.args[input + 1];
            output += 2;
          }
          phi.args.resize(output);
        }
    }
    instructions.resize(raising + 1);
    instructions.push_back(return_template);
    changed = true;
    if(stats) { ++stats->unreachable_edges_removed; ++stats->rewrites; }
  }
  return changed;
}

std::size_t UnreachableRoleIndex::symbol_count() const
{
  std::size_t result = 0;
  for(std::size_t i = 0; i < symbols_.size(); ++i) result += symbols_[i] != 0;
  return result;
}

}  // namespace lowir_opt
