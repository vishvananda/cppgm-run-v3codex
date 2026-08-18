#include "lowir_cleanup_o1.h"

#include "lowir_model.h"
#include "lowir_opt.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::BlockId;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::InstructionDebugLocation;
using lowir_model::Operand;

struct ResumeKey
{
  lowir_model::StringId file;
  std::size_t line;
  std::size_t column;
  std::size_t context;

  bool operator==(const ResumeKey & other) const
  {
    return file == other.file && line == other.line &&
      column == other.column && context == other.context;
  }
};

struct ResumeKeyHash
{
  std::size_t operator()(const ResumeKey & key) const
  {
    std::size_t result = static_cast<std::uint32_t>(key.file);
    result ^= key.line + static_cast<std::size_t>(0x9e3779b9U) +
      (result << 6) + (result >> 2);
    result ^= key.column + static_cast<std::size_t>(0x9e3779b9U) +
      (result << 6) + (result >> 2);
    result ^= key.context + static_cast<std::size_t>(0x9e3779b9U) +
      (result << 6) + (result >> 2);
    return result;
  }
};

ResumeKey resume_key(const InstructionDebugLocation & location,
                     std::size_t context)
{
  ResumeKey key;
  key.file = location.file;
  key.line = location.line;
  key.column = location.column;
  key.context = context;
  return key;
}

void redirect_target(Operand * target,
    const std::vector<BlockId> & replacements)
{
  if(target->kind != Operand::OP_LABEL) return;
  const std::uint32_t id = target->block;
  if(id < replacements.size() && replacements[id].valid())
    target->block = replacements[id];
}

void redirect_instruction_targets(Instruction * instruction,
    const std::vector<BlockId> & replacements)
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

void combine_hash(std::size_t * seed, std::size_t value)
{
  *seed ^= value + static_cast<std::size_t>(0x9e3779b9U) +
    (*seed << 6) + (*seed >> 2);
}

bool same_type(const lowir_model::LowType & left,
               const lowir_model::LowType & right)
{
  return lowir_model::same_lowir_type(left, right);
}

std::size_t type_hash(const lowir_model::LowType & type)
{
  std::size_t result = static_cast<std::size_t>(type.kind);
  combine_hash(&result, static_cast<std::size_t>(type.kind));
  combine_hash(&result, type.storage_size);
  combine_hash(&result, type.alignment);
  return result;
}

bool same_operand(const Operand & left, const Operand & right)
{
  if(left.kind != right.kind ||
     left.address_binding != right.address_binding ||
     left.has_int_value != right.has_int_value ||
     left.has_spelling != right.has_spelling ||
     left.int_value != right.int_value || left.int_high != right.int_high ||
     !same_type(left.literal_type, right.literal_type)) return false;
  if(left.kind == Operand::OP_LABEL) return left.block == right.block;
  if(left.kind == Operand::OP_SLOT) return left.slot == right.slot;
  if(left.kind == Operand::OP_TEMP) return left.value == right.value;
  if(left.kind == Operand::OP_GLOBAL) return left.symbol == right.symbol;
  if(left.has_spelling) return left.literal == right.literal;
  if(left.kind == Operand::OP_FLOAT)
    return (std::isnan(left.float_value) && std::isnan(right.float_value)) ||
      left.float_value == right.float_value;
  return true;
}

std::size_t operand_hash(const Operand & operand)
{
  std::size_t result = static_cast<std::size_t>(operand.kind);
  combine_hash(&result, static_cast<std::size_t>(operand.address_binding));
  combine_hash(&result, operand.has_int_value ? 1 : 0);
  combine_hash(&result, operand.has_spelling ? 1 : 0);
  combine_hash(&result, operand.kind == Operand::OP_LABEL ?
    static_cast<std::uint32_t>(operand.block) :
    operand.kind == Operand::OP_SLOT ?
    static_cast<std::uint32_t>(operand.slot) :
    operand.kind == Operand::OP_TEMP ?
    static_cast<std::uint32_t>(operand.value) :
    operand.kind == Operand::OP_GLOBAL ?
    static_cast<std::uint32_t>(operand.symbol) :
    (operand.kind == Operand::OP_FLOAT ||
     operand.kind == Operand::OP_INTEGER) && operand.has_spelling ?
    static_cast<std::uint32_t>(operand.literal) :
    lowir_model::kInvalidCompactId);
  combine_hash(&result, std::hash<long long>()(operand.int_value));
  combine_hash(&result, std::hash<std::uint64_t>()(operand.int_high));
  if(operand.kind == Operand::OP_FLOAT && !operand.has_spelling)
    combine_hash(&result, std::isnan(operand.float_value) ?
      static_cast<std::size_t>(0x7ff80000U) :
      std::hash<long double>()(operand.float_value));
  combine_hash(&result, type_hash(operand.literal_type));
  return result;
}

