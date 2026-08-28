#include "lowir/optimize/slot_forward_o1.h"

#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::Operand;

const std::size_t kNoBlock = static_cast<std::size_t>(-1);

bool is_eh_instruction(Instruction::Kind kind)
{
  return kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_RESUME;
}

}  // namespace

bool remove_dead_slots(Function * function, Stats * stats)
{
  if(function->slots.empty()) return false;
  std::vector<std::size_t> loads(function->slot_names.size(), 0);
  std::vector<unsigned char> escaped(function->slot_names.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        ++loads[ins.first.slot];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           (ins.volatile_access ||
            !((ins.kind == Instruction::IK_LOAD && k == 0) ||
              (ins.kind == Instruction::IK_STORE && k == 1))))
          escaped[values[k]->slot] = 1;
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT)
          escaped[ins.args[k].slot] = 1;
    }
  std::vector<unsigned char> dead(function->slot_names.size(), 0);
  std::size_t dead_count = 0;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const lowir_model::SlotId slot = function->slots[i];
    if(!loads[slot] && !escaped[slot]) { dead[slot] = 1; ++dead_count; }
  }
  if(dead_count == 0) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      if((ins.kind == Instruction::IK_LOAD &&
          ins.first.kind == Operand::OP_SLOT &&
          dead[ins.first.slot]) ||
         (ins.kind == Instruction::IK_STORE &&
          ins.second.kind == Operand::OP_SLOT &&
          dead[ins.second.slot])) {
        if(stats) ++stats->rewrites;
        continue;
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  const std::size_t original_slots = function->slots.size();
  std::size_t kept_slots = 0;
  for(std::size_t i = 0; i < original_slots; ++i)
    if(!dead[function->slots[i]]) {
      if(kept_slots != i)
        function->slots[kept_slots] = std::move(function->slots[i]);
      ++kept_slots;
    }
  function->slots.resize(kept_slots);
  return true;
}

