#include "lowir_native_analysis.h"

#include <deque>

namespace lowir_native {
namespace analysis {
namespace {

using lowir_model::Instruction;
using lowir_model::Operand;

unsigned instruction_clobber_mask(const Instruction & instruction)
{
  const bool wide = instruction.type.kind == lowir_model::LTK_I128;
  if(wide && (instruction.kind == Instruction::IK_CONST ||
              instruction.kind == Instruction::IK_COPY))
    return register_mask(XR_RAX) | register_mask(XR_RDX) |
      register_mask(XR_R11);
  if(instruction.kind == Instruction::IK_CALL)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_RDI) | register_mask(XR_R8) |
      register_mask(XR_R9) | register_mask(XR_R10) |
      register_mask(XR_R11);
  if(instruction.kind == Instruction::IK_COPYOBJ)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RSI) | register_mask(XR_RDI);
  if(instruction.kind == Instruction::IK_ZEROINIT)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDI);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op == "div" || instruction.op == "mod" ||
      instruction.op == "udiv" || instruction.op == "umod"))
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op == "shl" || instruction.op == "shr" ||
      instruction.op == "ushr"))
    return register_mask(XR_RCX) | register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_INDEX &&
     instruction.second.kind != Operand::OP_INTEGER)
    return register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_CMP)
    return wide ? register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_R10) | register_mask(XR_R11) :
      register_mask(XR_RAX) | register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_STORE)
    return wide ? register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_R11) :
      register_mask(XR_RAX) | register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_LOAD)
    return wide ? register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_R11) :
      register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_ATOMIC_LOAD)
    return wide ?
      register_mask(XR_RAX) | register_mask(XR_RBX) |
      register_mask(XR_RCX) | register_mask(XR_RDX) |
      register_mask(XR_R11) : register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_ATOMIC_STORE ||
     instruction.kind == Instruction::IK_ATOMIC_EXCHANGE)
    return register_mask(XR_RAX) | register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_ATOMIC_ADD_FETCH)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      (wide ?
       register_mask(XR_RBX) | register_mask(XR_R10) |
       register_mask(XR_R11) : 0);
  if(instruction.kind == Instruction::IK_BRANCH)
    return register_mask(XR_RAX);
  if(instruction.kind == Instruction::IK_SWITCH)
    return register_mask(XR_RAX) | register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_VA_START)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  return 0;
}

void note_operand(FunctionFacts & facts, const Operand & operand, std::size_t position)
{
  if(operand.kind != Operand::OP_TEMP) return;
  ++facts.uses[operand.text];
  if(!facts.first_use.count(operand.text)) facts.first_use[operand.text] = position;
  facts.last_use[operand.text] = position;
}

void note_instruction_operands(FunctionFacts & facts,
                               const Instruction & instruction,
                               std::size_t position)
{
  note_operand(facts, instruction.first, position);
  note_operand(facts, instruction.second, position);
  note_operand(facts, instruction.third, position);
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    note_operand(facts, instruction.args[i], position);
}

}  // namespace

unsigned register_mask(X64Register reg)
{
  return 1u << static_cast<unsigned>(reg);
}

