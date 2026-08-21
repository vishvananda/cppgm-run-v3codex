#include "lowir_small_object_promotion.h"

#include "lowir_identity.h"
#include "lowir_opt.h"
#include "lowir_slot_promotion.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using lowir_model::SlotId;
using lowir_model::ValueId;

struct AddressFact
{
  SlotId slot;
  bool known = false;
  bool zero_offset = false;
};

struct TypeFact
{
  LowType type;
  bool known = false;
  bool conflict = false;
};

class DisjointSlots
{
public:
  explicit DisjointSlots(std::size_t count) : parent_(count), rank_(count, 0)
  {
    for(std::size_t i = 0; i < count; ++i)
      parent_[i] = static_cast<std::uint32_t>(i);
  }

  std::uint32_t find(std::uint32_t slot)
  {
    std::uint32_t root = slot;
    while(parent_[root] != root) root = parent_[root];
    while(parent_[slot] != slot) {
      const std::uint32_t next = parent_[slot];
      parent_[slot] = root;
      slot = next;
    }
    return root;
  }

  void unite(std::uint32_t left, std::uint32_t right)
  {
    left = find(left);
    right = find(right);
    if(left == right) return;
    if(rank_[left] < rank_[right]) std::swap(left, right);
    parent_[right] = left;
    if(rank_[left] == rank_[right]) ++rank_[left];
  }

private:
  std::vector<std::uint32_t> parent_;
  std::vector<unsigned char> rank_;
};

class AddressProvenance
{
public:
  explicit AddressProvenance(const Function & function)
    : definitions_(function.value_names.size(), 0),
      facts_(function.value_names.size()),
      states_(function.value_names.size(), 0)
  {
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
        const Instruction & instruction = function.blocks[b].instructions[i];
        if(instruction.dest.valid() && instruction.dest < definitions_.size())
          definitions_[instruction.dest] = &instruction;
      }
  }

  AddressFact resolve(const Operand & operand)
  {
    if(operand.kind == Operand::OP_SLOT) {
      AddressFact result;
      result.slot = operand.slot;
      result.known = true;
      result.zero_offset = true;
      return result;
    }
    if(operand.kind != Operand::OP_TEMP) return AddressFact();
    return resolve(operand.value);
  }

  void materialize_all()
  {
    for(std::size_t value = 0; value < definitions_.size(); ++value)
      resolve(ValueId(static_cast<std::uint32_t>(value)));
  }

private:
  AddressFact resolve(ValueId value)
  {
    if(value >= definitions_.size()) return AddressFact();
    if(states_[value] == 2) return facts_[value];
    if(states_[value] == 1) return AddressFact();
    states_[value] = 1;
    const Instruction * instruction = definitions_[value];
    AddressFact result;
    if(instruction && instruction->kind == Instruction::IK_ADDR &&
       instruction->first.kind == Operand::OP_SLOT) {
      result.slot = instruction->first.slot;
      result.known = true;
      result.zero_offset = true;
    } else if(instruction && instruction->kind == Instruction::IK_COPY &&
              instruction->type.kind == lowir_model::LTK_PTR) {
      result = resolve(instruction->first);
    } else if(instruction && instruction->kind == Instruction::IK_INDEX) {
      result = resolve(instruction->first);
      if(result.known)
        result.zero_offset = result.zero_offset &&
          instruction->second.kind == Operand::OP_INTEGER &&
          instruction->second.has_int_value &&
          instruction->second.int_value == 0 &&
          instruction->second.int_high == 0;
    }
    facts_[value] = result;
    states_[value] = 2;
    return result;
  }

  std::vector<const Instruction *> definitions_;
  std::vector<AddressFact> facts_;
  std::vector<unsigned char> states_;
};

bool small_scalar_object(const LowType & type)
{
  return type.kind == lowir_model::LTK_OBJECT &&
    (type.storage_size == 1 || type.storage_size == 2 ||
     type.storage_size == 4 || type.storage_size == 8);
}

bool complete_scalar_type(const LowType & type, std::size_t bytes)
{
  return slot_is_phi_scalar_type(type) && type.storage_size == bytes;
}

bool same_type(const LowType & left, const LowType & right)
{
  return lowir_model::same_lowir_type(left, right);
}

void note_type(TypeFact * fact, const LowType & type)
{
  if(!fact->known) {
    fact->type = type;
    fact->known = true;
  } else if(!same_type(fact->type, type)) {
    fact->conflict = true;
  }
}

bool allowed_address_use(const Instruction & instruction,
                         const Operand & value, std::size_t operand)
{
  switch(instruction.kind) {
  case Instruction::IK_ADDR:
    return operand == 0 && value.kind == Operand::OP_SLOT;
  case Instruction::IK_COPY:
    return operand == 0 && value.kind == Operand::OP_TEMP &&
      instruction.type.kind == lowir_model::LTK_PTR;
  case Instruction::IK_INDEX:
    return operand == 0 && value.kind == Operand::OP_TEMP;
  case Instruction::IK_LOAD:
    return operand == 0;
  case Instruction::IK_STORE:
    return operand == 1;
  case Instruction::IK_COPYOBJ:
    return operand < 2;
  case Instruction::IK_ZEROINIT:
    return operand == 0;
  default:
    return false;
  }
}

