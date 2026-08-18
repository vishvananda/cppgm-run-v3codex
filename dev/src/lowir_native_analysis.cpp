#include "lowir_native_analysis.h"

#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <set>

namespace lowir_native {
namespace analysis {
namespace {

using lowir_model::Instruction;
using lowir_model::Operand;

unsigned instruction_clobber_mask(const Instruction & instruction,
                                  bool direct_memory_index = false)
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
  if(instruction.type.kind == lowir_model::LTK_OBJECT &&
     (instruction.kind == Instruction::IK_COPY ||
      instruction.kind == Instruction::IK_LOAD ||
      instruction.kind == Instruction::IK_STORE))
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RSI) | register_mask(XR_RDI) |
      register_mask(XR_R11);
  if(wide && instruction.kind == Instruction::IK_BINARY)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_RDI) | register_mask(XR_R10) |
      register_mask(XR_R11);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op == "div" || instruction.op == "mod" ||
      instruction.op == "udiv" || instruction.op == "umod"))
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op == "shl" || instruction.op == "shr" ||
      instruction.op == "ushr"))
    return register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_INDEX &&
     instruction.second.kind != Operand::OP_INTEGER)
    return direct_memory_index ? 0 : register_mask(XR_RDX);
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

bool direct_memory_index_use(const FunctionFacts & facts,
                             const std::vector<Instruction> & instructions,
                             std::size_t index)
{
  if(index + 1 >= instructions.size()) return false;
  const Instruction & instruction = instructions[index];
  if(instruction.kind != Instruction::IK_INDEX ||
     facts.edge_live.count(instruction.dest)) return false;
  const std::unordered_map<std::string, std::size_t>::const_iterator uses =
    facts.uses.find(instruction.dest);
  if(uses == facts.uses.end() || uses->second != 1) return false;
  const std::size_t scale = instruction.type.storage_size;
  if(scale != 1 && scale != 2 && scale != 4 && scale != 8) return false;
  const Instruction & consumer = instructions[index + 1];
  if(consumer.type.kind == lowir_model::LTK_F32 ||
     consumer.type.kind == lowir_model::LTK_F64 ||
     consumer.type.kind == lowir_model::LTK_F80 ||
     consumer.type.kind == lowir_model::LTK_I128 ||
     consumer.type.kind == lowir_model::LTK_OBJECT) return false;
  const bool load = consumer.kind == Instruction::IK_LOAD &&
    consumer.first.kind == Operand::OP_TEMP &&
    consumer.first.text == instruction.dest;
  const bool store = consumer.kind == Instruction::IK_STORE &&
    consumer.second.kind == Operand::OP_TEMP &&
    consumer.second.text == instruction.dest;
  return load || store;
}

bool copy_may_share_frame_home(const Instruction & instruction)
{
  return instruction.kind == Instruction::IK_COPY &&
    instruction.first.kind == Operand::OP_TEMP &&
    ((instruction.type.kind >= lowir_model::LTK_I1 &&
      instruction.type.kind <= lowir_model::LTK_I64) ||
     instruction.type.kind == lowir_model::LTK_PTR);
}

void extend_shared_storage_liveness(
    FunctionFacts & facts, const lowir_model::LowirFunction & function)
{
  for(std::size_t block = function.blocks.size(); block-- > 0; ) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    for(std::size_t index = instructions.size(); index-- > 0; ) {
      const Instruction & copy = instructions[index];
      if(!copy_may_share_frame_home(copy)) continue;
      std::size_t end = 0;
      const std::unordered_map<std::string, std::size_t>::const_iterator last =
        facts.last_use.find(copy.dest);
      if(last != facts.last_use.end()) end = last->second;
      const std::unordered_map<std::string, std::size_t>::const_iterator shared =
        facts.shared_storage_last_use.find(copy.dest);
      if(shared != facts.shared_storage_last_use.end())
        end = std::max(end, shared->second);
      const std::unordered_map<std::string, std::size_t>::const_iterator source =
        facts.last_use.find(copy.first.text);
      if(source != facts.last_use.end() && end > source->second) {
        std::size_t & extended =
          facts.shared_storage_last_use[copy.first.text];
        extended = std::max(extended, end);
      }
    }
  }
}

