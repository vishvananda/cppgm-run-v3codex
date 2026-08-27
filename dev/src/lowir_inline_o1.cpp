#include "lowir_inline_o1.h"
#include "lowir_unreachable_opt.h"
#include "lowir_inline_analysis.h"
#include "lowir_opt.h"
#include "lowir_phi_edges.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lowir_opt {

void apply_inline_limit_option(InlinePolicyOverrides * limits,
                               const std::string & spec)
{
  const std::size_t equals = spec.find('=');
  if(!limits || equals == std::string::npos || equals == 0 ||
     equals + 1 == spec.size())
    throw std::logic_error("invalid --inline-limit argument: " + spec);
  const std::string name = spec.substr(0, equals);
  const std::string text = spec.substr(equals + 1);
  for(std::size_t i = 0; i < text.size(); ++i)
    if(text[i] < '0' || text[i] > '9')
      throw std::logic_error("invalid --inline-limit value: " + spec);
  char * end = 0;
  const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
  if(end == text.c_str() || *end != '\0' || value == 0 ||
     value > static_cast<unsigned long long>(
       std::numeric_limits<std::size_t>::max()))
    throw std::logic_error("invalid --inline-limit value: " + spec);
  const std::size_t limit = static_cast<std::size_t>(value);
  if(name == "caller-budget") limits->caller_budget = limit;
  else if(name == "once-cap") limits->single_call_limit = limit;
  else if(name == "once-caller-budget")
    limits->single_call_caller_budget = limit;
  else if(name == "hint-late-cap") limits->hint_late_cap = limit;
  else throw std::logic_error("unknown --inline-limit name: " + spec);
}

namespace {

using lowir_model::Block;
using lowir_model::BlockId;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::LowirProgram;
using lowir_model::Operand;

const std::size_t kNoFunction = InlineCallGraph::no_function();
const std::size_t kInlineInstructionBudget = 768;
const std::size_t kSingleCallInstructionLimit = 512;
const std::size_t kSingleCallCallerBudget = 1024;
const std::size_t kMinimumSingleCallTranslationUnitBudget = 10240;
const std::size_t kLateNonleafInstructionLimit = 6;
const std::size_t kLateHintNonleafInstructionLimit = 48;
const std::size_t kOrdinarySizeCap = 40;
const std::size_t kTrivialLeafInstructionLimit = 4;

// The active policy limits: the shipped defaults with any nonzero fields of
// the driver's --inline-limit overrides applied on top.
struct InlinePolicyLimits
{
  std::size_t caller_budget;
  std::size_t single_call_limit;
  std::size_t single_call_caller_budget;
  std::size_t hint_late_nonleaf_limit;
  std::size_t hint_size_cap;
};

InlinePolicyLimits resolve_inline_policy_limits(
  const InlinePolicyOverrides * overrides)
{
  InlinePolicyLimits limits;
  limits.caller_budget = kInlineInstructionBudget;
  limits.single_call_limit = kSingleCallInstructionLimit;
  limits.single_call_caller_budget = kSingleCallCallerBudget;
  limits.hint_late_nonleaf_limit = kLateHintNonleafInstructionLimit;
  if(overrides) {
    if(overrides->caller_budget)
      limits.caller_budget = overrides->caller_budget;
    if(overrides->single_call_limit)
      limits.single_call_limit = overrides->single_call_limit;
    if(overrides->single_call_caller_budget)
      limits.single_call_caller_budget = overrides->single_call_caller_budget;
    if(overrides->hint_late_cap)
      limits.hint_late_nonleaf_limit = overrides->hint_late_cap;
  }
  limits.hint_size_cap =
    std::max(kOrdinarySizeCap, limits.hint_late_nonleaf_limit);
  return limits;
}
class ValueMap
{
public:
  explicit ValueMap(std::size_t size = 0)
    : values_(size), known_(size, 0), count_(0) {}

  void resize(std::size_t size)
  {
    if(values_.size() < size) {
      values_.resize(size);
      known_.resize(size, 0);
    }
  }

  void set(lowir_model::ValueId value, const Operand & replacement)
  {
    const std::uint32_t id = value;
    resize(static_cast<std::size_t>(id) + 1);
    if(!known_[id]) { known_[id] = 1; ++count_; }
    values_[id] = replacement;
  }

  const Operand * find(lowir_model::ValueId value) const
  {
    const std::uint32_t id = value;
    return id < values_.size() && known_[id] ? &values_[id] : 0;
  }

  bool empty() const { return count_ == 0; }
  std::size_t size() const { return values_.size(); }

private:
  std::vector<Operand> values_;
  std::vector<unsigned char> known_;
  std::size_t count_;
};
typedef std::vector<lowir_model::SlotId> SlotMap;

struct RenamedBlock
{
  BlockId id;
};

typedef std::vector<RenamedBlock> BlockMap;

bool is_eh_instruction(Instruction::Kind kind)
{
  return kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_RESUME;
}

bool direct_call(const Instruction & instruction,
                 lowir_model::SymbolId * target)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL) return false;
  if(target) *target = instruction.first.symbol;
  return true;
}

Instruction jump_to(BlockId block)
{
  Instruction result;
  result.kind = Instruction::IK_JUMP;
  result.first.kind = Operand::OP_LABEL;
  result.first.block = block;
  return result;
}

Operand value_operand(lowir_model::ValueId value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_TEMP;
  result.value = value;
  result.literal_type = type;
  return result;
}

Operand slot_operand(lowir_model::SlotId slot, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_SLOT;
  result.slot = slot;
  result.literal_type = type;
  return result;
}

bool contains_eh(const Function & function)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      if(is_eh_instruction(function.blocks[i].instructions[j].kind)) return true;
  return false;
}

std::size_t instruction_count(const Function & function)
{
  std::size_t result = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    result += function.blocks[i].instructions.size();
  return result;
}

struct Names
{
  lowir_model::StringPool & strings;
  Function & function;
  bool retain;
  std::uint32_t next_site_id;

  Names(LowirProgram & program, Function & function_value)
    : strings(program.strings), function(function_value),
      retain(program.presentation_policy ==
        lowir_model::PRESENTATION_SERIALIZABLE), next_site_id(0) {}

  std::uint32_t next_site()
  {
    while(function.generated_name_reservations.contains(
        lowir_model::GNR_O1_SITE, next_site_id)) ++next_site_id;
    const std::uint32_t result = next_site_id++;
    function.generated_name_reservations.reserve(
      lowir_model::GNR_O1_SITE, result);
    return result;
  }

  void inherit_sites(const Function & source)
  {
    function.generated_name_reservations.merge_kind(
      source.generated_name_reservations, lowir_model::GNR_O1_SITE);
  }

  lowir_model::StringId unique_slot(
      const std::string & stem,
      const lowir_model::GeneratedNameReservations & source,
      lowir_model::GeneratedNameReservationKind suffix_kind,
      std::uint32_t first_suffix = 1)
  {
    if(!retain)
      throw std::logic_error("object-only LowIR requested a display name");
    std::uint32_t suffix = first_suffix;
    while(source.contains(suffix_kind, suffix)) ++suffix;
    return strings.intern(stem + std::to_string(suffix));
  }
};

void rename_operand(Operand * operand, const ValueMap & values,
                    const SlotMap & slots, const BlockMap & blocks)
{
  if(operand->kind == Operand::OP_TEMP) {
    const Operand * found = values.find(operand->value);
    if(found) *operand = *found;
  } else if(operand->kind == Operand::OP_SLOT) {
    const std::uint32_t id = operand->slot;
    if(id < slots.size() && slots[id].valid()) operand->slot = slots[id];
  } else if(operand->kind == Operand::OP_LABEL) {
    const std::uint32_t id = operand->block;
    if(id < blocks.size() && blocks[id].id.valid())
      operand->block = blocks[id].id;
  }
}