bool same_boundary(const lowir_model::FunctionBoundaryMetadata & left,
                   const lowir_model::FunctionBoundaryMetadata & right)
{
  return left.arity == right.arity && left.effects == right.effects &&
    left.unwind == right.unwind && left.returns == right.returns;
}

std::size_t boundary_hash(
    const lowir_model::FunctionBoundaryMetadata & boundary)
{
  std::size_t result = static_cast<std::size_t>(boundary.arity);
  combine_hash(&result, static_cast<std::size_t>(boundary.effects));
  combine_hash(&result, static_cast<std::size_t>(boundary.unwind));
  combine_hash(&result, static_cast<std::size_t>(boundary.returns));
  return result;
}

bool same_parameter(const lowir_model::Parameter & left,
                    const lowir_model::Parameter & right)
{
  return left.name == right.name && same_type(left.type, right.type) &&
    left.metadata.passing == right.metadata.passing &&
    left.metadata.capture == right.metadata.capture &&
    left.metadata.access == right.metadata.access &&
    left.metadata.alias == right.metadata.alias;
}

std::size_t parameter_hash(const lowir_model::Parameter & parameter)
{
  std::size_t result = static_cast<std::uint32_t>(parameter.name);
  combine_hash(&result, type_hash(parameter.type));
  combine_hash(&result, static_cast<std::size_t>(parameter.metadata.passing));
  combine_hash(&result, static_cast<std::size_t>(parameter.metadata.capture));
  combine_hash(&result, static_cast<std::size_t>(parameter.metadata.access));
  combine_hash(&result, static_cast<std::size_t>(parameter.metadata.alias));
  return result;
}

bool same_debug(const InstructionDebugLocation & left,
                const InstructionDebugLocation & right)
{
  return left.file == right.file && left.line == right.line &&
    left.column == right.column;
}

std::size_t debug_hash(const InstructionDebugLocation & debug)
{
  std::size_t result = static_cast<std::uint32_t>(debug.file);
  combine_hash(&result, debug.line);
  combine_hash(&result, debug.column);
  return result;
}

bool same_instruction(const Instruction & left, const Instruction & right)
{
  if(left.kind != right.kind || left.dest != right.dest ||
     !same_type(left.type, right.type) ||
     !same_type(left.source_type, right.source_type) ||
     left.op != right.op || left.byte_count != right.byte_count ||
     left.byte_alignment != right.byte_alignment ||
     left.has_eh_selector != right.has_eh_selector ||
     left.eh_selector != right.eh_selector ||
     left.index_projection != right.index_projection ||
     !same_operand(left.first, right.first) ||
     !same_operand(left.second, right.second) ||
     !same_operand(left.third, right.third) ||
     left.args.size() != right.args.size() ||
     left.call_returns_void != right.call_returns_void ||
     left.has_call_signature != right.has_call_signature ||
     left.call_params.size() != right.call_params.size() ||
     !same_type(left.call_return_type, right.call_return_type) ||
     !same_boundary(left.call_boundary, right.call_boundary) ||
     !same_debug(left.debug_location, right.debug_location))
    return false;
  for(std::size_t i = 0; i < left.args.size(); ++i)
    if(!same_operand(left.args[i], right.args[i])) return false;
  for(std::size_t i = 0; i < left.call_params.size(); ++i)
    if(!same_parameter(left.call_params[i], right.call_params[i])) return false;
  return true;
}

