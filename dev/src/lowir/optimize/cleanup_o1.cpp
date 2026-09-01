#include "lowir/optimize/cleanup_o1.h"

#include "lowir/model/program.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/support.h"

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
using lowir_model::FunctionBoundaryMetadata;
using lowir_model::Instruction;
using lowir_model::InstructionDebugLocation;
using lowir_model::Operand;
using optimizer_support::all_operand_at;
using optimizer_support::all_operand_count;
using optimizer_support::combine_hash;

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
    combine_hash(&result, key.line);
    combine_hash(&result, key.column);
    combine_hash(&result, key.context);
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
     left.has_float_bits != right.has_float_bits ||
     left.has_spelling != right.has_spelling ||
     left.int_value != right.int_value || left.int_high != right.int_high ||
     !same_type(left.literal_type, right.literal_type)) return false;
  if(left.kind == Operand::OP_LABEL) return left.block == right.block;
  if(left.kind == Operand::OP_SLOT) return left.slot == right.slot;
  if(left.kind == Operand::OP_TEMP) return left.value == right.value;
  if(left.kind == Operand::OP_GLOBAL) return left.symbol == right.symbol;
  if(left.kind == Operand::OP_FLOAT)
    return left.literal_low == right.literal_low &&
      left.literal_high == right.literal_high;
  if(left.has_spelling) return left.literal == right.literal;
  return true;
}

std::size_t operand_hash(const Operand & operand)
{
  std::size_t result = static_cast<std::size_t>(operand.kind);
  combine_hash(&result, static_cast<std::size_t>(operand.address_binding));
  combine_hash(&result, operand.has_int_value ? 1 : 0);
  combine_hash(&result, operand.has_float_bits ? 1 : 0);
  combine_hash(&result, operand.has_spelling ? 1 : 0);
  combine_hash(&result, operand.kind == Operand::OP_LABEL ?
    static_cast<std::uint32_t>(operand.block) :
    operand.kind == Operand::OP_SLOT ?
    static_cast<std::uint32_t>(operand.slot) :
    operand.kind == Operand::OP_TEMP ?
    static_cast<std::uint32_t>(operand.value) :
    operand.kind == Operand::OP_GLOBAL ?
    static_cast<std::uint32_t>(operand.symbol) :
    operand.kind != Operand::OP_FLOAT && operand.has_spelling ?
    static_cast<std::uint32_t>(operand.literal) :
    lowir_model::kInvalidCompactId);
  combine_hash(&result, std::hash<long long>()(operand.int_value));
  combine_hash(&result, std::hash<std::uint64_t>()(operand.int_high));
  combine_hash(&result, type_hash(operand.literal_type));
  return result;
}

bool same_boundary(const lowir_model::FunctionBoundaryMetadata & left,
                   const lowir_model::FunctionBoundaryMetadata & right)
{
  return left.arity == right.arity && left.effects == right.effects &&
    left.unwind == right.unwind && left.returns == right.returns &&
    left.query == right.query;
}

std::size_t boundary_hash(
    const lowir_model::FunctionBoundaryMetadata & boundary)
{
  std::size_t result = static_cast<std::size_t>(boundary.arity);
  combine_hash(&result, static_cast<std::size_t>(boundary.effects));
  combine_hash(&result, static_cast<std::size_t>(boundary.unwind));
  combine_hash(&result, static_cast<std::size_t>(boundary.returns));
  combine_hash(&result, static_cast<std::size_t>(boundary.query));
  return result;
}

bool same_parameter(const lowir_model::Parameter & left,
                    const lowir_model::Parameter & right)
{
  return left.name == right.name && same_type(left.type, right.type) &&
    left.metadata.passing == right.metadata.passing &&
    left.metadata.alias == right.metadata.alias &&
    left.metadata.object_bytes == right.metadata.object_bytes;
}

