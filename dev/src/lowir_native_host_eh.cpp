#include "lowir_native_host_eh.h"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lowir_native {
namespace host_eh_detail {

bool requires_host_eh_storage(const lowir_model::LowirFunction & function)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      if(function.blocks[i].instructions[j].kind ==
           lowir_model::Instruction::IK_EH_TRY ||
         function.blocks[i].instructions[j].kind ==
           lowir_model::Instruction::IK_EH_CLEANUP)
        return true;
  return false;
}

void collect_host_eh_clauses(mir_model::MirFunction * function)
{
  if(!function->host_eh_enabled) return;
  std::unordered_set<std::string> landing_pads;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function->blocks[i].instructions[j];
      if(instruction.opcode == mir_model::MirInstruction::MI_EH_PUSH &&
         !instruction.operands.empty())
        landing_pads.insert(instruction.operands[0].text);
    }
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<mir_model::MirHostEhClause> clauses;
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function->blocks[i].instructions[j];
      mir_model::MirHostEhClause clause;
      if(instruction.opcode ==
           mir_model::MirInstruction::MI_EH_CLEANUP_CLAUSE) {
        clause.kind = mir_model::MirHostEhClause::HC_CLEANUP;
      } else if(instruction.opcode ==
                  mir_model::MirInstruction::MI_EH_CATCH) {
        clause.kind = mir_model::MirHostEhClause::HC_CATCH;
        clause.selector = instruction.operands[0].imm;
        clause.catch_all = instruction.operands.size() == 1;
        if(!clause.catch_all)
          clause.type_symbol = instruction.operands[1].text;
      } else continue;
      clauses.push_back(clause);
    }
    if(!landing_pads.count(function->blocks[i].label)) continue;
    if(clauses.empty()) {
      mir_model::MirHostEhClause cleanup;
      cleanup.kind = mir_model::MirHostEhClause::HC_CLEANUP;
      clauses.push_back(cleanup);
    }
    function->host_eh_clauses[function->blocks[i].label] = clauses;
  }
}

namespace {

struct RegionState
{
  std::size_t parent = 0;
  std::size_t active = 0;
  std::size_t landing_block = 0;
  bool consumed = false;
};

struct RegionStateKey
{
  std::size_t parent = 0;
  std::size_t landing_block = 0;
  bool consumed = false;

  bool operator==(const RegionStateKey & other) const
  {
    return parent == other.parent && landing_block == other.landing_block &&
      consumed == other.consumed;
  }
};

struct RegionStateKeyHash
{
  std::size_t operator()(const RegionStateKey & key) const
  {
    const std::size_t left = std::hash<std::size_t>()(key.parent);
    const std::size_t right = std::hash<std::size_t>()(key.landing_block);
    const std::size_t combined = left ^
      (right + static_cast<std::size_t>(0x9e3779b9) +
       (left << 6) + (left >> 2));
    return combined ^ (key.consumed ? static_cast<std::size_t>(0x85ebca6b) : 0);
  }
};

class RegionStateInterner
{
public:
  RegionStateInterner() : states_(1) {}

  std::size_t Push(std::size_t parent, std::size_t landing_block,
                   bool consumed = false)
  {
    RegionStateKey key;
    key.parent = parent;
    key.landing_block = landing_block;
    key.consumed = consumed;
    const std::unordered_map<RegionStateKey, std::size_t,
      RegionStateKeyHash>::const_iterator found = index_.find(key);
    if(found != index_.end()) return found->second;
    RegionState state;
    state.parent = parent;
    state.landing_block = landing_block;
    state.consumed = consumed;
    const std::size_t id = states_.size();
    state.active = consumed ? states_[parent].active : id;
    states_.push_back(state);
    index_.insert(std::make_pair(key, id));
    return id;
  }

  std::size_t Consume(std::size_t state)
  {
    if(state == 0 || state >= states_.size() || states_[state].consumed)
      throw std::logic_error("invalid host EH cleanup landing state");
    return Push(states_[state].parent, states_[state].landing_block, true);
  }

  std::size_t Parent(std::size_t state) const
  {
    if(state == 0 || state >= states_.size())
      throw std::logic_error("host EH protected-region stack underflow");
    std::size_t parent = states_[state].parent;
    // Consumed landing frames preserve unwind ownership without introducing
    // an extra source-level region for a later EH_END to close.
    while(parent != 0 && states_[parent].consumed)
      parent = states_[parent].parent;
    return parent;
  }

  std::size_t Active(std::size_t state) const
  {
    if(state >= states_.size())
      throw std::logic_error("invalid host EH protected-region state");
    return states_[state].active;
  }

  std::size_t ActiveLandingBlock(std::size_t state) const
  {
    const std::size_t active = Active(state);
    if(active == 0)
      throw std::logic_error("host EH protected call has no landing pad");
    return states_[active].landing_block;
  }

