#include "lowir_native_analysis.h"

#include "lowir_native.h"
#include "lowir_native_forward_edge_analysis.h"

#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <set>

namespace lowir_native {
namespace analysis {
namespace {

using lowir_model::Instruction;
using lowir_model::LowOperation;
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
  if(instruction.kind == Instruction::IK_SELECT)
    return register_mask(XR_RAX) | register_mask(XR_RDX) |
      register_mask(XR_R10) | register_mask(XR_R11);
  if(wide && instruction.kind == Instruction::IK_CONVERT)
    return register_mask(XR_RAX) | register_mask(XR_RDX);
  if(wide && instruction.kind == Instruction::IK_BINARY)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_RDI) | register_mask(XR_R10) |
      register_mask(XR_R11);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op.kind == LowOperation::LOP_DIV || instruction.op.kind == LowOperation::LOP_MOD ||
      instruction.op.kind == LowOperation::LOP_UDIV || instruction.op.kind == LowOperation::LOP_UMOD))
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op.kind == LowOperation::LOP_SHL || instruction.op.kind == LowOperation::LOP_SHR ||
      instruction.op.kind == LowOperation::LOP_USHR))
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
  const lowir_model::ValueId value = operand.value;
  ++facts.uses[value];
  facts.first_use[value] = std::min(facts.first_use[value], position);
  if(facts.last_use[value] == FunctionFacts::missing_position())
    facts.last_use[value] = position;
  else facts.last_use[value] = std::max(facts.last_use[value], position);
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
     facts.has(instruction.dest, FunctionFacts::VF_EDGE_LIVE)) return false;
  if(facts.uses[instruction.dest] != 1) return false;
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
    consumer.first.value == instruction.dest;
  const bool store = consumer.kind == Instruction::IK_STORE &&
    consumer.second.kind == Operand::OP_TEMP &&
    consumer.second.value == instruction.dest;
  return load || store;
}

bool scalar_identity_may_share_frame_home(const Instruction & instruction)
{
  if(instruction.first.kind != Operand::OP_TEMP ||
     !((instruction.type.kind >= lowir_model::LTK_I1 &&
        instruction.type.kind <= lowir_model::LTK_I64) ||
       instruction.type.kind == lowir_model::LTK_PTR))
    return false;
  return instruction.kind == Instruction::IK_COPY ||
    (instruction.kind == Instruction::IK_UNARY &&
     instruction.op.kind == lowir_model::LowOperation::LOP_DECAY);
}

void extend_shared_storage_liveness(
    FunctionFacts & facts, const lowir_model::LowirFunction & function)
{
  for(std::size_t block = function.blocks.size(); block-- > 0; ) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    for(std::size_t index = instructions.size(); index-- > 0; ) {
      const Instruction & identity = instructions[index];
      if(!scalar_identity_may_share_frame_home(identity)) continue;
      std::size_t end = facts.last_use[identity.dest] ==
          FunctionFacts::missing_position() ? 0 : facts.last_use[identity.dest];
      if(facts.shared_storage_last_use[identity.dest] !=
         FunctionFacts::missing_position())
        end = std::max(end, facts.shared_storage_last_use[identity.dest]);
      const lowir_model::ValueId source = identity.first.value;
      if(facts.last_use[source] != FunctionFacts::missing_position() &&
         end > facts.last_use[source]) {
        std::size_t & extended = facts.shared_storage_last_use[source];
        if(extended == FunctionFacts::missing_position()) extended = 0;
        extended = std::max(extended, end);
      }
    }
  }
}