void rename_instruction(Instruction * result, const ValueMap & values,
                        const SlotMap & slots, const BlockMap & blocks)
{
  if(result->dest.valid()) {
    const Operand * found = values.find(result->dest);
    if(!found || found->kind != Operand::OP_TEMP)
      throw std::logic_error("inlined result has no value name");
    result->dest = found->value;
  }
  rename_operand(&result->first, values, slots, blocks);
  rename_operand(&result->second, values, slots, blocks);
  rename_operand(&result->third, values, slots, blocks);
  if(result->kind == Instruction::IK_PHI) {
    // A callee phi may keep an entry for a predecessor an earlier cleanup
    // deleted; that identity has no clone, so the dead entry is dropped
    // rather than leaking a callee-space block id into the caller.
    std::size_t kept = 0;
    for(std::size_t i = 0; i + 1 < result->args.size(); i += 2) {
      const std::uint32_t id = result->args[i].block;
      if(result->args[i].kind != Operand::OP_LABEL ||
         id >= blocks.size() || !blocks[id].id.valid())
        continue;
      result->args[kept] = result->args[i];
      result->args[kept + 1] = result->args[i + 1];
      rename_operand(&result->args[kept], values, slots, blocks);
      rename_operand(&result->args[kept + 1], values, slots, blocks);
      kept += 2;
    }
    if(kept == 0 && result->args.size() >= 2) {
      // Every predecessor died, so the phi's block is unreachable; keep a
      // valid instruction shape for later cleanup to discard.
      result->kind = Instruction::IK_COPY;
      result->first = result->args[1];
      rename_operand(&result->first, values, slots, blocks);
      result->second = Operand();
      result->third = Operand();
      result->args.clear();
      return;
    }
    result->args.resize(kept);
    return;
  }
  for(std::size_t i = 0; i < result->args.size(); ++i)
    rename_operand(&result->args[i], values, slots, blocks);
}

Instruction clone_instruction(const Instruction & source,
                              const ValueMap & values,
                              const SlotMap & slots,
                              const BlockMap & blocks)
{
  Instruction result = source;
  rename_instruction(&result, values, slots, blocks);
  return result;
}

Instruction transfer_instruction(Instruction * source,
                                 const ValueMap & values,
                                 const SlotMap & slots,
                                 const BlockMap & blocks)
{
  Instruction result = std::move(*source);
  rename_instruction(&result, values, slots, blocks);
  return result;
}

Operand resolve_replacement(Operand value, const ValueMap & replacements)
{
  for(std::size_t step = 0;
      step < replacements.size() && value.kind == Operand::OP_TEMP; ++step) {
    const Operand * found = replacements.find(value.value);
    if(!found) break;
    value = *found;
  }
  return value;
}

void replace_values(Function * function, const ValueMap & replacements)
{
  if(replacements.empty()) return;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction & ins = function->blocks[i].instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        *operands[k] = resolve_replacement(*operands[k], replacements);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        ins.args[k] = resolve_replacement(ins.args[k], replacements);
    }
}

std::vector<std::size_t> normal_successors(const Function & function,
                                            std::size_t block,
                                            const std::vector<std::size_t> & index)
{
  const std::size_t no_block = static_cast<std::size_t>(-1);
  std::vector<std::size_t> result;
  if(function.blocks[block].instructions.empty()) return result;
  const Instruction & term = function.blocks[block].instructions.back();
  const Operand * targets[2] = {0, 0};
  if(term.kind == Instruction::IK_JUMP) targets[0] = &term.first;
  else if(term.kind == Instruction::IK_BRANCH) {
    targets[0] = &term.second; targets[1] = &term.third;
  }
  for(std::size_t i = 0; i < 2; ++i) if(targets[i]) {
    const std::uint32_t id = targets[i]->block;
    if(id < index.size() && index[id] != no_block)
      result.push_back(index[id]);
  }
  if(term.kind == Instruction::IK_SWITCH) {
    const std::uint32_t fallback = term.second.block;
    if(fallback < index.size() && index[fallback] != no_block)
      result.push_back(index[fallback]);
    for(std::size_t i = 1; i < term.args.size(); i += 2) {
      const std::uint32_t id = term.args[i].block;
      if(id < index.size() && index[id] != no_block)
        result.push_back(index[id]);
    }
  }
  return result;
}

bool contains_backward_edge(const Function & function)
{
  const std::size_t no_block = static_cast<std::size_t>(-1);
  std::vector<std::size_t> index(function.next_block_id, no_block);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    index[function.blocks[block].id] = block;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::vector<std::size_t> successors =
      normal_successors(function, block, index);
    for(std::size_t i = 0; i < successors.size(); ++i)
      if(successors[i] <= block) return true;
  }
  return false;
}

void remove_unreachable_blocks(Function * function)
{
  if(function->blocks.empty()) return;
  const std::size_t no_block = static_cast<std::size_t>(-1);
  std::vector<std::size_t> index(function->next_block_id, no_block);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    index[function->blocks[i].id] = i;
  std::vector<unsigned char> reachable(function->blocks.size(), 0);
  std::vector<std::size_t> pending(1, 0);
  reachable[0] = 1;
  for(std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
    std::vector<std::size_t> successors =
      normal_successors(*function, pending[cursor], index);
    const Block & block = function->blocks[pending[cursor]];
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const Instruction & instruction = block.instructions[j];
      if(instruction.kind != Instruction::IK_EH_TRY &&
         instruction.kind != Instruction::IK_EH_CLEANUP) continue;
      const std::uint32_t id = instruction.first.block;
      if(id < index.size() && index[id] != no_block)
        successors.push_back(index[id]);
    }
    for(std::size_t i = 0; i < successors.size(); ++i)
      if(!reachable[successors[i]]) {
        reachable[successors[i]] = 1;
        pending.push_back(successors[i]);
      }
  }
  if(pending.size() == function->blocks.size()) return;
  std::vector<Block> kept;
  kept.reserve(pending.size());
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    if(reachable[i]) kept.push_back(std::move(function->blocks[i]));
  function->blocks.swap(kept);
}

struct EhContext
{
  std::vector<unsigned char> incoming;
  std::vector<unsigned char> landing_blocks;
};

EhContext analyze_eh_context(const Function & function, Stats * stats)
{
  EhContext result;
  const std::size_t no_block = static_cast<std::size_t>(-1);
  std::vector<std::size_t> index(function.next_block_id, no_block);
  result.landing_blocks.assign(function.next_block_id, 0);
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    index[function.blocks[i].id] = i;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function.blocks[i].instructions[j];
      if(ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP)
        result.landing_blocks[ins.first.block] = 1;
    }
  }
  if(function.blocks.empty()) return result;
  std::vector<unsigned char> incoming(function.blocks.size(), 0), queued(
    function.blocks.size(), 0);
  std::deque<std::size_t> work;
  incoming[0] = 1;
  queued[0] = 1;
  work.push_back(0);
  if(stats) ++stats->worklist_pushes;
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    queued[block] = 0;
    bool active = incoming[block] == 2;
    for(std::size_t i = 0; i < function.blocks[block].instructions.size(); ++i) {
      const Instruction::Kind kind = function.blocks[block].instructions[i].kind;
      if(kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP)
        active = true;
      else if(kind == Instruction::IK_EH_END) active = false;
    }
    const unsigned char outgoing = active ? 2 : 1;
    const std::vector<std::size_t> successors =
      normal_successors(function, block, index);
    for(std::size_t i = 0; i < successors.size(); ++i) {
      const std::size_t successor = successors[i];
      if(incoming[successor] >= outgoing) continue;
      incoming[successor] = outgoing;
      if(stats) ++stats->dataflow_updates;
      if(!queued[successor]) {
        queued[successor] = 1;
        work.push_back(successor);
        if(stats) ++stats->worklist_pushes;
      }
    }
  }
  result.incoming = incoming;
  return result;
}