  std::size_t size() const { return states_.size(); }

private:
  std::vector<RegionState> states_;
  std::unordered_map<RegionStateKey, std::size_t, RegionStateKeyHash> index_;
};

bool is_branch(mir_model::MirInstruction::Opcode opcode)
{
  return opcode == mir_model::MirInstruction::MI_JMP ||
    opcode == mir_model::MirInstruction::MI_JCC ||
    opcode == mir_model::MirInstruction::MI_JNE;
}

bool is_unconditional_exit(const mir_model::MirInstruction & instruction)
{
  return instruction.opcode == mir_model::MirInstruction::MI_JMP ||
    instruction.opcode == mir_model::MirInstruction::MI_JMP_INDIRECT ||
    instruction.opcode == mir_model::MirInstruction::MI_RET ||
    instruction.opcode == mir_model::MirInstruction::MI_FRET ||
    instruction.opcode == mir_model::MirInstruction::MI_RESUME ||
    instruction.opcode == mir_model::MirInstruction::MI_THROW ||
    instruction.opcode == mir_model::MirInstruction::MI_EXIT;
}

bool is_function_exit(const mir_model::MirInstruction & instruction)
{
  return instruction.opcode == mir_model::MirInstruction::MI_RET ||
    instruction.opcode == mir_model::MirInstruction::MI_FRET;
}

bool landing_pad_has_catches(const mir_model::MirBlock & block)
{
  for(std::size_t i = 0; i < block.instructions.size(); ++i)
    if(block.instructions[i].opcode ==
         mir_model::MirInstruction::MI_EH_CATCH)
      return true;
  return false;
}

bool landing_pad_has_cleanup_clause(const mir_model::MirBlock & block)
{
  for(std::size_t i = 0; i < block.instructions.size(); ++i)
    if(block.instructions[i].opcode ==
         mir_model::MirInstruction::MI_EH_CLEANUP_CLAUSE)
      return true;
  return false;
}

bool is_catch_dispatch_block(const mir_model::MirBlock & block)
{
  bool has_catch = false;
  for(std::size_t i = 0; i < block.instructions.size(); ++i) {
    const mir_model::MirInstruction::Opcode opcode =
      block.instructions[i].opcode;
    if(opcode == mir_model::MirInstruction::MI_EH_CATCH) {
      has_catch = true;
      continue;
    }
    if(opcode == mir_model::MirInstruction::MI_EH_CLEANUP_CLAUSE ||
       is_branch(opcode)) continue;
    return false;
  }
  return has_catch;
}

}  // namespace