std::size_t instruction_hash(const Instruction & instruction)
{
  std::size_t result = static_cast<std::size_t>(instruction.kind);
  combine_hash(&result, static_cast<std::uint32_t>(instruction.dest));
  combine_hash(&result, type_hash(instruction.type));
  combine_hash(&result, type_hash(instruction.source_type));
  combine_hash(&result, lowir_model::lowir_operation_hash(instruction.op));
  combine_hash(&result, instruction.byte_count);
  combine_hash(&result, instruction.byte_alignment);
  combine_hash(&result, instruction.has_eh_selector ? 1 : 0);
  combine_hash(&result, std::hash<long long>()(instruction.eh_selector));
  combine_hash(&result,
    static_cast<std::size_t>(instruction.index_projection));
  combine_hash(&result, operand_hash(instruction.first));
  combine_hash(&result, operand_hash(instruction.second));
  combine_hash(&result, operand_hash(instruction.third));
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    combine_hash(&result, operand_hash(instruction.args[i]));
  combine_hash(&result, instruction.args.size());
  combine_hash(&result, instruction.call_returns_void ? 1 : 0);
  combine_hash(&result, instruction.has_call_signature ? 1 : 0);
  for(std::size_t i = 0; i < instruction.call_params.size(); ++i)
    combine_hash(&result, parameter_hash(instruction.call_params[i]));
  combine_hash(&result, instruction.call_params.size());
  combine_hash(&result, type_hash(instruction.call_return_type));
  combine_hash(&result, boundary_hash(instruction.call_boundary));
  combine_hash(&result, debug_hash(instruction.debug_location));
  return result;
}

bool may_be_shared_cleanup_instruction(Instruction::Kind kind)
{
  return !(kind >= Instruction::IK_EH_TRY &&
           kind <= Instruction::IK_EH_END) &&
    kind != Instruction::IK_THROW && kind != Instruction::IK_EXCEPTION &&
    kind != Instruction::IK_EXCEPTION_SELECTOR &&
    kind != Instruction::IK_RESUME && kind != Instruction::IK_JUMP &&
    kind != Instruction::IK_BRANCH && kind != Instruction::IK_SWITCH &&
    kind != Instruction::IK_RETURN;
}

struct EhFrame
{
  BlockId landing;
  bool consumed;

  bool operator==(const EhFrame & other) const
  {
    return landing == other.landing && consumed == other.consumed;
  }
};

typedef std::vector<EhFrame> EhState;

struct EhStateHash
{
  std::size_t operator()(const EhState & state) const
  {
    std::size_t result = state.size();
    for(std::size_t i = 0; i < state.size(); ++i) {
      combine_hash(&result, static_cast<std::uint32_t>(state[i].landing));
      combine_hash(&result, state[i].consumed ? 1 : 0);
    }
    return result;
  }
};

EhState active_eh_context(const EhState & state)
{
  for(std::size_t end = state.size(); end > 0; --end)
    if(!state[end - 1].consumed)
      return EhState(state.begin(), state.begin() + end);
  return EhState();
}

void end_eh_region(EhState * state)
{
  if(state->empty()) return;
  const bool consumed = state->back().consumed;
  state->pop_back();
  if(consumed)
    while(!state->empty() && state->back().consumed) state->pop_back();
}

bool block_has_catches(const Block & block)
{
  for(std::size_t i = 0; i < block.instructions.size(); ++i)
    if(block.instructions[i].kind == Instruction::IK_EH_CATCH ||
       block.instructions[i].kind == Instruction::IK_EH_FILTER)
      return true;
  return false;
}

bool block_has_eh_structure(const Block & block)
{
  for(std::size_t i = 0; i < block.instructions.size(); ++i)
    if(block.instructions[i].kind >= Instruction::IK_EH_TRY &&
       block.instructions[i].kind <= Instruction::IK_EH_END)
      return true;
  return false;
}

struct CleanupContexts
{
  std::size_t unprotected_context = 0;
  std::vector<std::size_t> active;
  std::vector<unsigned char> active_conflict;
  std::vector<unsigned char> cleanup_landing;
};

