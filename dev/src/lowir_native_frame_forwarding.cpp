#include "lowir_native_frame_forwarding.h"

#include "lowir_native_data_layout.h"

namespace lowir_native {
namespace frame_forwarding {
namespace {

using mir_model::MirInstruction;
using mir_model::MirOperand;

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

}  // namespace

bool parse_reload(
    const std::vector<MirInstruction> & instructions,
    std::size_t start, long long * frame_offset,
    X64Register * source, X64Register * destination)
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
  *frame_offset = store.operands[0].offset;
  *source = store.operands[1].reg;
  *destination = load.operands[0].reg;
  return true;
}

FrameReloadPlan find_single_use_reloads(const mir_model::MirFunction & function)
{
  std::unordered_map<long long, FrameUseFacts> uses;
  uses.reserve(function.frame_bindings.size());
  for(std::size_t i = 0; i < function.frame_bindings.size(); ++i) {
    const mir_model::MirFrameBinding & binding = function.frame_bindings[i];
    if(binding.kind == mir_model::MirFrameBinding::FB_TEMP &&
       (binding.type == "ptr" || binding.type == "i64" ||
        binding.type == "i32" || binding.type == "u32" ||
        binding.type == "i16" || binding.type == "u16" ||
        binding.type == "i8" || binding.type == "u8" ||
        binding.type == "i1"))
      uses.emplace(binding.offset, FrameUseFacts());
  }
  if(function.host_eh_enabled) {
    uses.erase(function.host_eh_exception_offset);
    uses.erase(function.host_eh_selector_offset);
  }
  FrameReloadPlan result;
  if(uses.empty()) return result;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::vector<MirInstruction> & instructions =
      function.blocks[i].instructions;
    for(std::size_t j = 0; j < instructions.size(); ++j) {
      for(std::size_t k = 0; k < instructions[j].operands.size(); ++k) {
        const MirOperand & operand = instructions[j].operands[k];
        if(operand.kind != MirOperand::OP_FRAME) continue;
        const std::unordered_map<long long, FrameUseFacts>::iterator found =
          uses.find(operand.offset);
        if(found != uses.end()) ++found->second.count;
      }
      const MirInstruction & instruction = instructions[j];
      if(instruction.operands.size() != 2) continue;
      if(instruction.opcode == MirInstruction::MI_STORE &&
         instruction.operands[0].kind == MirOperand::OP_FRAME &&
         instruction.operands[1].kind == MirOperand::OP_REG) {
        const std::unordered_map<long long, FrameUseFacts>::iterator found =
          uses.find(instruction.operands[0].offset);
        if(found != uses.end()) {
          found->second.store = &instruction;
          found->second.store_block = i;
          found->second.store_index = j;
        }
      } else if(instruction.opcode == MirInstruction::MI_LOAD &&
                instruction.operands[0].kind == MirOperand::OP_REG &&
                instruction.operands[1].kind == MirOperand::OP_FRAME) {
        const std::unordered_map<long long, FrameUseFacts>::iterator found =
          uses.find(instruction.operands[1].offset);
        if(found != uses.end()) {
          found->second.load = &instruction;
          found->second.load_block = i;
          found->second.load_index = j;
        }
      }
    }
  }
  result.adjacent.reserve(uses.size());
  result.delayed.reserve(uses.size() / 4);
  for(std::unordered_map<long long, FrameUseFacts>::const_iterator use =
        uses.begin(); use != uses.end(); ++use) {
    const FrameUseFacts & facts = use->second;
    if(facts.count != 2 || !facts.store || !facts.load ||
       facts.store_block != facts.load_block ||
       facts.store->type != facts.load->type ||
       facts.store_index >= facts.load_index)
      continue;
    const std::size_t gap = facts.load_index - facts.store_index - 1;
    if(gap == 0) {
      result.adjacent.insert(use->first);
      continue;
    }
    const X64Register source = facts.store->operands[1].reg;
    bool preserved = gap <= 5;
    for(std::size_t i = facts.store_index + 1;
        preserved && i < facts.load_index; ++i)
      preserved = preserves_register(
        function.blocks[facts.store_block].instructions[i], source);
    if(preserved) result.delayed.emplace(use->first, source);
  }
  return result;
}

bool load_zero_extends(
    const std::vector<MirInstruction> & instructions,
    std::size_t start, const FrameReloadPlan & plan)
{
  if(start == 0) return false;
  const MirInstruction & load = instructions[start - 1];
  if(load.opcode != MirInstruction::MI_LOAD || load.operands.size() != 2 ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_FRAME) return false;
  const std::unordered_map<long long, X64Register>::const_iterator delayed =
    plan.delayed.find(load.operands[1].offset);
  if(delayed != plan.delayed.end()) return delayed->second != load.operands[0].reg;
  long long offset = 0;
  X64Register source = XR_RAX, destination = XR_RAX;
  if(start >= 2 && parse_reload(instructions, start - 2,
       &offset, &source, &destination)) return source != destination;
  return true;
}

}  // namespace frame_forwarding
}  // namespace lowir_native
