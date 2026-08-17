#include "lowir_cleanup_o1.h"

#include "lowir_model.h"
#include "lowir_opt.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::InstructionDebugLocation;
using lowir_model::Operand;

struct ResumeKey
{
  std::string file;
  std::size_t line;
  std::size_t column;

  bool operator==(const ResumeKey & other) const
  {
    return file == other.file && line == other.line && column == other.column;
  }
};

struct ResumeKeyHash
{
  std::size_t operator()(const ResumeKey & key) const
  {
    std::size_t result = std::hash<std::string>()(key.file);
    result ^= key.line + static_cast<std::size_t>(0x9e3779b9U) +
      (result << 6) + (result >> 2);
    result ^= key.column + static_cast<std::size_t>(0x9e3779b9U) +
      (result << 6) + (result >> 2);
    return result;
  }
};

ResumeKey resume_key(const InstructionDebugLocation & location)
{
  ResumeKey key;
  key.file = location.file;
  key.line = location.line;
  key.column = location.column;
  return key;
}

void redirect_target(Operand * target,
    const std::unordered_map<std::string, std::string> & replacements)
{
  if(target->kind != Operand::OP_LABEL) return;
  const std::unordered_map<std::string, std::string>::const_iterator found =
    replacements.find(target->text);
  if(found != replacements.end()) target->text = found->second;
}

void redirect_instruction_targets(Instruction * instruction,
    const std::unordered_map<std::string, std::string> & replacements)
{
  if(instruction->kind == Instruction::IK_JUMP ||
     instruction->kind == Instruction::IK_EH_TRY ||
     instruction->kind == Instruction::IK_EH_CLEANUP)
    redirect_target(&instruction->first, replacements);
  else if(instruction->kind == Instruction::IK_BRANCH) {
    redirect_target(&instruction->second, replacements);
    redirect_target(&instruction->third, replacements);
  } else if(instruction->kind == Instruction::IK_SWITCH) {
    redirect_target(&instruction->second, replacements);
    for(std::size_t i = 1; i < instruction->args.size(); i += 2)
      redirect_target(&instruction->args[i], replacements);
  }
}

}  // namespace

bool share_terminal_resume_blocks(Function * function, Stats * stats)
{
  if(stats) ++stats->cleanup_resume_runs;
  std::size_t resume_count = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    if(stats) ++stats->cleanup_resume_block_visits;
    const Block & block = function->blocks[i];
    if(block.instructions.size() == 1 &&
       block.instructions[0].kind == Instruction::IK_RESUME)
      ++resume_count;
  }
  if(resume_count < 2) return false;

  std::unordered_map<ResumeKey, std::string, ResumeKeyHash> canonical;
  std::unordered_map<std::string, std::string> replacements;
  std::vector<unsigned char> duplicate(function->blocks.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    if(block.instructions.size() != 1 ||
       block.instructions[0].kind != Instruction::IK_RESUME)
      continue;
    const ResumeKey key = resume_key(block.instructions[0].debug_location);
    const std::unordered_map<ResumeKey, std::string,
      ResumeKeyHash>::const_iterator found = canonical.find(key);
    if(found == canonical.end()) canonical.emplace(key, block.label);
    else {
      replacements[block.label] = found->second;
      duplicate[i] = 1;
    }
  }
  if(replacements.empty()) return false;

  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j)
      redirect_instruction_targets(
        &function->blocks[i].instructions[j], replacements);

  std::vector<Block> retained;
  retained.reserve(function->blocks.size() - replacements.size());
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    if(!duplicate[i]) retained.push_back(std::move(function->blocks[i]));
  function->blocks.swap(retained);
  if(stats) {
    stats->cleanup_resume_blocks_removed += replacements.size();
    stats->rewrites += replacements.size();
  }
  return true;
}

}  // namespace lowir_opt
