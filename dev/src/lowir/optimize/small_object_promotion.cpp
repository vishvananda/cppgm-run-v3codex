#include "lowir/optimize/small_object_promotion.h"

#include "lowir/model/identity.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/slot_promotion.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
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

namespace {

// Byte-offset provenance for aggregate replacement: a pointer value is
// tracked back to (slot, constant byte offset) through addr/copy/index
// chains.  A non-constant step keeps the slot association (the slot still
// escapes analysis) but poisons the offset.
struct OffsetFact
{
  SlotId slot;
  std::size_t offset = 0;
  bool known = false;
  bool constant = false;
};

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
        if(instruction.dest.valid() && instruction.dest < definitions_.size())
          definitions_[instruction.dest] = &instruction;
      }
  }

  OffsetFact resolve(const Operand & operand)
  {
    if(operand.kind == Operand::OP_SLOT) {
      OffsetFact result;
      result.slot = operand.slot;
      result.known = true;
      result.constant = true;
      return result;
    }
    if(operand.kind != Operand::OP_TEMP) return OffsetFact();
    return resolve(operand.value);
  }

  void materialize_all()
  {
    for(std::size_t value = 0; value < definitions_.size(); ++value)
      resolve(ValueId(static_cast<std::uint32_t>(value)));
  }

private:
  OffsetFact resolve(ValueId value)
  {
    if(value >= definitions_.size()) return OffsetFact();
    if(states_[value] == 2) return facts_[value];
    if(states_[value] == 1) return OffsetFact();
    states_[value] = 1;
    const Instruction * instruction = definitions_[value];
    OffsetFact result;
    if(instruction && instruction->kind == Instruction::IK_ADDR &&
       instruction->first.kind == Operand::OP_SLOT) {
      result.slot = instruction->first.slot;
      result.known = true;
      result.constant = true;
    } else if(instruction && instruction->kind == Instruction::IK_COPY &&
              instruction->type.kind == lowir_model::LTK_PTR) {
      result = resolve(instruction->first);
    } else if(instruction && instruction->kind == Instruction::IK_INDEX) {
      result = resolve(instruction->first);
      if(result.known) {
        if(instruction->second.kind == Operand::OP_INTEGER &&
           instruction->second.has_int_value &&
           instruction->second.int_high == 0 &&
           instruction->second.int_value >= 0)
          result.offset += static_cast<std::size_t>(
            instruction->second.int_value) *
            instruction->type.storage_size;
        else
          result.constant = false;
      }
    }
    facts_[value] = result;
    states_[value] = 2;
    return result;
  }

  std::vector<const Instruction *> definitions_;
  std::vector<OffsetFact> facts_;
  std::vector<unsigned char> states_;
};

struct AggregateField
{
  std::size_t offset = 0;
  LowType type;
  SlotId scalar;
  bool synthesized = false;
};

struct AggregateLayout
{
  std::vector<AggregateField> fields;
  bool invalid = false;
};

bool note_field(AggregateLayout * layout, std::size_t offset,
                const LowType & type)
{
  if(!slot_is_phi_scalar_type(type)) {
    layout->invalid = true;
    return false;
  }
  for(std::size_t i = 0; i < layout->fields.size(); ++i) {
    AggregateField & field = layout->fields[i];
    if(field.offset == offset) {
      if(!same_type(field.type, type)) layout->invalid = true;
      return !layout->invalid;
    }
    const std::size_t begin = field.offset;
    const std::size_t end = begin + field.type.storage_size;
    if(offset > begin && offset < end) {
      layout->invalid = true;
      return false;
    }
    if(begin > offset && begin < offset + type.storage_size) {
      layout->invalid = true;
      return false;
    }
  }
  if(layout->fields.size() >= 16) {
    layout->invalid = true;
    return false;
  }
  AggregateField field;
  field.offset = offset;
  field.type = type;
  layout->fields.push_back(field);
  return true;
}

LowType scalar_gap_type(std::size_t bytes)
{
  LowType type;
  type.kind = bytes >= 8 ? lowir_model::LTK_I64 :
    bytes >= 4 ? lowir_model::LTK_I32 :
    bytes >= 2 ? lowir_model::LTK_I16 : lowir_model::LTK_I8;
  type.storage_size = bytes >= 8 ? 8 : bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;
  type.alignment = type.storage_size;
  return type;
}