FunctionFacts analyze_function(const lowir_model::LowirFunction & function)
{
  FunctionFacts facts;
  std::unordered_set<std::string> call_arguments;
  std::unordered_set<std::string> other_uses;
  std::size_t position = 0;
  for(std::size_t i = 0; i < function.params.size(); ++i)
    facts.definition[function.params[i].name] = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j, ++position) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      note_instruction_operands(facts, instruction, position);
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k)
        if(fixed[k]->kind == Operand::OP_TEMP) other_uses.insert(fixed[k]->text);
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_TEMP) {
          if(instruction.kind == Instruction::IK_CALL)
            call_arguments.insert(instruction.args[k].text);
          else other_uses.insert(instruction.args[k].text);
        }
      if(!instruction.dest.empty()) facts.definition[instruction.dest] = position;
      if(instruction.kind == Instruction::IK_CALL) facts.calls.push_back(position);
      if(instruction.kind == Instruction::IK_VA_START) facts.has_va_start = true;
      if((instruction.kind == Instruction::IK_ATOMIC_LOAD ||
          instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) &&
         instruction.type.kind == lowir_model::LTK_I128)
        facts.has_i128_atomic = true;
    }
  }
  for(std::unordered_set<std::string>::const_iterator value = call_arguments.begin();
      value != call_arguments.end(); ++value)
    if(!other_uses.count(*value)) facts.only_call_arguments.insert(*value);

  typedef std::unordered_set<std::string> ValueSet;
  const std::size_t block_count = function.blocks.size();
  std::vector<ValueSet> block_use(block_count);
  std::vector<ValueSet> block_def(block_count);
  std::vector<ValueSet> live_in(block_count);
  std::vector<ValueSet> live_out(block_count);
  std::vector<std::vector<std::size_t> > successors(block_count);
  std::vector<std::vector<std::size_t> > predecessors(block_count);
  std::unordered_map<std::string, std::size_t> block_index;
  for(std::size_t i = 0; i < block_count; ++i)
    block_index[function.blocks[i].label] = i;

  for(std::size_t i = 0; i < block_count; ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k)
        if(fixed[k]->kind == Operand::OP_TEMP &&
           !block_def[i].count(fixed[k]->text))
          block_use[i].insert(fixed[k]->text);
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_TEMP &&
           !block_def[i].count(instruction.args[k].text))
          block_use[i].insert(instruction.args[k].text);
      if(!instruction.dest.empty()) block_def[i].insert(instruction.dest);
    }
    if(function.blocks[i].instructions.empty()) continue;
    const Instruction & terminal = function.blocks[i].instructions.back();
    std::vector<std::string> targets;
    if(terminal.kind == Instruction::IK_JUMP) targets.push_back(terminal.first.text);
    else if(terminal.kind == Instruction::IK_BRANCH) {
      targets.push_back(terminal.second.text);
      targets.push_back(terminal.third.text);
    } else if(terminal.kind == Instruction::IK_SWITCH) {
      targets.push_back(terminal.second.text);
      for(std::size_t k = 1; k < terminal.args.size(); k += 2)
        targets.push_back(terminal.args[k].text);
    }
    for(std::size_t j = 0; j < targets.size(); ++j) {
      const std::unordered_map<std::string, std::size_t>::const_iterator found =
        block_index.find(targets[j]);
      if(found == block_index.end()) continue;
      successors[i].push_back(found->second);
      predecessors[found->second].push_back(i);
    }
  }

  std::deque<std::size_t> worklist;
  std::vector<bool> queued(block_count, true);
  for(std::size_t i = 0; i < block_count; ++i) worklist.push_back(i);
  while(!worklist.empty()) {
    const std::size_t block = worklist.front();
    worklist.pop_front();
    queued[block] = false;
    ValueSet new_out;
    for(std::size_t i = 0; i < successors[block].size(); ++i)
      new_out.insert(live_in[successors[block][i]].begin(),
                     live_in[successors[block][i]].end());
    ValueSet new_in = block_use[block];
    for(ValueSet::const_iterator value = new_out.begin(); value != new_out.end(); ++value)
      if(!block_def[block].count(*value)) new_in.insert(*value);
    if(new_out == live_out[block] && new_in == live_in[block]) continue;
    live_out[block].swap(new_out);
    live_in[block].swap(new_in);
    for(std::size_t i = 0; i < predecessors[block].size(); ++i) {
      const std::size_t predecessor = predecessors[block][i];
      if(queued[predecessor]) continue;
      queued[predecessor] = true;
      worklist.push_back(predecessor);
    }
  }

  for(std::size_t i = 0; i < block_count; ++i) {
    facts.edge_live.insert(live_in[i].begin(), live_in[i].end());
    facts.edge_live.insert(live_out[i].begin(), live_out[i].end());
    ValueSet live = live_out[i];
    for(std::size_t j = function.blocks[i].instructions.size(); j != 0; --j) {
      const Instruction & instruction = function.blocks[i].instructions[j - 1];
      if(!instruction.dest.empty()) live.erase(instruction.dest);
      const unsigned clobbers = instruction_clobber_mask(instruction);
      if(clobbers)
        for(ValueSet::const_iterator value = live.begin(); value != live.end(); ++value)
          facts.live_across_clobbers[*value] |= clobbers;
      if(instruction.kind == Instruction::IK_CALL)
        facts.live_across_call.insert(live.begin(), live.end());
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k)
        if(fixed[k]->kind == Operand::OP_TEMP) live.insert(fixed[k]->text);
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_TEMP)
          live.insert(instruction.args[k].text);
    }
  }
  return facts;
}

