#include "lowir/optimize/staged_copy_forwarding.h"
#include "lowir/optimize/scalar_rules.h"

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

struct ScalarCopyGroup
{
  std::size_t start = 0;
  long long offset = 0;
  std::size_t bytes = 0;
  Operand source_base;
  Operand destination_base;
  lowir_model::ValueId source_address;
  lowir_model::ValueId destination_address;
  lowir_model::ValueId loaded_value;
  std::size_t alignment = 1;
};

bool noalias_parameter_value(const Function & function,
                             const Operand & operand)
{
  if(operand.kind != Operand::OP_TEMP) return false;
  for(std::size_t i = 0; i < function.params.size(); ++i)
    if(function.params[i].value == operand.value)
      return function.params[i].metadata.alias ==
        lowir_model::PALM_NOALIAS;
  return false;
}

bool proven_disjoint_bases(const Function & function,
                           const Operand & source,
                           const Operand & destination)
{
  if(lowir_opt::same_operand(source, destination)) return true;
  return noalias_parameter_value(function, source) &&
    noalias_parameter_value(function, destination);
}

struct RelativePointer
{
  long long offset = 0;
  bool known = false;
};

struct RootedPointer
{
  Operand root;
  long long offset = 0;
  bool known = false;
};

class RootedPointerProvenance
{
public:
  explicit RootedPointerProvenance(const Function & function)
    : definitions_(function.value_names.size(), 0),
      facts_(function.value_names.size()),
      states_(function.value_names.size(), 0)
  {
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
        const Instruction & instruction =
          function.blocks[b].instructions[i];
        if(instruction.dest.valid() && instruction.dest < definitions_.size())
          definitions_[instruction.dest] = &instruction;
      }
  }

  RootedPointer resolve(const Operand & operand)
  {
    if(operand.kind == Operand::OP_SLOT) {
      RootedPointer result;
      result.root = operand;
      result.known = true;
      return result;
    }
    if(operand.kind != Operand::OP_TEMP) return RootedPointer();
    return resolve(operand.value);
  }

private:
  RootedPointer resolve(ValueId value)
  {
    if(value >= definitions_.size()) return RootedPointer();
    if(states_[value] == 2) return facts_[value];
    if(states_[value] == 1) return RootedPointer();
    states_[value] = 1;
    const Instruction * instruction = definitions_[value];
    RootedPointer result;
    if(!instruction) {
      result.root.kind = Operand::OP_TEMP;
      result.root.value = value;
      result.root.literal_type =
        lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
      result.known = true;
    } else if(instruction->kind == Instruction::IK_ADDR &&
              instruction->first.kind == Operand::OP_SLOT) {
      result.root = instruction->first;
      result.known = true;
    } else if(instruction->kind == Instruction::IK_COPY &&
              instruction->type.kind == lowir_model::LTK_PTR) {
      result = resolve(instruction->first);
    } else if(instruction->kind == Instruction::IK_INDEX &&
              instruction->second.kind == Operand::OP_INTEGER &&
              instruction->second.has_int_value &&
              instruction->second.int_high == 0) {
      result = resolve(instruction->first);
      if(result.known)
        result.offset += instruction->second.int_value;
    }
    facts_[value] = result;
    states_[value] = 2;
    return result;
  }

  std::vector<const Instruction *> definitions_;
  std::vector<RootedPointer> facts_;
  std::vector<unsigned char> states_;
};

bool same_rooted_pointer(const RootedPointer & left,
                         const RootedPointer & right)
{
  return left.known && right.known && left.offset == right.offset &&
    lowir_opt::same_operand(left.root, right.root);
}