// Fill uncovered ranges with synthesized integer fields so a whole-object
// copy expansion moves every byte, exactly like the copyobj it replaces.
void fill_layout_gaps(AggregateLayout * layout, std::size_t total)
{
  std::sort(layout->fields.begin(), layout->fields.end(),
            [](const AggregateField & left, const AggregateField & right) {
              return left.offset < right.offset;
            });
  std::vector<AggregateField> filled;
  std::size_t cursor = 0;
  for(std::size_t i = 0; i <= layout->fields.size(); ++i) {
    const std::size_t next = i < layout->fields.size() ?
      layout->fields[i].offset : total;
    while(cursor < next) {
      AggregateField gap;
      gap.type = scalar_gap_type(next - cursor);
      // Alignment of the gap start bounds the chunk width.
      while(cursor % gap.type.storage_size != 0 ||
            cursor + gap.type.storage_size > next)
        gap.type = scalar_gap_type(gap.type.storage_size / 2);
      gap.offset = cursor;
      gap.synthesized = true;
      filled.push_back(gap);
      cursor += gap.type.storage_size;
      if(filled.size() + layout->fields.size() > 24) {
        layout->invalid = true;
        return;
      }
    }
    if(i < layout->fields.size()) {
      filled.push_back(layout->fields[i]);
      cursor = layout->fields[i].offset +
        layout->fields[i].type.storage_size;
    }
  }
  layout->fields.swap(filled);
}

lowir_model::StringId fresh_field_slot_name(
    lowir_model::LowirProgram * program, const Function & function,
    const std::string & base, std::size_t offset)
{
  if(program->presentation_policy == lowir_model::PRESENTATION_OBJECT_ONLY)
    return lowir_model::StringId();
  std::size_t ordinal = 0;
  for(;;) {
    const std::string candidate = base + "__sroa" +
      std::to_string(offset) +
      (ordinal ? "_" + std::to_string(ordinal) : std::string());
    const lowir_model::StringId interned =
      program->strings.intern(candidate);
    bool taken = false;
    for(std::size_t i = 0; i < function.slot_names.size(); ++i)
      if(function.slot_names[i] == interned) {
        taken = true;
        break;
      }
    if(!taken) return interned;
    ++ordinal;
  }
}

}  // namespace

