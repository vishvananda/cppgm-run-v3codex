#include "lowir_native_frame_forwarding.h"

#include "lowir_native.h"
#include "lowir_native_data_layout.h"
#include "lowir_native_opt.h"
#include "lowir_native_scalar_memory.h"

#include <algorithm>
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
  bool carried = false;
};

// r10 may carry a value only across instructions that cannot touch it, at
// either level: the MIR stream (operands and the machine_opt definition
// mask, which models the implicit EH/i128/bulk/call clobbers) and the
// encode-time rewriters (constant-division magic burns r10/r11 around
// div/mul; global and symbol addressing materializes through the
// scalar-memory scratch pick; out-of-int32 immediates materialize through
// scratch).  Anything matching those shapes blocks the window.
bool touches_carry_scratch(const MirInstruction & instruction)
{
  if(machine_opt::instruction_definition_mask(instruction) &
     (std::uint64_t(1) << XR_R10))
    return true;
  if(instruction.opcode == MirInstruction::MI_MUL ||
     instruction.opcode == MirInstruction::MI_IMUL ||
     instruction.opcode == MirInstruction::MI_IDIV ||
     instruction.opcode == MirInstruction::MI_DIV)
    return true;
  for(std::size_t i = 0; i < instruction.operands.size(); ++i) {
    const MirOperand & operand = instruction.operands[i];
    if(operand.kind == MirOperand::OP_GLOBAL ||
       operand.kind == MirOperand::OP_SYMBOL)
      return true;
    if(operand.kind == MirOperand::OP_IMM &&
       (operand.imm > 0x7fffffffLL || operand.imm < -0x80000000LL))
      return true;
    if((operand.kind == MirOperand::OP_REG ||
        operand.kind == MirOperand::OP_DEREF) && operand.reg == XR_R10)
      return true;
    if(operand.kind == MirOperand::OP_DEREF && operand.has_index &&
       operand.index == XR_R10)
      return true;
  }
  return false;
}

bool eligible_reload_type(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_PTR ||
    (type.kind >= lowir_model::LTK_I1 && type.kind <= lowir_model::LTK_I64);
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
    if(!preserved) {
      // The source register does not survive; the value can still ride the
      // r10 carry when the whole window is provably free of r10 touches.
      // An MI_MOV immediately before the store blocks the carry: the
      // encode-time mov/store fold and byte-store coalescing start at that
      // mov and would consume the store, orphaning the carry replacement.
      if(gap > 40) return;
      if(facts.store_index > 0 &&
         function.blocks[facts.store_block]
           .instructions[facts.store_index - 1].opcode ==
             MirInstruction::MI_MOV)
        return;
      for(std::size_t i = facts.store_index + 1; i < facts.load_index; ++i)
        if(touches_carry_scratch(
             function.blocks[facts.store_block].instructions[i]))
          return;
      reload.carried = true;
    }
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
    FrameReloadPlan::InstructionAction & load = plan->actions[
      plan->block_starts[reload.load_block] + reload.load_index];
    load.kind = FrameReloadPlan::InstructionAction::IA_FORWARD_DELAYED_LOAD;
    if(reload.carried) {
      store.kind =
        FrameReloadPlan::InstructionAction::IA_CARRY_SCRATCH_STORE;
      store.source = static_cast<std::uint8_t>(reload.source);
      load.source = static_cast<std::uint8_t>(XR_R10);
      continue;
    }
    store.kind = FrameReloadPlan::InstructionAction::IA_SKIP_DELAYED_STORE;
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

bool emit_carry_scratch_store(
    elf_detail::CodeBuffer & out, const MirInstruction & instruction,
    const FrameReloadPlan::InstructionAction & action, Stats * stats)
{
  if(action.kind != FrameReloadPlan::InstructionAction::
       IA_CARRY_SCRATCH_STORE)
    return false;
  emit_normalized_register_move(
    out, XR_R10, action.source_register(), instruction.type);
  if(stats) ++stats->scratch_carried_reloads;
  return true;
}

bool emit_delayed_frame_forwarding(
    elf_detail::CodeBuffer & out, const MirInstruction & instruction,
    const FrameReloadPlan::InstructionAction & action)
{
  if(instruction.opcode == MirInstruction::MI_STORE &&
     instruction.operands.size() == 2 &&
     instruction.operands[0].kind == MirOperand::OP_FRAME &&
     action.kind == FrameReloadPlan::InstructionAction::
       IA_SKIP_DELAYED_STORE)
    return true;
  if(instruction.opcode != MirInstruction::MI_LOAD ||
     instruction.operands.size() != 2 ||
     instruction.operands[0].kind != MirOperand::OP_REG ||
     instruction.operands[1].kind != MirOperand::OP_FRAME)
    return false;
  if(action.kind != FrameReloadPlan::InstructionAction::
       IA_FORWARD_DELAYED_LOAD)
    return false;
  emit_normalized_register_move(
    out, instruction.operands[0].reg, action.source_register(),
    instruction.type);
  return true;
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
     store.volatile_access || load.volatile_access ||
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
         !instruction.volatile_access &&
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
         !instruction.volatile_access &&
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
  // All carried windows share the single r10 carry, so overlapping carried
  // windows would clobber each other; keep a greedy non-overlapping subset
  // per block, position-ordered.
  std::vector<PlannedReload> accepted;
  accepted.reserve(reloads.size());
  std::vector<const PlannedReload *> carried;
  for(std::size_t i = 0; i < reloads.size(); ++i) {
    if(reloads[i].carried) carried.push_back(&reloads[i]);
    else accepted.push_back(reloads[i]);
  }
  std::sort(carried.begin(), carried.end(),
            [](const PlannedReload * left, const PlannedReload * right) {
              return left->store_block < right->store_block ||
                (left->store_block == right->store_block &&
                 left->store_index < right->store_index);
            });
  std::size_t open_block = static_cast<std::size_t>(-1);
  std::size_t open_end = 0;
  for(std::size_t i = 0; i < carried.size(); ++i) {
    if(carried[i]->store_block == open_block &&
       carried[i]->store_index <= open_end)
      continue;
    open_block = carried[i]->store_block;
    open_end = carried[i]->load_index;
    accepted.push_back(*carried[i]);
  }
  build_action_table(function, accepted, &result);
  return result;
}

}  // namespace frame_forwarding
}  // namespace lowir_native
