#include "native/eh/lowir_native_host_eh.h"
#include "native/mir/control_flow.h"

#include <stdexcept>
#include <unordered_map>
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
  std::vector<unsigned char> landing_pads(function->block_labels.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function->blocks[i].instructions[j];
      if(instruction.opcode == mir_model::MirInstruction::MI_EH_PUSH &&
         !instruction.operands.empty() &&
         instruction.operands[0].kind == mir_model::MirOperand::OP_LABEL) {
        const std::uint32_t block = instruction.operands[0].block;
        if(block < landing_pads.size()) landing_pads[block] = 1;
      }
    }
  function->host_eh_clauses.clear();
  function->host_eh_clauses.resize(function->block_labels.size());
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
          clause.type_symbol = instruction.operands[1].symbol;
      } else if(instruction.opcode ==
                  mir_model::MirInstruction::MI_EH_FILTER) {
		if(instruction.operands.empty() ||
		   instruction.operands[0].kind != mir_model::MirOperand::OP_IMM)
		  throw std::logic_error("invalid MIR host EH filter clause");
        clause.kind = mir_model::MirHostEhClause::HC_FILTER;
        clause.selector = instruction.operands[0].imm;
        for(std::size_t type = 1; type < instruction.operands.size(); ++type)
          clause.filter_type_symbols.push_back(
            instruction.operands[type].symbol);
      } else continue;
      clauses.push_back(clause);
    }
    const std::uint32_t block = function->blocks[i].id;
    if(block >= landing_pads.size() || !landing_pads[block]) continue;
    if(clauses.empty()) {
      mir_model::MirHostEhClause cleanup;
      cleanup.kind = mir_model::MirHostEhClause::HC_CLEANUP;
      clauses.push_back(cleanup);
    }
    function->host_eh_clauses[block] = clauses;
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
    // an extra source-level region for a later EH_END to close.  A nested
    // region still returns to that consumed frame when it closes.
    if(states_[state].consumed)
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

  bool Consumed(std::size_t state) const
  {
    if(state >= states_.size())
      throw std::logic_error("invalid host EH protected-region state");
    return state != 0 && states_[state].consumed;
  }

  bool EquivalentAfterLanding(std::size_t left, std::size_t right) const
  {
    if(left == right) return true;
    return Consumed(left) && Consumed(right) &&
      Active(left) == Active(right);
  }

  bool PreferIncoming(std::size_t current, std::size_t incoming) const
  {
    return Active(current) == Active(incoming) &&
      !Consumed(current) && Consumed(incoming);
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

bool is_function_exit(const mir_model::MirInstruction & instruction)
{
  return instruction.opcode == mir_model::MirInstruction::MI_RET ||
    instruction.opcode == mir_model::MirInstruction::MI_FRET;
}

bool landing_pad_has_catches(const mir_model::MirBlock & block)
{
  for(std::size_t i = 0; i < block.instructions.size(); ++i)
    if(block.instructions[i].opcode ==
         mir_model::MirInstruction::MI_EH_CATCH ||
       block.instructions[i].opcode ==
         mir_model::MirInstruction::MI_EH_FILTER)
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
    if(opcode == mir_model::MirInstruction::MI_EH_CATCH ||
       opcode == mir_model::MirInstruction::MI_EH_FILTER) {
      has_catch = true;
      continue;
    }
    if(opcode == mir_model::MirInstruction::MI_EH_CLEANUP_CLAUSE ||
       is_branch(opcode)) continue;
    return false;
  }
  return has_catch;
}

void validate_protected_region_markers(
    const mir_model::MirFunction & function,
    const std::vector<std::size_t> & block_index)
{
  const std::size_t unknown = static_cast<std::size_t>(-1);
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function.blocks[i].instructions[j];
      if(instruction.opcode != mir_model::MirInstruction::MI_EH_PUSH) continue;
      if(instruction.operands.size() != 2 ||
         instruction.operands[0].kind != mir_model::MirOperand::OP_LABEL ||
         !instruction.operands[0].block.valid())
        throw std::logic_error("invalid MIR host EH protected-region marker");
      const std::uint32_t target = instruction.operands[0].block;
      if(target >= block_index.size() || block_index[target] == unknown)
        throw std::logic_error(
          "host EH landing pad has no MIR block: block #" +
          std::to_string(target));
    }
}

}  // namespace

