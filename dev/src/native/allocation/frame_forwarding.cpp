#include "native/allocation/frame_forwarding.h"

#include "native/lowering/lowir_native.h"
#include "native/analysis/data_layout.h"
#include "native/mir/optimize.h"
#include "native/encoding/scalar_memory.h"

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
  std::size_t store_block = 0;
  std::size_t store_index = 0;
  // Every matching load of the slot, in scan order: (block, index, type).
  std::vector<std::size_t> load_blocks;
  std::vector<std::size_t> load_indices;
  std::vector<const MirInstruction *> load_instructions;
};

struct PlannedReload
{
  std::size_t store_block = 0;
  std::size_t store_index = 0;
  std::vector<std::size_t> load_indices;
  X64Register source = XR_RAX;
  bool adjacent = false;
  bool r10_ok = false;
  bool r11_ok = false;
  bool carried = false;
  X64Register carry = XR_R10;
};

// A scratch register may carry a value only across instructions that
// cannot touch it, at either level: the MIR stream (operands and the
// machine_opt definition mask, which models the implicit i128/bulk/call
// clobbers) and the encode-time rewriters, which the oracle enumerates BY
// EMISSION SITE: constant-division magic burns r10/r11 around div/mul;
// float emission materializes immediates, sign masks, and x87 staging
// through both; global and symbol addressing takes the scalar-memory
// scratch pick; out-of-int32 immediates materialize through scratch; the
// EH markers not covered by the definition mask (filter, cleanup clause,
// resume) stage their records through r11; and copy-bytes argument
// shuffling swaps through r11.
bool touches_carry_scratch(const MirInstruction & instruction,
                           X64Register scratch)
{
  if(machine_opt::instruction_definition_mask(instruction) &
     (std::uint64_t(1) << scratch))
    return true;
  if(instruction.opcode == MirInstruction::MI_MUL ||
     instruction.opcode == MirInstruction::MI_IMUL ||
     instruction.opcode == MirInstruction::MI_IDIV ||
     instruction.opcode == MirInstruction::MI_DIV)
    return true;
  if(instruction.opcode >= MirInstruction::MI_EH_PUSH &&
     instruction.opcode <= MirInstruction::MI_RESUME)
    return true;
  if(instruction.opcode == MirInstruction::MI_COPY_BYTES ||
     instruction.opcode == MirInstruction::MI_ZERO_BYTES)
    return true;
  if(instruction.type.kind == lowir_model::LTK_F32 ||
     instruction.type.kind == lowir_model::LTK_F64 ||
     instruction.type.kind == lowir_model::LTK_F80)
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
        operand.kind == MirOperand::OP_DEREF) && operand.reg == scratch)
      return true;
    if(operand.kind == MirOperand::OP_DEREF && operand.has_index &&
       operand.index == scratch)
      return true;
    if(operand.kind == MirOperand::OP_XMM) return true;
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
  // Every reference to the slot must be the one store or a matching load,
  // all in the store's block and after it; anything else (a second store,
  // an address-taken use, a cross-block read) keeps the slot.
  if(!facts.store || facts.load_indices.empty() ||
     facts.count != 1 + facts.load_indices.size())
    return;
  for(std::size_t i = 0; i < facts.load_indices.size(); ++i)
    if(facts.load_blocks[i] != facts.store_block ||
       facts.load_indices[i] <= facts.store_index ||
       facts.store->type != facts.load_instructions[i]->type)
      return;
  PlannedReload reload;
  reload.store_block = facts.store_block;
  reload.store_index = facts.store_index;
  reload.load_indices = facts.load_indices;
  std::sort(reload.load_indices.begin(), reload.load_indices.end());
  reload.source = facts.store->operands[1].reg;
  const std::size_t last = reload.load_indices.back();
  const std::size_t gap = last - facts.store_index - 1;
  reload.adjacent = reload.load_indices.size() == 1 && gap == 0;
  if(!reload.adjacent) {
    // The preserved walk is exact per instruction, so the window cap is
    // just a cost bound shared with the carry path.
    bool preserved = gap <= 40;
    for(std::size_t i = facts.store_index + 1;
        preserved && i < last; ++i)
      preserved = preserves_register(
        function.blocks[facts.store_block].instructions[i], reload.source);
    if(!preserved) {
      // The source register does not survive; the value can still ride a
      // scratch carry when the whole window is provably free of touches of
      // that register.  An MI_MOV immediately before the store blocks the
      // carry: the encode-time mov/store fold and byte-store coalescing
      // start at that mov and would consume the store, orphaning the carry
      // replacement.
      if(gap > 40) return;
      if(facts.store_index > 0 &&
         function.blocks[facts.store_block]
           .instructions[facts.store_index - 1].opcode ==
             MirInstruction::MI_MOV)
        return;
      reload.r10_ok = true;
      reload.r11_ok = true;
      for(std::size_t i = facts.store_index + 1;
          (reload.r10_ok || reload.r11_ok) && i < last; ++i) {
        const MirInstruction & inside =
          function.blocks[facts.store_block].instructions[i];
        if(reload.r10_ok && touches_carry_scratch(inside, XR_R10))
          reload.r10_ok = false;
        if(reload.r11_ok && touches_carry_scratch(inside, XR_R11))
          reload.r11_ok = false;
      }
      if(!reload.r10_ok && !reload.r11_ok) return;
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
    const X64Register forward_source =
      reload.carried ? reload.carry : reload.source;
    for(std::size_t j = 0; j < reload.load_indices.size(); ++j) {
      FrameReloadPlan::InstructionAction & load = plan->actions[
        plan->block_starts[reload.store_block] + reload.load_indices[j]];
      load.kind = FrameReloadPlan::InstructionAction::IA_FORWARD_DELAYED_LOAD;
      load.source = static_cast<std::uint8_t>(forward_source);
    }
    if(reload.carried) {
      store.kind = reload.carry == XR_R10 ?
        FrameReloadPlan::InstructionAction::IA_CARRY_SCRATCH_STORE :
        FrameReloadPlan::InstructionAction::IA_CARRY_SCRATCH_STORE_R11;
      store.source = static_cast<std::uint8_t>(reload.source);
      continue;
    }
    store.kind = FrameReloadPlan::InstructionAction::IA_SKIP_DELAYED_STORE;
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
       IA_CARRY_SCRATCH_STORE &&
     action.kind != FrameReloadPlan::InstructionAction::
       IA_CARRY_SCRATCH_STORE_R11)
    return false;
  const X64Register carry = action.kind ==
    FrameReloadPlan::InstructionAction::IA_CARRY_SCRATCH_STORE ?
    XR_R10 : XR_R11;
  emit_normalized_register_move(
    out, carry, action.source_register(), instruction.type);
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
          uses[ordinal].load_blocks.push_back(i);
          uses[ordinal].load_indices.push_back(j);
          uses[ordinal].load_instructions.push_back(&instruction);
        }
      }
    }
  }
  for(std::size_t i = 1; i < uses.size(); ++i)
    if(bindings.eligible[i]) append_reload(function, uses[i], &reloads);
  // Carried windows on the same register must not overlap (they would
  // clobber each other's carry); r10 and r11 windows may interleave
  // freely, since each window's oracle already excludes explicit mentions
  // of its own register.  Greedy per block, position-ordered, r10 first.
  std::vector<PlannedReload> accepted;
  accepted.reserve(reloads.size());
  std::vector<PlannedReload *> carried;
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
  std::size_t open_block[2] = {static_cast<std::size_t>(-1),
                               static_cast<std::size_t>(-1)};
  std::size_t open_end[2] = {0, 0};
  for(std::size_t i = 0; i < carried.size(); ++i) {
    PlannedReload & reload = *carried[i];
    const bool eligible[2] = {reload.r10_ok, reload.r11_ok};
    static const X64Register registers[2] = {XR_R10, XR_R11};
    for(std::size_t slot = 0; slot < 2; ++slot) {
      if(!eligible[slot]) continue;
      if(reload.store_block == open_block[slot] &&
         reload.store_index <= open_end[slot])
        continue;
      open_block[slot] = reload.store_block;
      open_end[slot] = reload.load_indices.back();
      reload.carry = registers[slot];
      accepted.push_back(reload);
      break;
    }
  }
  build_action_table(function, accepted, &result);
  return result;
}

}  // namespace frame_forwarding
}  // namespace lowir_native