CleanupContexts analyze_cleanup_contexts(const Function & function)
{
  CleanupContexts result;
  const std::size_t count = function.blocks.size();
  result.active.assign(count, 0);
  result.active_conflict.assign(count, 0);
  result.cleanup_landing.assign(count, 0);
  if(count == 0) return result;

  const std::size_t no_block = static_cast<std::size_t>(-1);
  std::vector<std::size_t> block_index(function.next_block_id, no_block);
  std::vector<unsigned char> has_catches(count, 0);
  for(std::size_t i = 0; i < count; ++i) {
    block_index[function.blocks[i].id] = i;
    has_catches[i] = block_has_catches(function.blocks[i]);
  }
  std::unordered_map<EhState, std::size_t, EhStateHash> context_ids;
  const auto context_id = [&](const EhState & state) {
    const EhState active = active_eh_context(state);
    const std::unordered_map<EhState, std::size_t,
      EhStateHash>::const_iterator found = context_ids.find(active);
    if(found != context_ids.end()) return found->second;
    const std::size_t id = context_ids.size() + 1;
    context_ids.emplace(active, id);
    return id;
  };
  result.unprotected_context = context_id(EhState());

  std::vector<EhState> entries(count);
  std::vector<unsigned char> known(count, 0);
  std::vector<unsigned char> state_conflict(count, 0);
  std::deque<std::size_t> worklist;
  const auto merge_entry = [&](std::size_t block, const EhState & state) {
    const std::size_t active = context_id(state);
    if(result.active[block] == 0) result.active[block] = active;
    else if(result.active[block] != active) {
      result.active[block] = 0;
      result.active_conflict[block] = 1;
    }
    if(!known[block]) {
      known[block] = 1;
      entries[block] = state;
      worklist.push_back(block);
    } else if(entries[block] != state) state_conflict[block] = 1;
  };
  const auto merge_label = [&](const Operand & target, const EhState & state) {
    if(target.kind != Operand::OP_LABEL) return;
    const std::uint32_t id = target.block;
    if(id < block_index.size() && block_index[id] != no_block)
      merge_entry(block_index[id], state);
  };

  merge_entry(0, EhState());
  while(!worklist.empty()) {
    const std::size_t block_number = worklist.front();
    worklist.pop_front();
    if(state_conflict[block_number]) continue;
    const Block & block = function.blocks[block_number];
    EhState state = entries[block_number];
    bool exits = false;
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const Instruction & instruction = block.instructions[i];
      if(instruction.kind == Instruction::IK_EH_TRY ||
         instruction.kind == Instruction::IK_EH_CLEANUP) {
        const std::uint32_t target_id = instruction.first.block;
        if(target_id < block_index.size() &&
           block_index[target_id] != no_block) {
          const std::size_t target = block_index[target_id];
          EhState landing = state;
          landing.push_back(EhFrame{instruction.first.block, true});
          merge_entry(target, landing);
          if(instruction.kind == Instruction::IK_EH_CLEANUP ||
             !has_catches[target])
            result.cleanup_landing[target] = 1;
        }
        state.push_back(EhFrame{instruction.first.block, false});
      } else if(instruction.kind == Instruction::IK_EH_END) {
        end_eh_region(&state);
      }

      if(instruction.kind == Instruction::IK_JUMP) {
        merge_label(instruction.first, state);
        exits = true;
      } else if(instruction.kind == Instruction::IK_BRANCH) {
        merge_label(instruction.second, state);
        merge_label(instruction.third, state);
        exits = true;
      } else if(instruction.kind == Instruction::IK_SWITCH) {
        merge_label(instruction.second, state);
        for(std::size_t arg = 1; arg < instruction.args.size(); arg += 2)
          merge_label(instruction.args[arg], state);
        exits = true;
      } else if(instruction.kind == Instruction::IK_RETURN ||
                instruction.kind == Instruction::IK_RESUME ||
                instruction.kind == Instruction::IK_THROW ||
                (instruction.kind == Instruction::IK_CALL &&
                 instruction.call_boundary.returns ==
                   lowir_model::CRM_NORETURN)) {
        exits = true;
      }
      if(exits) break;
    }
    if(!exits && block_number + 1 < count)
      merge_entry(block_number + 1, state);
  }
  return result;
}

struct SuffixNode
{
  const Instruction * instruction;
  std::size_t next;
};

struct SuffixNodeHash
{
  std::size_t operator()(const SuffixNode & node) const
  {
    std::size_t result = instruction_hash(*node.instruction);
    combine_hash(&result, node.next);
    return result;
  }
};

struct SuffixNodeEqual
{
  bool operator()(const SuffixNode & left, const SuffixNode & right) const
  {
    return left.next == right.next &&
      same_instruction(*left.instruction, *right.instruction);
  }
};