namespace {

bool promote_small_objects_impl(Function * function, Stats * stats,
                                bool recover_addressed_scalars)
{
  if(function->slots.empty() || function->blocks.empty()) return false;
  const std::size_t slot_count = function->slot_names.size();
  std::vector<unsigned char> candidate(slot_count, 0);
  std::vector<unsigned char> object_candidate(slot_count, 0);
  std::vector<unsigned char> invalid(slot_count, 0);
  std::vector<TypeFact> observed_types(slot_count);
  std::size_t candidate_count = 0;
  std::size_t object_candidate_count = 0;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const SlotId slot = function->slots[i];
    if(small_scalar_object(lowir_model::lowir_slot_type(*function, slot))) {
      candidate[slot] = 1;
      object_candidate[slot] = 1;
      ++candidate_count;
      ++object_candidate_count;
    }
  }
  // Scalar reference temporaries frequently take their address only to feed
  // an inlined load/store pair.  Admit those slots to the same complete-use
  // proof so their derived memory operands can be canonicalized back to the
  // slot form consumed by ordinary scalar promotion.
  for(std::size_t b = 0; recover_addressed_scalars &&
                         b < function->blocks.size(); ++b)
    for(std::size_t i = 0; i < function->blocks[b].instructions.size(); ++i) {
      const Instruction & instruction = function->blocks[b].instructions[i];
      if(instruction.kind != Instruction::IK_ADDR ||
         instruction.first.kind != Operand::OP_SLOT) continue;
      const SlotId slot = instruction.first.slot;
      if(slot >= slot_count || candidate[slot] ||
         !slot_is_phi_scalar_type(
           lowir_model::lowir_slot_type(*function, slot))) continue;
      candidate[slot] = 1;
      ++candidate_count;
      if(stats) ++stats->addressed_scalar_candidates;
    }
  if(stats) stats->small_object_candidates += object_candidate_count;
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
          if(instruction.volatile_access || !address.zero_offset ||
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
        // The scalar rewrite turns the opposite operand into a load or store
        // address, so it must be pointer-valued: an object passed by value
        // has no address at this level.
        const bool source_is_object_value =
          instruction.first.kind == Operand::OP_TEMP &&
          lowir_model::lowir_value_type(
            *function, instruction.first.value).kind == lowir_model::LTK_OBJECT;
        const bool destination_is_object_value =
          instruction.second.kind == Operand::OP_TEMP &&
          lowir_model::lowir_value_type(
            *function, instruction.second.value).kind ==
            lowir_model::LTK_OBJECT;
        if(source_candidate) {
          const LowType & type =
            lowir_model::lowir_slot_type(*function, source.slot);
          if(!source.zero_offset ||
             instruction.byte_count != type.storage_size ||
             destination_is_object_value)
            invalid[source.slot] = 1;
        }
        if(destination_candidate) {
          const LowType & type =
            lowir_model::lowir_slot_type(*function, destination.slot);
          if(!destination.zero_offset ||
             instruction.byte_count != type.storage_size ||
             source_is_object_value)
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
  std::size_t promoted_object_count = 0;
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
    if(object_candidate[slot]) ++promoted_object_count;
    else if(stats) ++stats->addressed_scalars_promoted;
  }
  if(stats) stats->small_objects_promoted += promoted_object_count;
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
          if(stats) {
            if(object_candidate[fact.slot])
              ++stats->small_object_memory_rewrites;
            else
              ++stats->addressed_scalar_memory_rewrites;
          }
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
            if(stats) {
              if(object_candidate[source.slot])
                ++stats->small_object_copies_rewritten;
              else
                ++stats->addressed_scalar_copies_rewritten;
            }
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
          if(stats) {
            const bool object_rewrite =
              (scalar_source && object_candidate[source.slot]) ||
              (scalar_destination && object_candidate[destination.slot]);
            const bool addressed_scalar_rewrite =
              (scalar_source && !object_candidate[source.slot]) ||
              (scalar_destination && !object_candidate[destination.slot]);
            if(object_rewrite) ++stats->small_object_copies_rewritten;
            if(addressed_scalar_rewrite)
              ++stats->addressed_scalar_copies_rewritten;
          }
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
          if(stats) {
            if(object_candidate[destination.slot])
              ++stats->small_object_memory_rewrites;
            else
              ++stats->addressed_scalar_memory_rewrites;
          }
        }
      }
      rewritten.push_back(std::move(instruction));
    }
    function->blocks[b].instructions.swap(rewritten);
  }
  return true;
}

}  // namespace

bool promote_small_objects(Function * function, Stats * stats)
{
  return promote_small_objects_impl(function, stats, false);
}

bool promote_small_objects_with_addressed_scalars(
    Function * function, Stats * stats)
{
  return promote_small_objects_impl(function, stats, true);
}