std::size_t parameter_hash(const lowir_model::Parameter & parameter)
{
  std::size_t result = static_cast<std::uint32_t>(parameter.name);
  combine_hash(&result, type_hash(parameter.type));
  combine_hash(&result, static_cast<std::size_t>(parameter.metadata.passing));
  combine_hash(&result, static_cast<std::size_t>(parameter.metadata.alias));
  combine_hash(&result, parameter.metadata.object_bytes);
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
     left.volatile_access != right.volatile_access ||
     left.copy_elision_candidate != right.copy_elision_candidate ||
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
  combine_hash(&result, instruction.volatile_access ? 1 : 0);
  combine_hash(&result, instruction.copy_elision_candidate ? 1 : 0);
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
    kind != Instruction::IK_PHI &&
    kind != Instruction::IK_RESUME && kind != Instruction::IK_JUMP &&
    kind != Instruction::IK_BRANCH && kind != Instruction::IK_SWITCH &&
    kind != Instruction::IK_RETURN && kind != Instruction::IK_UNREACHABLE;
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
                instruction.kind == Instruction::IK_UNREACHABLE ||
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


enum ColdSuccessorPolicy
{
  CSP_LAYOUT,
  CSP_RAISING_PATH
};

template<ColdSuccessorPolicy Policy>
static bool has_cold_successors(Instruction::Kind kind)
{
  if(kind == Instruction::IK_JUMP || kind == Instruction::IK_BRANCH)
    return true;
  if(Policy == CSP_LAYOUT)
    return kind == Instruction::IK_EH_TRY ||
      kind == Instruction::IK_EH_CLEANUP;
  return kind == Instruction::IK_SWITCH;
}

static std::vector<std::size_t> block_position_index(
    const Function & function)
{
  const std::size_t count = function.blocks.size();
  std::vector<std::size_t> position(count, count);
  for(std::size_t block = 0; block < count; ++block) {
    const std::uint32_t id = function.blocks[block].id;
    if(id < position.size()) position[id] = block;
    else {
      position.resize(static_cast<std::size_t>(id) + 1, count);
      position[id] = block;
    }
  }
  return position;
}

// Propagate coldness only through the successor vocabulary owned by the
// caller.  Layout deliberately ignores switch cases and includes EH region
// labels; raising-path sinking includes switch arguments and excludes EH
// markers.  The reverse sweeps are bounded by the block count.
template<ColdSuccessorPolicy Policy>
static void propagate_cold_successors(
    const Function & function,
    const std::vector<std::size_t> & position,
    std::vector<unsigned char> * cold)
{
  const std::size_t count = function.blocks.size();
  for(std::size_t sweep = 0; sweep < count; ++sweep) {
    bool grew = false;
    for(std::size_t block = count; block != 0; --block) {
      if((*cold)[block - 1]) continue;
      std::size_t successors = 0;
      bool all_cold = true;
      for(std::size_t index = 0;
          index < function.blocks[block - 1].instructions.size(); ++index) {
        const Instruction & ins =
          function.blocks[block - 1].instructions[index];
        if(!has_cold_successors<Policy>(ins.kind)) continue;
        const Operand * fixed[] = {&ins.first, &ins.second, &ins.third};
        const std::size_t fixed_count = sizeof(fixed) / sizeof(fixed[0]);
        const std::size_t operand_count = fixed_count +
          (Policy == CSP_RAISING_PATH ? ins.args.size() : 0);
        for(std::size_t operand = 0; operand < operand_count; ++operand) {
          const Operand & label = operand < fixed_count ?
            *fixed[operand] : ins.args[operand - fixed_count];
          if(label.kind != Operand::OP_LABEL) continue;
          ++successors;
          const std::uint32_t id = label.block;
          const std::size_t target =
            id < position.size() ? position[id] : count;
          all_cold = all_cold && target < count && (*cold)[target];
        }
      }
      if(successors != 0 && all_cold) {
        (*cold)[block - 1] = 1;
        grew = true;
      }
    }
    if(!grew) break;
  }
}

static std::vector<unsigned char> cold_block_mask(
    const Function & function,
    const std::vector<unsigned char> & noreturn_symbols)
{
  const std::size_t count = function.blocks.size();
  std::vector<unsigned char> cold(count, 0);
  for(std::size_t block = 0; block < count; ++block) {
    bool raises = false;
    bool ordinary_successor = false;
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function.blocks[block].instructions[index];
      if(ins.kind == Instruction::IK_THROW ||
         ins.kind == Instruction::IK_RESUME ||
         ins.kind == Instruction::IK_UNREACHABLE)
        raises = true;
      else if(ins.kind == Instruction::IK_JUMP ||
              ins.kind == Instruction::IK_BRANCH ||
              ins.kind == Instruction::IK_SWITCH ||
              ins.kind == Instruction::IK_EH_TRY ||
              ins.kind == Instruction::IK_EH_CLEANUP)
        ordinary_successor = true;
      else if(ins.kind == Instruction::IK_CALL &&
              ins.first.kind == Operand::OP_GLOBAL &&
              static_cast<std::uint32_t>(ins.first.symbol) <
                noreturn_symbols.size() &&
              noreturn_symbols[ins.first.symbol])
        raises = true;
    }
    // A raising block that still names an ordinary successor would leave a
    // later phi reading a definition serialized after it, so only
    // successor-free raising blocks sink directly.
    cold[block] = raises && !ordinary_successor;
  }
  const std::vector<std::size_t> position = block_position_index(function);
  propagate_cold_successors<CSP_LAYOUT>(function, position, &cold);
  return cold;
}

