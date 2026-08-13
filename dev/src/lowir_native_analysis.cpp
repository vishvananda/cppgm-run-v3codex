#include "lowir_native_analysis.h"

#include <algorithm>
#include <queue>

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
  if(instruction.kind >= Instruction::IK_EH_TRY &&
     instruction.kind <= Instruction::IK_RESUME)
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
  if(instruction.kind == Instruction::IK_VA_ARG)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_RDI) | register_mask(XR_R8) |
      register_mask(XR_R9) | register_mask(XR_R10);
  if(instruction.kind == Instruction::IK_STACK_ALLOC)
    return register_mask(XR_RAX);
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
  std::unordered_set<std::string> parameter_names;
  std::unordered_map<std::string, std::size_t> definition_blocks;
  std::vector<std::pair<std::string, std::size_t> > block_uses;
  std::vector<std::size_t> block_last_position(function.blocks.size(), 0);
  std::vector<std::vector<std::size_t> > clobber_positions(16);
  std::size_t position = 0;
  for(std::size_t i = 0; i < function.params.size(); ++i) {
    facts.definition[function.params[i].name] = 0;
    facts.parameters.insert(function.params[i].name);
    parameter_names.insert(function.params[i].name);
    definition_blocks[function.params[i].name] = 0;
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j, ++position) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      note_instruction_operands(facts, instruction, position);
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k)
        if(fixed[k]->kind == Operand::OP_TEMP) {
          other_uses.insert(fixed[k]->text);
          block_uses.push_back(std::make_pair(fixed[k]->text, i));
          const std::unordered_map<std::string, std::size_t>::const_iterator definition =
            definition_blocks.find(fixed[k]->text);
          if(definition != definition_blocks.end() && definition->second != i)
            facts.edge_live.insert(fixed[k]->text);
        }
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_TEMP) {
          block_uses.push_back(std::make_pair(instruction.args[k].text, i));
          const std::unordered_map<std::string, std::size_t>::const_iterator definition =
            definition_blocks.find(instruction.args[k].text);
          if(definition != definition_blocks.end() && definition->second != i)
            facts.edge_live.insert(instruction.args[k].text);
          if(instruction.kind == Instruction::IK_CALL)
            call_arguments.insert(instruction.args[k].text);
          else other_uses.insert(instruction.args[k].text);
        }
      if(!instruction.dest.empty()) {
        facts.definition[instruction.dest] = position;
        definition_blocks[instruction.dest] = i;
      }
      const unsigned clobbers = instruction_clobber_mask(instruction);
      for(std::size_t reg = 0; reg < clobber_positions.size(); ++reg)
        if(clobbers & (1u << reg)) clobber_positions[reg].push_back(position);
      if(instruction.kind == Instruction::IK_INDEX &&
         instruction.first.kind == Operand::OP_TEMP &&
         parameter_names.count(instruction.first.text) &&
         instruction.second.kind == Operand::OP_INTEGER &&
         instruction.second.text == "0")
        facts.zero_index_parameters.insert(instruction.first.text);
      if(instruction.kind == Instruction::IK_SWITCH &&
         instruction.first.kind == Operand::OP_TEMP &&
         parameter_names.count(instruction.first.text))
        facts.switch_parameters.insert(instruction.first.text);
      if(instruction.kind == Instruction::IK_BINARY &&
         instruction.first.kind == Operand::OP_TEMP &&
         parameter_names.count(instruction.first.text))
        facts.destructive_parameters.insert(instruction.first.text);
      if(instruction.kind == Instruction::IK_CALL) facts.calls.push_back(position);
      if(instruction.kind == Instruction::IK_VA_START) facts.has_va_start = true;
      if(instruction.kind == Instruction::IK_STACK_ALLOC)
        facts.has_dynamic_stack = true;
      if((instruction.kind == Instruction::IK_ATOMIC_LOAD ||
          instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) &&
         instruction.type.kind == lowir_model::LTK_I128)
        facts.has_i128_atomic = true;
    }
    block_last_position[i] = position ? position - 1 : 0;
  }
  for(std::unordered_set<std::string>::const_iterator value = call_arguments.begin();
      value != call_arguments.end(); ++value)
    if(!other_uses.count(*value)) facts.only_call_arguments.insert(*value);

  position = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::vector<Instruction> & instructions = function.blocks[i].instructions;
    std::unordered_map<std::string, std::size_t> comparisons;
    std::unordered_map<std::string, std::size_t> definitions;
    const std::size_t block_start = position;
    for(std::size_t j = 0; j < instructions.size(); ++j, ++position) {
      const Instruction & instruction = instructions[j];
      if(!instruction.dest.empty()) definitions[instruction.dest] = j;
      if(instruction.kind == Instruction::IK_CMP)
        comparisons[instruction.dest] = j;
      if(instruction.kind != Instruction::IK_BRANCH ||
         instruction.first.kind != Operand::OP_TEMP) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator branch_definition =
        definitions.find(instruction.first.text);
      if(branch_definition != definitions.end() &&
         instructions[branch_definition->second].kind == Instruction::IK_CALL &&
         facts.uses.find(instruction.first.text) != facts.uses.end() &&
         facts.uses.find(instruction.first.text)->second == 1)
        facts.direct_branch_call_results.insert(instruction.first.text);
      const std::unordered_map<std::string, std::size_t>::const_iterator comparison =
        comparisons.find(instruction.first.text);
      if(comparison == comparisons.end() ||
         facts.uses.find(instruction.first.text) == facts.uses.end() ||
         facts.uses.find(instruction.first.text)->second != 1) continue;
      const Instruction & source = instructions[comparison->second];
      const Operand * operands[] = {&source.first, &source.second};
      for(std::size_t operand = 0; operand < 2; ++operand) {
        if(operands[operand]->kind == Operand::OP_TEMP) {
          facts.direct_branch_sources.insert(operands[operand]->text);
          if(parameter_names.count(operands[operand]->text))
            facts.has_direct_branch_parameter = true;
        }
        const std::unordered_map<std::string, std::size_t>::const_iterator definition =
          definitions.find(operands[operand]->text);
        if(comparison->second + 1 != j || definition == definitions.end()) continue;
        const Instruction & producer = instructions[definition->second];
        if(producer.kind != Instruction::IK_LOAD ||
           !lowir_model::same_lowir_type(producer.type, source.type) ||
           (producer.first.kind != Operand::OP_SLOT &&
            producer.first.kind != Operand::OP_GLOBAL) ||
           facts.uses.find(producer.dest) == facts.uses.end() ||
           facts.uses.find(producer.dest)->second != 1) continue;
        if(definition->second + 1 == comparison->second)
          facts.direct_compare_storage_values.insert(producer.dest);
        else if(operand == 0 && definition->second + 2 == comparison->second &&
                instructions[definition->second + 1].kind == Instruction::IK_LOAD &&
                source.second.kind == Operand::OP_TEMP &&
                instructions[definition->second + 1].dest == source.second.text)
          facts.direct_compare_rax_values.insert(producer.dest);
      }
      if(comparison->second + 1 == j) continue;
      facts.deferred_branch_comparisons[instruction.first.text] = &source;
      for(std::size_t operand = 0; operand < 2; ++operand) {
        if(operands[operand]->kind != Operand::OP_TEMP) continue;
        const std::string & name = operands[operand]->text;
        facts.last_use[name] = std::max(facts.last_use[name], block_start + j);
        for(std::size_t k = comparison->second + 1; k < j; ++k) {
          const unsigned clobbers = instruction_clobber_mask(instructions[k]);
          facts.live_across_clobbers[name] |= clobbers;
          if(instructions[k].kind == Instruction::IK_CALL)
            facts.live_across_call.insert(name);
        }
      }
    }
  }

  const std::size_t block_count = function.blocks.size();
  // LowIR validation gives every temporary one definition before its uses.
  // Model those values as linearized live intervals, extending uses through
  // source-order loop ranges.  Per-register clobber indexes then answer each
  // interval query without materializing the values x blocks liveness
  // relation.  The register set is fixed at 16, so this remains O(IR log IR)
  // time and O(IR) storage for instructions, operands, blocks, and CFG edges.
  std::unordered_map<std::string, std::size_t> block_index;
  for(std::size_t i = 0; i < block_count; ++i)
    block_index[function.blocks[i].label] = i;
  std::vector<std::vector<std::size_t> > loop_ends_at(block_count);
  for(std::size_t i = 0; i < block_count; ++i) {
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
      if(found != block_index.end() && found->second <= i)
        loop_ends_at[found->second].push_back(i);
    }
  }
  std::vector<std::size_t> loop_end_for_block(block_count, 0);
  std::priority_queue<std::size_t> active_loop_ends;
  for(std::size_t block = 0; block < block_count; ++block) {
    for(std::size_t i = 0; i < loop_ends_at[block].size(); ++i)
      active_loop_ends.push(loop_ends_at[block][i]);
    while(!active_loop_ends.empty() && active_loop_ends.top() < block)
      active_loop_ends.pop();
    loop_end_for_block[block] = active_loop_ends.empty() ? block :
      active_loop_ends.top();
  }
  for(std::size_t i = 0; i < block_uses.size(); ++i) {
    const std::size_t use_block = block_uses[i].second;
    const std::size_t loop_end = loop_end_for_block[use_block];
    if(loop_end != use_block)
      facts.last_use[block_uses[i].first] = std::max(
        facts.last_use[block_uses[i].first], block_last_position[loop_end]);
  }
  for(std::unordered_map<std::string, std::size_t>::const_iterator value =
      facts.last_use.begin(); value != facts.last_use.end(); ++value) {
    const std::unordered_map<std::string, std::size_t>::const_iterator definition =
      facts.definition.find(value->first);
    if(definition == facts.definition.end()) continue;
    const std::size_t start = facts.parameters.count(value->first) ? 0 :
      definition->second + 1;
    const std::size_t end = value->second;
    if(start >= end) continue;
    unsigned mask = 0;
    for(std::size_t reg = 0; reg < clobber_positions.size(); ++reg) {
      const std::vector<std::size_t>::const_iterator clobber = std::lower_bound(
        clobber_positions[reg].begin(), clobber_positions[reg].end(), start);
      if(clobber != clobber_positions[reg].end() && *clobber < end)
        mask |= 1u << reg;
    }
    if(mask) facts.live_across_clobbers[value->first] |= mask;
    const std::vector<std::size_t>::const_iterator call = std::lower_bound(
      facts.calls.begin(), facts.calls.end(), start);
    if(call != facts.calls.end() && *call < end)
      facts.live_across_call.insert(value->first);
  }
  for(std::size_t i = 0; i < block_count; ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      if(instruction.kind == Instruction::IK_INDEX &&
         parameter_names.count(instruction.first.text) &&
         facts.live_across_call.count(instruction.dest))
        facts.forwarded_parameters_across_call.insert(instruction.first.text);
    }
  for(std::unordered_set<std::string>::iterator value =
        facts.destructive_parameters.begin(); value != facts.destructive_parameters.end(); )
    if(facts.uses[*value] != 1) value = facts.destructive_parameters.erase(value);
    else ++value;
  return facts;
}