void extend_loop_liveness(FunctionFacts & facts,
    const lowir_model::LowirFunction & function,
    const std::vector<std::size_t> & definition_blocks,
    const std::vector<std::pair<lowir_model::ValueId, std::size_t> > & block_uses,
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
    const lowir_model::ValueId value = block_uses[i].first;
    const std::size_t definition = definition_blocks[value];
    const bool parameter = facts.has(value, FunctionFacts::VF_PARAMETER);
    const bool loop_invariant =
      loop_start_for_block[use_block] != block_count &&
      definition != no_block &&
      (parameter || definition < loop_start_for_block[use_block]);
    const bool region_invariant =
      loop_region_start[use_block] != block_count &&
      definition != no_block &&
      (parameter || definition < loop_region_start[use_block]);
    if(loop_invariant || region_invariant) {
      facts.mark(value, FunctionFacts::VF_LOOP_INVARIANT);
      facts.last_use[value] = std::max(facts.last_use[value],
        block_last_position[region_invariant ?
          std::max(loop_end, loop_region_end[use_block]) : loop_end]);
    }
    if(call_loop_start_for_block[use_block] != block_count &&
       definition != no_block &&
       (parameter ||
        definition < call_loop_start_for_block[use_block]))
      facts.mark(value, FunctionFacts::VF_LIVE_ACROSS_CALL);
  }
  for(std::size_t raw_value = 0; raw_value < facts.last_use.size(); ++raw_value) {
    const lowir_model::ValueId value(static_cast<std::uint32_t>(raw_value));
    if(facts.has(value, FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE)) {
      const std::size_t block = definition_blocks[value];
      const std::size_t first_use = facts.first_use[value];
      const bool intact = facts.uses[value] > 1 && block != no_block &&
        first_use != FunctionFacts::missing_position() &&
        first_use > facts.definition[value] &&
        first_use <= block_last_position[block];
      if(!intact)
        facts.value_flags[value] &=
          ~FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE;
    }
    if(facts.last_use[value] == FunctionFacts::missing_position() ||
       facts.definition[value] == FunctionFacts::missing_position()) continue;
    const std::size_t start = facts.has(value, FunctionFacts::VF_PARAMETER) ? 0 :
      facts.definition[value] + 1;
    const std::size_t end = facts.last_use[value];
    if(start >= end) continue;
    unsigned mask = 0;
    for(std::size_t reg = 0; reg < clobber_positions.size(); ++reg) {
      const std::vector<std::size_t>::const_iterator clobber = std::lower_bound(
        clobber_positions[reg].begin(), clobber_positions[reg].end(), start);
      if(reg == static_cast<std::size_t>(XR_RAX) &&
         facts.has(value, FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE) &&
         clobber != clobber_positions[reg].end() &&
         *clobber < facts.first_use[value])
        facts.value_flags[value] &=
          ~FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE;
      if(clobber != clobber_positions[reg].end() && *clobber < end)
        mask |= 1u << reg;
    }
    if(mask) facts.live_across_clobbers[value] |= mask;
    const std::vector<std::size_t>::const_iterator call = std::lower_bound(
      facts.calls.begin(), facts.calls.end(), start);
    if(call != facts.calls.end() && *call < end)
      facts.mark(value, FunctionFacts::VF_LIVE_ACROSS_CALL);
  }
  for(std::size_t i = 0; i < block_count; ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      if(instruction.kind == Instruction::IK_INDEX &&
         instruction.first.kind == Operand::OP_TEMP &&
         facts.has(instruction.first.value, FunctionFacts::VF_PARAMETER) &&
         facts.has(instruction.dest, FunctionFacts::VF_LIVE_ACROSS_CALL))
        facts.mark(instruction.first.value,
                   FunctionFacts::VF_FORWARDED_PARAMETER_ACROSS_CALL);
    }
}