class Inliner
{
public:
  Inliner(LowirProgram * program,
          const InlineCallGraph & call_graph,
          const std::vector<unsigned char> & prepared_oversized_symbols,
          const std::vector<std::size_t> & original_instruction_counts,
          std::vector<unsigned char> * rewritten_symbols,
          Stats * stats, bool optimized_late_wave = false,
          bool definition_removing_only = false,
          const InlineCleanup * cleanup = 0,
          const InlinePolicyOverrides * limit_overrides = 0)
    : program_(*program), rewritten_symbols_(rewritten_symbols),
      stats_(stats),
      call_graph_(call_graph),
      no_unwind_(program->symbol_names.size(), 0),
      prepared_oversized_symbols_(prepared_oversized_symbols),
      original_instruction_counts_(original_instruction_counts),
      remaining_single_call_translation_unit_budget_(
        kMinimumSingleCallTranslationUnitBudget),
      limits_(resolve_inline_policy_limits(limit_overrides)),
      optimized_late_wave_(optimized_late_wave),
      definition_removing_only_(definition_removing_only), cleanup_(cleanup),
      rewrites_(0)
  {
    if(original_instruction_counts_.size() != program_.functions.size())
      throw std::logic_error("inline cost summary count mismatch");
    contains_eh_.resize(program_.functions.size(), 0);
    loop_inline_shapes_.resize(program_.functions.size(), 0);
    leaf_inline_shapes_.resize(program_.functions.size(), 0);
    instruction_counts_.resize(program_.functions.size(), 0);
    single_call_discardable_.resize(program_.functions.size(), 0);
    candidate_callers_.resize(program_.functions.size(), 0);
    std::size_t input_instruction_budget = 0;
    noreturn_symbols_ = noreturn_symbol_index(program_);
    cold_discounted_counts_.resize(program_.functions.size(), 0);
    effective_leaf_shapes_.resize(program_.functions.size(), 0);
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      contains_eh_[i] = contains_eh(program_.functions[i]);
      loop_inline_shapes_[i] = contains_backward_edge(program_.functions[i]);
      leaf_inline_shapes_[i] = leaf_inline_shape(program_.functions[i]);
      instruction_counts_[i] = instruction_count(program_.functions[i]);
      refresh_cold_discounted_shape(i);
      const Function & function = program_.functions[i];
      const std::size_t direct_uses =
        call_graph_.reverse_offsets[i + 1] - call_graph_.reverse_offsets[i];
      single_call_discardable_[i] = direct_uses == 1 &&
        !call_graph_.non_call_use[i] &&
        (function.metadata.binding == lowir_model::SBM_INTERNAL ||
         function.metadata.binding == lowir_model::SBM_WEAK) &&
        !function.metadata.object_output_root &&
        !function.metadata.keep_internal_alias &&
        function.metadata.role == lowir_model::SR_NONE &&
        !function.metadata.tls_for_symbol_id.valid();
      if(definition_removing_only_ && single_call_discardable_[i]) {
        const std::size_t caller =
          call_graph_.reverse_edges[call_graph_.reverse_offsets[i]];
        candidate_callers_[caller] = 1;
      }
      if(input_instruction_budget <=
         std::numeric_limits<std::size_t>::max() -
           original_instruction_counts_[i])
        input_instruction_budget += original_instruction_counts_[i];
      else
        input_instruction_budget = std::numeric_limits<std::size_t>::max();
    }
    remaining_single_call_translation_unit_budget_ = std::max(
      kMinimumSingleCallTranslationUnitBudget, input_instruction_budget);
    if(stats_ && definition_removing_only_)
      stats_->post_prune_inline_translation_unit_budget =
        remaining_single_call_translation_unit_budget_;
    else if(stats_ && !optimized_late_wave_)
      stats_->inline_single_call_translation_unit_budget =
        remaining_single_call_translation_unit_budget_;
    if(stats_ && !optimized_late_wave_ && !definition_removing_only_)
      stats_->inline_input_instructions =
        std::accumulate(instruction_counts_.begin(), instruction_counts_.end(),
          static_cast<std::size_t>(0));
    infer_no_unwind();
    state_.assign(program_.functions.size(), 0);
    remaining_inline_budget_.assign(program_.functions.size(),
      limits_.caller_budget);
    remaining_single_call_budget_.assign(program_.functions.size(),
      limits_.single_call_caller_budget);
  }

  std::size_t run()
  {
    std::deque<std::size_t> stripped;
    for(std::size_t cursor = 0;
        cursor < call_graph_.callee_first_order.size(); ++cursor) {
      const std::size_t function = call_graph_.callee_first_order[cursor];
      if(definition_removing_only_ && !candidate_callers_[function]) continue;
      if(expand(function) && has_eh_blocked_callers(function) &&
         instruction_counts_[function] <= 4 &&
         leaf_inline_shapes_[function]) {
        stripped.push_back(function);
        if(stats_) ++stats_->worklist_pushes;
      }
    }
    while(!stripped.empty()) {
      const std::size_t target = stripped.front();
      stripped.pop_front();
      std::vector<std::size_t> & blocked =
        eh_blocked_callers_.find(target)->second;
      std::sort(blocked.begin(), blocked.end());
      blocked.erase(std::unique(blocked.begin(), blocked.end()), blocked.end());
      for(std::size_t i = 0; i < blocked.size(); ++i) {
        const std::size_t caller = blocked[i];
        if(stats_) ++stats_->inline_revisited_callers;
        const bool caller_inlined = inline_calls(caller);
        const bool caller_stripped =
          strip_dead_eh_regions(&program_.functions[caller]);
        if(caller_stripped) {
          ++rewrites_;
          if(rewritten_symbols_)
            (*rewritten_symbols_)[program_.functions[caller].symbol] = 1;
        }
        if((caller_inlined || caller_stripped) && cleanup_ && cleanup_->run)
          cleanup_->run(&program_.functions[caller], stats_, cleanup_->context);
        contains_eh_[caller] = contains_eh(program_.functions[caller]);
        refresh_cold_discounted_shape(caller);
        leaf_inline_shapes_[caller] =
          leaf_inline_shape(program_.functions[caller]);
        instruction_counts_[caller] =
          instruction_count(program_.functions[caller]);
        if(caller_stripped &&
           has_eh_blocked_callers(caller) &&
           instruction_counts_[caller] <= 4 &&
           leaf_inline_shapes_[caller]) {
          stripped.push_back(caller);
          if(stats_) ++stats_->worklist_pushes;
        }
      }
    }
    if(stats_) {
      if(definition_removing_only_)
        stats_->post_prune_inline_translation_unit_budget_remaining =
          remaining_single_call_translation_unit_budget_;
      else if(!optimized_late_wave_)
        stats_->inline_single_call_translation_unit_budget_remaining =
          remaining_single_call_translation_unit_budget_;
      if(!definition_removing_only_) {
        stats_->inline_output_instructions = 0;
        for(std::size_t i = 0; i < program_.functions.size(); ++i)
          stats_->inline_output_instructions +=
            instruction_count(program_.functions[i]);
      }
    }
    return rewrites_;
  }

