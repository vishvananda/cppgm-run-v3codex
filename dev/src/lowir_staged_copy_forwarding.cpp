#include "lowir_staged_copy_forwarding.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using lowir_model::SlotId;
using lowir_model::ValueId;

struct SlotAddress
{
  std::uint32_t slot = 0;
  long long offset = 0;
  bool known = false;
};

std::size_t scalar_store_bytes(const LowType & type)
{
  switch(type.kind) {
  case lowir_model::LTK_I1:
  case lowir_model::LTK_I8:
  case lowir_model::LTK_U8: return 1;
  case lowir_model::LTK_I16:
  case lowir_model::LTK_U16: return 2;
  case lowir_model::LTK_I32:
  case lowir_model::LTK_U32:
  case lowir_model::LTK_F32: return 4;
  case lowir_model::LTK_I64:
  case lowir_model::LTK_PTR:
  case lowir_model::LTK_F64: return 8;
  default: return 0;
  }
}

// Memoized slot/offset provenance over addr/copy/index chains.
class OffsetProvenance
{
public:
  explicit OffsetProvenance(const Function & function)
    : definitions_(function.value_names.size(), 0),
      facts_(function.value_names.size()),
      states_(function.value_names.size(), 0)
  {
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
        const Instruction & instruction = function.blocks[b].instructions[i];
        if(instruction.dest.valid() &&
           static_cast<std::size_t>(instruction.dest) < definitions_.size())
          definitions_[instruction.dest] = &instruction;
      }
  }

  SlotAddress resolve(const Operand & operand)
  {
    if(operand.kind == Operand::OP_SLOT) {
      SlotAddress result;
      result.slot = operand.slot;
      result.known = true;
      return result;
    }
    if(operand.kind != Operand::OP_TEMP) return SlotAddress();
    return resolve(operand.value);
  }

private:
  SlotAddress resolve(ValueId value)
  {
    if(static_cast<std::size_t>(value) >= definitions_.size())
      return SlotAddress();
    if(states_[value] == 2) return facts_[value];
    if(states_[value] == 1) return SlotAddress();
    states_[value] = 1;
    const Instruction * instruction = definitions_[value];
    SlotAddress result;
    if(instruction && instruction->kind == Instruction::IK_ADDR &&
       instruction->first.kind == Operand::OP_SLOT) {
      result.slot = instruction->first.slot;
      result.known = true;
    } else if(instruction && instruction->kind == Instruction::IK_COPY &&
              instruction->type.kind == lowir_model::LTK_PTR) {
      result = resolve(instruction->first);
    } else if(instruction && instruction->kind == Instruction::IK_INDEX &&
              instruction->second.kind == Operand::OP_INTEGER &&
              instruction->second.has_int_value &&
              instruction->second.int_high == 0) {
      result = resolve(instruction->first);
      result.offset += instruction->second.int_value;
    }
    facts_[value] = result;
    states_[value] = 2;
    return result;
  }

  std::vector<const Instruction *> definitions_;
  std::vector<SlotAddress> facts_;
  std::vector<unsigned char> states_;
};

struct StagedStore
{
  long long offset = 0;
  std::size_t bytes = 0;
  std::size_t block = 0;
  std::size_t index = 0;
};

struct SlotPlan
{
  bool disqualified = false;
  std::vector<StagedStore> stores;
  std::size_t copy_block = 0;
  std::size_t copy_index = 0;
  std::size_t copy_sites = 0;
};

bool plumbing_definition(const Instruction & instruction, std::size_t operand)
{
  if(operand != 0) return false;
  if(instruction.kind == Instruction::IK_ADDR) return true;
  if(instruction.kind == Instruction::IK_COPY &&
     instruction.type.kind == lowir_model::LTK_PTR) return true;
  return instruction.kind == Instruction::IK_INDEX &&
    instruction.second.kind == Operand::OP_INTEGER &&
    instruction.second.has_int_value && instruction.second.int_high == 0;
}

}  // namespace

namespace forwarding_detail {

// One census-and-rewrite round.  Rewriting shifts instruction positions, so
// each round forwards a single slot and the caller repeats until quiet.
bool forward_one_staged_copy(
    Function * function,
    lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats)
{
  const std::size_t slot_count = function->slot_names.size();
  if(slot_count == 0 || function->blocks.empty()) return false;
  std::vector<unsigned char> candidate(slot_count, 0);
  bool any = false;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const SlotId slot = function->slots[i];
    const LowType & type = lowir_model::lowir_slot_type(*function, slot);
    if(type.kind == lowir_model::LTK_OBJECT && type.storage_size > 8 &&
       type.storage_size <= 64) {
      candidate[slot] = 1;
      any = true;
    }
  }
  if(!any) return false;