struct TailOccurrence
{
  std::size_t block;
  std::size_t start;
  std::size_t length;
  std::size_t context;
};

struct TailCandidate
{
  std::size_t suffix;
  std::size_t length;
  std::size_t benefit;
  std::size_t context;
};

std::size_t candidate_benefit(
    const std::vector<TailOccurrence> & occurrences, std::size_t length)
{
  bool has_whole_block = false;
  for(std::size_t i = 0; i < occurrences.size(); ++i)
    if(occurrences[i].start == 0) has_whole_block = true;
  const std::size_t gross = (occurrences.size() - 1) * (length - 1);
  if(!has_whole_block) return 0;
  return gross;
}

Instruction jump_to(BlockId block,
                    const InstructionDebugLocation & debug)
{
  Instruction result;
  result.kind = Instruction::IK_JUMP;
  result.first.kind = Operand::OP_LABEL;
  result.first.block = block;
  result.debug_location = debug;
  return result;
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

  const CleanupContexts contexts = analyze_cleanup_contexts(*function);

  std::unordered_map<ResumeKey, BlockId, ResumeKeyHash> canonical;
  std::vector<BlockId> replacements(function->next_block_id);
  std::size_t replacement_count = 0;
  std::vector<unsigned char> duplicate(function->blocks.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    if(block.instructions.size() != 1 ||
       block.instructions[0].kind != Instruction::IK_RESUME ||
       contexts.active_conflict[i] || contexts.active[i] == 0 ||
       (contexts.active[i] != contexts.unprotected_context &&
        !contexts.cleanup_landing[i]))
      continue;
    const ResumeKey key = resume_key(block.instructions[0].debug_location,
                                     contexts.active[i]);
    const std::unordered_map<ResumeKey, BlockId,
      ResumeKeyHash>::const_iterator found = canonical.find(key);
    if(found == canonical.end()) canonical.emplace(key, block.id);
    else {
      replacements[block.id] = found->second;
      ++replacement_count;
      duplicate[i] = 1;
    }
  }
  if(replacement_count == 0) return false;

  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j)
      redirect_instruction_targets(
        &function->blocks[i].instructions[j], replacements);

  std::vector<Block> retained;
  retained.reserve(function->blocks.size() - replacement_count);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    if(!duplicate[i]) retained.push_back(std::move(function->blocks[i]));
  function->blocks.swap(retained);
  if(stats) {
    stats->cleanup_resume_blocks_removed += replacement_count;
    stats->rewrites += replacement_count;
  }
  return true;
}