private:
  LowirProgram & program_;
  std::vector<unsigned char> * rewritten_symbols_;
  Stats * stats_;
  const InlineCallGraph & call_graph_;
  std::vector<unsigned char> no_unwind_;
  const std::vector<unsigned char> & prepared_oversized_symbols_;
  const std::vector<std::size_t> & original_instruction_counts_;
  std::vector<unsigned char> state_, contains_eh_, loop_inline_shapes_,
    leaf_inline_shapes_;
  std::vector<std::size_t> cold_discounted_counts_;
  std::vector<unsigned char> effective_leaf_shapes_;
  std::vector<unsigned char> noreturn_symbols_;
  std::vector<unsigned char> single_call_discardable_;
  std::vector<unsigned char> candidate_callers_;
  std::vector<std::size_t> instruction_counts_;
  std::vector<std::size_t> remaining_inline_budget_;
  std::vector<std::size_t> remaining_single_call_budget_;
  std::size_t remaining_single_call_translation_unit_budget_;
  InlinePolicyLimits limits_;
  bool optimized_late_wave_;
  bool definition_removing_only_;
  const InlineCleanup * cleanup_;
  std::unordered_map<std::size_t, std::vector<std::size_t> >
    eh_blocked_callers_;
  std::size_t rewrites_;

  std::size_t callee(const Instruction & instruction) const
  {
    lowir_model::SymbolId symbol;
    if(!direct_call(instruction, &symbol)) return kNoFunction;
    return call_graph_.definition_by_symbol[symbol];
  }

  bool has_eh_blocked_callers(std::size_t target) const
  {
    const std::unordered_map<std::size_t,
      std::vector<std::size_t> >::const_iterator found =
        eh_blocked_callers_.find(target);
    return found != eh_blocked_callers_.end() && !found->second.empty();
  }

  void infer_no_unwind()
  {
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i)
      if(program_.function_declarations[i].boundary.unwind == lowir_model::CUM_NO)
        no_unwind_[program_.function_declarations[i].symbol] = 1;
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(program_.functions[i].boundary.unwind == lowir_model::CUM_NO)
        no_unwind_[program_.functions[i].symbol] = 1;
    std::vector<std::size_t> unresolved(program_.functions.size(), 0);
    std::vector<unsigned char> unsafe(program_.functions.size(), 0);
    std::vector<std::vector<std::size_t> > dependents(
      program_.symbol_names.size());
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & function = program_.functions[i];
      if(no_unwind_[function.symbol]) continue;
      unsafe[i] = contains_eh_[i];
      for(std::size_t b = 0; b < function.blocks.size(); ++b)
        for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
          const Instruction & ins = function.blocks[b].instructions[j];
          if(ins.kind == Instruction::IK_THROW || ins.kind == Instruction::IK_RESUME)
            unsafe[i] = 1;
          else if(ins.kind == Instruction::IK_CALL) {
            lowir_model::SymbolId target;
            if(!direct_call(ins, &target)) {
              unsafe[i] = 1;
              continue;
            }
            if(no_unwind_[target]) continue;
            ++unresolved[i];
			dependents[target].push_back(i);
          }
        }
    }
    std::deque<std::size_t> work;
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(!unsafe[i] && unresolved[i] == 0 &&
         !no_unwind_[program_.functions[i].symbol]) {
        no_unwind_[program_.functions[i].symbol] = 1;
        work.push_back(i);
        if(stats_) ++stats_->worklist_pushes;
      }
    while(!work.empty()) {
      const std::size_t resolved = work.front();
      work.pop_front();
      const std::vector<std::size_t> & found =
        dependents[program_.functions[resolved].symbol];
      for(std::size_t i = 0; i < found.size(); ++i) {
        const std::size_t caller = found[i];
        if(unsafe[caller] || unresolved[caller] == 0) continue;
        --unresolved[caller];
        if(stats_) ++stats_->dataflow_updates;
        if(unresolved[caller] == 0 &&
           !no_unwind_[program_.functions[caller].symbol]) {
          no_unwind_[program_.functions[caller].symbol] = 1;
          work.push_back(caller);
          if(stats_) ++stats_->worklist_pushes;
        }
      }
    }
  }

  bool candidate(std::size_t caller, std::size_t target,
                 const Instruction & call,
                 bool landing, bool record_stats = false)
  {
    if(target == kNoFunction) return false;
    if(record_stats && stats_ && definition_removing_only_ &&
       single_call_discardable_[target])
      ++stats_->post_prune_inline_considered_single_calls;
    if(call_graph_.recursive[target] || target == caller) {
      if(record_stats && stats_) ++stats_->inline_reject_recursive;
      if(record_stats && stats_ && definition_removing_only_ &&
         single_call_discardable_[target])
        ++stats_->post_prune_inline_reject_recursive;
      return false;
    }
    const Function & callee_function = program_.functions[target];
    if(callee_function.metadata.no_inline) {
      if(record_stats && stats_) ++stats_->inline_reject_no_inline;
      if(record_stats && stats_ && definition_removing_only_ &&
         single_call_discardable_[target])
        ++stats_->post_prune_inline_reject_no_inline;
      return false;
    }
    // Pulling a shared ordinary loop body into its callers duplicates its
    // control-flow state and, on this backend, commonly turns the loop's
    // induction values into frame traffic. A single-use body still removes a
    // definition, while an explicit source inline preference remains honored.
    if(loop_inline_shapes_[target] && !single_call_discardable_[target] &&
       !callee_function.metadata.inline_hint &&
       !callee_function.metadata.force_inline) {
      if(record_stats && stats_) ++stats_->inline_reject_loop_body;
      return false;
    }
    // Some pre-PA37 virtual-base ABI wrappers intentionally leave hidden
    // boundary operands to native lowering.  They are valid backend calls but
    // are not structurally safe to substitute as ordinary LowIR parameters.
    if(callee_function.params.size() != call.args.size()) {
      if(record_stats && stats_) ++stats_->inline_reject_argument_shape;
      if(record_stats && stats_ && definition_removing_only_ &&
         single_call_discardable_[target])
        ++stats_->post_prune_inline_reject_argument_shape;
      return false;
    }
    if(callee_function.boundary.arity == lowir_model::CAM_VARIADIC) {
      if(record_stats && stats_) ++stats_->inline_reject_variadic;
      if(record_stats && stats_ && definition_removing_only_ &&
         single_call_discardable_[target])
        ++stats_->post_prune_inline_reject_variadic;
      return false;
    }
    const std::size_t inline_cost = std::max(
      instruction_counts_[target], original_instruction_counts_[target]);
    const bool bounded_single_call = !optimized_late_wave_ &&
      single_call_discardable_[target] &&
      inline_cost <= limits_.single_call_limit;
    if(definition_removing_only_ && !bounded_single_call) {
      if(record_stats && stats_ && single_call_discardable_[target])
        ++stats_->post_prune_inline_reject_size;
      return false;
    }
    const std::size_t size_cap = callee_function.metadata.inline_hint ?
      limits_.hint_size_cap : kOrdinarySizeCap;
    if(cold_discounted_counts_[target] > size_cap &&
       (optimized_late_wave_ ||
        !callee_function.metadata.prefer_local_object_binding) &&
       !bounded_single_call) {
      if(record_stats && stats_) ++stats_->inline_reject_callee_size;
      return false;
    }
    const std::size_t late_nonleaf_limit = callee_function.metadata.inline_hint ?
      limits_.hint_late_nonleaf_limit : kLateNonleafInstructionLimit;
    if(optimized_late_wave_ &&
       instruction_counts_[target] > late_nonleaf_limit &&
       !leaf_inline_shapes_[target] &&
       !(effective_leaf_shapes_[target] &&
         cold_discounted_counts_[target] <= size_cap)) {
      if(record_stats && stats_ && callee_function.metadata.inline_hint)
        ++stats_->inline_hint_size_rejects;
      if(record_stats && stats_) ++stats_->inline_reject_prepared_size;
      return false;
    }
    if(!optimized_late_wave_ &&
       prepared_oversized_symbols_[callee_function.symbol] &&
       (instruction_counts_[target] > 4 ||
        !leaf_inline_shapes_[target]) && !bounded_single_call) {
      if(record_stats && stats_) ++stats_->inline_reject_prepared_size;
      return false;
    }
    if(landing &&
       (!no_unwind_[callee_function.symbol] || contains_eh_[target])) {
      if(record_stats && stats_) ++stats_->inline_reject_landing;
      if(record_stats && stats_ && definition_removing_only_)
        ++stats_->post_prune_inline_reject_landing;
      return false;
    }
    if(contains_eh_[target]) {
      if(callee_function.boundary.unwind == lowir_model::CUM_NO &&
         instruction_counts_[target] <= 8) {
        eh_blocked_callers_[target].push_back(caller);
        if(stats_) ++stats_->inline_eh_blocked_records;
      }
      if(record_stats && stats_) ++stats_->inline_reject_callee_eh;
      if(record_stats && stats_ && definition_removing_only_)
        ++stats_->post_prune_inline_reject_callee_eh;
      return false;
    }
    if(record_stats && stats_ && bounded_single_call &&
       !definition_removing_only_)
      ++stats_->inline_single_call_candidates;
    if(record_stats && stats_ && callee_function.metadata.inline_hint)
      ++stats_->inline_hint_candidates;
    if(record_stats && stats_) ++stats_->inline_candidate_calls;
    return true;
  }

  bool consume_inline_budget(std::size_t target, std::size_t * remaining,
                             std::size_t * single_call_remaining,
                             bool * used_single_call)
  {
    *used_single_call = false;
    const std::size_t cost = std::max<std::size_t>(optimized_late_wave_ ?
      instruction_counts_[target] :
      std::max(instruction_counts_[target],
        original_instruction_counts_[target]), 1);
    if(!optimized_late_wave_ && single_call_discardable_[target] &&
       cost <= limits_.single_call_limit) {
      if(cost <= *single_call_remaining &&
         cost <= remaining_single_call_translation_unit_budget_) {
        *single_call_remaining -= cost;
        remaining_single_call_translation_unit_budget_ -= cost;
        *used_single_call = true;
        return true;
      }
      if(stats_) {
        if(definition_removing_only_)
          ++stats_->post_prune_inline_budget_skips;
        else {
          ++stats_->inline_single_call_budget_skips;
          if(cost > *single_call_remaining)
            ++stats_->inline_single_call_caller_budget_skips;
          if(cost > remaining_single_call_translation_unit_budget_)
            ++stats_->inline_single_call_translation_unit_budget_skips;
        }
      }
      if(definition_removing_only_) return false;
      const Function & function = program_.functions[target];
      const bool ordinary_size_candidate =
        (cold_discounted_counts_[target] <=
           (function.metadata.inline_hint ?
            limits_.hint_size_cap : kOrdinarySizeCap) ||
         function.metadata.prefer_local_object_binding) &&
        (!prepared_oversized_symbols_[function.symbol] ||
         (instruction_counts_[target] <= 4 &&
          leaf_inline_shapes_[target]));
      if(!ordinary_size_candidate)
        return false;
    }
    if(cost > *remaining) {
      // The call sequence itself costs as much as a trivial leaf body, so
      // substituting one cannot grow text; budget exhaustion never blocks
      // it.  Definition-removing waves still require their own accounting.
      if(!definition_removing_only_ &&
         cost <= kTrivialLeafInstructionLimit && leaf_inline_shapes_[target])
        return true;
      if(stats_) ++stats_->budget_skips;
      return false;
    }
    *remaining -= cost;
    return true;
  }

  void discard_single_call_body(std::size_t target, bool used_single_call)
  {
    if(!used_single_call) return;
    std::vector<Block>().swap(program_.functions[target].blocks);
    instruction_counts_[target] = 0;
    cold_discounted_counts_[target] = 0;
    effective_leaf_shapes_[target] = 0;
    contains_eh_[target] = 0;
    leaf_inline_shapes_[target] = 0;
    single_call_discardable_[target] = 0;
    if(stats_) {
      if(definition_removing_only_)
        ++stats_->post_prune_inline_discarded_bodies;
      else ++stats_->inline_single_call_discarded_bodies;
    }
  }

  bool instruction_can_unwind(const Instruction & instruction) const
  {
    if(instruction.kind == Instruction::IK_THROW ||
       instruction.kind == Instruction::IK_RESUME) return true;
    if(instruction.kind != Instruction::IK_CALL) return false;
    lowir_model::SymbolId target;
    return !direct_call(instruction, &target) || target >= no_unwind_.size() ||
      !no_unwind_[target];
  }

  bool body_is_no_unwind(const Function & function) const
  {
    if(contains_eh(function)) return false;
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j)
        if(instruction_can_unwind(function.blocks[b].instructions[j]))
          return false;
    return true;
  }

  bool strip_all_no_unwind_eh(Function * function)
  {
    if(function->boundary.unwind != lowir_model::CUM_NO) return false;
    for(std::size_t b = 0; b < function->blocks.size(); ++b)
      for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
        const Instruction & ins = function->blocks[b].instructions[j];
        if(instruction_can_unwind(ins)) return false;
      }
    bool changed = false;
    for(std::size_t b = 0; b < function->blocks.size(); ++b) {
      std::vector<Instruction> kept;
      for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
        const Instruction & ins = function->blocks[b].instructions[j];
        if(ins.kind == Instruction::IK_EH_TRY ||
           ins.kind == Instruction::IK_EH_CLEANUP ||
           ins.kind == Instruction::IK_EH_END) {
          changed = true;
          continue;
        }
        kept.push_back(std::move(function->blocks[b].instructions[j]));
      }
      function->blocks[b].instructions.swap(kept);
    }
    remove_unreachable_blocks(function);
    return changed;
  }

  bool strip_dead_eh_regions(Function * function)
  {
    if(!contains_eh(*function)) return false;
    if(strip_all_no_unwind_eh(function)) return true;
    const std::uint32_t kUnknown = UINT32_MAX;
    const std::uint32_t kAmbiguous = UINT32_MAX - 1;
    struct Region {
      std::uint32_t parent;
      bool parent_known, unsafe, closed;
      Region() : parent(0), parent_known(false), unsafe(false), closed(false) {}
    };
    std::vector<std::size_t> offsets(function->blocks.size() + 1, 0);
    for(std::size_t b = 0; b < function->blocks.size(); ++b)
      offsets[b + 1] = offsets[b] + function->blocks[b].instructions.size();
    std::vector<std::uint32_t> marker_region(offsets.back(), kUnknown);
    std::vector<Region> regions;
    for(std::size_t b = 0; b < function->blocks.size(); ++b)
      for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
        const Instruction::Kind kind = function->blocks[b].instructions[j].kind;
        if(kind != Instruction::IK_EH_TRY &&
           kind != Instruction::IK_EH_CLEANUP) continue;
        marker_region[offsets[b] + j] =
          static_cast<std::uint32_t>(regions.size());
        regions.push_back(Region());
      }
    if(regions.empty()) return false;
    if(stats_) stats_->inline_eh_regions_analyzed += regions.size();

    const std::size_t no_block = static_cast<std::size_t>(-1);
    std::vector<std::size_t> index(function->next_block_id, no_block);
    for(std::size_t b = 0; b < function->blocks.size(); ++b)
      index[function->blocks[b].id] = b;
    std::vector<std::uint32_t> incoming(function->blocks.size(), kUnknown);
    std::vector<unsigned char> queued(function->blocks.size(), 0);
    std::deque<std::size_t> work;
    incoming[0] = 0;
    queued[0] = 1;
    work.push_back(0);
    bool ambiguous = false;
    while(!work.empty() && !ambiguous) {
      const std::size_t block = work.front();
      work.pop_front();
      queued[block] = 0;
      std::uint32_t top = incoming[block];
      for(std::size_t j = 0;
          j < function->blocks[block].instructions.size(); ++j) {
        const Instruction & instruction =
          function->blocks[block].instructions[j];
        if(instruction.kind == Instruction::IK_EH_TRY ||
           instruction.kind == Instruction::IK_EH_CLEANUP) {
          const std::uint32_t region = marker_region[offsets[block] + j];
          if(!regions[region].parent_known) {
            regions[region].parent = top;
            regions[region].parent_known = true;
          } else if(regions[region].parent != top) {
            ambiguous = true;
            break;
          }
          top = region + 1;
        } else if(instruction.kind == Instruction::IK_EH_END) {
          if(top == 0) {
            ambiguous = true;
            break;
          }
          const std::uint32_t region = top - 1;
          marker_region[offsets[block] + j] = region;
          regions[region].closed = true;
          top = regions[region].parent;
        } else if(top != 0 && instruction_can_unwind(instruction))
          regions[top - 1].unsafe = true;
      }
      if(ambiguous) break;
      const std::vector<std::size_t> successors =
        normal_successors(*function, block, index);
      for(std::size_t i = 0; i < successors.size(); ++i) {
        const std::size_t successor = successors[i];
        if(incoming[successor] == kUnknown) incoming[successor] = top;
        else if(incoming[successor] != top) {
          incoming[successor] = kAmbiguous;
          ambiguous = true;
          break;
        } else continue;
        if(!queued[successor]) {
          queued[successor] = 1;
          work.push_back(successor);
          if(stats_) ++stats_->worklist_pushes;
        }
      }
    }
    if(ambiguous) {
      if(stats_) ++stats_->inline_eh_ambiguous_functions;
      return false;
    }
    for(std::size_t i = regions.size(); i != 0; --i) {
      Region & region = regions[i - 1];
      if(!region.unsafe || region.parent == 0) continue;
      regions[region.parent - 1].unsafe = true;
    }
    std::size_t removed_regions = 0;
    for(std::size_t i = 0; i < regions.size(); ++i)
      if(regions[i].closed && !regions[i].unsafe) ++removed_regions;
    if(removed_regions == 0) return false;
    for(std::size_t b = 0; b < function->blocks.size(); ++b) {
      std::vector<Instruction> kept;
      kept.reserve(function->blocks[b].instructions.size());
      for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
        const Instruction & instruction = function->blocks[b].instructions[j];
        const std::uint32_t region = marker_region[offsets[b] + j];
        const bool marker = instruction.kind == Instruction::IK_EH_TRY ||
          instruction.kind == Instruction::IK_EH_CLEANUP ||
          instruction.kind == Instruction::IK_EH_END;
        if(marker && region != kUnknown && regions[region].closed &&
           !regions[region].unsafe) continue;
        kept.push_back(std::move(function->blocks[b].instructions[j]));
      }
      function->blocks[b].instructions.swap(kept);
    }
    remove_unreachable_blocks(function);
    if(stats_) stats_->inline_eh_regions_removed += removed_regions;
    return true;
  }

  bool expand(std::size_t function_index)
  {
    if(state_[function_index] == 2 || state_[function_index] == 1) return false;
    state_[function_index] = 1;
    const bool inlined = inline_calls(function_index);
    const bool stripped =
      strip_dead_eh_regions(&program_.functions[function_index]);
    if(stripped) {
      ++rewrites_;
      if(rewritten_symbols_)
        (*rewritten_symbols_)[program_.functions[function_index].symbol] = 1;
    }
    if((inlined || stripped) && cleanup_ && cleanup_->run)
      cleanup_->run(
        &program_.functions[function_index], stats_, cleanup_->context);
    contains_eh_[function_index] = contains_eh(program_.functions[function_index]);
    refresh_cold_discounted_shape(function_index);
    leaf_inline_shapes_[function_index] =
      leaf_inline_shape(program_.functions[function_index]);
    instruction_counts_[function_index] =
      instruction_count(program_.functions[function_index]);
    const lowir_model::SymbolId symbol =
      program_.functions[function_index].symbol;
    if(stripped && !no_unwind_[symbol] &&
       body_is_no_unwind(program_.functions[function_index])) {
      no_unwind_[symbol] = 1;
      if(stats_) ++stats_->inline_no_unwind_published_after_strip;
    }
    state_[function_index] = 2;
    return stripped;
  }

  void build_maps(Function * caller, const Function & callee_function,
                  const Instruction & call,
                  const std::string & prefix, Names * names, ValueMap * values,
                  SlotMap * slots, BlockMap * blocks)
  {
    if(callee_function.params.size() != call.args.size())
      throw std::runtime_error("inline call argument count mismatch");
    names->inherit_sites(callee_function);
    for(std::size_t i = 0; i < callee_function.params.size(); ++i)
      values->set(callee_function.params[i].value, call.args[i]);
    slots->resize(callee_function.slot_names.size());
    for(std::size_t i = 0; i < callee_function.slots.size(); ++i) {
      const lowir_model::SlotId source = callee_function.slots[i];
      lowir_model::StringId renamed_id;
      if(names->retain) {
        const std::string renamed = prefix +
          lowir_model::lowir_slot_name(
            program_.strings, callee_function, source);
        renamed_id = program_.strings.intern(renamed);
      }
      (*slots)[source] = lowir_model::append_lowir_slot(
        *caller, renamed_id,
        lowir_model::lowir_slot_type(callee_function, source));
    }
    blocks->resize(callee_function.next_block_id);
    for(std::size_t i = 0; i < callee_function.blocks.size(); ++i) {
      RenamedBlock block;
      lowir_model::StringId renamed_id;
      if(names->retain) {
        const std::string renamed = prefix +
          lowir_model::lowir_block_label(
            program_.strings, callee_function,
            callee_function.blocks[i].id);
        renamed_id = program_.strings.intern(renamed);
      }
      block.id = lowir_model::allocate_lowir_block_id(*caller, renamed_id);
      (*blocks)[callee_function.blocks[i].id] = block;
      for(std::size_t j = 0;
          j < callee_function.blocks[i].instructions.size(); ++j) {
        const Instruction & ins = callee_function.blocks[i].instructions[j];
        if(!ins.dest.valid()) continue;
        const LowType & type = lowir_model::lowir_value_type(
          callee_function, ins.dest);
        lowir_model::ValueId renamed;
        if(names->retain) {
          const std::string value = prefix +
            lowir_model::lowir_value_name(
              program_.strings, callee_function, ins.dest);
          renamed = lowir_model::append_lowir_value(
            *caller, program_.strings.intern(value), type);
        } else renamed = lowir_model::append_lowir_unnamed_value(*caller, type);
        values->set(ins.dest, value_operand(renamed, type));
      }
    }
  }

  // A block that raises or resumes an exception terminates the hot path:
  // its instructions do not count toward the callee's size, and calls
  // confined to such blocks do not make the callee callful for the late
  // nonleaf limit.  The checked-accessor pattern (bounds test plus a
  // throwing arm) is the population this admits.
  void cold_discounted_shape(const Function & function,
                             std::size_t * count,
                             bool * effective_leaf) const
  {
    *count = 0;
    *effective_leaf = true;
    for(std::size_t b = 0; b < function.blocks.size(); ++b) {
      const std::vector<Instruction> & instructions =
        function.blocks[b].instructions;
      bool cold = false;
      for(std::size_t i = 0; i < instructions.size() && !cold; ++i)
        cold = instructions[i].kind == Instruction::IK_THROW ||
          instructions[i].kind == Instruction::IK_RESUME ||
          instructions[i].kind == Instruction::IK_UNREACHABLE ||
          (instructions[i].kind == Instruction::IK_CALL &&
           (instructions[i].call_boundary.returns ==
              lowir_model::CRM_NORETURN ||
            (instructions[i].first.kind == Operand::OP_GLOBAL &&
             instructions[i].first.symbol < noreturn_symbols_.size() &&
             noreturn_symbols_[instructions[i].first.symbol])));
      if(cold) continue;
      *count += instructions.size();
      for(std::size_t i = 0; i < instructions.size(); ++i)
        if(instructions[i].kind == Instruction::IK_CALL)
          *effective_leaf = false;
    }
  }
  void refresh_cold_discounted_shape(std::size_t function_index)
  {
    std::size_t count = 0;
    bool effective_leaf = false;
    cold_discounted_shape(program_.functions[function_index], &count,
                          &effective_leaf);
    cold_discounted_counts_[function_index] = count;
    effective_leaf_shapes_[function_index] = effective_leaf ? 1 : 0;
  }
  bool leaf_inline_shape(const Function & function) const
  {
    if(function.blocks.size() != 1) return false;
    std::size_t returns = 0;
    for(std::size_t i = 0; i < function.blocks[0].instructions.size(); ++i) {
      if(function.blocks[0].instructions[i].kind == Instruction::IK_CALL)
        return false;
      if(function.blocks[0].instructions[i].kind == Instruction::IK_RETURN)
        ++returns;
    }
    return returns == 1;
  }

  void inline_leaf_call(std::size_t caller_index, const Instruction & call,
                        Function & callee_function, bool transfer_body,
                        Names * names,
                        ValueMap * replacements,
                        std::vector<Instruction> * output)
  {
    Block & source = callee_function.blocks[0];
    const std::uint32_t site = names->next_site();
    const std::string prefix = names->retain ?
      "__o1inl" + std::to_string(site) + "__" : std::string();
    ValueMap values;
    Function & caller = program_.functions[caller_index];
    SlotMap slots;
    BlockMap blocks;
    build_maps(&caller, callee_function, call, prefix, names, &values, &slots,
               &blocks);
    for(std::size_t i = 0; i < source.instructions.size(); ++i) {
      Instruction & instruction = source.instructions[i];
      if(instruction.kind != Instruction::IK_RETURN) {
        output->push_back(transfer_body ?
          transfer_instruction(&instruction, values, slots, blocks) :
          clone_instruction(instruction, values, slots, blocks));
        continue;
      }
      if(call.call_returns_void) continue;
      Operand returned = instruction.first;
      rename_operand(&returned, values, slots, blocks);
      replacements->set(call.dest, returned);
    }
  }

  void inline_call(std::size_t caller_index, std::size_t block_index,
                   std::size_t instruction_index, Function & callee_function,
                   bool transfer_body, Names * names, ValueMap * replacements,
                   std::vector<unsigned char> * block_eh,
                   bool inside_eh)
  {
    Function & caller = program_.functions[caller_index];
    const Instruction call = caller.blocks[block_index].instructions[instruction_index];
    const std::uint32_t site = names->next_site();
    const std::string prefix = names->retain ?
      "__o1inl" + std::to_string(site) + "__" : std::string();
    ValueMap values;
    SlotMap slots;
    BlockMap blocks;
    build_maps(&caller, callee_function, call, prefix, names, &values, &slots,
               &blocks);
    if(block_eh->size() < caller.next_block_id)
      block_eh->resize(caller.next_block_id, 0);
    for(std::size_t i = 0; i < blocks.size(); ++i)
      if(blocks[i].id.valid())
        (*block_eh)[blocks[i].id] = inside_eh ? 1 : 0;
    std::size_t returns = 0;
    for(std::size_t b = 0; b < callee_function.blocks.size(); ++b)
      for(std::size_t j = 0; j < callee_function.blocks[b].instructions.size(); ++j)
        if(callee_function.blocks[b].instructions[j].kind == Instruction::IK_RETURN)
          ++returns;
    const bool object_result =
      callee_function.return_type.kind == lowir_model::LTK_OBJECT;
    const bool has_result = !call.call_returns_void;
    lowir_model::SlotId merge_slot;
    if(has_result && returns > 1) {
      const lowir_model::StringId name = names->retain ?
        names->unique_slot(
          prefix + (object_result ? "retmergeobj__" : "retmerge__"),
          callee_function.generated_name_reservations,
          object_result ? lowir_model::GNR_O1_OBJECT_MERGE_SUFFIX :
            lowir_model::GNR_O1_SCALAR_MERGE_SUFFIX) :
        lowir_model::StringId();
      merge_slot = lowir_model::append_lowir_slot(
        caller, name, call.type);
    }

    std::vector<Instruction> tail(
      std::make_move_iterator(
        caller.blocks[block_index].instructions.begin() + instruction_index + 1),
      std::make_move_iterator(caller.blocks[block_index].instructions.end()));
    caller.blocks[block_index].instructions.resize(instruction_index);

    Operand single_object_return;
    bool have_single_object_return = false;
    Operand single_scalar_return;
    bool have_single_scalar_return = false;
    if(callee_function.blocks.size() == 1 && returns == 1) {
      Block & source = callee_function.blocks[0];
      bool void_call_wrapper = call.call_returns_void;
      bool wrapper_has_call = false;
      for(std::size_t j = 0; j < source.instructions.size(); ++j)
        wrapper_has_call = wrapper_has_call ||
          source.instructions[j].kind == Instruction::IK_CALL;
      void_call_wrapper = void_call_wrapper && wrapper_has_call;
      const BlockId wrapper_continuation_id = void_call_wrapper ?
        lowir_model::allocate_lowir_block_id(
          caller, names->retain ?
            program_.strings.intern(prefix + "cont") :
            lowir_model::StringId()) :
        BlockId();
      for(std::size_t j = 0; j < source.instructions.size(); ++j) {
        Instruction & ins = source.instructions[j];
        if(ins.kind != Instruction::IK_RETURN) {
          caller.blocks[block_index].instructions.push_back(
            transfer_body ?
              transfer_instruction(&ins, values, slots, blocks) :
              clone_instruction(ins, values, slots, blocks));
        } else if(void_call_wrapper) {
          caller.blocks[block_index].instructions.push_back(
            jump_to(wrapper_continuation_id));
        } else if(has_result && object_result) {
          single_object_return = ins.first;
          rename_operand(&single_object_return, values, slots, blocks);
          have_single_object_return = true;
        } else if(has_result) {
          single_scalar_return = ins.first;
          rename_operand(&single_scalar_return, values, slots, blocks);
          have_single_scalar_return = true;
        }
      }
      if(void_call_wrapper) {
        if(!tail.empty())
          lowir_phi_edges::rewrite_moved_phi_edges(
            &caller, tail.back(), caller.blocks[block_index].id,
            wrapper_continuation_id);
        Block continuation_block;
        continuation_block.id = wrapper_continuation_id;
        continuation_block.instructions = std::move(tail);
        caller.blocks.insert(caller.blocks.begin() + block_index + 1,
          std::move(continuation_block));
        block_eh->resize(caller.next_block_id, 0);
        (*block_eh)[wrapper_continuation_id] = inside_eh ? 1 : 0;
      } else caller.blocks[block_index].instructions.insert(
          caller.blocks[block_index].instructions.end(),
          std::make_move_iterator(tail.begin()),
          std::make_move_iterator(tail.end()));
      if(have_single_object_return)
        replacements->set(call.dest, single_object_return);
      if(have_single_scalar_return)
        replacements->set(call.dest, single_scalar_return);
      return;
    }

    const BlockId continuation_id =
      lowir_model::allocate_lowir_block_id(
        caller, names->retain ? program_.strings.intern(prefix + "cont") :
          lowir_model::StringId());
    if(!tail.empty())
      lowir_phi_edges::rewrite_moved_phi_edges(
        &caller, tail.back(), caller.blocks[block_index].id, continuation_id);
    block_eh->resize(caller.next_block_id, 0);
    (*block_eh)[continuation_id] = inside_eh ? 1 : 0;
    caller.blocks[block_index].instructions.push_back(jump_to(
      blocks[callee_function.blocks[0].id].id));
    std::vector<Block> inserted;
    for(std::size_t b = 0; b < callee_function.blocks.size(); ++b) {
      Block block;
      block.id = blocks[callee_function.blocks[b].id].id;
      for(std::size_t j = 0;
          j < callee_function.blocks[b].instructions.size(); ++j) {
        Instruction & ins = callee_function.blocks[b].instructions[j];
        if(ins.kind != Instruction::IK_RETURN) {
          block.instructions.push_back(transfer_body ?
            transfer_instruction(&ins, values, slots, blocks) :
            clone_instruction(ins, values, slots, blocks));
          continue;
        }
        Operand returned = ins.first;
        rename_operand(&returned, values, slots, blocks);
        if(has_result && returns > 1) {
          Instruction merge;
          if(object_result) {
            merge.kind = Instruction::IK_COPYOBJ;
            merge.byte_count = call.type.storage_size;
            merge.byte_alignment = call.type.alignment;
            merge.first = returned;
            merge.second = slot_operand(merge_slot, call.type);
          } else {
            merge.kind = Instruction::IK_STORE;
            merge.type = call.type;
            merge.first = returned;
            merge.second = slot_operand(merge_slot, call.type);
          }
          block.instructions.push_back(merge);
        } else if(has_result && object_result) {
          single_object_return = returned;
          have_single_object_return = true;
        } else if(has_result) {
          single_scalar_return = returned;
          have_single_scalar_return = true;
        }
        block.instructions.push_back(jump_to(continuation_id));
      }
      inserted.push_back(std::move(block));
    }
    Block continuation_block;
    continuation_block.id = continuation_id;
    if(has_result && returns > 1 && !object_result) {
      Instruction load;
      load.kind = Instruction::IK_LOAD;
      load.dest = call.dest;
      load.type = call.type;
      load.first = slot_operand(merge_slot, call.type);
      continuation_block.instructions.push_back(load);
    }
    continuation_block.instructions.insert(continuation_block.instructions.end(),
      std::make_move_iterator(tail.begin()),
      std::make_move_iterator(tail.end()));
    inserted.push_back(std::move(continuation_block));
    caller.blocks.insert(caller.blocks.begin() + block_index + 1,
      std::make_move_iterator(inserted.begin()),
      std::make_move_iterator(inserted.end()));
    if(has_result && object_result) {
      const Operand replacement = returns > 1 ?
        slot_operand(merge_slot, call.type) : single_object_return;
      replacements->set(call.dest, replacement);
    }
    if(has_result && !object_result && returns == 1 && have_single_scalar_return)
      replacements->set(call.dest, single_scalar_return);
  }

  bool batch_inline_leaf_calls(std::size_t function_index,
                               std::size_t block_index,
                               const EhContext & eh,
                               Names * names, ValueMap * replacements,
                               std::size_t * inline_budget)
  {
    Function & function = program_.functions[function_index];
    std::vector<Instruction> source;
    source.swap(function.blocks[block_index].instructions);
    const std::uint32_t id = function.blocks[block_index].id;
    const bool landing = id < eh.landing_blocks.size() &&
      eh.landing_blocks[id] != 0;
    bool batch_safe = true;
    for(std::size_t i = 0; batch_safe && i < source.size(); ++i) {
      const std::size_t target = callee(source[i]);
      if(target != kNoFunction &&
         candidate(function_index, target, source[i], landing) &&
         !leaf_inline_shapes_[target])
        batch_safe = false;
    }
    if(!batch_safe) {
      function.blocks[block_index].instructions.swap(source);
      return false;
    }

    std::vector<Instruction> rebuilt;
    rebuilt.reserve(source.size());
    bool changed = false;
    for(std::size_t i = 0; i < source.size(); ++i) {
      const Instruction & ins = source[i];
      const std::size_t target = callee(ins);
      bool eligible = false;
      if(target != kNoFunction) {
        if(stats_) ++stats_->inline_call_visits;
        if(stats_ && optimized_late_wave_)
          ++stats_->late_inline_call_visits;
        eligible = candidate(
          function_index, target, ins, landing, true);
      }
      if(eligible &&
         leaf_inline_shapes_[target]) {
        bool used_single_call = false;
        if(!consume_inline_budget(target, inline_budget,
             &remaining_single_call_budget_[function_index],
             &used_single_call)) {
          rebuilt.push_back(std::move(source[i]));
          continue;
        }
        const std::size_t cloned_instructions = instruction_counts_[target];
        const bool inline_hint =
          program_.functions[target].metadata.inline_hint;
        inline_leaf_call(function_index, ins, program_.functions[target],
          used_single_call, names, replacements, &rebuilt);
        discard_single_call_body(target, used_single_call);
        ++rewrites_;
        changed = true;
        if(stats_) {
          ++stats_->inline_calls;
          if(inline_hint) ++stats_->inline_hint_calls;
          stats_->inline_cloned_instructions += cloned_instructions;
          if(used_single_call) {
            if(definition_removing_only_) {
              ++stats_->post_prune_inline_calls;
              stats_->post_prune_inline_instructions += cloned_instructions;
            } else {
              ++stats_->inline_single_call_calls;
              stats_->inline_single_call_instructions += cloned_instructions;
            }
          }
          if(optimized_late_wave_) {
            ++stats_->late_inline_calls;
            stats_->late_inline_cloned_instructions +=
              cloned_instructions;
          }
        }
        continue;
      }
      rebuilt.push_back(std::move(source[i]));
    }
    function.blocks[block_index].instructions.swap(rebuilt);
    return changed;
  }

  bool inline_calls(std::size_t function_index)
  {
    bool has_candidate = false;
    const Function & original = program_.functions[function_index];
    for(std::size_t b = 0; !has_candidate && b < original.blocks.size(); ++b)
      for(std::size_t j = 0; j < original.blocks[b].instructions.size(); ++j) {
        const std::size_t target = callee(original.blocks[b].instructions[j]);
        if(candidate(function_index, target, original.blocks[b].instructions[j],
             false)) {
          has_candidate = true;
          break;
        }
      }
    if(!has_candidate) return false;

    Names names(program_, program_.functions[function_index]);
    EhContext eh;
    if(contains_eh_[function_index])
      eh = analyze_eh_context(program_.functions[function_index], stats_);
    else {
      eh.incoming.assign(
        program_.functions[function_index].blocks.size(), 1);
      eh.landing_blocks.assign(
        program_.functions[function_index].next_block_id, 0);
    }
    std::vector<unsigned char> block_eh(
      program_.functions[function_index].next_block_id, 0);
    for(std::size_t b = 0; b < program_.functions[function_index].blocks.size(); ++b)
      block_eh[program_.functions[function_index].blocks[b].id] =
        b < eh.incoming.size() && eh.incoming[b] == 2 ? 1 : 0;
    ValueMap replacements;
    bool changed = false;
    std::size_t & inline_budget = remaining_inline_budget_[function_index];
    for(std::size_t b = 0;
        b < program_.functions[function_index].blocks.size(); ++b) {
      changed |= batch_inline_leaf_calls(function_index, b, eh,
        &names, &replacements, &inline_budget);
      bool active = block_eh[
        program_.functions[function_index].blocks[b].id] != 0;
      std::size_t j = 0;
      while(j < program_.functions[function_index].blocks[b].instructions.size()) {
        const Instruction & ins =
          program_.functions[function_index].blocks[b].instructions[j];
        const std::size_t target = callee(ins);
        if(target != kNoFunction) {
          if(stats_) ++stats_->inline_call_visits;
          if(stats_ && optimized_late_wave_)
            ++stats_->late_inline_call_visits;
          if(candidate(function_index, target, ins,
               static_cast<std::uint32_t>(
                 program_.functions[function_index].blocks[b].id) <
                 eh.landing_blocks.size() &&
               eh.landing_blocks[
                 program_.functions[function_index].blocks[b].id] != 0,
               true)) {
            bool used_single_call = false;
            if(!consume_inline_budget(target, &inline_budget,
                 &remaining_single_call_budget_[function_index],
                 &used_single_call)) {
              ++j;
              continue;
            }
            const std::size_t cloned_instructions = instruction_counts_[target];
            const bool inline_hint =
              program_.functions[target].metadata.inline_hint;
            inline_call(function_index, b, j, program_.functions[target],
              used_single_call, &names, &replacements, &block_eh, active);
            discard_single_call_body(target, used_single_call);
            ++rewrites_;
            changed = true;
            if(stats_) {
              ++stats_->inline_calls;
              if(inline_hint) ++stats_->inline_hint_calls;
              stats_->inline_cloned_instructions += cloned_instructions;
              if(used_single_call) {
                if(definition_removing_only_) {
                  ++stats_->post_prune_inline_calls;
                  stats_->post_prune_inline_instructions +=
                    cloned_instructions;
                } else {
                  ++stats_->inline_single_call_calls;
                  stats_->inline_single_call_instructions +=
                    cloned_instructions;
                }
              }
              if(optimized_late_wave_) {
                ++stats_->late_inline_calls;
                stats_->late_inline_cloned_instructions +=
                  cloned_instructions;
              }
            }
            continue;
          }
        }
        const Instruction::Kind kind = ins.kind;
        if(kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP)
          active = true;
        else if(kind == Instruction::IK_EH_END) active = false;
        ++j;
      }
    }
    replace_values(&program_.functions[function_index], replacements);
    if(changed && rewritten_symbols_)
      (*rewritten_symbols_)[program_.functions[function_index].symbol] = 1;
    return changed;
  }
};


}  // namespace