void extend_loop_liveness(FunctionFacts & facts,
    const lowir_model::LowirFunction & function,
    const std::unordered_set<std::string> & parameter_names,
    const std::unordered_map<std::string, std::size_t> & definition_blocks,
    const std::vector<std::pair<std::string, std::size_t> > & block_uses,
    const std::vector<std::size_t> & block_first_position,
    const std::vector<std::size_t> & block_last_position,
    const std::vector<std::vector<std::size_t> > & clobber_positions)
{
  const std::size_t block_count = function.blocks.size();
  const std::size_t no_block = static_cast<std::size_t>(-1);
  std::vector<std::size_t> block_index(function.next_block_id, no_block);
  for(std::size_t i = 0; i < block_count; ++i)
    block_index[function.blocks[i].id] = i;
  std::vector<std::vector<std::size_t> > loop_ends_at(block_count);
  for(std::size_t i = 0; i < block_count; ++i) {
    if(function.blocks[i].instructions.empty()) continue;
    const Instruction & terminal = function.blocks[i].instructions.back();
    std::vector<lowir_model::BlockId> targets;
    if(terminal.kind == Instruction::IK_JUMP) targets.push_back(terminal.first.block);
    else if(terminal.kind == Instruction::IK_BRANCH) {
      targets.push_back(terminal.second.block);
      targets.push_back(terminal.third.block);
    } else if(terminal.kind == Instruction::IK_SWITCH) {
      targets.push_back(terminal.second.block);
      for(std::size_t k = 1; k < terminal.args.size(); k += 2)
        targets.push_back(terminal.args[k].block);
    }
    for(std::size_t j = 0; j < targets.size(); ++j) {
      const std::uint32_t id = targets[j];
      if(id < block_index.size() && block_index[id] != no_block &&
         block_index[id] <= i)
        loop_ends_at[block_index[id]].push_back(i);
    }
  }
  // A nested loop's blocks may follow the outer loop's textual backedge.
  // Merge overlapping source-order loop intervals so values defined outside
  // the entire cyclic region remain live through those later blocks.  Each
  // block and backedge is visited a constant number of times.
  std::vector<std::size_t> loop_region_start(block_count, block_count);
  std::vector<std::size_t> loop_region_end(block_count, 0);
  for(std::size_t start = 0; start < block_count; ) {
    if(loop_ends_at[start].empty()) {
      ++start;
      continue;
    }
    std::size_t end = *std::max_element(
      loop_ends_at[start].begin(), loop_ends_at[start].end());
    for(std::size_t scan = start + 1; scan <= end; ++scan)
      if(!loop_ends_at[scan].empty())
        end = std::max(end, *std::max_element(
          loop_ends_at[scan].begin(), loop_ends_at[scan].end()));
    for(std::size_t block = start; block <= end; ++block) {
      loop_region_start[block] = start;
      loop_region_end[block] = end;
    }
    start = end + 1;
  }
  std::vector<std::vector<std::size_t> > call_loop_ends_at(block_count);
  for(std::size_t start = 0; start < block_count; ++start)
    for(std::size_t i = 0; i < loop_ends_at[start].size(); ++i) {
      const std::size_t end = loop_ends_at[start][i];
      const std::vector<std::size_t>::const_iterator call = std::lower_bound(
        facts.calls.begin(), facts.calls.end(), block_first_position[start]);
      if(call != facts.calls.end() && *call <= block_last_position[end])
        call_loop_ends_at[start].push_back(end);
    }
  std::vector<std::size_t> loop_end_for_block(block_count, 0);
  std::vector<std::size_t> loop_start_for_block(block_count, block_count);
  std::vector<std::size_t> call_loop_start_for_block(block_count, block_count);
  std::vector<std::vector<std::size_t> > loop_starts_expiring_at(
    block_count + 1);
  std::vector<std::vector<std::size_t> > call_loop_starts_expiring_at(
    block_count + 1);
  std::multiset<std::size_t> active_loop_starts;
  std::multiset<std::size_t> active_call_loop_starts;
  std::priority_queue<std::size_t> active_loop_ends;
  for(std::size_t block = 0; block < block_count; ++block) {
    for(std::size_t i = 0; i < loop_starts_expiring_at[block].size(); ++i) {
      const std::multiset<std::size_t>::iterator active =
        active_loop_starts.find(loop_starts_expiring_at[block][i]);
      if(active != active_loop_starts.end()) active_loop_starts.erase(active);
    }
    for(std::size_t i = 0;
        i < call_loop_starts_expiring_at[block].size(); ++i) {
      const std::multiset<std::size_t>::iterator active =
        active_call_loop_starts.find(call_loop_starts_expiring_at[block][i]);
      if(active != active_call_loop_starts.end())
        active_call_loop_starts.erase(active);
    }
    for(std::size_t i = 0; i < loop_ends_at[block].size(); ++i) {
      active_loop_ends.push(loop_ends_at[block][i]);
      active_loop_starts.insert(block);
      if(loop_ends_at[block][i] + 1 < loop_starts_expiring_at.size())
        loop_starts_expiring_at[loop_ends_at[block][i] + 1].push_back(block);
    }
    for(std::size_t i = 0; i < call_loop_ends_at[block].size(); ++i) {
      active_call_loop_starts.insert(block);
      if(call_loop_ends_at[block][i] + 1 <
         call_loop_starts_expiring_at.size())
        call_loop_starts_expiring_at[call_loop_ends_at[block][i] + 1].
          push_back(block);
    }
    while(!active_loop_ends.empty() && active_loop_ends.top() < block)
      active_loop_ends.pop();
    loop_end_for_block[block] = active_loop_ends.empty() ? block :
      active_loop_ends.top();
    if(!active_loop_starts.empty())
      loop_start_for_block[block] = *active_loop_starts.rbegin();
    if(!active_call_loop_starts.empty())
      call_loop_start_for_block[block] = *active_call_loop_starts.rbegin();
  }
  for(std::size_t i = 0; i < block_uses.size(); ++i) {
    const std::size_t use_block = block_uses[i].second;
    const std::size_t loop_end = loop_end_for_block[use_block];
    const std::unordered_map<std::string, std::size_t>::const_iterator definition =
      definition_blocks.find(block_uses[i].first);
    const bool parameter = parameter_names.count(block_uses[i].first) != 0;
    const bool loop_invariant =
      loop_start_for_block[use_block] != block_count &&
      definition != definition_blocks.end() &&
      (parameter || definition->second < loop_start_for_block[use_block]);
    const bool region_invariant =
      loop_region_start[use_block] != block_count &&
      definition != definition_blocks.end() &&
      (parameter || definition->second < loop_region_start[use_block]);
    if(loop_invariant || region_invariant) {
      facts.loop_invariant_values.insert(block_uses[i].first);
      facts.last_use[block_uses[i].first] = std::max(
        facts.last_use[block_uses[i].first],
        block_last_position[region_invariant ?
          std::max(loop_end, loop_region_end[use_block]) : loop_end]);
    }
    if(call_loop_start_for_block[use_block] != block_count &&
       definition != definition_blocks.end() &&
       (parameter ||
        definition->second < call_loop_start_for_block[use_block]))
      facts.live_across_call.insert(block_uses[i].first);
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
}

}  // namespace