// Execution-frequency coldness for definition sinking.  Unlike the block
// reordering mask, a raising block stays cold even when it carries EH
// region markers or a dead ordinary successor: everything after the
// noreturn call is unreachable, and an unwind edge is not a fallthrough.
// Blocks whose every jump, branch, or switch target is cold inherit it.
static std::vector<unsigned char> raising_path_mask(
    const Function & function,
    const std::vector<unsigned char> & noreturn_symbols)
{
  const std::size_t count = function.blocks.size();
  std::vector<unsigned char> cold(count, 0);
  for(std::size_t block = 0; block < count; ++block) {
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function.blocks[block].instructions[index];
      if(ins.kind == Instruction::IK_THROW ||
         ins.kind == Instruction::IK_RESUME ||
         ins.kind == Instruction::IK_UNREACHABLE ||
         (ins.kind == Instruction::IK_CALL &&
          ins.first.kind == Operand::OP_GLOBAL &&
          static_cast<std::uint32_t>(ins.first.symbol) <
            noreturn_symbols.size() &&
          noreturn_symbols[ins.first.symbol])) {
        cold[block] = 1;
        break;
      }
    }
  }
  const std::vector<std::size_t> position = block_position_index(function);
  propagate_cold_successors<CSP_RAISING_PATH>(function, position, &cold);
  // Landing pads are excluded even when they only raise: their entry state
  // is the unwinder's, and the exception-region rematerializer already owns
  // value placement for them.
  for(std::size_t block = 0; block < count; ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function.blocks[block].instructions[index];
      if(ins.kind != Instruction::IK_EH_TRY &&
         ins.kind != Instruction::IK_EH_CLEANUP) continue;
      if(ins.first.kind != Operand::OP_LABEL) continue;
      const std::uint32_t id = ins.first.block;
      const std::size_t target = id < position.size() ? position[id] : count;
      if(target < count) cold[target] = 0;
    }
  return cold;
}