bool local_slot_forward(Function * function, Stats * stats)
{
  if(function->slots.empty()) return false;
  struct UseBlocks
  {
    std::size_t first = kNoBlock;
    bool multiple = false;
  };
  std::vector<UseBlocks> use_blocks(function->value_names.size());
  const auto note_use = [&use_blocks](lowir_model::ValueId value,
                                      std::size_t block) {
    UseBlocks & uses = use_blocks[value];
    if(uses.first == kNoBlock) uses.first = block;
    else if(uses.first != block) uses.multiple = true;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      for(std::size_t k = 0;
          k < optimizer_support::all_operand_count(ins); ++k) {
        const Operand & operand = optimizer_support::all_operand_at(ins, k);
        if(operand.kind == Operand::OP_TEMP) note_use(operand.value, i);
      }
    }
  bool changed = false;
  // Epoch-stamped slot values and load aliases: the per-block vector
  // rebuilds were O(values + slots) each, and the bulk invalidation at
  // every call, bulk-memory, indirect-store, or EH instruction was
  // O(slots).  A stamp mismatch means "absent", so block entry and bulk
  // invalidation are one counter bump; entries stamped 0 stay invalid
  // (the counters start at 1 and only grow).
  std::vector<Operand> values(function->slot_names.size());
  std::vector<std::uint32_t> value_stamp(function->slot_names.size(), 0);
  std::vector<Operand> aliases(function->value_names.size());
  std::vector<std::uint32_t> alias_stamp(function->value_names.size(), 0);
  std::uint32_t value_epoch = 1;
  std::uint32_t alias_epoch = 1;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    ++value_epoch;
    ++alias_epoch;
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      for(std::size_t k = 0;
          k < optimizer_support::all_operand_count(ins); ++k) {
        Operand & operand = optimizer_support::all_operand_at(ins, k);
        if(operand.kind == Operand::OP_TEMP &&
           alias_stamp[operand.value] == alias_epoch)
          operand = aliases[operand.value];
      }
      // Taking a slot's address or storing through an indirect pointer can
      // change a previously recorded slot value.  Inlining commonly exposes
      // exactly this shape, so retaining the old value here would turn a real
      // load into a stale constant before the escape-aware O2 pass sees it.
      const Operand * slot_operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(slot_operands[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          value_stamp[slot_operands[k]->slot] = 0;
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind != Operand::OP_SLOT)
        ++value_epoch;
      if(ins.volatile_access) {
        if(ins.kind == Instruction::IK_STORE &&
           ins.second.kind == Operand::OP_SLOT)
          value_stamp[ins.second.slot] = 0;
        if(kept != j) instructions[kept] = std::move(ins);
        ++kept;
        continue;
      }
      if(ins.kind == Instruction::IK_STORE && ins.second.kind == Operand::OP_SLOT) {
        values[ins.second.slot] = ins.first;
        value_stamp[ins.second.slot] = value_epoch;
      } else if(ins.kind == Instruction::IK_LOAD &&
                ins.first.kind == Operand::OP_SLOT &&
                value_stamp[ins.first.slot] == value_epoch &&
                (!use_blocks[ins.dest].multiple &&
                 (use_blocks[ins.dest].first == kNoBlock ||
                  use_blocks[ins.dest].first == i))) {
        aliases[ins.dest] = values[ins.first.slot];
        alias_stamp[ins.dest] = alias_epoch;
        changed = true;
        if(stats) ++stats->rewrites;
        continue;
      } else {
        if(ins.kind == Instruction::IK_CALL || ins.kind == Instruction::IK_COPYOBJ ||
           ins.kind == Instruction::IK_ZEROINIT || is_eh_instruction(ins.kind))
          ++value_epoch;
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  return changed;
}

bool forward_single_store_slots(Function * function, Stats * stats)
{
  if(function->slots.empty()) return false;
  struct SlotFact
  {
    std::size_t stores = 0;
    std::size_t store_block = 0;
    std::size_t store_instruction = 0;
    Operand value;
    bool escaped = false;
    bool dominates_loads = true;
    std::size_t first_entry_load = kNoBlock;
    bool has_nonentry_load = false;
  };
  struct LoadFact
  {
    std::size_t slot;
    lowir_model::ValueId destination;
  };
  std::vector<SlotFact> facts(function->slot_names.size());
  std::vector<unsigned char> eligible(function->slot_names.size(), 0);
  std::vector<LoadFact> loads;
  std::vector<unsigned char> storage_temporaries(
    function->value_names.size(), 0);
  std::size_t first_exception_edge = kNoBlock;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const lowir_model::SlotId slot = function->slots[i];
    if(lowir_model::lowir_slot_type(*function, slot).kind !=
       lowir_model::LTK_OBJECT)
      eligible[slot] = 1;
  }
  const auto find_slot = [&eligible](const Operand & operand) {
    if(operand.kind != Operand::OP_SLOT) return kNoBlock;
    const std::uint32_t slot = operand.slot;
    return slot < eligible.size() && eligible[slot] ? slot : kNoBlock;
  };
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[b].instructions[j];
      const std::size_t stored_slot = ins.kind == Instruction::IK_STORE ?
        find_slot(ins.second) : kNoBlock;
      if(stored_slot != kNoBlock) {
        SlotFact & fact = facts[stored_slot];
        ++fact.stores;
        fact.store_block = b;
        fact.store_instruction = j;
        fact.value = ins.first;
      }
      const std::size_t loaded_slot = ins.kind == Instruction::IK_LOAD ?
        find_slot(ins.first) : kNoBlock;
      if(loaded_slot != kNoBlock) {
        SlotFact & fact = facts[loaded_slot];
        if(b == 0) fact.first_entry_load =
          std::min(fact.first_entry_load, j);
        else fact.has_nonentry_load = true;
        loads.push_back(LoadFact{loaded_slot, ins.dest});
      }
      if(b == 0 && (ins.kind == Instruction::IK_EH_TRY ||
                    ins.kind == Instruction::IK_EH_CLEANUP))
        first_exception_edge = std::min(first_exception_edge, j);
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries[ins.first.value] = 1;
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries[ins.second.value] = 1;
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k) {
        const std::size_t slot = find_slot(*operands[k]);
        if(slot != kNoBlock &&
           (ins.volatile_access ||
            !((ins.kind == Instruction::IK_LOAD && k == 0) ||
              (ins.kind == Instruction::IK_STORE && k == 1))))
          facts[slot].escaped = true;
      }
      for(std::size_t k = 0; k < ins.args.size(); ++k) {
        const std::size_t slot = find_slot(ins.args[k]);
        if(slot != kNoBlock) facts[slot].escaped = true;
      }
    }
  for(std::size_t i = 0; i < facts.size(); ++i) {
    if(!eligible[i]) continue;
    SlotFact & fact = facts[i];
    if(fact.stores != 1 || fact.store_block != 0 || fact.escaped) continue;
    if(fact.first_entry_load < fact.store_instruction ||
       (fact.has_nonentry_load &&
        first_exception_edge < fact.store_instruction))
      fact.dominates_loads = false;
  }
  std::vector<unsigned char> forwarded(facts.size(), 0);
  std::size_t forwarded_count = 0;
  for(std::size_t i = 0; i < facts.size(); ++i)
    if(eligible[i] && facts[i].stores == 1 && facts[i].store_block == 0 &&
       !facts[i].escaped && facts[i].dominates_loads) {
      forwarded[i] = 1;
      ++forwarded_count;
    }
  if(!forwarded_count) return false;

  std::vector<Operand> aliases(function->value_names.size());
  std::vector<unsigned char> has_alias(function->value_names.size(), 0);
  for(std::size_t i = 0; i < loads.size(); ++i)
    if(forwarded[loads[i].slot] &&
       !storage_temporaries[loads[i].destination]) {
      aliases[loads[i].destination] = facts[loads[i].slot].value;
      has_alias[loads[i].destination] = 1;
    }
  const auto resolve_alias = [&aliases, &has_alias](Operand value) {
    for(std::size_t step = 0;
        step < aliases.size() && value.kind == Operand::OP_TEMP; ++step) {
      const std::uint32_t id = value.value;
      if(id >= aliases.size() || !has_alias[id]) break;
      value = aliases[id];
    }
    return value;
  };
  for(std::size_t b = 0; b < function->blocks.size(); ++b) {
    std::vector<Instruction> & instructions =
      function->blocks[b].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      const std::size_t loaded_slot = ins.kind == Instruction::IK_LOAD ?
        find_slot(ins.first) : kNoBlock;
      const std::size_t stored_slot = ins.kind == Instruction::IK_STORE ?
        find_slot(ins.second) : kNoBlock;
      if(loaded_slot != kNoBlock && forwarded[loaded_slot] &&
         storage_temporaries[ins.dest]) {
        ins.first = resolve_alias(facts[loaded_slot].value);
        ins.kind = ins.first.kind == Operand::OP_INTEGER ||
          ins.first.kind == Operand::OP_FLOAT ?
            Instruction::IK_CONST : Instruction::IK_COPY;
        ins.second = Operand();
        ins.third = Operand();
        if(stats) ++stats->rewrites;
      } else if((loaded_slot != kNoBlock && forwarded[loaded_slot]) ||
         (stored_slot != kNoBlock && forwarded[stored_slot])) {
        if(stats) ++stats->rewrites;
        continue;
      } else {
        for(std::size_t k = 0;
            k < optimizer_support::all_operand_count(ins); ++k) {
          Operand & operand = optimizer_support::all_operand_at(ins, k);
          operand = resolve_alias(operand);
        }
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  const std::size_t original_slots = function->slots.size();
  std::size_t kept_slots = 0;
  for(std::size_t i = 0; i < original_slots; ++i)
    if(!forwarded[function->slots[i]]) {
      if(kept_slots != i)
        function->slots[kept_slots] = std::move(function->slots[i]);
      ++kept_slots;
    }
  function->slots.resize(kept_slots);
  return true;
}

}  // namespace lowir_opt