unsigned register_mask(X64Register reg)
{
  return 1u << static_cast<unsigned>(reg);
}

bool crosses_register_clobber(const FunctionFacts & facts,
                              const std::string & name, X64Register reg)
{
  const std::unordered_map<std::string, unsigned>::const_iterator found =
    facts.live_across_clobbers.find(name);
  return found != facts.live_across_clobbers.end() &&
    (found->second & register_mask(reg)) != 0;
}

bool register_was_clobbered_before(const FunctionFacts & facts,
                                   X64Register reg, std::size_t position)
{
  const std::size_t index = static_cast<std::size_t>(reg);
  return index < facts.first_register_clobber.size() &&
    facts.first_register_clobber[index] < position;
}

FunctionFacts analyze_function(const lowir_model::LowirFunction & function)
{
  FunctionFacts facts;
  facts.first_register_clobber.assign(
    16, std::numeric_limits<std::size_t>::max());
  std::unordered_set<std::string> call_arguments;
  std::unordered_set<std::string> other_uses;
  std::unordered_set<std::string> parameter_names;
  std::unordered_map<std::string, std::size_t> definition_blocks;
  std::vector<std::pair<std::string, std::size_t> > block_uses;
  std::vector<std::size_t> block_first_position(function.blocks.size(), 0);
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
    block_first_position[i] = position;
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
      if(instruction.kind == Instruction::IK_INDEX &&
         instruction.first.kind == Operand::OP_TEMP &&
         parameter_names.count(instruction.first.text) &&
         facts.uses.find(instruction.first.text) != facts.uses.end() &&
         facts.uses.find(instruction.first.text)->second == 1)
        facts.sole_index_bases.insert(instruction.first.text);
      const bool direct_index = direct_memory_index_use(facts, instructions, j);
      if(direct_index)
        facts.direct_memory_index_values.insert(instruction.dest);
      const unsigned clobbers =
        instruction_clobber_mask(instruction, direct_index);
      for(std::size_t reg = 0; reg < clobber_positions.size(); ++reg)
        if(clobbers & (1u << reg)) {
          clobber_positions[reg].push_back(position);
          if(facts.first_register_clobber[reg] ==
             std::numeric_limits<std::size_t>::max())
            facts.first_register_clobber[reg] = position;
        }
      if(instruction.kind != Instruction::IK_BRANCH ||
         instruction.first.kind != Operand::OP_TEMP) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator branch_definition =
        definitions.find(instruction.first.text);
      bool return_register_preserved = branch_definition != definitions.end();
      if(return_register_preserved) {
        const std::vector<std::size_t> & rax_clobbers =
          clobber_positions[static_cast<unsigned>(XR_RAX)];
        const std::vector<std::size_t>::const_iterator clobber =
          std::lower_bound(rax_clobbers.begin(), rax_clobbers.end(),
                           block_start + branch_definition->second + 1);
        return_register_preserved =
          clobber == rax_clobbers.end() || *clobber >= block_start + j;
      }
      if(branch_definition != definitions.end() &&
         instructions[branch_definition->second].kind == Instruction::IK_CALL &&
         facts.uses.find(instruction.first.text) != facts.uses.end() &&
         facts.uses.find(instruction.first.text)->second == 1 &&
         return_register_preserved)
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
                instructions[definition->second + 1].dest == source.second.text &&
                (instructions[definition->second + 1].first.kind ==
                   Operand::OP_SLOT ||
                 instructions[definition->second + 1].first.kind ==
                   Operand::OP_GLOBAL))
          facts.direct_compare_rax_values.insert(producer.dest);
      }
      if(comparison->second + 1 == j) continue;
      facts.deferred_branch_comparisons[instruction.first.text] = &source;
      for(std::size_t operand = 0; operand < 2; ++operand) {
        if(operands[operand]->kind != Operand::OP_TEMP) continue;
        const std::string & name = operands[operand]->text;
        facts.last_use[name] = std::max(facts.last_use[name], block_start + j);
        for(std::size_t k = comparison->second + 1; k < j; ++k) {
          const unsigned clobbers = instruction_clobber_mask(
            instructions[k],
            facts.direct_memory_index_values.count(instructions[k].dest));
          facts.live_across_clobbers[name] |= clobbers;
          if(instructions[k].kind == Instruction::IK_CALL)
            facts.live_across_call.insert(name);
        }
      }
    }
  }

  // LowIR validation gives every temporary one definition before its uses.
  // Model those values as linearized live intervals, extending uses through
  // source-order loop ranges.  Per-register clobber indexes then answer each
  // interval query without materializing the values x blocks liveness
  // relation.  The register set is fixed at 16, so this remains O(IR log IR)
  // time and O(IR) storage for instructions, operands, blocks, and CFG edges.
  extend_loop_liveness(facts, function, parameter_names, definition_blocks,
    block_uses, block_first_position, block_last_position, clobber_positions);
  extend_shared_storage_liveness(facts, function);
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
  facts.promoted_parameter_clobbers.assign(function.params.size(), 0);
  std::unordered_map<std::string, std::size_t> parameters_by_name;
  std::unordered_map<std::string, std::size_t> parameters_by_suffix;
  parameters_by_name.reserve(function.params.size());
  parameters_by_suffix.reserve(function.params.size());
  for(std::size_t p = 0; p < function.params.size(); ++p) {
    parameters_by_name[function.params[p].name] = p;
    if(function.params[p].name.size() >= 2)
      parameters_by_suffix[function.params[p].name.substr(1)] = p;
  }

  const std::size_t no_index = std::numeric_limits<std::size_t>::max();
  enum SlotFlag {
    SF_WRITTEN = 1,
    SF_OBSERVED = 2,
    SF_OBJECT_SEEN = 4
  };
  std::unordered_map<std::string, std::size_t> slots_by_name;
  slots_by_name.reserve(function.slots.size());
  std::vector<unsigned char> slot_flags(function.slots.size(), 0);
  std::vector<std::size_t> object_slot_parameters(function.slots.size(), no_index);
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const std::string & slot = function.slots[s].first;
    slots_by_name[slot] = s;
    if(function.slots[s].second.kind == lowir_model::LTK_OBJECT) {
      if(slot.size() < 2) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator parameter =
        parameters_by_suffix.find(slot.substr(1));
      if(parameter != parameters_by_suffix.end() &&
         lowir_model::same_lowir_type(function.params[parameter->second].type,
                                      function.slots[s].second))
        object_slot_parameters[s] = parameter->second;
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
    std::size_t parameter = no_index;
    const std::string * loaded_name = 0;
    unsigned intervening_clobbers = 0;
  };
  std::vector<std::size_t> scalar_state_indexes(function.slots.size(), no_index);
  std::vector<ScalarSlotState> scalar_states;
  if(function.blocks.size() == 1) {
    scalar_states.reserve(function.slots.size());
    for(std::size_t s = 0; s < function.slots.size(); ++s)
      if(function.slots[s].second.kind != lowir_model::LTK_OBJECT) {
        scalar_state_indexes[s] = scalar_states.size();
        scalar_states.push_back(ScalarSlotState());
      }
  }
  std::array<std::size_t, 16> last_clobber;
  last_clobber.fill(std::numeric_limits<std::size_t>::max());

  for(std::size_t b = 0; b < function.blocks.size(); ++b) {
    const std::vector<Instruction> & instructions = function.blocks[b].instructions;
    for(std::size_t i = 0; i < instructions.size(); ++i) {
      const Instruction & instruction = instructions[i];
      const Operand * operands[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      std::size_t operand_slots[] = {no_index, no_index, no_index};
      for(std::size_t k = 0; k < sizeof(operands) / sizeof(operands[0]); ++k) {
        if(operands[k]->kind != Operand::OP_SLOT) continue;
        const std::unordered_map<std::string, std::size_t>::const_iterator slot =
          slots_by_name.find(operands[k]->text);
        if(slot != slots_by_name.end()) operand_slots[k] = slot->second;
      }
      const bool has_dead_store_candidate =
        instruction.kind == Instruction::IK_STORE &&
        operand_slots[1] != no_index &&
        operand_slots[0] != operand_slots[1] &&
        operand_slots[2] != operand_slots[1];
      if(has_dead_store_candidate)
        slot_flags[operand_slots[1]] |= SF_WRITTEN;
      for(std::size_t k = 0; k < sizeof(operand_slots) / sizeof(operand_slots[0]); ++k)
        if(operand_slots[k] != no_index &&
           (!has_dead_store_candidate || operand_slots[k] != operand_slots[1]))
          slot_flags[operand_slots[k]] |= SF_OBSERVED;
      for(std::size_t k = 0; k < instruction.args.size(); ++k) {
        if(instruction.args[k].kind != Operand::OP_SLOT) continue;
        const std::unordered_map<std::string, std::size_t>::const_iterator slot =
          slots_by_name.find(instruction.args[k].text);
        if(slot != slots_by_name.end() &&
           (!has_dead_store_candidate || slot->second != operand_slots[1]))
          slot_flags[slot->second] |= SF_OBSERVED;
      }
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

      std::size_t mentioned[3];
      std::size_t mentioned_count = 0;
      for(std::size_t k = 0; k < sizeof(operands) / sizeof(operands[0]); ++k) {
        if(operand_slots[k] == no_index) continue;
        bool duplicate = false;
        for(std::size_t m = 0; m < mentioned_count; ++m)
          if(mentioned[m] == operand_slots[k]) duplicate = true;
        if(duplicate) continue;
        mentioned[mentioned_count++] = operand_slots[k];

        const std::size_t slot_index = operand_slots[k];
        const std::size_t object_parameter = object_slot_parameters[slot_index];
        if(object_parameter != no_index &&
           !(slot_flags[slot_index] & SF_OBJECT_SEEN)) {
          slot_flags[slot_index] |= SF_OBJECT_SEEN;
          if(instruction.kind == Instruction::IK_ADDR &&
             i + 1 < instructions.size()) {
            const lowir_model::LowirParameter & parameter =
              function.params[object_parameter];
            const Instruction & copy = instructions[i + 1];
            if(copy.kind == Instruction::IK_COPYOBJ &&
               copy.first.kind == Operand::OP_TEMP && copy.first.text == parameter.name &&
               copy.second.kind == Operand::OP_TEMP && copy.second.text == instruction.dest &&
               copy.byte_count == parameter.type.storage_size)
              facts.parameter_slot_aliases[function.slots[slot_index].first] =
                parameter.name;
          }
        }

        if(scalar_state_indexes[slot_index] == no_index) continue;
        ScalarSlotState & state = scalar_states[scalar_state_indexes[slot_index]];
        const bool first = operand_slots[0] == slot_index;
        const bool second = operand_slots[1] == slot_index;
        const bool third = operand_slots[2] == slot_index;
        if(!state.initialized && instruction.kind == Instruction::IK_STORE && second &&
           instruction.first.kind == Operand::OP_TEMP) {
          const std::unordered_map<std::string, std::size_t>::const_iterator parameter =
            parameters_by_name.find(instruction.first.text);
          if(parameter != parameters_by_name.end() &&
             lowir_model::same_lowir_type(function.params[parameter->second].type,
                                          function.slots[slot_index].second)) {
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
          state.loaded_name = &instruction.dest;
          const std::unordered_map<std::string, unsigned>::const_iterator
            result_clobbers =
              function_facts.live_across_clobbers.find(instruction.dest);
          if(result_clobbers != function_facts.live_across_clobbers.end())
            state.intervening_clobbers |= result_clobbers->second;
          for(std::size_t reg = 0; reg < last_clobber.size(); ++reg)
            if(last_clobber[reg] != std::numeric_limits<std::size_t>::max() &&
               last_clobber[reg] > state.store_position)
              state.intervening_clobbers |= 1u << reg;
        } else if(first || second || third) {
          state.valid = false;
        }
      }
      const unsigned clobbers = instruction_clobber_mask(
        instruction,
        function_facts.direct_memory_index_values.count(instruction.dest));
      for(std::size_t reg = 0; reg < last_clobber.size(); ++reg)
        if(clobbers & (1u << reg)) last_clobber[reg] = i;
    }
  }

  for(std::size_t s = 0; s < function.slots.size(); ++s)
    if((slot_flags[s] & SF_WRITTEN) && !(slot_flags[s] & SF_OBSERVED))
      facts.dead_store_slots.insert(function.slots[s].first);

  std::unordered_map<std::string, std::size_t> loaded_slots;
  loaded_slots.reserve(scalar_states.size());
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    if(scalar_state_indexes[s] == no_index) continue;
    const ScalarSlotState & state = scalar_states[scalar_state_indexes[s]];
    if(state.valid && state.initialized && state.loaded)
      loaded_slots[*state.loaded_name] = s;
  }
  for(std::size_t i = 0; i < (function.blocks.empty() ? 0 :
      function.blocks[0].instructions.size()); ++i) {
    const Instruction & instruction = function.blocks[0].instructions[i];
    const Operand * operands[] = {
      &instruction.first, &instruction.second, &instruction.third
    };
    for(std::size_t k = 0; k < sizeof(operands) / sizeof(operands[0]); ++k) {
      if(operands[k]->kind != Operand::OP_TEMP) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator slot =
        loaded_slots.find(operands[k]->text);
      if(slot != loaded_slots.end() &&
         !(instruction.kind == Instruction::IK_LOAD && k == 0))
        scalar_states[scalar_state_indexes[slot->second]].read_through = false;
    }
  }
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    if(scalar_state_indexes[s] == no_index) continue;
    const ScalarSlotState & state = scalar_states[scalar_state_indexes[s]];
    if(!state.valid || !state.initialized || !state.loaded ||
       (!function_facts.calls.empty() && state.load_count != 1))
      continue;
    const std::string & parameter = function.params[state.parameter].name;
    if(!function_facts.calls.empty() && function_facts.uses.find(parameter)->second != 1)
      continue;
    if(state.read_through)
      facts.promoted_parameter_slots[function.slots[s].first] = parameter;
    else
      facts.forwarded_parameter_slots[function.slots[s].first] = parameter;
    facts.promoted_parameters.insert(parameter);
    facts.promoted_parameter_clobbers[state.parameter] |=
      state.intervening_clobbers;
    const std::vector<std::size_t>::const_iterator call = std::upper_bound(
      function_facts.calls.begin(), function_facts.calls.end(),
      state.store_position);
    if(function_facts.live_across_call.count(*state.loaded_name) ||
       (call != function_facts.calls.end() &&
        *call < state.load_position))
      facts.promoted_parameters_across_call.insert(parameter);
  }
  return facts;
}

}  // namespace analysis
}  // namespace lowir_native