// A pure operand-free definition (a constant or a global, function, or
// slot address) whose only uses sit in raising-cold blocks rides the hot
// path for nothing: it typically becomes live across the hot calls, claims a
// callee-saved register, and pays an eager frame home on every invocation.
// A single-block consumer takes the definition itself; consumers spread over
// several cold blocks each receive a rematerialized copy, which is free for
// operand-free instructions.
bool sink_cold_only_definitions(Function * function,
                                const std::vector<unsigned char> & noreturn_symbols,
                                Stats * stats)
{
  const std::size_t count = function->blocks.size();
  if(count < 2) return false;
  const std::vector<unsigned char> cold =
    raising_path_mask(*function, noreturn_symbols);
  bool any_cold = false;
  for(std::size_t block = 0; block < count && !any_cold; ++block)
    any_cold = cold[block] != 0;
  if(!any_cold) return false;

  const std::size_t values = function->value_names.size();
  const std::size_t no_uses = static_cast<std::size_t>(-1);
  const std::size_t many = static_cast<std::size_t>(-2);
  std::vector<std::size_t> use_block(values, no_uses);
  std::vector<unsigned char> blocked(values, 0);
  for(std::size_t block = 0; block < count; ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function->blocks[block].instructions[index];
      for(std::size_t operand = 0; operand < all_operand_count(ins); ++operand) {
        const Operand & use = all_operand_at(ins, operand);
        if(use.kind != Operand::OP_TEMP) continue;
        const std::uint32_t value = static_cast<std::uint32_t>(use.value);
        if(value >= values) continue;
        if(ins.kind == Instruction::IK_PHI || !cold[block]) blocked[value] = 1;
        else if(use_block[value] == no_uses) use_block[value] = block;
        else if(use_block[value] != block) use_block[value] = many;
      }
    }

  struct Move
  {
    Instruction payload;
    std::size_t source_block;
    std::size_t source_index;
    std::size_t target_block;
  };
  std::vector<Move> moves;
  std::vector<Move> clones;
  for(std::size_t block = 0; block < count; ++block) {
    if(cold[block]) continue;
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function->blocks[block].instructions[index];
      if((ins.kind != Instruction::IK_CONST &&
          ins.kind != Instruction::IK_ADDR) ||
         !ins.dest.valid() ||
         ins.first.kind == Operand::OP_TEMP ||
         ins.second.kind == Operand::OP_TEMP ||
         ins.third.kind == Operand::OP_TEMP ||
         !ins.args.empty())
        continue;
      const std::uint32_t value = static_cast<std::uint32_t>(ins.dest);
      if(value >= values || blocked[value] || use_block[value] == no_uses)
        continue;
      Move move;
      move.payload = ins;
      move.source_block = block;
      move.source_index = index;
      move.target_block = use_block[value];
      if(use_block[value] == many) clones.push_back(move);
      else moves.push_back(move);
    }
  }
  if(moves.empty() && clones.empty()) return false;

  // Erase originals from the rear so earlier source indices stay valid;
  // every destination is a cold block and every source is hot, so target
  // positions never shift.
  std::vector<Move> removals = moves;
  removals.insert(removals.end(), clones.begin(), clones.end());
  std::sort(removals.begin(), removals.end(),
            [](const Move & left, const Move & right) {
              return left.source_block < right.source_block ||
                (left.source_block == right.source_block &&
                 left.source_index < right.source_index);
            });
  for(std::size_t i = removals.size(); i != 0; --i) {
    const Move & move = removals[i - 1];
    std::vector<Instruction> & source =
      function->blocks[move.source_block].instructions;
    source.erase(source.begin() +
                 static_cast<std::ptrdiff_t>(move.source_index));
  }

  const auto first_use_index = [](const std::vector<Instruction> & body,
                                  lowir_model::ValueId value) {
    for(std::size_t index = 0; index < body.size(); ++index) {
      const Instruction & ins = body[index];
      for(std::size_t operand = 0; operand < all_operand_count(ins); ++operand) {
        const Operand & use = all_operand_at(ins, operand);
        if(use.kind == Operand::OP_TEMP && use.value == value) return index;
      }
    }
    return body.size();
  };
  for(std::size_t i = 0; i < moves.size(); ++i) {
    std::vector<Instruction> & target =
      function->blocks[moves[i].target_block].instructions;
    target.insert(target.begin() + static_cast<std::ptrdiff_t>(
                    first_use_index(target, moves[i].payload.dest)),
                  moves[i].payload);
  }
  std::size_t rematerialized = 0;
  for(std::size_t i = 0; i < clones.size(); ++i) {
    const lowir_model::ValueId original = clones[i].payload.dest;
    for(std::size_t block = 0; block < count; ++block) {
      if(!cold[block]) continue;
      std::vector<Instruction> & body = function->blocks[block].instructions;
      const std::size_t first = first_use_index(body, original);
      if(first == body.size()) continue;
      const lowir_model::ValueId copy =
        lowir_model::append_lowir_fresh_generated_value(
          *function, clones[i].payload.type);
      for(std::size_t index = first; index < body.size(); ++index) {
        Instruction & ins = body[index];
        Operand * fixed[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t operand = 0;
            operand < sizeof(fixed) / sizeof(fixed[0]) + ins.args.size();
            ++operand) {
          Operand & use =
            operand < sizeof(fixed) / sizeof(fixed[0]) ?
            *fixed[operand] :
            ins.args[operand - sizeof(fixed) / sizeof(fixed[0])];
          if(use.kind == Operand::OP_TEMP && use.value == original)
            use.value = copy;
        }
      }
      Instruction replacement = clones[i].payload;
      replacement.dest = copy;
      body.insert(body.begin() + static_cast<std::ptrdiff_t>(first),
                  replacement);
      ++rematerialized;
    }
  }
  if(stats) {
    stats->cold_sunk_definitions += moves.size() + rematerialized;
    stats->rewrites += moves.size() + rematerialized;
  }
  return true;
}

bool sink_cold_blocks(Function * function,
                      const std::vector<unsigned char> & noreturn_symbols,
                      Stats * stats)
{
  const std::size_t count = function->blocks.size();
  if(count < 2) return false;
  const std::vector<unsigned char> cold =
    cold_block_mask(*function, noreturn_symbols);
  // The entry block stays first even when the whole function raises.
  bool moves = false;
  for(std::size_t block = 1; block + 1 < count; ++block)
    if(cold[block] && !cold[block + 1]) {
      moves = true;
      break;
    }
  if(!moves) return false;
  std::vector<Block> ordered;
  ordered.reserve(count);
  ordered.push_back(std::move(function->blocks[0]));
  for(std::size_t block = 1; block < count; ++block)
    if(!cold[block]) ordered.push_back(std::move(function->blocks[block]));
  for(std::size_t block = 1; block < count; ++block)
    if(cold[block]) ordered.push_back(std::move(function->blocks[block]));
  function->blocks.swap(ordered);
  if(stats) ++stats->rewrites;
  return true;
}