StorageFacts analyze_storage(
    const lowir_model::LowirFunction & function,
    const FunctionFacts & function_facts,
    const std::unordered_map<std::string, std::string> & tls_wrappers)
{
  StorageFacts facts;
  std::unordered_set<std::string> written_slots;
  std::unordered_set<std::string> observed_slots;
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
    } else if(function.blocks.size() == 1) {
      scalar_slots[slot] = s;
    }
  }

  struct ScalarSlotState
  {
    bool initialized = false;
    bool loaded = false;
    std::size_t load_count = 0;
    std::size_t store_position = 0;
    std::size_t load_position = 0;
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
      const bool has_dead_store_candidate =
        instruction.kind == Instruction::IK_STORE &&
        instruction.second.kind == Operand::OP_SLOT &&
        !(instruction.first.kind == Operand::OP_SLOT &&
          instruction.first.text == instruction.second.text) &&
        !(instruction.third.kind == Operand::OP_SLOT &&
          instruction.third.text == instruction.second.text);
      if(has_dead_store_candidate)
        written_slots.insert(instruction.second.text);
      const Operand * storage_operands[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0;
          k < sizeof(storage_operands) / sizeof(storage_operands[0]); ++k)
        if(storage_operands[k]->kind == Operand::OP_SLOT &&
           (!has_dead_store_candidate ||
            storage_operands[k]->text != instruction.second.text))
          observed_slots.insert(storage_operands[k]->text);
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_SLOT &&
           (!has_dead_store_candidate ||
            instruction.args[k].text != instruction.second.text))
          observed_slots.insert(instruction.args[k].text);
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
            state.store_position = i;
          } else {
            state.valid = false;
          }
        } else if(state.initialized && instruction.kind == Instruction::IK_LOAD && first) {
          state.loaded = true;
          ++state.load_count;
          state.load_position = i;
          state.loaded_name = instruction.dest;
        } else if(first || second || third) {
          state.valid = false;
        }
      }
    }
  }

  for(std::unordered_set<std::string>::const_iterator slot = written_slots.begin();
      slot != written_slots.end(); ++slot)
    if(!observed_slots.count(*slot)) facts.dead_store_slots.insert(*slot);

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
    if(!state->second.valid || !state->second.initialized || !state->second.loaded ||
       (!function_facts.calls.empty() && state->second.load_count != 1))
      continue;
    const std::string & parameter = function.params[state->second.parameter].name;
    if(!function_facts.calls.empty() && function_facts.uses.find(parameter)->second != 1)
      continue;
    if(state->second.read_through)
      facts.promoted_parameter_slots[state->first] = parameter;
    else
      facts.forwarded_parameter_slots[state->first] = parameter;
    facts.promoted_parameters.insert(parameter);
    const std::vector<std::size_t>::const_iterator call = std::upper_bound(
      function_facts.calls.begin(), function_facts.calls.end(),
      state->second.store_position);
    if(function_facts.live_across_call.count(state->second.loaded_name) ||
       (call != function_facts.calls.end() &&
        *call < state->second.load_position))
      facts.promoted_parameters_across_call.insert(parameter);
  }
  return facts;
}

}  // namespace analysis
}  // namespace lowir_native
