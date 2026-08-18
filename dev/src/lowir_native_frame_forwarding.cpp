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

struct PlannedReload
{
  std::size_t store_block = 0;
  std::size_t store_index = 0;
  std::size_t load_block = 0;
  std::size_t load_index = 0;
  X64Register source = XR_RAX;
  bool adjacent = false;
};

bool eligible_reload_type(const std::string & type)
{
  return type == "ptr" || type == "i64" ||
    type == "i32" || type == "u32" ||
    type == "i16" || type == "u16" ||
    type == "i8" || type == "u8" || type == "i1";
}

struct FrameBindingIndex
{
  std::vector<unsigned char> eligible;
  std::unordered_map<long long, std::uint32_t> unique_offsets;

  explicit FrameBindingIndex(const mir_model::MirFunction & function)
    : eligible(function.frame_bindings.size() + 1, 0)
  {
    unique_offsets.reserve(function.frame_bindings.size());
    for(std::size_t i = 0; i < function.frame_bindings.size(); ++i) {
      const mir_model::MirFrameBinding & binding = function.frame_bindings[i];
      if(binding.kind != mir_model::MirFrameBinding::FB_TEMP ||
         !eligible_reload_type(binding.type) ||
         (function.host_eh_enabled &&
          (binding.offset == function.host_eh_exception_offset ||
           binding.offset == function.host_eh_selector_offset)))
        continue;
      const std::uint32_t ordinal = static_cast<std::uint32_t>(i + 1);
      eligible[ordinal] = 1;
      const std::pair<std::unordered_map<long long, std::uint32_t>::iterator,
                      bool> inserted = unique_offsets.emplace(
        binding.offset, ordinal);
      if(!inserted.second) inserted.first->second = 0;
    }
  }

  std::uint32_t resolve(const MirOperand & operand) const
  {
    if(operand.frame_binding != 0)
      return operand.frame_binding < eligible.size() &&
        eligible[operand.frame_binding] ? operand.frame_binding : 0;
    const std::unordered_map<long long, std::uint32_t>::const_iterator found =
      unique_offsets.find(operand.offset);
    return found == unique_offsets.end() ? 0 : found->second;
  }
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
     store.operands[0].offset != load.operands[1].offset ||
     (store.operands[0].frame_binding != 0 &&
      load.operands[1].frame_binding != 0 &&
      store.operands[0].frame_binding != load.operands[1].frame_binding))
    return false;
  *source = store.operands[1].reg;
  *destination = load.operands[0].reg;
  return true;
}

FrameReloadPlan find_single_use_reloads(const mir_model::MirFunction & function)
{
  // Frame offsets may be reused by disjoint temporary lifetimes.  Code-block
  // layout is independent of LowIR definition order, so a defining store
  // cannot delimit those lifetimes after block placement.  Track annotated
  // homes by their compact binding ordinal instead.  The offset index is only
  // a compatibility path for unique, unannotated homes.
  const FrameBindingIndex bindings(function);
  FrameReloadPlan result;
  if(bindings.unique_offsets.empty()) return result;
  std::vector<FrameUseFacts> uses(function.frame_bindings.size() + 1);
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
        const MirOperand & operand = instruction.operands[0];
        const std::uint32_t ordinal = bindings.resolve(operand);
        if(ordinal != 0) {
          uses[ordinal].store = &instruction;
          uses[ordinal].store_block = i;
          uses[ordinal].store_index = j;
        }
      }
      for(std::size_t k = 0; k < instructions[j].operands.size(); ++k) {
        const MirOperand & operand = instructions[j].operands[k];
        if(operand.kind != MirOperand::OP_FRAME) continue;
        const std::uint32_t ordinal = bindings.resolve(operand);
        if(ordinal != 0)
          ++uses[ordinal].count;
      }
      if(instruction.operands.size() != 2) continue;
      if(instruction.opcode == MirInstruction::MI_LOAD &&
         instruction.operands[0].kind == MirOperand::OP_REG &&
         instruction.operands[1].kind == MirOperand::OP_FRAME) {
        const MirOperand & operand = instruction.operands[1];
        const std::uint32_t ordinal = bindings.resolve(operand);
        if(ordinal != 0) {
          uses[ordinal].load = &instruction;
          uses[ordinal].load_block = i;
          uses[ordinal].load_index = j;
        }
      }
    }
  }
  for(std::size_t i = 1; i < uses.size(); ++i)
    if(bindings.eligible[i]) append_reload(function, uses[i], &reloads);
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