void DceScratch::reset(const Function & function)
{
  values.assign(function.value_names.size(), DceValueLiveness());
  if(dead.size() < function.blocks.size()) dead.resize(function.blocks.size());
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    dead[block].assign(function.blocks[block].instructions.size(), 0);
  work.clear();
}

namespace {
bool call_is_removable(const Instruction & ins,
                       const FunctionBoundaries & boundaries)
{
  if(ins.kind != Instruction::IK_CALL || !ins.dest.valid()) return false;
  FunctionBoundaryMetadata boundary = ins.call_boundary;
  if(ins.first.kind == Operand::OP_GLOBAL && boundaries.known[ins.first.symbol])
    boundary = boundaries.values[ins.first.symbol];
  return (boundary.effects == lowir_model::CFXM_READNONE ||
          boundary.effects == lowir_model::CFXM_READONLY) &&
    boundary.unwind == lowir_model::CUM_NO &&
    boundary.returns != lowir_model::CRM_NORETURN;
}
}  // namespace

bool eliminate_dead_code(Function * function,
                         const FunctionBoundaries & boundaries,
                         Stats * stats,
                         DceScratch * reusable_scratch)
{
  bool has_candidate = false;
  for(std::size_t i = 0; !has_candidate && i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(local_value_definition_is_pure(ins.kind) ||
         (ins.kind == Instruction::IK_LOAD && !ins.volatile_access) ||
         call_is_removable(ins, boundaries)) {
        has_candidate = true;
        break;
      }
    }
  if(!has_candidate) {
    if(stats) ++stats->dce_candidate_skips;
    return false;
  }
  DceScratch owned_scratch;
  DceScratch & scratch = reusable_scratch ? *reusable_scratch : owned_scratch;
  scratch.reset(*function);
  std::vector<DceValueLiveness> & values = scratch.values;
  std::vector<std::vector<unsigned char> > & dead = scratch.dead;
  const auto count_use = [&values](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP) ++values[operand.value].uses;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.dest.valid()) {
        DceValueLiveness & value = values[ins.dest];
        value.definition = DceLocation(i, j);
        value.defined = true;
      }
      count_use(ins.first);
      count_use(ins.second);
      count_use(ins.third);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        count_use(ins.args[k]);
      if(stats) ++stats->instruction_visits;
    }
  }

  std::deque<DceLocation> & work = scratch.work;
  for(std::size_t i = 0; i < values.size(); ++i) {
    const DceValueLiveness & value = values[i];
    if(!value.defined) continue;
    const Instruction & ins =
      function->blocks[value.definition.first].instructions[
        value.definition.second];
    if(value.uses == 0 &&
       (local_value_definition_is_pure(ins.kind) ||
         (ins.kind == Instruction::IK_LOAD && !ins.volatile_access) ||
        call_is_removable(ins, boundaries))) {
      work.push_back(value.definition);
      if(stats) ++stats->worklist_pushes;
    }
  }

  const auto release_operand = [&](const Operand & operand) {
    if(operand.kind != Operand::OP_TEMP) return;
    DceValueLiveness & value = values[operand.value];
    if(value.uses == 0) return;
    --value.uses;
    if(value.uses != 0 || !value.defined) return;
    const Instruction & producer =
      function->blocks[value.definition.first].instructions[
        value.definition.second];
    if(local_value_definition_is_pure(producer.kind) ||
       (producer.kind == Instruction::IK_LOAD && !producer.volatile_access) ||
       call_is_removable(producer, boundaries)) {
      work.push_back(value.definition);
      if(stats) ++stats->worklist_pushes;
    }
  };

  std::size_t removed = 0;
  while(!work.empty()) {
    const DceLocation location = work.front();
    work.pop_front();
    if(dead[location.first][location.second]) continue;
    dead[location.first][location.second] = 1;
    ++removed;
    const Instruction & ins =
      function->blocks[location.first].instructions[location.second];
    release_operand(ins.first);
    release_operand(ins.second);
    release_operand(ins.third);
    for(std::size_t i = 0; i < ins.args.size(); ++i)
      release_operand(ins.args[i]);
  }
  if(!removed) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j)
      if(!dead[i][j]) {
        if(kept != j) instructions[kept] = std::move(instructions[j]);
        ++kept;
      }
    instructions.resize(kept);
  }
  if(stats) stats->rewrites += removed;
  return true;
}

}  // namespace lowir_opt
