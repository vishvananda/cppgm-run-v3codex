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

std::size_t UnreachableRoleIndex::symbol_count() const
{
  std::size_t result = 0;
  for(std::size_t i = 0; i < symbols_.size(); ++i) result += symbols_[i] != 0;
  return result;
}

}  // namespace lowir_opt