Operand slot_operand(SlotId slot, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_SLOT;
  result.slot = slot;
  result.literal_type = type;
  return result;
}

Operand temp_operand(ValueId value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_TEMP;
  result.value = value;
  result.literal_type = type;
  return result;
}

Operand zero_operand(const LowType & type)
{
  Operand result;
  result.literal_type = type;
  if(type.kind == lowir_model::LTK_F32 ||
     type.kind == lowir_model::LTK_F64) {
    result.kind = Operand::OP_FLOAT;
    result.has_float_bits = true;
    result.literal_low = 0;
    result.literal_high = 0;
  } else {
    result.kind = Operand::OP_INTEGER;
    result.has_int_value = true;
    result.int_value = 0;
    result.int_high = 0;
  }
  return result;
}

}  // namespace

bool promote_small_objects(Function * function, Stats * stats)
{
  if(function->slots.empty() || function->blocks.empty()) return false;
  const std::size_t slot_count = function->slot_names.size();
  std::vector<unsigned char> candidate(slot_count, 0);
  std::vector<unsigned char> invalid(slot_count, 0);
  std::vector<TypeFact> observed_types(slot_count);
  std::size_t candidate_count = 0;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const SlotId slot = function->slots[i];
    if(small_scalar_object(lowir_model::lowir_slot_type(*function, slot))) {
      candidate[slot] = 1;
      ++candidate_count;
    }
  }
  if(stats) stats->small_object_candidates += candidate_count;
  if(candidate_count == 0) return false;

  AddressProvenance provenance(*function);
  DisjointSlots sets(slot_count);
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    for(std::size_t i = 0; i < function->blocks[b].instructions.size(); ++i) {
      const Instruction & instruction = function->blocks[b].instructions[i];
      const Operand * operands[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t operand = 0; operand < 3; ++operand) {
        const AddressFact address = provenance.resolve(*operands[operand]);
        if(!address.known || address.slot >= slot_count ||
           !candidate[address.slot]) continue;
        if(!allowed_address_use(instruction, *operands[operand], operand)) {
          invalid[address.slot] = 1;
          continue;
        }
        const bool carries_address =
          (instruction.kind == Instruction::IK_ADDR ||
           instruction.kind == Instruction::IK_COPY ||
           instruction.kind == Instruction::IK_INDEX) && operand == 0;
        if(!carries_address && !address.zero_offset)
          invalid[address.slot] = 1;
      }
      for(std::size_t operand = 0; operand < instruction.args.size(); ++operand) {
        const AddressFact address = provenance.resolve(instruction.args[operand]);
        if(address.known && address.slot < slot_count && candidate[address.slot])
          invalid[address.slot] = 1;
      }

      if(instruction.kind == Instruction::IK_LOAD ||
         instruction.kind == Instruction::IK_STORE) {
        const AddressFact address = provenance.resolve(
          instruction.kind == Instruction::IK_LOAD ?
            instruction.first : instruction.second);
        if(address.known && address.slot < slot_count &&
           candidate[address.slot]) {
          const std::size_t bytes =
            lowir_model::lowir_slot_type(*function, address.slot).storage_size;
          if(!address.zero_offset ||
             !complete_scalar_type(instruction.type, bytes))
            invalid[address.slot] = 1;
          else
            note_type(&observed_types[address.slot], instruction.type);
        }
      } else if(instruction.kind == Instruction::IK_COPYOBJ) {
        const AddressFact source = provenance.resolve(instruction.first);
        const AddressFact destination = provenance.resolve(instruction.second);
        const bool source_candidate = source.known && source.slot < slot_count &&
          candidate[source.slot];
        const bool destination_candidate = destination.known &&
          destination.slot < slot_count && candidate[destination.slot];
        if(source_candidate) {
          const LowType & type =
            lowir_model::lowir_slot_type(*function, source.slot);
          if(!source.zero_offset || instruction.byte_count != type.storage_size)
            invalid[source.slot] = 1;
        }
        if(destination_candidate) {
          const LowType & type =
            lowir_model::lowir_slot_type(*function, destination.slot);
          if(!destination.zero_offset ||
             instruction.byte_count != type.storage_size)
            invalid[destination.slot] = 1;
        }
        if(source_candidate && destination_candidate &&
           source.zero_offset && destination.zero_offset &&
           instruction.byte_count ==
             lowir_model::lowir_slot_type(*function, source.slot).storage_size &&
           instruction.byte_count ==
             lowir_model::lowir_slot_type(*function, destination.slot).storage_size)
          sets.unite(source.slot, destination.slot);
      } else if(instruction.kind == Instruction::IK_ZEROINIT) {
        const AddressFact destination = provenance.resolve(instruction.first);
        if(destination.known && destination.slot < slot_count &&
           candidate[destination.slot]) {
          const LowType & type =
            lowir_model::lowir_slot_type(*function, destination.slot);
          if(!destination.zero_offset ||
             instruction.byte_count != type.storage_size)
            invalid[destination.slot] = 1;
        }
      }
    }

  std::vector<TypeFact> component_types(slot_count);
  std::vector<unsigned char> component_invalid(slot_count, 0);
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    if(!candidate[slot]) continue;
    const std::uint32_t root = sets.find(static_cast<std::uint32_t>(slot));
    component_invalid[root] = component_invalid[root] || invalid[slot];
    if(observed_types[slot].conflict) component_invalid[root] = 1;
    if(observed_types[slot].known)
      note_type(&component_types[root], observed_types[slot].type);
  }
  std::vector<unsigned char> active(slot_count, 0);
  std::vector<LowType> scalar_types(slot_count);
  std::size_t promoted_count = 0;
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    if(!candidate[slot]) continue;
    const std::uint32_t root = sets.find(static_cast<std::uint32_t>(slot));
    const TypeFact & type = component_types[root];
    if(component_invalid[root] || type.conflict || !type.known) continue;
    const LowType & object_type = lowir_model::lowir_slot_type(
      *function, SlotId(static_cast<std::uint32_t>(slot)));
    if(!complete_scalar_type(type.type, object_type.storage_size)) continue;
    active[slot] = 1;
    scalar_types[slot] = type.type;
    function->slot_types[slot] = type.type;
    ++promoted_count;
  }
  if(stats) stats->small_objects_promoted += promoted_count;
  if(promoted_count == 0) return false;
  // Rewriting moves block instruction vectors.  Cache every value fact while
  // the definition table still points into the original vectors.
  provenance.materialize_all();

  for(std::size_t b = 0; b < function->blocks.size(); ++b) {
    std::vector<Instruction> rewritten;
    rewritten.reserve(function->blocks[b].instructions.size());
    for(std::size_t i = 0; i < function->blocks[b].instructions.size(); ++i) {
      Instruction instruction =
        std::move(function->blocks[b].instructions[i]);
      if(instruction.kind == Instruction::IK_LOAD ||
         instruction.kind == Instruction::IK_STORE) {
        Operand & address = instruction.kind == Instruction::IK_LOAD ?
          instruction.first : instruction.second;
        const AddressFact fact = provenance.resolve(address);
        if(fact.known && fact.zero_offset && fact.slot < slot_count &&
           active[fact.slot]) {
          address = slot_operand(fact.slot, scalar_types[fact.slot]);
          if(stats) ++stats->small_object_memory_rewrites;
        }
        rewritten.push_back(std::move(instruction));
        continue;
      }
      if(instruction.kind == Instruction::IK_COPYOBJ) {
        const AddressFact source = provenance.resolve(instruction.first);
        const AddressFact destination = provenance.resolve(instruction.second);
        const bool scalar_source = source.known && source.zero_offset &&
          source.slot < slot_count && active[source.slot];
        const bool scalar_destination = destination.known &&
          destination.zero_offset && destination.slot < slot_count &&
          active[destination.slot];
        if(scalar_source || scalar_destination) {
          if(scalar_source && scalar_destination &&
             source.slot == destination.slot) {
            if(stats) ++stats->small_object_copies_rewritten;
            continue;
          }
          const LowType & type = scalar_source ? scalar_types[source.slot] :
            scalar_types[destination.slot];
          Instruction load;
          load.kind = Instruction::IK_LOAD;
          load.type = type;
          load.dest = lowir_model::append_lowir_fresh_generated_value(
            *function, type);
          load.first = scalar_source ? slot_operand(source.slot, type) :
            instruction.first;
          load.debug_location = instruction.debug_location;
          rewritten.push_back(std::move(load));

          Instruction store;
          store.kind = Instruction::IK_STORE;
          store.type = type;
          store.first = temp_operand(rewritten.back().dest, type);
          store.second = scalar_destination ?
            slot_operand(destination.slot, type) : instruction.second;
          store.debug_location = instruction.debug_location;
          rewritten.push_back(std::move(store));
          if(stats) ++stats->small_object_copies_rewritten;
          continue;
        }
      } else if(instruction.kind == Instruction::IK_ZEROINIT) {
        const AddressFact destination = provenance.resolve(instruction.first);
        if(destination.known && destination.zero_offset &&
           destination.slot < slot_count && active[destination.slot]) {
          instruction.kind = Instruction::IK_STORE;
          instruction.type = scalar_types[destination.slot];
          instruction.first = zero_operand(instruction.type);
          instruction.second = slot_operand(destination.slot, instruction.type);
          instruction.third = Operand();
          instruction.args.clear();
          instruction.byte_count = 0;
          instruction.byte_alignment = 1;
          if(stats) ++stats->small_object_memory_rewrites;
        }
      }
      rewritten.push_back(std::move(instruction));
    }
    function->blocks[b].instructions.swap(rewritten);
  }
  return true;
}

}  // namespace lowir_opt