HostEhRegionPlan analyze_host_eh_regions(
    const mir_model::MirFunction & function)
{
  HostEhRegionPlan result;
  result.call_landing_blocks.resize(function.blocks.size());
  if(function.blocks.empty()) return result;

  std::unordered_map<std::string, std::size_t> block_index;
  block_index.reserve(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    if(!block_index.emplace(function.blocks[i].label, i).second)
      throw std::logic_error("duplicate MIR block label in host EH analysis: " +
                             function.blocks[i].label);
    result.call_landing_blocks[i].resize(
      function.blocks[i].instructions.size());
  }

  RegionStateInterner states;
  const std::size_t unknown = static_cast<std::size_t>(-1);
  std::vector<std::size_t> entries(function.blocks.size(), unknown);
  std::vector<std::size_t> worklist;
  worklist.reserve(function.blocks.size());
  std::vector<bool> catch_dispatch_blocks(function.blocks.size(), false);
  std::vector<bool> catch_entry_blocks(function.blocks.size(), false);
  std::vector<std::size_t> catch_entry_states(function.blocks.size(), unknown);

  const auto merge_entry = [&](std::size_t block, std::size_t state,
                               std::vector<std::size_t> * pending) {
    if(entries[block] == unknown) {
      entries[block] = state;
      pending->push_back(block);
    } else if(entries[block] != state) {
      throw std::logic_error(
        "host EH protected-region state mismatch at MIR block: " +
        function.blocks[block].label);
    }
  };
  const auto merge_label = [&](const std::string & label, std::size_t state,
                               std::vector<std::size_t> * pending) {
    const std::unordered_map<std::string, std::size_t>::const_iterator target =
      block_index.find(label);
    if(target == block_index.end())
      throw std::logic_error("host EH control-flow target has no MIR block: " +
                             label);
    merge_entry(target->second, state, pending);
  };

  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    catch_dispatch_blocks[i] =
      is_catch_dispatch_block(function.blocks[i]);
    if(!catch_dispatch_blocks[i]) continue;
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function.blocks[i].instructions[j];
      if(!is_branch(instruction.opcode)) continue;
      if(instruction.operands.size() != 1 ||
         instruction.operands[0].kind != mir_model::MirOperand::OP_LABEL)
        throw std::logic_error("invalid MIR branch in host EH analysis");
      const std::unordered_map<std::string, std::size_t>::const_iterator target =
        block_index.find(instruction.operands[0].text);
      if(target == block_index.end())
        throw std::logic_error(
          "host EH control-flow target has no MIR block: " +
          instruction.operands[0].text);
      catch_entry_blocks[target->second] = true;
    }
  }

  merge_entry(0, 0, &worklist);
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function.blocks[i].instructions[j];
      if(instruction.opcode != mir_model::MirInstruction::MI_EH_PUSH) continue;
      if(instruction.operands.size() != 2 ||
         instruction.operands[0].kind != mir_model::MirOperand::OP_LABEL ||
         instruction.operands[0].text.empty())
        throw std::logic_error("invalid MIR host EH protected-region marker");
      if(!block_index.count(instruction.operands[0].text))
        throw std::logic_error(
          "host EH landing pad has no MIR block: " +
          instruction.operands[0].text);
    }

  std::size_t next = 0;
  while(next < worklist.size()) {
    const std::size_t block_number = worklist[next++];
    const mir_model::MirBlock & block = function.blocks[block_number];
    std::size_t state = entries[block_number];
    bool unconditional_exit = false;
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const mir_model::MirInstruction & instruction = block.instructions[i];
      if(instruction.opcode == mir_model::MirInstruction::MI_EH_PUSH) {
        if(instruction.operands.size() != 2 ||
           instruction.operands[0].kind != mir_model::MirOperand::OP_LABEL ||
           instruction.operands[0].text.empty())
          throw std::logic_error(
            "invalid MIR host EH protected-region marker");
        const std::size_t parent = state;
        const std::unordered_map<std::string, std::size_t>::const_iterator
          landing = block_index.find(instruction.operands[0].text);
        if(landing == block_index.end())
          throw std::logic_error(
            "host EH landing pad has no MIR block: " +
            instruction.operands[0].text);
        state = states.Push(state, landing->second + 1);
        if(instruction.operands[1].kind != mir_model::MirOperand::OP_IMM)
          throw std::logic_error(
            "invalid MIR host EH protected-region kind");
        const bool cleanup_landing = instruction.operands[1].imm != 0 ||
          landing_pad_has_cleanup_clause(function.blocks[landing->second]) ||
          !landing_pad_has_catches(function.blocks[landing->second]);
        ++result.edge_count;
        merge_label(instruction.operands[0].text,
          cleanup_landing ? states.Consume(state) : parent,
          &worklist);
      } else if(instruction.opcode ==
                  mir_model::MirInstruction::MI_EH_POP) {
        if(!instruction.operands.empty())
          throw std::logic_error(
            "invalid MIR host EH protected-region end marker");
        if(state == 0)
          throw std::logic_error(
            "host EH protected-region stack underflow at MIR block: " +
            block.label);
        state = states.Parent(state);
      }

      const bool call =
        instruction.opcode == mir_model::MirInstruction::MI_CALL ||
        instruction.opcode == mir_model::MirInstruction::MI_CALL_INDIRECT;
      if(call && !instruction.call_unwind_no && states.Active(state) != 0) {
        result.call_landing_blocks[block_number][i] =
          states.ActiveLandingBlock(state);
        ++result.protected_call_count;
      }
      if(call && instruction.call_returns_noreturn) {
        unconditional_exit = true;
        break;
      }

      if(is_branch(instruction.opcode)) {
        if(instruction.operands.size() != 1 ||
           instruction.operands[0].kind != mir_model::MirOperand::OP_LABEL)
          throw std::logic_error("invalid MIR branch in host EH analysis");
        ++result.edge_count;
        const std::unordered_map<std::string, std::size_t>::const_iterator
          target_entry = block_index.find(instruction.operands[0].text);
        if(target_entry == block_index.end())
          throw std::logic_error(
            "host EH control-flow target has no MIR block: " +
            instruction.operands[0].text);
        const std::size_t target = target_entry->second;
        if(catch_dispatch_blocks[block_number]) {
          if(catch_entry_states[target] == unknown)
            catch_entry_states[target] = state;
          else if(catch_entry_states[target] != state)
            throw std::logic_error(
              "host EH catch-entry state mismatch at MIR block: " +
              function.blocks[target].label);
          merge_entry(target, state, &worklist);
        } else if(catch_entry_blocks[target]) {
          // A failed inner catch enters the enclosing catch through the same
          // compiler-generated entry block as its dispatch landing pad.  The
          // dispatch edge is authoritative: it has already consumed the
          // enclosing protected region represented by this forwarded edge.
          if(catch_entry_states[target] != unknown)
            merge_entry(target, catch_entry_states[target], &worklist);
        } else {
          merge_entry(target, state, &worklist);
        }
      }
      if(is_function_exit(instruction) && state != 0)
        throw std::logic_error(
          "host EH protected region remains active at function exit");
      if(is_unconditional_exit(instruction)) unconditional_exit = true;
    }
    if(!unconditional_exit && block_number + 1 < function.blocks.size()) {
      ++result.edge_count;
      merge_entry(block_number + 1, state, &worklist);
    }
  }
  result.state_count = states.size() - 1;
  return result;
}

}  // namespace host_eh_detail
}  // namespace lowir_native
