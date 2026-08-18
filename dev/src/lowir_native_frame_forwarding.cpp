#include "lowir_native_frame_forwarding.h"

#include "lowir_native_data_layout.h"

#include <unordered_map>

namespace lowir_native {
namespace frame_forwarding {
namespace {

using mir_model::MirInstruction;
using mir_model::MirOperand;

static_assert(sizeof(FrameReloadPlan::InstructionAction) == 2,
              "frame reload actions must remain compact");

struct FrameUseFacts
{
  std::size_t count = 0;
  const MirInstruction * store = 0;
  const MirInstruction * load = 0;
  std::size_t store_block = 0;
  std::size_t store_index = 0;
  std::size_t load_block = 0;
  std::size_t load_index = 0;
};

struct FrameOffsetFacts
{
  std::size_t binding_count = 0;
  FrameUseFacts single;
  FrameUseFacts active;
  bool has_active = false;
};

struct PlannedReload
{
  std::size_t store_block = 0;
  std::size_t store_index = 0;
  std::size_t load_block = 0;
  std::size_t load_index = 0;
  X64Register source = XR_RAX;
  bool adjacent = false;
};

bool preserves_register(const MirInstruction & instruction,
                        X64Register source)
{
  if(instruction.opcode != MirInstruction::MI_LOAD &&
     instruction.opcode != MirInstruction::MI_LEA &&
     instruction.opcode != MirInstruction::MI_MOV)
    return false;
  return !instruction.operands.empty() &&
    instruction.operands[0].kind == MirOperand::OP_REG &&
    instruction.operands[0].reg != source;
}

void append_reload(const mir_model::MirFunction & function,
                   const FrameUseFacts & facts,
                   std::vector<PlannedReload> * reloads)
{
  if(facts.count != 2 || !facts.store || !facts.load ||
     facts.store_block != facts.load_block ||
     facts.store->type != facts.load->type ||
     facts.store_index >= facts.load_index)
    return;
  const std::size_t gap = facts.load_index - facts.store_index - 1;
  PlannedReload reload;
  reload.store_block = facts.store_block;
  reload.store_index = facts.store_index;
  reload.load_block = facts.load_block;
  reload.load_index = facts.load_index;
  reload.source = facts.store->operands[1].reg;
  reload.adjacent = gap == 0;
  if(!reload.adjacent) {
    bool preserved = gap <= 5;
    for(std::size_t i = facts.store_index + 1;
        preserved && i < facts.load_index; ++i)
      preserved = preserves_register(
        function.blocks[facts.store_block].instructions[i], reload.source);
    if(!preserved) return;
  }
  reloads->push_back(reload);
}

void begin_reused_lifetime(const mir_model::MirFunction & function,
                           FrameOffsetFacts * offset,
                           std::vector<PlannedReload> * reloads)
{
  if(offset->has_active)
    append_reload(function, offset->active, reloads);
  offset->active = FrameUseFacts();
  offset->has_active = true;
}

void build_action_table(const mir_model::MirFunction & function,
                        const std::vector<PlannedReload> & reloads,
                        FrameReloadPlan * plan)
{
  if(reloads.empty()) return;
  plan->block_starts.resize(function.blocks.size() + 1, 0);
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    plan->block_starts[i + 1] = plan->block_starts[i] +
      function.blocks[i].instructions.size();
  plan->actions.resize(plan->block_starts.back());
  for(std::size_t i = 0; i < reloads.size(); ++i) {
    const PlannedReload & reload = reloads[i];
    FrameReloadPlan::InstructionAction & store = plan->actions[
      plan->block_starts[reload.store_block] + reload.store_index];
    if(reload.adjacent) {
      store.kind = FrameReloadPlan::InstructionAction::IA_DROP_ADJACENT_STORE;
      continue;
    }
    store.kind = FrameReloadPlan::InstructionAction::IA_SKIP_DELAYED_STORE;
    FrameReloadPlan::InstructionAction & load = plan->actions[
      plan->block_starts[reload.load_block] + reload.load_index];
    load.kind = FrameReloadPlan::InstructionAction::IA_FORWARD_DELAYED_LOAD;
    load.source = static_cast<std::uint8_t>(reload.source);
  }
}

}  // namespace

FrameReloadPlan::InstructionAction FrameReloadPlan::action(
    std::size_t block, std::size_t instruction) const
{
  if(actions.empty()) return InstructionAction();
  return actions[block_starts[block] + instruction];
}

bool parse_reload(
    const std::vector<MirInstruction> & instructions,
    std::size_t start, X64Register * source, X64Register * destination)
{
  if(start > instructions.size() || instructions.size() - start < 2)
    return false;
  const MirInstruction & store = instructions[start];
  const MirInstruction & load = instructions[start + 1];
  if(store.opcode != MirInstruction::MI_STORE ||
     load.opcode != MirInstruction::MI_LOAD ||
     store.type != load.type || data_layout::type_width(store.type) > 64 ||
     store.operands.size() != 2 || load.operands.size() != 2 ||
     store.operands[0].kind != MirOperand::OP_FRAME ||
     store.operands[1].kind != MirOperand::OP_REG ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_FRAME ||
     store.operands[0].offset != load.operands[1].offset)
    return false;
  *source = store.operands[1].reg;
  *destination = load.operands[0].reg;
  return true;
}

FrameReloadPlan find_single_use_reloads(const mir_model::MirFunction & function)
{
  std::unordered_map<long long, FrameOffsetFacts> uses;
  uses.reserve(function.frame_bindings.size());
  for(std::size_t i = 0; i < function.frame_bindings.size(); ++i) {
    const mir_model::MirFrameBinding & binding = function.frame_bindings[i];
    if(binding.kind == mir_model::MirFrameBinding::FB_TEMP &&
       (binding.type == "ptr" || binding.type == "i64" ||
        binding.type == "i32" || binding.type == "u32" ||
        binding.type == "i16" || binding.type == "u16" ||
        binding.type == "i8" || binding.type == "u8" ||
        binding.type == "i1")) {
      const std::pair<std::unordered_map<long long, FrameOffsetFacts>::iterator,
                      bool> inserted =
        uses.emplace(binding.offset, FrameOffsetFacts());
      ++inserted.first->second.binding_count;
    }
  }
  if(function.host_eh_enabled) {
    uses.erase(function.host_eh_exception_offset);
    uses.erase(function.host_eh_selector_offset);
  }
  FrameReloadPlan result;
  if(uses.empty()) return result;
  std::vector<PlannedReload> reloads;
  reloads.reserve(function.frame_bindings.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::vector<MirInstruction> & instructions =
      function.blocks[i].instructions;
    for(std::size_t j = 0; j < instructions.size(); ++j) {
      const MirInstruction & instruction = instructions[j];
      if(instruction.opcode == MirInstruction::MI_STORE &&
         instruction.operands.size() == 2 &&
         instruction.operands[0].kind == MirOperand::OP_FRAME &&
         instruction.operands[1].kind == MirOperand::OP_REG) {
        const std::unordered_map<long long, FrameOffsetFacts>::iterator found =
          uses.find(instruction.operands[0].offset);
        if(found != uses.end()) {
          FrameOffsetFacts & offset = found->second;
          FrameUseFacts * facts = &offset.single;
          if(offset.binding_count > 1) {
            begin_reused_lifetime(function, &offset, &reloads);
            facts = &offset.active;
          }
          facts->store = &instruction;
          facts->store_block = i;
          facts->store_index = j;
        }
      }
      for(std::size_t k = 0; k < instructions[j].operands.size(); ++k) {
        const MirOperand & operand = instructions[j].operands[k];
        if(operand.kind != MirOperand::OP_FRAME) continue;
        const std::unordered_map<long long, FrameOffsetFacts>::iterator found =
          uses.find(operand.offset);
        if(found == uses.end()) continue;
        FrameOffsetFacts & offset = found->second;
        if(offset.binding_count == 1) ++offset.single.count;
        else if(offset.has_active) ++offset.active.count;
      }
      if(instruction.operands.size() != 2) continue;
      if(instruction.opcode == MirInstruction::MI_LOAD &&
         instruction.operands[0].kind == MirOperand::OP_REG &&
         instruction.operands[1].kind == MirOperand::OP_FRAME) {
        const std::unordered_map<long long, FrameOffsetFacts>::iterator found =
          uses.find(instruction.operands[1].offset);
        if(found != uses.end()) {
          FrameOffsetFacts & offset = found->second;
          FrameUseFacts * facts = offset.binding_count == 1 ?
            &offset.single : offset.has_active ? &offset.active : 0;
          if(facts) {
            facts->load = &instruction;
            facts->load_block = i;
            facts->load_index = j;
          }
        }
      }
    }
  }
  for(std::unordered_map<long long, FrameOffsetFacts>::const_iterator use =
        uses.begin(); use != uses.end(); ++use) {
    const FrameOffsetFacts & offset = use->second;
    if(offset.binding_count == 1)
      append_reload(function, offset.single, &reloads);
    else if(offset.has_active)
      append_reload(function, offset.active, &reloads);
  }
  build_action_table(function, reloads, &result);
  return result;
}

bool load_zero_extends(
    const std::vector<MirInstruction> & instructions,
    std::size_t block, std::size_t start, const FrameReloadPlan & plan)
{
  if(start == 0) return false;
  const MirInstruction & load = instructions[start - 1];
  if(load.opcode != MirInstruction::MI_LOAD || load.operands.size() != 2 ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_FRAME) return false;
  const FrameReloadPlan::InstructionAction action =
    plan.action(block, start - 1);
  if(action.kind ==
       FrameReloadPlan::InstructionAction::IA_FORWARD_DELAYED_LOAD)
    return action.source_register() != load.operands[0].reg;
  X64Register source = XR_RAX, destination = XR_RAX;
  if(start >= 2 && parse_reload(instructions, start - 2,
       &source, &destination)) return source != destination;
  return true;
}

}  // namespace frame_forwarding
}  // namespace lowir_native