namespace {

struct AggregateState
{
  std::vector<unsigned char> candidate;
  std::vector<unsigned char> invalid;
  std::vector<AggregateLayout> layouts;
  std::vector<unsigned char> active;
  std::vector<std::uint32_t> component;
};

Operand offset_operand(std::size_t offset)
{
  Operand result;
  result.kind = Operand::OP_INTEGER;
  result.has_int_value = true;
  result.int_value = static_cast<long long>(offset);
  result.int_high = 0;
  result.literal_type.kind = lowir_model::LTK_I64;
  result.literal_type.storage_size = 8;
  result.literal_type.alignment = 8;
  return result;
}

LowType byte_type()
{
  LowType type;
  type.kind = lowir_model::LTK_I8;
  type.storage_size = 1;
  type.alignment = 1;
  return type;
}

LowType pointer_type()
{
  LowType type;
  type.kind = lowir_model::LTK_PTR;
  type.storage_size = 8;
  type.alignment = 8;
  return type;
}

const AggregateField * layout_field(const AggregateLayout & layout,
                                    std::size_t offset)
{
  for(std::size_t i = 0; i < layout.fields.size(); ++i)
    if(layout.fields[i].offset == offset) return &layout.fields[i];
  return 0;
}

void collect_aggregate_slots(const Function & function,
                             OffsetProvenance & provenance,
                             DisjointSlots & sets,
                             AggregateState & state)
{
  const std::size_t slot_count = function.slot_names.size();
  for(std::size_t b = 0; b < function.blocks.size(); ++b)
    for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
      const Instruction & instruction = function.blocks[b].instructions[i];
      const Operand * operands[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t operand = 0; operand < 3; ++operand) {
        const OffsetFact address = provenance.resolve(*operands[operand]);
        if(!address.known || address.slot >= slot_count ||
           !state.candidate[address.slot]) continue;
        if(!allowed_address_use(instruction, *operands[operand], operand))
          state.invalid[address.slot] = 1;
      }
      for(std::size_t operand = 0;
          operand < instruction.args.size(); ++operand) {
        const OffsetFact address = provenance.resolve(instruction.args[operand]);
        if(address.known && address.slot < slot_count &&
           state.candidate[address.slot])
          state.invalid[address.slot] = 1;
      }
      if(instruction.kind == Instruction::IK_LOAD ||
         instruction.kind == Instruction::IK_STORE) {
        const OffsetFact address = provenance.resolve(
          instruction.kind == Instruction::IK_LOAD ?
            instruction.first : instruction.second);
        if(!address.known || address.slot >= slot_count ||
           !state.candidate[address.slot]) continue;
        if(instruction.volatile_access || !address.constant)
          state.invalid[address.slot] = 1;
        else
          note_field(&state.layouts[address.slot], address.offset,
                     instruction.type);
      } else if(instruction.kind == Instruction::IK_COPYOBJ) {
        const OffsetFact source = provenance.resolve(instruction.first);
        const OffsetFact destination = provenance.resolve(instruction.second);
        const OffsetFact * sides[] = {&source, &destination};
        const Operand * others[] = {&instruction.second, &instruction.first};
        for(std::size_t side = 0; side < 2; ++side) {
          const OffsetFact & fact = *sides[side];
          if(!fact.known || fact.slot >= slot_count ||
             !state.candidate[fact.slot]) continue;
          const LowType & type =
            lowir_model::lowir_slot_type(function, fact.slot);
          const bool other_is_object_value =
            others[side]->kind == Operand::OP_TEMP &&
            lowir_model::lowir_value_type(
              function, others[side]->value).kind == lowir_model::LTK_OBJECT;
          if(!fact.constant || fact.offset != 0 ||
             instruction.byte_count != type.storage_size ||
             other_is_object_value)
            state.invalid[fact.slot] = 1;
        }
        if(source.known && source.slot < slot_count &&
           state.candidate[source.slot] &&
           destination.known && destination.slot < slot_count &&
           state.candidate[destination.slot])
          sets.unite(source.slot, destination.slot);
      } else if(instruction.kind == Instruction::IK_ZEROINIT) {
        const OffsetFact destination = provenance.resolve(instruction.first);
        if(destination.known && destination.slot < slot_count &&
           state.candidate[destination.slot]) {
          const LowType & type =
            lowir_model::lowir_slot_type(function, destination.slot);
          if(!destination.constant || destination.offset != 0 ||
             instruction.byte_count != type.storage_size)
            state.invalid[destination.slot] = 1;
        }
      }
    }
}