void note_instruction_uses(
    FunctionFacts * facts, const Instruction & instruction,
    std::size_t position, std::size_t block,
    const std::vector<std::size_t> & block_index,
    const std::vector<std::size_t> & block_last_position,
    const std::vector<std::size_t> & definition_blocks,
    std::vector<std::pair<lowir_model::ValueId, std::size_t> > * block_uses,
    std::vector<unsigned char> * call_arguments,
    std::vector<unsigned char> * other_uses)
{
  if(instruction.kind != Instruction::IK_PHI)
    note_instruction_operands(*facts, instruction, position);
  else for(std::size_t i = 1; i < instruction.args.size(); i += 2) {
    const Operand & operand = instruction.args[i];
    if(operand.kind != Operand::OP_TEMP) continue;
    const std::uint32_t predecessor_id = instruction.args[i - 1].block;
    if(predecessor_id >= block_index.size() ||
       block_index[predecessor_id] == FunctionFacts::missing_position())
      continue;
    const std::size_t predecessor = block_index[predecessor_id];
    const lowir_model::ValueId value = operand.value;
    note_operand(*facts, operand, block_last_position[predecessor]);
    block_uses->push_back(std::make_pair(value, predecessor));
    (*other_uses)[value] = 1;
    if(definition_blocks[value] != predecessor)
      facts->mark(value, FunctionFacts::VF_EDGE_LIVE);
  }
  const Operand * fixed[] = {
    &instruction.first, &instruction.second, &instruction.third
  };
  for(std::size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
    if(fixed[i]->kind == Operand::OP_TEMP) {
      const lowir_model::ValueId value = fixed[i]->value;
      (*other_uses)[value] = 1;
      block_uses->push_back(std::make_pair(value, block));
      if(definition_blocks[value] != FunctionFacts::missing_position() &&
         definition_blocks[value] != block)
        facts->mark(value, FunctionFacts::VF_EDGE_LIVE);
    }
  for(std::size_t i = 0;
      instruction.kind != Instruction::IK_PHI &&
      i < instruction.args.size(); ++i)
    if(instruction.args[i].kind == Operand::OP_TEMP) {
      const lowir_model::ValueId value = instruction.args[i].value;
      block_uses->push_back(std::make_pair(value, block));
      if(definition_blocks[value] != FunctionFacts::missing_position() &&
         definition_blocks[value] != block)
        facts->mark(value, FunctionFacts::VF_EDGE_LIVE);
      if(instruction.kind == Instruction::IK_CALL)
        (*call_arguments)[value] = 1;
      else (*other_uses)[value] = 1;
    }
}

void note_storage_address_uses(
    const Instruction & instruction,
    std::vector<unsigned char> * address_uses,
    std::vector<unsigned char> * other_uses)
{
  const bool first_is_address =
    instruction.kind == Instruction::IK_LOAD ||
    instruction.kind == Instruction::IK_INDEX ||
    instruction.kind == Instruction::IK_COPYOBJ ||
    instruction.kind == Instruction::IK_ZEROINIT;
  const bool second_is_address =
    instruction.kind == Instruction::IK_STORE ||
    instruction.kind == Instruction::IK_COPYOBJ;
  const Operand * fixed[] = {
    &instruction.first, &instruction.second, &instruction.third
  };
  const bool address_role[] = {first_is_address, second_is_address, false};
  for(std::size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
    if(fixed[i]->kind != Operand::OP_TEMP) continue;
    if(address_role[i]) (*address_uses)[fixed[i]->value] = 1;
    else (*other_uses)[fixed[i]->value] = 1;
  }
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    if(instruction.args[i].kind == Operand::OP_TEMP)
      (*other_uses)[instruction.args[i].value] = 1;
}

}  // namespace

unsigned register_mask(X64Register reg)
{
  return 1u << static_cast<unsigned>(reg);
}

bool crosses_register_clobber(const FunctionFacts & facts,
                              lowir_model::ValueId value, X64Register reg)
{
  return (facts.live_across_clobbers[value] & register_mask(reg)) != 0;
}

bool register_was_clobbered_before(const FunctionFacts & facts,
                                   X64Register reg, std::size_t position)
{
  const std::size_t index = static_cast<std::size_t>(reg);
  return index < facts.first_register_clobber.size() &&
    facts.first_register_clobber[index] < position;
}