StorageFacts analyze_storage(
    const lowir_model::LowirFunction & function,
    const FunctionFacts & function_facts,
    const std::unordered_map<std::string, std::string> & tls_wrappers)
{
  StorageFacts facts;
  std::unordered_map<std::string, std::size_t> parameters_by_name;
  std::unordered_map<std::string, std::size_t> parameters_by_suffix;
  for(std::size_t p = 0; p < function.params.size(); ++p) {
    parameters_by_name[function.params[p].name] = p;
    if(function.params[p].name.size() >= 2)
      parameters_by_suffix[function.params[p].name.substr(1)] = p;
  }

  std::unordered_map<std::string, std::size_t> object_slots;
  std::unordered_map<std::string, std::size_t> scalar_slots;
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const std::string & slot = function.slots[s].first;
    if(function.slots[s].second.kind == lowir_model::LTK_OBJECT) {
      if(slot.size() < 2) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator parameter =
        parameters_by_suffix.find(slot.substr(1));
      if(parameter != parameters_by_suffix.end() &&
         lowir_model::same_lowir_type(function.params[parameter->second].type,
                                      function.slots[s].second))
        object_slots[slot] = parameter->second;
    } else if(function_facts.calls.empty() && function.blocks.size() == 1) {
      scalar_slots[slot] = s;
    }
  }

  struct ScalarSlotState
  {
    bool initialized = false;
    bool loaded = false;
    bool valid = true;
    bool read_through = true;
    std::size_t parameter = static_cast<std::size_t>(-1);
    std::string loaded_name;
  };
  std::unordered_map<std::string, ScalarSlotState> scalar_states;
  for(std::unordered_map<std::string, std::size_t>::const_iterator slot =
        scalar_slots.begin(); slot != scalar_slots.end(); ++slot)
    scalar_states.emplace(slot->first, ScalarSlotState());
  std::unordered_set<std::string> seen_object_slots;

  for(std::size_t b = 0; b < function.blocks.size(); ++b) {
    const std::vector<Instruction> & instructions = function.blocks[b].instructions;
    for(std::size_t i = 0; i < instructions.size(); ++i) {
      const Instruction & instruction = instructions[i];
      if(instruction.kind == Instruction::IK_STORE &&
         instruction.first.kind == Operand::OP_TEMP &&
         instruction.second.kind == Operand::OP_GLOBAL &&
         tls_wrappers.count(instruction.second.text))
        facts.tls_store_inputs.insert(instruction.first.text);
      if(instruction.kind == Instruction::IK_STORE &&
         instruction.first.kind == Operand::OP_TEMP &&
         instruction.second.kind == Operand::OP_SLOT &&
         function_facts.uses.find(instruction.first.text) != function_facts.uses.end() &&
         function_facts.uses.find(instruction.first.text)->second == 1)
        facts.dead_slot_only_parameters.insert(instruction.first.text);

      const Operand * operands[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      std::string mentioned[3];
      std::size_t mentioned_count = 0;
      for(std::size_t k = 0; k < sizeof(operands) / sizeof(operands[0]); ++k) {
        if(operands[k]->kind != Operand::OP_SLOT) continue;
        bool duplicate = false;
        for(std::size_t m = 0; m < mentioned_count; ++m)
          if(mentioned[m] == operands[k]->text) duplicate = true;
        if(duplicate) continue;
        mentioned[mentioned_count++] = operands[k]->text;

        const std::unordered_map<std::string, std::size_t>::const_iterator object =
          object_slots.find(operands[k]->text);
        if(object != object_slots.end() &&
           seen_object_slots.insert(object->first).second &&
           instruction.kind == Instruction::IK_ADDR && i + 1 < instructions.size()) {
          const lowir_model::LowirParameter & parameter =
            function.params[object->second];
          const Instruction & copy = instructions[i + 1];
          if(copy.kind == Instruction::IK_COPYOBJ &&
             copy.first.kind == Operand::OP_TEMP && copy.first.text == parameter.name &&
             copy.second.kind == Operand::OP_TEMP && copy.second.text == instruction.dest &&
             copy.byte_count == parameter.type.storage_size)
            facts.parameter_slot_aliases[object->first] = parameter.name;
        }

        const std::unordered_map<std::string, std::size_t>::const_iterator scalar =
          scalar_slots.find(operands[k]->text);
        if(scalar == scalar_slots.end()) continue;
        ScalarSlotState & state = scalar_states[scalar->first];
        const bool first = instruction.first.kind == Operand::OP_SLOT &&
          instruction.first.text == scalar->first;
        const bool second = instruction.second.kind == Operand::OP_SLOT &&
          instruction.second.text == scalar->first;
        const bool third = instruction.third.kind == Operand::OP_SLOT &&
          instruction.third.text == scalar->first;
        if(!state.initialized && instruction.kind == Instruction::IK_STORE && second &&
           instruction.first.kind == Operand::OP_TEMP) {
          const std::unordered_map<std::string, std::size_t>::const_iterator parameter =
            parameters_by_name.find(instruction.first.text);
          if(parameter != parameters_by_name.end() &&
             lowir_model::same_lowir_type(function.params[parameter->second].type,
                                          function.slots[scalar->second].second)) {
            state.initialized = true;
            state.parameter = parameter->second;
          } else {
            state.valid = false;
          }
        } else if(state.initialized && instruction.kind == Instruction::IK_LOAD && first) {
          state.loaded = true;
          state.loaded_name = instruction.dest;
        } else if(first || second || third) {
          state.valid = false;
        }
      }
    }
  }

  std::unordered_map<std::string, std::string> loaded_slots;
  for(std::unordered_map<std::string, ScalarSlotState>::const_iterator state =
        scalar_states.begin(); state != scalar_states.end(); ++state)
    if(state->second.valid && state->second.initialized && state->second.loaded)
      loaded_slots[state->second.loaded_name] = state->first;
  for(std::size_t i = 0; i < (function.blocks.empty() ? 0 :
      function.blocks[0].instructions.size()); ++i) {
    const Instruction & instruction = function.blocks[0].instructions[i];
    const Operand * operands[] = {
      &instruction.first, &instruction.second, &instruction.third
    };
    for(std::size_t k = 0; k < sizeof(operands) / sizeof(operands[0]); ++k) {
      if(operands[k]->kind != Operand::OP_TEMP) continue;
      const std::unordered_map<std::string, std::string>::const_iterator slot =
        loaded_slots.find(operands[k]->text);
      if(slot != loaded_slots.end() &&
         !(instruction.kind == Instruction::IK_LOAD && k == 0))
        scalar_states[slot->second].read_through = false;
    }
  }
  for(std::unordered_map<std::string, ScalarSlotState>::const_iterator state =
        scalar_states.begin(); state != scalar_states.end(); ++state) {
    if(!state->second.valid || !state->second.initialized || !state->second.loaded)
      continue;
    const std::string & parameter = function.params[state->second.parameter].name;
    if(state->second.read_through)
      facts.promoted_parameter_slots[state->first] = parameter;
    else
      facts.forwarded_parameter_slots[state->first] = parameter;
    facts.promoted_parameters.insert(parameter);
  }
  return facts;
}

}  // namespace analysis
}  // namespace lowir_native