HostEhRegionPlan analyze_host_eh_regions(
    const mir_model::MirFunction & function,
    const lowir_model::SealedStringPool & strings,
    const std::string & function_name)
{
  HostEhRegionPlan result;
  result.call_landing_blocks.resize(function.blocks.size());
  if(function.blocks.empty()) return result;

  const std::size_t unknown = static_cast<std::size_t>(-1);
  std::vector<std::size_t> block_index(function.block_labels.size(), unknown);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::uint32_t id = function.blocks[i].id;
    if(id >= block_index.size() || block_index[id] != unknown)
      throw std::logic_error("duplicate or invalid MIR block identity in host EH analysis");
    block_index[id] = i;
    result.call_landing_blocks[i].resize(
      function.blocks[i].instructions.size());
  }
  const auto block_position = [&](lowir_model::BlockId id) {
    const std::uint32_t index = id;
    return id.valid() && index < block_index.size() ? block_index[index] : unknown;
  };
  const auto block_name = [&](lowir_model::BlockId id)
      -> std::string {
    const std::uint32_t index = id;
    if(!id.valid() || index >= function.block_labels.size())
      throw std::logic_error("invalid MIR block identity");
    if(!function.block_labels[index].valid())
      return std::string("block #") + std::to_string(index);
    return strings.get(function.block_labels[index]);
  };

  RegionStateInterner states;
  std::vector<std::size_t> entries(function.blocks.size(), unknown);
  std::vector<std::size_t> worklist;
  worklist.reserve(function.blocks.size());
  std::vector<bool> catch_dispatch_blocks(function.blocks.size(), false);
  std::vector<bool> catch_entry_blocks(function.blocks.size(), false);
  std::vector<bool> cleanup_landing_blocks(function.blocks.size(), false);
  std::vector<std::size_t> catch_entry_states(function.blocks.size(), unknown);

  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function.blocks[i].instructions[j];
      if(instruction.opcode == mir_model::MirInstruction::MI_RESUME)
        ++result.resume_instruction_count;
      if(instruction.opcode != mir_model::MirInstruction::MI_EH_PUSH ||
         instruction.operands.size() != 2 ||
         instruction.operands[0].kind != mir_model::MirOperand::OP_LABEL)
        continue;
      const std::size_t landing = block_position(instruction.operands[0].block);
      if(landing == unknown) continue;
      cleanup_landing_blocks[landing] =
        instruction.operands[1].kind == mir_model::MirOperand::OP_IMM &&
        (instruction.operands[1].imm != 0 ||
         landing_pad_has_cleanup_clause(function.blocks[landing]) ||
         !landing_pad_has_catches(function.blocks[landing]));
    }

  const auto merge_entry = [&](std::size_t block, std::size_t state,
                               std::vector<std::size_t> * pending) {
    if(entries[block] == unknown) {
      entries[block] = state;
      pending->push_back(block);
    } else if(entries[block] != state) {
      const mir_model::MirBlock & target = function.blocks[block];
      const std::size_t existing_active = states.Active(entries[block]);
      const std::size_t incoming_active = states.Active(state);
      const bool prefer_consumed =
        states.PreferIncoming(entries[block], state);
      if(prefer_consumed) {
        entries[block] = state;
        pending->push_back(block);
        return;
      }
      if((cleanup_landing_blocks[block] &&
          existing_active == incoming_active) ||
         states.EquivalentAfterLanding(entries[block], state) ||
         (target.instructions.size() == 1 &&
          target.instructions[0].opcode ==
            mir_model::MirInstruction::MI_RESUME &&
          existing_active == 0 && incoming_active == 0))
        return;
      throw std::logic_error(
        "host EH protected-region state mismatch in " + function_name +
        " at MIR block " + std::to_string(
          static_cast<std::uint32_t>(function.blocks[block].id)) +
        " (existing state " + std::to_string(entries[block]) +
        ", incoming state " + std::to_string(state) +
        ", existing active " + std::to_string(existing_active) +
        ", incoming active " + std::to_string(incoming_active) + ")");
    }
  };
  const auto merge_label = [&](lowir_model::BlockId label, std::size_t state,
                               std::vector<std::size_t> * pending) {
    const std::size_t target = block_position(label);
    if(target == unknown)
      throw std::logic_error("host EH control-flow target has no MIR block: " +
                             block_name(label));
    merge_entry(target, state, pending);
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
      const std::size_t target = block_position(instruction.operands[0].block);
      if(target == unknown)
        throw std::logic_error(
          "host EH control-flow target has no MIR block: " +
          block_name(instruction.operands[0].block));
      catch_entry_blocks[target] = true;
    }
    if(i + 1 < function.blocks.size() &&
       (function.blocks[i].instructions.empty() ||
        !mir_control_flow::ends_unconditional_control_flow(
          function.blocks[i].instructions.back().opcode)))
      catch_entry_blocks[i + 1] = true;
  }

  const auto merge_control_flow_edge =
      [&](std::size_t source, std::size_t target, std::size_t state,
          std::vector<std::size_t> * pending) {
    if(catch_dispatch_blocks[source]) {
      if(catch_entry_states[target] == unknown)
        catch_entry_states[target] = state;
      else if(states.PreferIncoming(catch_entry_states[target], state))
        catch_entry_states[target] = state;
      else if(!states.EquivalentAfterLanding(
                catch_entry_states[target], state))
        throw std::logic_error(
          "host EH catch-entry state mismatch at MIR block: " +
          block_name(function.blocks[target].id));
      merge_entry(target, state, pending);
    } else if(catch_entry_blocks[target]) {
      // A failed inner catch enters the enclosing catch through the same
      // compiler-generated entry block as its dispatch landing pad.  The
      // dispatch edge is authoritative: it has already consumed the
      // enclosing protected region represented by this forwarded edge.
      if(catch_entry_states[target] != unknown)
        merge_entry(target, catch_entry_states[target], pending);
    } else {
      merge_entry(target, state, pending);
    }
  };

  merge_entry(0, 0, &worklist);
  validate_protected_region_markers(function, block_index);

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
           !instruction.operands[0].block.valid())
          throw std::logic_error(
            "invalid MIR host EH protected-region marker");
        const std::size_t parent = state;
        const std::size_t landing = block_position(instruction.operands[0].block);
        if(landing == unknown)
          throw std::logic_error(
            "host EH landing pad has no MIR block: " +
            block_name(instruction.operands[0].block));
        state = states.Push(state, landing + 1);
        if(instruction.operands[1].kind != mir_model::MirOperand::OP_IMM)
          throw std::logic_error(
            "invalid MIR host EH protected-region kind");
        const bool cleanup_landing = instruction.operands[1].imm != 0 ||
          landing_pad_has_cleanup_clause(function.blocks[landing]) ||
          !landing_pad_has_catches(function.blocks[landing]);
        ++result.edge_count;
        merge_label(instruction.operands[0].block,
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
            block_name(block.id));
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
        const std::size_t target = block_position(instruction.operands[0].block);
        if(target == unknown)
          throw std::logic_error(
            "host EH control-flow target has no MIR block: " +
            block_name(instruction.operands[0].block));
        merge_control_flow_edge(block_number, target, state, &worklist);
      }
      if(is_function_exit(instruction) && state != 0)
        throw std::logic_error(
          "host EH protected region remains active at function exit: " +
          function_name + " block " + block_name(block.id));
      if(mir_control_flow::ends_unconditional_control_flow(instruction.opcode))
        unconditional_exit = true;
    }
    if(!unconditional_exit && block_number + 1 < function.blocks.size()) {
      ++result.edge_count;
      merge_control_flow_edge(block_number, block_number + 1, state,
                              &worklist);
    }
  }
  result.state_count = states.size() - 1;
  return result;
}

}  // namespace host_eh_detail
}  // namespace lowir_native