void rewrite_aggregate_block(Function * function,
                             OffsetProvenance & provenance,
                             const AggregateState & state,
                             std::vector<Instruction> * instructions,
                             Stats * stats)
{
  const std::size_t slot_count = function->slot_names.size();
  std::vector<Instruction> rewritten;
  rewritten.reserve(instructions->size());
  const auto copy_field = [&](const AggregateField & field,
                              bool slot_is_source,
                              lowir_model::SlotId slot_side,
                              const Operand & other,
                              const lowir_model::InstructionDebugLocation & debug) {
    Operand other_address = other;
    if(other_address.kind == Operand::OP_SLOT) {
      Instruction take_address;
      take_address.kind = Instruction::IK_ADDR;
      take_address.type = pointer_type();
      take_address.dest = lowir_model::append_lowir_fresh_generated_value(
        *function, pointer_type());
      take_address.first = other;
      take_address.debug_location = debug;
      other_address = temp_operand(take_address.dest, pointer_type());
      rewritten.push_back(std::move(take_address));
    }
    if(field.offset != 0) {
      Instruction index;
      index.kind = Instruction::IK_INDEX;
      index.type = byte_type();
      index.index_projection = lowir_model::IPK_FIELD;
      index.dest = lowir_model::append_lowir_fresh_generated_value(
        *function, pointer_type());
      index.first = other;
      index.second = offset_operand(field.offset);
      index.debug_location = debug;
      other_address = temp_operand(index.dest, pointer_type());
      rewritten.push_back(std::move(index));
    }
    Instruction load;
    load.kind = Instruction::IK_LOAD;
    load.type = field.type;
    load.dest = lowir_model::append_lowir_fresh_generated_value(
      *function, field.type);
    load.first = slot_is_source ?
      slot_operand(field.scalar, field.type) : other_address;
    load.debug_location = debug;
    const ValueId loaded = load.dest;
    rewritten.push_back(std::move(load));
    Instruction store;
    store.kind = Instruction::IK_STORE;
    store.type = field.type;
    store.first = temp_operand(loaded, field.type);
    store.second = slot_is_source ?
      other_address : slot_operand(field.scalar, field.type);
    store.debug_location = debug;
    rewritten.push_back(std::move(store));
    (void)slot_side;
  };
  for(std::size_t i = 0; i < instructions->size(); ++i) {
    Instruction instruction = std::move((*instructions)[i]);
    if(instruction.kind == Instruction::IK_LOAD ||
       instruction.kind == Instruction::IK_STORE) {
      Operand & address = instruction.kind == Instruction::IK_LOAD ?
        instruction.first : instruction.second;
      const OffsetFact fact = provenance.resolve(address);
      if(fact.known && fact.constant && fact.slot < slot_count &&
         state.active[fact.slot]) {
        const AggregateField * field =
          layout_field(state.layouts[state.component[fact.slot]],
                       fact.offset);
        if(field) {
          const AggregateField * own = layout_field(
            state.layouts[state.component[fact.slot]], fact.offset);
          (void)own;
          // Field slots are per-slot; look up this slot's own copy.
          const AggregateLayout & layout = state.layouts[fact.slot];
          const AggregateField * mine = layout_field(layout, fact.offset);
          address = slot_operand(mine->scalar, mine->type);
          if(stats) ++stats->sroa_memory_rewrites;
        }
      }
      rewritten.push_back(std::move(instruction));
      continue;
    }
    if(instruction.kind == Instruction::IK_COPYOBJ) {
      const OffsetFact source = provenance.resolve(instruction.first);
      const OffsetFact destination = provenance.resolve(instruction.second);
      const bool active_source = source.known && source.constant &&
        source.slot < slot_count && state.active[source.slot];
      const bool active_destination = destination.known &&
        destination.constant && destination.slot < slot_count &&
        state.active[destination.slot];
      if(active_source && active_destination) {
        const AggregateLayout & from = state.layouts[source.slot];
        const AggregateLayout & to = state.layouts[destination.slot];
        for(std::size_t field = 0; field < from.fields.size(); ++field) {
          Instruction load;
          load.kind = Instruction::IK_LOAD;
          load.type = from.fields[field].type;
          load.dest = lowir_model::append_lowir_fresh_generated_value(
            *function, load.type);
          load.first = slot_operand(from.fields[field].scalar, load.type);
          load.debug_location = instruction.debug_location;
          const ValueId loaded = load.dest;
          rewritten.push_back(std::move(load));
          Instruction store;
          store.kind = Instruction::IK_STORE;
          store.type = to.fields[field].type;
          store.first = temp_operand(loaded, store.type);
          store.second = slot_operand(to.fields[field].scalar, store.type);
          store.debug_location = instruction.debug_location;
          rewritten.push_back(std::move(store));
        }
        if(stats) ++stats->sroa_copy_expansions;
        continue;
      }
      if(active_source || active_destination) {
        const lowir_model::SlotId slot =
          active_source ? source.slot : destination.slot;
        const AggregateLayout & layout = state.layouts[slot];
        const Operand other =
          active_source ? instruction.second : instruction.first;
        for(std::size_t field = 0; field < layout.fields.size(); ++field)
          copy_field(layout.fields[field], active_source, slot, other,
                     instruction.debug_location);
        if(stats) ++stats->sroa_copy_expansions;
        continue;
      }
      rewritten.push_back(std::move(instruction));
      continue;
    }
    if(instruction.kind == Instruction::IK_ZEROINIT) {
      const OffsetFact destination = provenance.resolve(instruction.first);
      if(destination.known && destination.constant &&
         destination.slot < slot_count && state.active[destination.slot]) {
        const AggregateLayout & layout = state.layouts[destination.slot];
        for(std::size_t field = 0; field < layout.fields.size(); ++field) {
          Instruction store;
          store.kind = Instruction::IK_STORE;
          store.type = layout.fields[field].type;
          store.first = zero_operand(store.type);
          store.second =
            slot_operand(layout.fields[field].scalar, store.type);
          store.debug_location = instruction.debug_location;
          rewritten.push_back(std::move(store));
        }
        if(stats) ++stats->sroa_memory_rewrites;
        continue;
      }
    }
    rewritten.push_back(std::move(instruction));
  }
  instructions->swap(rewritten);
}

}  // namespace