bool overwrite_scan_pure(Instruction::Kind kind)
{
  return kind == Instruction::IK_CONST || kind == Instruction::IK_COPY ||
    kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

RelativePointer relative_pointer(
    const Operand & operand, const Operand & base,
    const std::vector<RelativePointer> & values)
{
  if(lowir_opt::same_operand(operand, base)) {
    RelativePointer result;
    result.known = true;
    return result;
  }
  if(operand.kind == Operand::OP_TEMP && operand.value < values.size())
    return values[operand.value];
  return RelativePointer();
}

bool parse_scalar_copy_group(const std::vector<Instruction> & instructions,
                             std::size_t start,
                             const std::vector<std::size_t> & uses,
                             ScalarCopyGroup * result)
{
  if(start + 4 > instructions.size()) return false;
  const Instruction & first_index = instructions[start];
  const Instruction & second_index = instructions[start + 1];
  const Instruction & load = instructions[start + 2];
  const Instruction & store = instructions[start + 3];
  if(first_index.kind != Instruction::IK_INDEX ||
     second_index.kind != Instruction::IK_INDEX ||
     first_index.second.kind != Operand::OP_INTEGER ||
     second_index.second.kind != Operand::OP_INTEGER ||
     !first_index.second.has_int_value ||
     !second_index.second.has_int_value ||
     first_index.second.int_high != 0 ||
     second_index.second.int_high != 0 ||
     first_index.second.int_value < 0 ||
     first_index.second.int_value != second_index.second.int_value ||
     load.kind != Instruction::IK_LOAD || load.volatile_access ||
     load.first.kind != Operand::OP_TEMP ||
     store.kind != Instruction::IK_STORE || store.volatile_access ||
     store.first.kind != Operand::OP_TEMP ||
     store.first.value != load.dest ||
     store.second.kind != Operand::OP_TEMP ||
     !lowir_model::same_lowir_type(load.type, store.type)) return false;
  const std::size_t bytes = scalar_store_bytes(load.type);
  if(bytes == 0 || !first_index.dest.valid() ||
     !second_index.dest.valid() || !load.dest.valid()) return false;
  const Instruction * source_index = 0;
  const Instruction * destination_index = 0;
  if(load.first.value == first_index.dest &&
     store.second.value == second_index.dest) {
    source_index = &first_index;
    destination_index = &second_index;
  } else if(load.first.value == second_index.dest &&
            store.second.value == first_index.dest) {
    source_index = &second_index;
    destination_index = &first_index;
  } else return false;
  const std::uint32_t ids[] = {
    static_cast<std::uint32_t>(first_index.dest),
    static_cast<std::uint32_t>(second_index.dest),
    static_cast<std::uint32_t>(load.dest)};
  for(std::size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    if(ids[i] >= uses.size() || uses[ids[i]] != 1) return false;
  result->start = start;
  result->offset = first_index.second.int_value;
  result->bytes = bytes;
  result->source_base = source_index->first;
  result->destination_base = destination_index->first;
  result->source_address = source_index->dest;
  result->destination_address = destination_index->dest;
  result->loaded_value = load.dest;
  result->alignment = load.type.alignment;
  return true;
}

std::vector<std::size_t> instruction_uses(const Function & function)
{
  std::vector<std::size_t> result(function.value_names.size(), 0);
  const auto record = [&result](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP && operand.value < result.size())
      ++result[operand.value];
  };
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t i = 0; i < function.blocks[block].instructions.size(); ++i) {
      const Instruction & instruction =
        function.blocks[block].instructions[i];
      record(instruction.first);
      record(instruction.second);
      record(instruction.third);
      for(std::size_t arg = 0; arg < instruction.args.size(); ++arg)
        record(instruction.args[arg]);
    }
  return result;
}

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
        if(ins.volatile_access) { plan.disqualified = true; continue; }
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
    // A slot-valued copy destination cannot carry member indexing or
    // scalar stores directly; take its address once and let the member
    // accesses run through the pointer.
    Operand copy_base = copy.second;
    if(copy_base.kind == Operand::OP_SLOT) {
      Instruction take_address;
      take_address.kind = Instruction::IK_ADDR;
      take_address.type = lowir_model::builtin_lowir_type(
        lowir_model::LTK_PTR);
      take_address.dest = lowir_model::append_lowir_fresh_generated_value(
        *function, take_address.type);
      take_address.first = copy.second;
      take_address.debug_location = copy.debug_location;
      copy_base = Operand();
      copy_base.kind = Operand::OP_TEMP;
      copy_base.value = take_address.dest;
      replacement.push_back(take_address);
    }
    for(std::size_t i = 0; i < plan.stores.size(); ++i) {
      const Instruction & staged = function->blocks[plan.stores[i].block]
        .instructions[plan.stores[i].index];
      Operand destination = copy_base;
      if(plan.stores[i].offset != 0) {
        Instruction index_ins;
        index_ins.kind = Instruction::IK_INDEX;
        index_ins.type = lowir_model::builtin_lowir_type(
          lowir_model::LTK_I8);
        index_ins.index_projection = lowir_model::IPK_FIELD;
        index_ins.dest = lowir_model::append_lowir_fresh_generated_value(
          *function, lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
        index_ins.first = copy_base;
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

bool eliminate_fully_overwritten_zero_inits(Function * function, Stats * stats)
{
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    std::vector<unsigned char> dead(instructions.size(), 0);
    for(std::size_t start = 0; start < instructions.size(); ++start) {
      const Instruction & zero = instructions[start];
      if(zero.kind != Instruction::IK_ZEROINIT || zero.byte_count == 0 ||
         zero.byte_count > 64 || zero.volatile_access)
        continue;
      std::vector<RelativePointer> values(function->value_names.size());
      std::vector<unsigned char> covered(zero.byte_count, 0);
      std::size_t covered_bytes = 0;
      for(std::size_t scan = start + 1; scan < instructions.size(); ++scan) {
        const Instruction & instruction = instructions[scan];
        if(overwrite_scan_pure(instruction.kind)) {
          if(instruction.dest.valid() && instruction.dest < values.size()) {
            RelativePointer fact;
            if(instruction.kind == Instruction::IK_COPY &&
               instruction.type.kind == lowir_model::LTK_PTR)
              fact = relative_pointer(instruction.first, zero.first, values);
            else if(instruction.kind == Instruction::IK_INDEX &&
                    instruction.index_projection == lowir_model::IPK_FIELD &&
                    instruction.second.kind == Operand::OP_INTEGER &&
                    instruction.second.has_int_value &&
                    instruction.second.int_high == 0) {
              fact = relative_pointer(instruction.first, zero.first, values);
              if(fact.known)
                fact.offset += instruction.second.int_value;
            }
            values[instruction.dest] = fact;
          }
          continue;
        }
        if(instruction.kind != Instruction::IK_STORE ||
           instruction.volatile_access)
          break;
        const RelativePointer destination =
          relative_pointer(instruction.second, zero.first, values);
        const std::size_t bytes = scalar_store_bytes(instruction.type);
        if(!destination.known || destination.offset < 0 || bytes == 0 ||
           static_cast<std::size_t>(destination.offset) > zero.byte_count ||
           bytes > zero.byte_count -
             static_cast<std::size_t>(destination.offset))
          break;
        const std::size_t first = static_cast<std::size_t>(destination.offset);
        for(std::size_t byte = first; byte < first + bytes; ++byte)
          if(!covered[byte]) {
            covered[byte] = 1;
            ++covered_bytes;
          }
        if(covered_bytes == zero.byte_count) {
          dead[start] = 1;
          changed = true;
          if(stats) {
            ++stats->overwritten_zero_inits;
            stats->overwritten_zero_bytes += zero.byte_count;
            ++stats->rewrites;
          }
          break;
        }
      }
    }
    if(std::find(dead.begin(), dead.end(), 1) != dead.end()) {
      std::vector<Instruction> kept;
      kept.reserve(instructions.size());
      for(std::size_t i = 0; i < instructions.size(); ++i)
        if(!dead[i]) kept.push_back(std::move(instructions[i]));
      instructions.swap(kept);
    }
  }
  return changed;
}

bool coalesce_adjacent_scalar_copies(Function * function, Stats * stats)
{
  const std::vector<std::size_t> uses = instruction_uses(*function);
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    std::vector<Instruction> rebuilt;
    rebuilt.reserve(instructions.size());
    for(std::size_t index = 0; index < instructions.size();) {
      ScalarCopyGroup first;
      if(!parse_scalar_copy_group(instructions, index, uses, &first)) {
        rebuilt.push_back(std::move(instructions[index++]));
        continue;
      }
      if(!proven_disjoint_bases(
           *function, first.source_base, first.destination_base)) {
        rebuilt.push_back(std::move(instructions[index++]));
        continue;
      }
      std::size_t end = index + 4;
      std::size_t bytes = first.bytes;
      std::size_t alignment = first.alignment;
      std::size_t groups = 1;
      while(end < instructions.size()) {
        ScalarCopyGroup next;
        if(!parse_scalar_copy_group(instructions, end, uses, &next) ||
           !lowir_opt::same_operand(
             first.source_base, next.source_base) ||
           !lowir_opt::same_operand(
             first.destination_base, next.destination_base) ||
           next.offset != first.offset + static_cast<long long>(bytes))
          break;
        bytes += next.bytes;
        alignment = std::min(alignment, next.alignment);
        ++groups;
        end += 4;
      }
      if(groups < 2) {
        rebuilt.push_back(std::move(instructions[index++]));
        continue;
      }
      rebuilt.push_back(std::move(instructions[index]));
      rebuilt.push_back(std::move(instructions[index + 1]));
      Instruction copy;
      copy.kind = Instruction::IK_COPYOBJ;
      copy.byte_count = bytes;
      copy.byte_alignment = alignment;
      copy.first.kind = Operand::OP_TEMP;
      copy.first.value = first.source_address;
      copy.first.literal_type = lowir_model::builtin_lowir_type(
        lowir_model::LTK_PTR);
      copy.second.kind = Operand::OP_TEMP;
      copy.second.value = first.destination_address;
      copy.second.literal_type = lowir_model::builtin_lowir_type(
        lowir_model::LTK_PTR);
      copy.debug_location = instructions[index + 3].debug_location;
      rebuilt.push_back(std::move(copy));
      index = end;
      changed = true;
      if(stats) {
        ++stats->adjacent_scalar_copy_runs;
        stats->adjacent_scalar_copy_groups += groups;
        stats->adjacent_scalar_copy_bytes += bytes;
        stats->rewrites += groups * 4 - 3;
      }
    }
    instructions.swap(rebuilt);
  }
  return changed;
}

namespace {

bool pointer_parameter(const Function & function, const Operand & operand)
{
  if(operand.kind != Operand::OP_TEMP) return false;
  for(std::size_t i = 0; i < function.params.size(); ++i)
    if(function.params[i].value == operand.value)
      return function.params[i].type.kind == lowir_model::LTK_PTR;
  return false;
}

bool private_object_slot(const Function & function, const Operand & operand,
                         std::size_t bytes)
{
  if(operand.kind != Operand::OP_SLOT || operand.slot >= function.slot_names.size())
    return false;
  const LowType & type = lowir_model::lowir_slot_type(function, operand.slot);
  return type.kind == lowir_model::LTK_OBJECT &&
    type.storage_size == bytes;
}

bool in_byte_span(long long offset, std::size_t bytes, std::size_t span)
{
  return offset >= 0 && static_cast<std::size_t>(offset) <= span &&
    bytes <= span - static_cast<std::size_t>(offset);
}

void cover_bytes(std::vector<unsigned char> * covered,
                 long long offset, std::size_t bytes)
{
  for(std::size_t i = 0; i < bytes; ++i)
    (*covered)[static_cast<std::size_t>(offset) + i] = 1;
}

Instruction indexed_address(Function * function, const Operand & base,
                            std::size_t offset,
                            const Instruction & source)
{
  Instruction result;
  result.kind = Instruction::IK_INDEX;
  result.type = lowir_model::builtin_lowir_type(lowir_model::LTK_I8);
  result.index_projection = lowir_model::IPK_FIELD;
  result.dest = lowir_model::append_lowir_fresh_generated_value(
    *function, lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
  result.first = base;
  result.second.kind = Operand::OP_INTEGER;
  result.second.has_int_value = true;
  result.second.int_value = static_cast<long long>(offset);
  result.second.int_high = 0;
  result.debug_location = source.debug_location;
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

LowType integer_chunk_type(std::size_t bytes)
{
  switch(bytes) {
  case 8: return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  case 4: return lowir_model::builtin_lowir_type(lowir_model::LTK_U32);
  case 2: return lowir_model::builtin_lowir_type(lowir_model::LTK_U16);
  default: return lowir_model::builtin_lowir_type(lowir_model::LTK_U8);
  }
}

void append_scalar_swap(Function * function,
                        std::vector<Instruction> * replacement,
                        const Operand & first, const Operand & second,
                        std::size_t bytes, std::size_t alignment,
                        const Instruction & source)
{
  std::size_t offset = 0;
  while(offset < bytes) {
    const std::size_t remaining = bytes - offset;
    std::size_t chunk = alignment >= 8 && remaining >= 8 ? 8 :
      alignment >= 4 && remaining >= 4 ? 4 :
      alignment >= 2 && remaining >= 2 ? 2 : 1;
    Operand first_address = first;
    Operand second_address = second;
    if(offset != 0) {
      Instruction first_index = indexed_address(
        function, first, offset, source);
      first_address = temp_operand(first_index.dest,
        lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
      replacement->push_back(first_index);
      Instruction second_index = indexed_address(
        function, second, offset, source);
      second_address = temp_operand(second_index.dest,
        lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
      replacement->push_back(second_index);
    }
    const LowType type = integer_chunk_type(chunk);
    Instruction first_load;
    first_load.kind = Instruction::IK_LOAD;
    first_load.type = type;
    first_load.dest = lowir_model::append_lowir_fresh_generated_value(
      *function, type);
    first_load.first = first_address;
    first_load.debug_location = source.debug_location;
    replacement->push_back(first_load);
    Instruction second_load = first_load;
    second_load.dest = lowir_model::append_lowir_fresh_generated_value(
      *function, type);
    second_load.first = second_address;
    replacement->push_back(second_load);
    Instruction first_store;
    first_store.kind = Instruction::IK_STORE;
    first_store.type = type;
    first_store.first = temp_operand(second_load.dest, type);
    first_store.second = first_address;
    first_store.debug_location = source.debug_location;
    replacement->push_back(first_store);
    Instruction second_store = first_store;
    second_store.first = temp_operand(first_load.dest, type);
    second_store.second = second_address;
    replacement->push_back(second_store);
    offset += chunk;
  }
}

}  // namespace

bool lower_terminal_staged_object_swap(Function * function, Stats * stats)
{
  if(function->blocks.size() != 1) return false;
  std::vector<Instruction> & instructions = function->blocks[0].instructions;
  if(instructions.size() < 4 ||
     instructions.back().kind != Instruction::IK_RETURN)
    return false;
  const std::size_t second_copy_index = instructions.size() - 2;
  const std::size_t first_copy_index = second_copy_index - 1;
  const Instruction & first_copy = instructions[first_copy_index];
  const Instruction & second_copy = instructions[second_copy_index];
  if(first_copy.kind != Instruction::IK_COPYOBJ ||
     second_copy.kind != Instruction::IK_COPYOBJ ||
     first_copy.byte_count == 0 ||
     first_copy.byte_count != second_copy.byte_count ||
     first_copy.byte_alignment != second_copy.byte_alignment)
    return false;

  RootedPointerProvenance provenance(*function);
  const RootedPointer second = provenance.resolve(first_copy.first);
  const RootedPointer first = provenance.resolve(first_copy.second);
  const RootedPointer stage = provenance.resolve(second_copy.first);
  const RootedPointer second_destination =
    provenance.resolve(second_copy.second);
  const std::size_t bytes = first_copy.byte_count;
  if(!first.known || !second.known || !stage.known ||
     first.offset != 0 || second.offset != 0 || stage.offset != 0 ||
     !same_rooted_pointer(second, second_destination) ||
     !pointer_parameter(*function, first.root) ||
     !pointer_parameter(*function, second.root) ||
     !private_object_slot(*function, stage.root, bytes))
    return false;

  std::vector<unsigned char> private_slot(function->slot_names.size(), 0);
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const SlotId slot = function->slots[i];
    if(slot < private_slot.size()) private_slot[slot] = 1;
  }
  std::vector<unsigned char> covered(bytes, 0);
  std::vector<RootedPointer> loaded(function->value_names.size());
  std::vector<std::size_t> loaded_bytes(function->value_names.size(), 0);
  bool first_was_overwritten = false;
  bool saw_capture = false;

  for(std::size_t index = 0; index < first_copy_index; ++index) {
    const Instruction & instruction = instructions[index];
    if(instruction.volatile_access) return false;
    if(instruction.kind == Instruction::IK_ADDR ||
       (instruction.kind == Instruction::IK_COPY &&
        instruction.type.kind == lowir_model::LTK_PTR) ||
       (instruction.kind == Instruction::IK_INDEX &&
        instruction.second.kind == Operand::OP_INTEGER &&
        instruction.second.has_int_value &&
        instruction.second.int_high == 0))
      continue;
    if(instruction.kind == Instruction::IK_LOAD) {
      const RootedPointer source = provenance.resolve(instruction.first);
      const std::size_t width = scalar_store_bytes(instruction.type);
      if(!source.known || width == 0 || !instruction.dest.valid() ||
         instruction.dest >= loaded.size()) return false;
      if(lowir_opt::same_operand(source.root, second.root)) return false;
      if(lowir_opt::same_operand(source.root, first.root) &&
         first_was_overwritten) return false;
      if(!lowir_opt::same_operand(source.root, first.root) &&
         (source.root.kind != Operand::OP_SLOT ||
          source.root.slot >= private_slot.size() ||
          !private_slot[source.root.slot])) return false;
      loaded[instruction.dest] = source;
      loaded_bytes[instruction.dest] = width;
      continue;
    }
    if(instruction.kind == Instruction::IK_STORE) {
      const RootedPointer destination = provenance.resolve(instruction.second);
      const std::size_t width = scalar_store_bytes(instruction.type);
      if(!destination.known || width == 0) return false;
      if(lowir_opt::same_operand(destination.root, second.root)) return false;
      if(lowir_opt::same_operand(destination.root, stage.root)) {
        if(first_was_overwritten || instruction.first.kind != Operand::OP_TEMP ||
           instruction.first.value >= loaded.size() ||
           loaded_bytes[instruction.first.value] != width) return false;
        const RootedPointer source = loaded[instruction.first.value];
        if(!source.known ||
           !lowir_opt::same_operand(source.root, first.root) ||
           source.offset != destination.offset ||
           !in_byte_span(destination.offset, width, bytes)) return false;
        cover_bytes(&covered, destination.offset, width);
        saw_capture = true;
      } else if(lowir_opt::same_operand(destination.root, first.root)) {
        first_was_overwritten = true;
      } else if(destination.root.kind != Operand::OP_SLOT ||
                destination.root.slot >= private_slot.size() ||
                !private_slot[destination.root.slot]) return false;
      continue;
    }
    if(instruction.kind == Instruction::IK_COPYOBJ) {
      const RootedPointer source = provenance.resolve(instruction.first);
      const RootedPointer destination = provenance.resolve(instruction.second);
      if(!source.known || !destination.known || instruction.byte_count == 0)
        return false;
      if(lowir_opt::same_operand(destination.root, second.root)) return false;
      if(lowir_opt::same_operand(destination.root, stage.root)) {
        if(first_was_overwritten ||
           !lowir_opt::same_operand(source.root, first.root) ||
           source.offset != destination.offset ||
           !in_byte_span(destination.offset, instruction.byte_count, bytes))
          return false;
        cover_bytes(&covered, destination.offset, instruction.byte_count);
        saw_capture = true;
      } else if(lowir_opt::same_operand(destination.root, first.root)) {
        if(source.root.kind != Operand::OP_SLOT ||
           source.root.slot >= private_slot.size() ||
           !private_slot[source.root.slot]) return false;
        first_was_overwritten = true;
      } else return false;
      continue;
    }
    if(instruction.kind == Instruction::IK_ZEROINIT) {
      const RootedPointer destination = provenance.resolve(instruction.first);
      if(!destination.known || destination.root.kind != Operand::OP_SLOT ||
         destination.root.slot >= private_slot.size() ||
         !private_slot[destination.root.slot]) return false;
      continue;
    }
    return false;
  }
  if(!saw_capture ||
     std::find(covered.begin(), covered.end(), 0) != covered.end())
    return false;

  // Every local slot address used by the proof must be confined to the
  // replaced terminal region.  The single-block/terminal restriction makes
  // this a compact escape and observability check rather than an alias guess.
  for(std::size_t index = second_copy_index + 1;
      index < instructions.size(); ++index) {
    const Instruction & instruction = instructions[index];
    const Operand * operands[] = {
      &instruction.first, &instruction.second, &instruction.third};
    for(std::size_t i = 0; i < sizeof(operands) / sizeof(operands[0]); ++i) {
      const RootedPointer pointer = provenance.resolve(*operands[i]);
      if(pointer.known && pointer.root.kind == Operand::OP_SLOT)
        return false;
    }
    for(std::size_t i = 0; i < instruction.args.size(); ++i) {
      const RootedPointer pointer = provenance.resolve(instruction.args[i]);
      if(pointer.known && pointer.root.kind == Operand::OP_SLOT)
        return false;
    }
  }

  std::vector<Instruction> replacement;
  replacement.reserve(bytes / 8 * 6 + 1);
  append_scalar_swap(function, &replacement, first.root, second.root,
                     bytes, first_copy.byte_alignment, second_copy);
  replacement.push_back(instructions.back());
  instructions.swap(replacement);
  if(stats) {
    ++stats->rewrites;
  }
  return true;
}

}  // namespace lowir_opt