bool share_exact_cleanup_tails(Function * function, Stats * stats)
{
  if(stats) ++stats->cleanup_tail_runs;
  std::size_t resume_blocks = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    if(stats) ++stats->cleanup_tail_block_visits;
    const Block & block = function->blocks[i];
    if(block.instructions.size() >= 2 &&
       block.instructions.back().kind == Instruction::IK_RESUME)
      ++resume_blocks;
  }
  if(resume_blocks < 2) return false;

  const CleanupContexts contexts = analyze_cleanup_contexts(*function);

  typedef std::unordered_map<SuffixNode, std::size_t,
    SuffixNodeHash, SuffixNodeEqual> SuffixIndex;
  SuffixIndex suffix_index;
  std::vector<std::vector<TailOccurrence> > occurrences(1);
  for(std::size_t block_index = 0;
      block_index < function->blocks.size(); ++block_index) {
    const Block & block = function->blocks[block_index];
    if(block.instructions.size() < 2 ||
       block.instructions.back().kind != Instruction::IK_RESUME ||
       contexts.active_conflict[block_index] ||
       contexts.active[block_index] == 0 ||
       !contexts.cleanup_landing[block_index] ||
       block_has_eh_structure(block))
      continue;
    std::size_t next = 0;
    for(std::size_t end = block.instructions.size(); end > 0; --end) {
      const std::size_t start = end - 1;
      const Instruction & instruction = block.instructions[start];
      if(start + 1 != block.instructions.size() &&
         !may_be_shared_cleanup_instruction(instruction.kind))
        break;
      const SuffixNode node = {&instruction, next};
      const SuffixIndex::const_iterator found = suffix_index.find(node);
      std::size_t suffix = 0;
      if(found == suffix_index.end()) {
        suffix = occurrences.size();
        suffix_index.emplace(node, suffix);
        occurrences.push_back(std::vector<TailOccurrence>());
      } else suffix = found->second;
      next = suffix;
      const std::size_t length = block.instructions.size() - start;
      if(length >= 2)
        occurrences[suffix].push_back(
          TailOccurrence{block_index, start, length,
                         contexts.active[block_index]});
    }
  }

  std::vector<TailCandidate> candidates;
  for(std::size_t suffix = 1; suffix < occurrences.size(); ++suffix) {
    if(occurrences[suffix].size() < 2) continue;
    const std::size_t length = occurrences[suffix][0].length;
    for(std::size_t first = 0; first < occurrences[suffix].size(); ++first) {
      const std::size_t context = occurrences[suffix][first].context;
      bool seen = false;
      for(std::size_t prior = 0; prior < first; ++prior)
        if(occurrences[suffix][prior].context == context) seen = true;
      if(seen) continue;
      std::vector<TailOccurrence> compatible;
      for(std::size_t j = first; j < occurrences[suffix].size(); ++j)
        if(occurrences[suffix][j].context == context)
          compatible.push_back(occurrences[suffix][j]);
      if(compatible.size() < 2) continue;
      const std::size_t benefit = candidate_benefit(compatible, length);
      if(benefit)
        candidates.push_back(
          TailCandidate{suffix, length, benefit, context});
    }
  }
  std::sort(candidates.begin(), candidates.end(),
    [](const TailCandidate & left, const TailCandidate & right) {
      if(left.benefit != right.benefit)
        return left.benefit > right.benefit;
      if(left.length != right.length) return left.length > right.length;
      if(left.suffix != right.suffix) return left.suffix < right.suffix;
      return left.context < right.context;
    });

  struct RewriteGroup
  {
    TailOccurrence canonical;
    std::vector<TailOccurrence> others;
  };
  std::vector<RewriteGroup> rewrites;
  std::vector<unsigned char> claimed(function->blocks.size(), 0);
  for(std::size_t i = 0; i < candidates.size(); ++i) {
    const std::vector<TailOccurrence> & all =
      occurrences[candidates[i].suffix];
    std::vector<TailOccurrence> available;
    for(std::size_t j = 0; j < all.size(); ++j)
      if(all[j].context == candidates[i].context &&
         !claimed[all[j].block])
        available.push_back(all[j]);
    if(available.size() < 2 ||
       !candidate_benefit(available, candidates[i].length))
      continue;
    std::size_t canonical = 0;
    for(std::size_t j = 0; j < available.size(); ++j)
      if(available[j].start == 0) { canonical = j; break; }
    RewriteGroup group;
    group.canonical = available[canonical];
    for(std::size_t j = 0; j < available.size(); ++j) {
      claimed[available[j].block] = 1;
      if(j != canonical) group.others.push_back(available[j]);
    }
    rewrites.push_back(std::move(group));
  }
  if(rewrites.empty()) return false;

  std::size_t rewritten_blocks = 0;
  std::size_t before_instructions = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    before_instructions += function->blocks[i].instructions.size();

  for(std::size_t i = 0; i < rewrites.size(); ++i) {
    const TailOccurrence canonical = rewrites[i].canonical;
    Block & canonical_block = function->blocks[canonical.block];
    const BlockId target = canonical_block.id;
    for(std::size_t j = 0; j < rewrites[i].others.size(); ++j) {
      const TailOccurrence occurrence = rewrites[i].others[j];
      Block & block = function->blocks[occurrence.block];
      const InstructionDebugLocation debug =
        block.instructions[occurrence.start].debug_location;
      block.instructions.erase(block.instructions.begin() + occurrence.start,
                               block.instructions.end());
      block.instructions.push_back(jump_to(target, debug));
      ++rewritten_blocks;
    }
  }

  std::size_t after_instructions = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    after_instructions += function->blocks[i].instructions.size();
  const std::size_t removed = before_instructions - after_instructions;
  if(stats) {
    stats->cleanup_tail_groups_shared += rewrites.size();
    stats->cleanup_tail_blocks_rewritten += rewritten_blocks;
    stats->cleanup_tail_instructions_removed += removed;
    stats->rewrites += removed;
  }
  return true;
}

}  // namespace lowir_opt