std::size_t inline_o1_calls(
  LowirProgram & program,
  const InlineCallGraph & call_graph,
  const std::vector<unsigned char> & prepared_oversized_symbols,
  const std::vector<std::size_t> & original_instruction_counts,
  std::vector<unsigned char> * rewritten_symbols,
  Stats * stats,
  const InlinePolicyOverrides * limit_overrides)
{
  Inliner inliner(&program, call_graph, prepared_oversized_symbols,
    original_instruction_counts, rewritten_symbols, stats, false, false, 0,
    limit_overrides);
  return inliner.run();
}

std::size_t inline_optimized_calls(
  LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols,
  Stats * stats,
  const InlineCleanup * cleanup,
  const InlinePolicyOverrides * limit_overrides)
{
  std::vector<unsigned char> no_prepared_oversized(
    program.symbol_names.size(), 0);
  std::vector<std::size_t> optimized_instruction_counts(
    program.functions.size(), 0);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    optimized_instruction_counts[i] = instruction_count(program.functions[i]);
  Inliner inliner(&program, call_graph, no_prepared_oversized,
    optimized_instruction_counts, rewritten_symbols, stats, true, false,
    cleanup, limit_overrides);
  return inliner.run();
}

std::size_t inline_post_prune_single_calls(
  LowirProgram & program,
  const InlineCallGraph & call_graph,
  std::vector<unsigned char> * rewritten_symbols,
  Stats * stats,
  const InlinePolicyOverrides * limit_overrides)
{
  std::vector<unsigned char> no_prepared_oversized(
    program.symbol_names.size(), 0);
  std::vector<std::size_t> optimized_instruction_counts(
    program.functions.size(), 0);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    optimized_instruction_counts[i] = instruction_count(program.functions[i]);
  Inliner inliner(&program, call_graph, no_prepared_oversized,
    optimized_instruction_counts, rewritten_symbols, stats, false, true, 0,
    limit_overrides);
  return inliner.run();
}

}  // namespace lowir_opt