FunctionFacts analyze_function(const lowir_model::LowirFunction & function,
                               Stats * stats)
{
  FunctionFacts facts;
  const std::size_t value_count = function.value_names.size();
  facts.uses.assign(value_count, 0);
  facts.first_use.assign(value_count, FunctionFacts::missing_position());
  facts.last_use.assign(value_count, FunctionFacts::missing_position());
  facts.shared_storage_last_use.assign(
    value_count, FunctionFacts::missing_position());
  facts.definition.assign(value_count, FunctionFacts::missing_position());
  facts.value_flags.assign(value_count, 0);
  facts.deferred_branch_comparisons.assign(value_count, 0);
  facts.live_across_clobbers.assign(value_count, 0);
  facts.first_register_clobber.assign(
    16, std::numeric_limits<std::size_t>::max());
  std::vector<unsigned char> call_arguments(value_count, 0);
  std::vector<unsigned char> other_uses(value_count, 0);
  std::vector<unsigned char> storage_address_uses(value_count, 0);
  std::vector<unsigned char> non_storage_address_uses(value_count, 0);
  std::vector<std::size_t> definition_blocks(
    value_count, FunctionFacts::missing_position());
  std::vector<std::pair<lowir_model::ValueId, std::size_t> > block_uses;
  std::vector<std::size_t> block_first_position(function.blocks.size(), 0);
  std::vector<std::size_t> block_last_position(function.blocks.size(), 0);
  std::vector<std::vector<std::size_t> > clobber_positions(16);
  std::vector<std::size_t> block_index(function.next_block_id,
                                       FunctionFacts::missing_position());
  std::size_t position = 0;
  for(std::size_t i = 0; i < function.params.size(); ++i) {
    const lowir_model::ValueId value = function.params[i].value;
    facts.definition[value] = 0;
    facts.mark(value, FunctionFacts::VF_PARAMETER);
    definition_blocks[value] = 0;
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    block_index[function.blocks[i].id] = i;
    block_first_position[i] = position;
    for(std::size_t j = 0; j < function.blocks[i].instructions.size();
        ++j, ++position) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      if(!instruction.dest.valid()) continue;
      facts.definition[instruction.dest] = position;
      definition_blocks[instruction.dest] = i;
    }
    block_last_position[i] = position ? position - 1 : 0;
  }
  position = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j, ++position) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      note_instruction_uses(
        &facts, instruction, position, i, block_index, block_last_position,
        definition_blocks, &block_uses, &call_arguments, &other_uses);
      note_storage_address_uses(
        instruction, &storage_address_uses, &non_storage_address_uses);
      if(instruction.kind == Instruction::IK_INDEX &&
         instruction.first.kind == Operand::OP_TEMP &&
         facts.has(instruction.first.value, FunctionFacts::VF_PARAMETER) &&
         instruction.second.kind == Operand::OP_INTEGER &&
         instruction.second.has_int_value &&
		 instruction.second.int_value == 0 &&
		 instruction.second.int_high == 0)
        facts.mark(instruction.first.value,
                   FunctionFacts::VF_ZERO_INDEX_PARAMETER);
      if(instruction.kind == Instruction::IK_SWITCH &&
         instruction.first.kind == Operand::OP_TEMP &&
         facts.has(instruction.first.value, FunctionFacts::VF_PARAMETER))
        facts.mark(instruction.first.value, FunctionFacts::VF_SWITCH_PARAMETER);
      if(instruction.kind == Instruction::IK_BINARY &&
         instruction.first.kind == Operand::OP_TEMP &&
         facts.has(instruction.first.value, FunctionFacts::VF_PARAMETER))
        facts.mark(instruction.first.value,
                   FunctionFacts::VF_DESTRUCTIVE_PARAMETER);
      if(instruction.kind == Instruction::IK_CALL) facts.calls.push_back(position);
      if(instruction.kind == Instruction::IK_CALL &&
         instruction.dest.valid() && !instruction.call_returns_void &&
         (instruction.type.kind == lowir_model::LTK_I64 ||
          instruction.type.kind == lowir_model::LTK_PTR))
        facts.mark(instruction.dest,
                   FunctionFacts::VF_CALL_RESULT_RAX_FIRST_USE);
      if(instruction.kind == Instruction::IK_VA_START) facts.has_va_start = true;
      if(instruction.kind == Instruction::IK_STACK_ALLOC)
        facts.has_dynamic_stack = true;
      if((instruction.kind >= Instruction::IK_EH_TRY &&
          instruction.kind <= Instruction::IK_RESUME) ||
         instruction.kind == Instruction::IK_THROW)
        facts.has_eh = true;
      if((instruction.kind == Instruction::IK_ATOMIC_LOAD ||
          instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) &&
         instruction.type.kind == lowir_model::LTK_I128)
        facts.has_i128_atomic = true;
    }
  }
  for(std::size_t value = 0; value < value_count; ++value)
    if(call_arguments[value] && !other_uses[value])
      facts.mark(lowir_model::ValueId(static_cast<std::uint32_t>(value)),
                 FunctionFacts::VF_ONLY_CALL_ARGUMENT);
  for(std::size_t value = 0; value < value_count; ++value)
    if(storage_address_uses[value] && !non_storage_address_uses[value])
      facts.mark(lowir_model::ValueId(static_cast<std::uint32_t>(value)),
                 FunctionFacts::VF_ONLY_STORAGE_ADDRESS);

  position = 0;
  std::vector<std::size_t> comparisons(
    value_count, FunctionFacts::missing_position());
  std::vector<std::size_t> definitions(
    value_count, FunctionFacts::missing_position());
  std::vector<lowir_model::ValueId> touched_comparisons;
  std::vector<lowir_model::ValueId> touched_definitions;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::vector<Instruction> & instructions = function.blocks[i].instructions;
    for(std::size_t value = 0; value < touched_comparisons.size(); ++value)
      comparisons[touched_comparisons[value]] = FunctionFacts::missing_position();
    for(std::size_t value = 0; value < touched_definitions.size(); ++value)
      definitions[touched_definitions[value]] = FunctionFacts::missing_position();
    touched_comparisons.clear();
    touched_definitions.clear();
    const std::size_t block_start = position;
    for(std::size_t j = 0; j < instructions.size(); ++j, ++position) {
      const Instruction & instruction = instructions[j];
      if(instruction.dest.valid()) {
        definitions[instruction.dest] = j;
        touched_definitions.push_back(instruction.dest);
      }
      if(instruction.kind == Instruction::IK_CMP) {
        comparisons[instruction.dest] = j;
        touched_comparisons.push_back(instruction.dest);
      }
      if(instruction.kind == Instruction::IK_INDEX &&
         instruction.first.kind == Operand::OP_TEMP &&
         facts.has(instruction.first.value, FunctionFacts::VF_PARAMETER) &&
         facts.uses[instruction.first.value] == 1)
        facts.mark(instruction.first.value, FunctionFacts::VF_SOLE_INDEX_BASE);
      const bool direct_index = direct_memory_index_use(facts, instructions, j);
      if(direct_index)
        facts.mark(instruction.dest, FunctionFacts::VF_DIRECT_MEMORY_INDEX);
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
      const lowir_model::ValueId branch_value = instruction.first.value;
      const std::size_t branch_definition = definitions[branch_value];
      bool return_register_preserved =
        branch_definition != FunctionFacts::missing_position();
      if(return_register_preserved) {
        const std::vector<std::size_t> & rax_clobbers =
          clobber_positions[static_cast<unsigned>(XR_RAX)];
        const std::vector<std::size_t>::const_iterator clobber =
          std::lower_bound(rax_clobbers.begin(), rax_clobbers.end(),
                           block_start + branch_definition + 1);
        return_register_preserved =
          clobber == rax_clobbers.end() || *clobber >= block_start + j;
      }
      if(branch_definition != FunctionFacts::missing_position() &&
         instructions[branch_definition].kind == Instruction::IK_CALL &&
         facts.uses[branch_value] == 1 &&
         return_register_preserved)
        facts.mark(branch_value, FunctionFacts::VF_DIRECT_BRANCH_CALL_RESULT);
      const std::size_t comparison = comparisons[branch_value];
      if(comparison == FunctionFacts::missing_position() ||
         facts.uses[branch_value] != 1) continue;
      const Instruction & source = instructions[comparison];
      const Operand * operands[] = {&source.first, &source.second};
      for(std::size_t operand = 0; operand < 2; ++operand) {
        if(operands[operand]->kind == Operand::OP_TEMP) {
          facts.mark(operands[operand]->value,
                     FunctionFacts::VF_DIRECT_BRANCH_SOURCE);
          if(facts.has(operands[operand]->value, FunctionFacts::VF_PARAMETER))
            facts.has_direct_branch_parameter = true;
        }
        if(operands[operand]->kind != Operand::OP_TEMP) continue;
        const std::size_t definition = definitions[operands[operand]->value];
        if(comparison + 1 != j ||
           definition == FunctionFacts::missing_position()) continue;
        const Instruction & producer = instructions[definition];
        if(producer.kind != Instruction::IK_LOAD ||
           !lowir_model::same_lowir_type(producer.type, source.type) ||
           (producer.first.kind != Operand::OP_SLOT &&
            producer.first.kind != Operand::OP_GLOBAL) ||
           facts.uses[producer.dest] != 1) continue;
        if(definition + 1 == comparison)
          facts.mark(producer.dest, FunctionFacts::VF_DIRECT_COMPARE_STORAGE);
        else if(operand == 0 && definition + 2 == comparison &&
                instructions[definition + 1].kind == Instruction::IK_LOAD &&
                source.second.kind == Operand::OP_TEMP &&
                instructions[definition + 1].dest == source.second.value &&
                (instructions[definition + 1].first.kind ==
                   Operand::OP_SLOT ||
                 instructions[definition + 1].first.kind ==
                   Operand::OP_GLOBAL))
          facts.mark(producer.dest, FunctionFacts::VF_DIRECT_COMPARE_RAX);
      }
      if(comparison + 1 == j) continue;
      facts.deferred_branch_comparisons[branch_value] = &source;
      for(std::size_t operand = 0; operand < 2; ++operand) {
        if(operands[operand]->kind != Operand::OP_TEMP) continue;
        const lowir_model::ValueId value = operands[operand]->value;
        facts.last_use[value] = std::max(facts.last_use[value], block_start + j);
        for(std::size_t k = comparison + 1; k < j; ++k) {
          const unsigned clobbers = instruction_clobber_mask(
            instructions[k],
            facts.has(instructions[k].dest,
                      FunctionFacts::VF_DIRECT_MEMORY_INDEX));
          facts.live_across_clobbers[value] |= clobbers;
          if(instructions[k].kind == Instruction::IK_CALL)
            facts.mark(value, FunctionFacts::VF_LIVE_ACROSS_CALL);
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
  extend_loop_liveness(facts, function, definition_blocks,
    block_uses, block_first_position, block_last_position, clobber_positions);
  extend_shared_storage_liveness(facts, function);
  mark_exact_forward_edge_values(
    facts, function, definition_blocks, block_uses, stats);
  for(std::size_t value = 0; value < value_count; ++value)
    if(facts.uses[value] != 1)
      facts.value_flags[value] &= ~FunctionFacts::VF_DESTRUCTIVE_PARAMETER;
  return facts;
}

static void initialize_storage_facts(
    const lowir_model::LowirFunction & function,
    const FunctionFacts & function_facts, StorageFacts * facts,
    std::vector<std::size_t> * value_parameters, std::size_t no_index)
{
  facts->promoted_parameter_clobbers.assign(function.params.size(), 0);
  facts->parameter_selected_uses.resize(function.params.size());
  facts->parameter_slot_aliases.resize(function.slot_names.size());
  facts->promoted_parameter_slots.resize(function.slot_names.size());
  facts->forwarded_parameter_slots.resize(function.slot_names.size());
  facts->value_flags.assign(function.value_names.size(), 0);
  facts->dead_store_slots.assign(function.slot_names.size(), 0);
  value_parameters->assign(function.value_names.size(), no_index);
  for(std::size_t parameter = 0; parameter < function.params.size(); ++parameter) {
    (*value_parameters)[function.params[parameter].value] = parameter;
    facts->parameter_selected_uses[parameter] =
      function_facts.uses[function.params[parameter].value];
  }
}

StorageFacts analyze_storage(
    const lowir_model::LowirFunction & function,
    const FunctionFacts & function_facts,
    const std::vector<lowir_model::SymbolId> & tls_wrappers)
{
  StorageFacts facts;
  const std::size_t no_index = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> value_parameters;
  initialize_storage_facts(
    function, function_facts, &facts, &value_parameters, no_index);
  enum SlotFlag {
    SF_WRITTEN = 1,
    SF_OBSERVED = 2,
    SF_OBJECT_SEEN = 4
  };
  std::vector<unsigned char> slot_flags(function.slot_names.size(), 0);
  std::vector<std::size_t> object_slot_parameters(
    function.slot_names.size(), no_index);
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const lowir_model::SlotId slot_id = function.slots[s];
    const lowir_model::LowType & slot_type =
      lowir_model::lowir_slot_type(function, slot_id);
    if(slot_type.kind == lowir_model::LTK_OBJECT) {
      const lowir_model::ValueId parameter_value =
        function.slot_parameter_values[slot_id];
      if(parameter_value.valid())
		object_slot_parameters[slot_id] = value_parameters[parameter_value];
    }
  }

  struct ScalarSlotState
  {
    bool initialized = false;
    bool loaded = false;
    std::size_t load_count = 0;
    std::size_t loaded_uses = 0;
    std::size_t store_position = 0;
    std::size_t load_position = 0;
    bool valid = true;
    bool read_through = true;
    std::size_t parameter = no_index;
    lowir_model::ValueId loaded_value;
    unsigned intervening_clobbers = 0;
  };
  std::vector<std::size_t> scalar_state_indexes(
    function.slot_names.size(), no_index);
  std::vector<ScalarSlotState> scalar_states;
  if(function.blocks.size() == 1) {
    scalar_states.reserve(function.slots.size());
    for(std::size_t s = 0; s < function.slots.size(); ++s) {
      const lowir_model::SlotId slot = function.slots[s];
      if(lowir_model::lowir_slot_type(function, slot).kind !=
         lowir_model::LTK_OBJECT) {
        scalar_state_indexes[slot] = scalar_states.size();
        scalar_states.push_back(ScalarSlotState());
      }
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
        const std::uint32_t slot = operands[k]->slot;
        if(slot < slot_flags.size()) operand_slots[k] = slot;
      }
      const bool has_dead_store_candidate =
        instruction.kind == Instruction::IK_STORE &&
        !instruction.volatile_access &&
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
        const std::uint32_t slot = instruction.args[k].slot;
        if(slot < slot_flags.size() &&
           (!has_dead_store_candidate || slot != operand_slots[1]))
          slot_flags[slot] |= SF_OBSERVED;
      }
      if(instruction.kind == Instruction::IK_STORE &&
         instruction.first.kind == Operand::OP_TEMP &&
         instruction.second.kind == Operand::OP_GLOBAL &&
         tls_wrappers[instruction.second.symbol].valid())
        facts.mark(instruction.first.value, StorageFacts::VF_TLS_STORE_INPUT);
      if(instruction.kind == Instruction::IK_STORE &&
         instruction.first.kind == Operand::OP_TEMP &&
         instruction.second.kind == Operand::OP_SLOT &&
         function_facts.uses[instruction.first.value] == 1)
        facts.mark(instruction.first.value,
                   StorageFacts::VF_DEAD_SLOT_ONLY_PARAMETER);

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
               copy.first.kind == Operand::OP_TEMP &&
               copy.first.value == parameter.value &&
               copy.second.kind == Operand::OP_TEMP &&
               copy.second.value == instruction.dest &&
               copy.byte_count == parameter.type.storage_size)
              facts.parameter_slot_aliases[slot_index] = parameter.value;
          }
        }

        if(scalar_state_indexes[slot_index] == no_index) continue;
        ScalarSlotState & state = scalar_states[scalar_state_indexes[slot_index]];
        if(instruction.volatile_access) state.valid = false;
        const bool first = operand_slots[0] == slot_index;
        const bool second = operand_slots[1] == slot_index;
        const bool third = operand_slots[2] == slot_index;
        if(!state.initialized && instruction.kind == Instruction::IK_STORE && second &&
           instruction.first.kind == Operand::OP_TEMP) {
          const std::size_t parameter = value_parameters[instruction.first.value];
          if(parameter != no_index &&
             lowir_model::same_lowir_type(function.params[parameter].type,
                lowir_model::lowir_slot_type(
                  function, lowir_model::SlotId(slot_index)))) {
            state.initialized = true;
            state.parameter = parameter;
            state.store_position = i;
          } else {
            state.valid = false;
          }
        } else if(state.initialized && instruction.kind == Instruction::IK_LOAD && first) {
          state.loaded = true;
          ++state.load_count;
          state.loaded_uses += function_facts.uses[instruction.dest];
          state.load_position = i;
          state.loaded_value = instruction.dest;
          state.intervening_clobbers |=
            function_facts.live_across_clobbers[instruction.dest];
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
        function_facts.has(instruction.dest,
                           FunctionFacts::VF_DIRECT_MEMORY_INDEX));
      for(std::size_t reg = 0; reg < last_clobber.size(); ++reg)
        if(clobbers & (1u << reg)) last_clobber[reg] = i;
    }
  }

  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const lowir_model::SlotId slot = function.slots[s];
    if((slot_flags[slot] & SF_WRITTEN) && !(slot_flags[slot] & SF_OBSERVED))
      facts.dead_store_slots[slot] = 1;
  }

  std::vector<std::size_t> loaded_slots(function.value_names.size(), no_index);
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const lowir_model::SlotId slot = function.slots[s];
    if(scalar_state_indexes[slot] == no_index) continue;
    const ScalarSlotState & state = scalar_states[scalar_state_indexes[slot]];
    if(state.valid && state.initialized && state.loaded)
      loaded_slots[state.loaded_value] = slot;
  }
  for(std::size_t i = 0; i < (function.blocks.empty() ? 0 :
      function.blocks[0].instructions.size()); ++i) {
    const Instruction & instruction = function.blocks[0].instructions[i];
    const Operand * operands[] = {
      &instruction.first, &instruction.second, &instruction.third
    };
    for(std::size_t k = 0; k < sizeof(operands) / sizeof(operands[0]); ++k) {
      if(operands[k]->kind != Operand::OP_TEMP) continue;
      const std::size_t slot = loaded_slots[operands[k]->value];
      if(slot != no_index &&
         !(instruction.kind == Instruction::IK_LOAD && k == 0))
        scalar_states[scalar_state_indexes[slot]].read_through = false;
    }
  }
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const lowir_model::SlotId slot = function.slots[s];
    if(scalar_state_indexes[slot] == no_index) continue;
    const ScalarSlotState & state = scalar_states[scalar_state_indexes[slot]];
    if(facts.dead_store_slots[slot] && state.initialized) {
      std::size_t & selected = facts.parameter_selected_uses[state.parameter];
      if(selected) --selected;
    }
    if(!state.valid || !state.initialized || !state.loaded ||
       (!function_facts.calls.empty() && state.load_count != 1))
      continue;
    const lowir_model::ValueId parameter = function.params[state.parameter].value;
    if(!function_facts.calls.empty() && function_facts.uses[parameter] != 1)
      continue;
    std::size_t & selected = facts.parameter_selected_uses[state.parameter];
    if(selected) --selected;
    selected += state.loaded_uses;
    if(state.read_through) {
      facts.promoted_parameter_slots[slot] = parameter;
      facts.has_promoted_parameter_slots = true;
    } else
      facts.forwarded_parameter_slots[slot] = parameter;
    facts.mark(parameter, StorageFacts::VF_PROMOTED_PARAMETER);
    facts.promoted_parameter_clobbers[state.parameter] |=
      state.intervening_clobbers;
    const std::vector<std::size_t>::const_iterator call = std::upper_bound(
      function_facts.calls.begin(), function_facts.calls.end(),
      state.store_position);
    if(function_facts.has(state.loaded_value,
                          FunctionFacts::VF_LIVE_ACROSS_CALL) ||
       (call != function_facts.calls.end() &&
        *call < state.load_position))
      facts.mark(parameter, StorageFacts::VF_PROMOTED_ACROSS_CALL);
  }
  return facts;
}

}  // namespace analysis
}  // namespace lowir_native