bool scalar_replace_aggregate_slots(lowir_model::LowirProgram * program,
                                    Function * function, Stats * stats)
{
  if(function->slots.empty() || function->blocks.empty()) return false;
  const std::size_t slot_count = function->slot_names.size();
  AggregateState state;
  state.candidate.assign(slot_count, 0);
  state.invalid.assign(slot_count, 0);
  state.layouts.assign(slot_count, AggregateLayout());
  state.active.assign(slot_count, 0);
  state.component.assign(slot_count, 0);
  std::size_t candidate_count = 0;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const SlotId slot = function->slots[i];
    const LowType & type = lowir_model::lowir_slot_type(*function, slot);
    if(type.kind == lowir_model::LTK_OBJECT && type.storage_size > 8 &&
       type.storage_size <= 64 &&
       !(static_cast<std::size_t>(slot) <
           function->slot_parameter_values.size() &&
         function->slot_parameter_values[slot].valid())) {
      state.candidate[slot] = 1;
      ++candidate_count;
    }
  }
  if(stats) stats->sroa_candidates += candidate_count;
  if(candidate_count == 0) return false;

  OffsetProvenance provenance(*function);
  DisjointSlots sets(slot_count);
  collect_aggregate_slots(*function, provenance, sets, state);

  // Merge component layouts and validity.
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    if(!state.candidate[slot]) continue;
    state.component[slot] = sets.find(static_cast<std::uint32_t>(slot));
  }
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    if(!state.candidate[slot]) continue;
    const std::uint32_t root = state.component[slot];
    if(root == slot) continue;
    AggregateLayout & mine = state.layouts[slot];
    AggregateLayout & merged = state.layouts[root];
    merged.invalid = merged.invalid || mine.invalid;
    state.invalid[root] = state.invalid[root] || state.invalid[slot];
    for(std::size_t field = 0; field < mine.fields.size(); ++field)
      note_field(&merged, mine.fields[field].offset,
                 mine.fields[field].type);
  }
  std::size_t replaced = 0;
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    if(!state.candidate[slot] || state.component[slot] != slot) continue;
    AggregateLayout & layout = state.layouts[slot];
    if(state.invalid[slot] || layout.invalid || layout.fields.empty())
      continue;
    fill_layout_gaps(&layout,
      lowir_model::lowir_slot_type(
        *function, SlotId(static_cast<std::uint32_t>(slot))).storage_size);
    if(layout.invalid) continue;
    state.active[slot] = 1;
  }
  // Distribute the component layout to every member and materialize the
  // per-slot field slots.
  for(std::size_t slot = 0; slot < slot_count; ++slot) {
    if(!state.candidate[slot]) continue;
    const std::uint32_t root = state.component[slot];
    if(state.invalid[slot] || !state.active[root]) continue;
    if(root != slot) state.layouts[slot] = state.layouts[root];
    AggregateLayout & layout = state.layouts[slot];
    std::string base;
    if(program->presentation_policy !=
       lowir_model::PRESENTATION_OBJECT_ONLY)
      base = lowir_model::lowir_slot_name(
        program->strings, *function,
        SlotId(static_cast<std::uint32_t>(slot)));
    for(std::size_t field = 0; field < layout.fields.size(); ++field) {
      layout.fields[field].scalar = lowir_model::append_lowir_slot(
        *function,
        fresh_field_slot_name(program, *function, base,
                              layout.fields[field].offset),
        layout.fields[field].type);
      if(stats) ++stats->sroa_field_slots;
    }
    state.active[slot] = 1;
    ++replaced;
  }
  // Deactivate members of inactive components.
  for(std::size_t slot = 0; slot < slot_count; ++slot)
    if(state.candidate[slot] &&
       (!state.active[state.component[slot]] || state.invalid[slot]))
      state.active[slot] = 0;
  if(stats) stats->sroa_slots_replaced += replaced;
  if(replaced == 0) return false;
  provenance.materialize_all();
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    rewrite_aggregate_block(function, provenance, state,
                            &function->blocks[b].instructions, stats);
  return true;
}

}  // namespace lowir_opt