  OffsetProvenance provenance(*function);
  std::vector<SlotPlan> plans(slot_count);
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function->blocks[block].instructions[index];
      const Operand * fixed[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t operand = 0;
          operand < sizeof(fixed) / sizeof(fixed[0]) + ins.args.size();
          ++operand) {
        const Operand & use =
          operand < sizeof(fixed) / sizeof(fixed[0]) ?
          *fixed[operand] :
          ins.args[operand - sizeof(fixed) / sizeof(fixed[0])];
        const SlotAddress address = provenance.resolve(use);
        if(!address.known || address.slot >= slot_count ||
           !candidate[address.slot])
          continue;
        SlotPlan & plan = plans[address.slot];
        if(plan.disqualified) continue;
        if(plumbing_definition(ins, operand)) continue;
        if(ins.kind == Instruction::IK_STORE && operand == 1) {
          const std::size_t bytes = scalar_store_bytes(ins.type);
          const LowType & slot_type = lowir_model::lowir_slot_type(
            *function, SlotId(address.slot));
          if(bytes == 0 || address.offset < 0 ||
             static_cast<std::size_t>(address.offset) + bytes >
               slot_type.storage_size) {
            plan.disqualified = true;
            continue;
          }
          StagedStore store;
          store.offset = address.offset;
          store.bytes = bytes;
          store.block = block;
          store.index = index;
          plan.stores.push_back(store);
          continue;
        }
        if(ins.kind == Instruction::IK_COPYOBJ && operand == 0 &&
           address.offset == 0 &&
           ins.byte_count == lowir_model::lowir_slot_type(
             *function, SlotId(address.slot)).storage_size) {
          plan.copy_block = block;
          plan.copy_index = index;
          ++plan.copy_sites;
          continue;
        }
        plan.disqualified = true;
      }
    }

  const lowir_analysis::Graph & graph = analysis->graph();
  const lowir_analysis::DominatorTree & dominators =
    analysis->dominator_tree();
  bool changed = false;
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    SlotPlan & plan = plans[slot];
    if(!candidate[slot] || plan.disqualified || plan.copy_sites != 1 ||
       plan.stores.empty())
      continue;
    std::sort(plan.stores.begin(), plan.stores.end(),
              [](const StagedStore & left, const StagedStore & right) {
                return left.offset < right.offset;
              });
    bool valid = true;
    for(std::size_t i = 0; i < plan.stores.size() && valid; ++i) {
      if(i != 0 &&
         plan.stores[i - 1].offset +
           static_cast<long long>(plan.stores[i - 1].bytes) >
           plan.stores[i].offset)
        valid = false;
      const std::size_t store_node = graph.find(
        function->blocks[plan.stores[i].block].id);
      const std::size_t copy_node = graph.find(
        function->blocks[plan.copy_block].id);
      if(store_node >= graph.successors.size() ||
         copy_node >= graph.successors.size() ||
         !dominators.dominates(store_node, copy_node))
        valid = false;
      if(plan.stores[i].block == plan.copy_block &&
         plan.stores[i].index >= plan.copy_index)
        valid = false;
    }
    if(!valid) continue;

    // Build the member-wise stores in place of the copy, then retire the
    // staging stores; the address plumbing dies in the next DCE.
    std::vector<Instruction> & copy_body =
      function->blocks[plan.copy_block].instructions;
    const Instruction copy = copy_body[plan.copy_index];
    std::vector<Instruction> replacement;
    for(std::size_t i = 0; i < plan.stores.size(); ++i) {
      const Instruction & staged = function->blocks[plan.stores[i].block]
        .instructions[plan.stores[i].index];
      Operand destination = copy.second;
      if(plan.stores[i].offset != 0) {
        Instruction index_ins;
        index_ins.kind = Instruction::IK_INDEX;
        index_ins.type = lowir_model::builtin_lowir_type(
          lowir_model::LTK_I8);
        index_ins.index_projection = lowir_model::IPK_FIELD;
        index_ins.dest = lowir_model::append_lowir_fresh_generated_value(
          *function, lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
        index_ins.first = copy.second;
        index_ins.second = Operand();
        index_ins.second.kind = Operand::OP_INTEGER;
        index_ins.second.has_int_value = true;
        index_ins.second.int_value = plan.stores[i].offset;
        index_ins.second.int_high = 0;
        index_ins.debug_location = copy.debug_location;
        destination = Operand();
        destination.kind = Operand::OP_TEMP;
        destination.value = index_ins.dest;
        replacement.push_back(index_ins);
      }
      Instruction store;
      store.kind = Instruction::IK_STORE;
      store.type = staged.type;
      store.first = staged.first;
      store.second = destination;
      store.debug_location = copy.debug_location;
      replacement.push_back(store);
    }
    copy_body.erase(copy_body.begin() +
                    static_cast<std::ptrdiff_t>(plan.copy_index));
    copy_body.insert(copy_body.begin() +
                     static_cast<std::ptrdiff_t>(plan.copy_index),
                     replacement.begin(), replacement.end());
    std::vector<StagedStore> removals = plan.stores;
    std::sort(removals.begin(), removals.end(),
              [](const StagedStore & left, const StagedStore & right) {
                return left.block < right.block ||
                  (left.block == right.block && left.index < right.index);
              });
    for(std::size_t i = removals.size(); i != 0; --i) {
      std::vector<Instruction> & body =
        function->blocks[removals[i - 1].block].instructions;
      std::size_t erase_index = removals[i - 1].index;
      if(removals[i - 1].block == plan.copy_block &&
         erase_index > plan.copy_index)
        erase_index += replacement.size() - 1;
      body.erase(body.begin() + static_cast<std::ptrdiff_t>(erase_index));
    }
    changed = true;
    if(stats) {
      ++stats->staged_copies_forwarded;
      stats->rewrites += plan.stores.size();
    }
    return true;
  }
  return changed;
}

}  // namespace forwarding_detail

bool forward_staged_object_copies(
    Function * function,
    lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats)
{
  bool changed = false;
  for(std::size_t round = 0; round < function->slot_names.size(); ++round) {
    if(!forwarding_detail::forward_one_staged_copy(function, analysis, stats))
      break;
    changed = true;
  }
  return changed;
}

}  // namespace lowir_opt
