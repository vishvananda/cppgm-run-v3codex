#include "lowir/optimize/memory_gvn.h"

#include "lowir/analysis/eh_context.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/support.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using optimizer_support::combine_hash;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);
const std::size_t kMaximumClasses = 4096;
const std::size_t kMaximumMergeVersions = 8192;
const std::size_t kMaximumCaptureRanges = 64;
const std::size_t kMaximumParameterAddressRematerializations = 64;

enum MemoryClassKind
{
  MCK_SLOT,
  MCK_GLOBAL,
  MCK_PARAMETER,
  MCK_UNKNOWN_POINTER,
  MCK_UNKNOWN_GLOBAL,
  MCK_EH_BARRIER
};

struct MemoryClass
{
  MemoryClassKind kind;
  std::uint32_t identity;
  lowir_model::ValueId address_identity;
  long long byte_offset;
  std::size_t byte_size;
  std::size_t next_same_root;
  bool precise;
  bool readonly;
  bool exclusive;
  bool enabled;
};

bool scalar_load_type(const LowType & type)
{
  return type.kind != lowir_model::LTK_INVALID &&
    type.kind != lowir_model::LTK_VOID &&
    type.kind != lowir_model::LTK_OBJECT;
}

enum AddressRootKind
{
  ARK_NONE,
  ARK_SLOT,
  ARK_GLOBAL,
  ARK_PARAMETER,
  ARK_UNKNOWN
};

struct AddressFact
{
  AddressRootKind root_kind = ARK_NONE;
  std::uint32_t identity = 0;
  lowir_model::ValueId address_identity;
  long long byte_offset = 0;
  long long minimum_offset = 0;
  long long maximum_offset = 0;
  bool precise = false;
  bool bounded = false;

  bool known() const { return root_kind != ARK_NONE; }
};

struct IntegerRange
{
  unsigned long long minimum = 0;
  unsigned long long maximum = 0;
  bool known = false;
};

IntegerRange operand_range(
    const Operand & operand, const std::vector<IntegerRange> & ranges)
{
  if(operand.kind == Operand::OP_INTEGER && operand.has_int_value &&
     operand.int_high == 0 && operand.int_value >= 0) {
    IntegerRange result;
    result.minimum = result.maximum =
      static_cast<unsigned long long>(operand.int_value);
    result.known = true;
    return result;
  }
  return operand.kind == Operand::OP_TEMP && operand.value < ranges.size() ?
    ranges[operand.value] : IntegerRange();
}

AddressFact operand_address(
    const Operand & operand, const std::vector<AddressFact> & addresses)
{
  AddressFact result;
  if(operand.kind == Operand::OP_SLOT) {
    result.root_kind = ARK_SLOT;
    result.identity = operand.slot;
    result.precise = true;
    result.bounded = true;
  } else if(operand.kind == Operand::OP_GLOBAL) {
    result.root_kind = ARK_GLOBAL;
    result.identity = operand.symbol;
    result.precise = true;
    result.bounded = true;
  } else if(operand.kind == Operand::OP_TEMP &&
            operand.value < addresses.size()) {
    result = addresses[operand.value];
    if(!result.known()) {
      result.root_kind = ARK_UNKNOWN;
      result.identity = operand.value;
      result.address_identity = operand.value;
    }
  }
  return result;
}

bool same_address(const AddressFact & left, const AddressFact & right)
{
  if(left.root_kind != right.root_kind ||
     left.identity != right.identity || left.precise != right.precise)
    return false;
  return left.precise ? left.byte_offset == right.byte_offset :
    left.address_identity == right.address_identity;
}

bool offset_address(const AddressFact & base, const Instruction & instruction,
                    const std::vector<IntegerRange> & ranges,
                    AddressFact * result)
{
  if(!base.known()) return false;
  *result = base;
  result->address_identity = instruction.dest;
  if(!base.precise || instruction.second.kind != Operand::OP_INTEGER ||
     !instruction.second.has_int_value || instruction.second.int_high != 0) {
    result->precise = false;
    const IntegerRange index = operand_range(instruction.second, ranges);
    if(base.bounded && index.known) {
      const __int128 minimum = static_cast<__int128>(base.minimum_offset) +
        static_cast<__int128>(index.minimum) * instruction.type.storage_size;
      const __int128 maximum = static_cast<__int128>(base.maximum_offset) +
        static_cast<__int128>(index.maximum) * instruction.type.storage_size;
      if(minimum >= std::numeric_limits<long long>::min() &&
         maximum <= std::numeric_limits<long long>::max()) {
        result->minimum_offset = static_cast<long long>(minimum);
        result->maximum_offset = static_cast<long long>(maximum);
        result->bounded = true;
      } else result->bounded = false;
    } else result->bounded = false;
    return true;
  }
  const __int128 offset = static_cast<__int128>(base.byte_offset) +
    static_cast<__int128>(instruction.second.int_value) *
      static_cast<__int128>(instruction.type.storage_size);
  if(offset < static_cast<__int128>(
       std::numeric_limits<long long>::min()) ||
     offset > static_cast<__int128>(
       std::numeric_limits<long long>::max())) {
    result->precise = false;
    return true;
  }
  result->byte_offset = static_cast<long long>(offset);
  result->minimum_offset = result->maximum_offset = result->byte_offset;
  result->bounded = true;
  return true;
}

std::vector<AddressFact> derive_addresses(const Function & function)
{
  std::vector<AddressFact> result(function.value_names.size());
  std::vector<IntegerRange> ranges(function.value_names.size());
  for(std::size_t parameter = 0;
      parameter < function.params.size(); ++parameter) {
    const lowir_model::Parameter & source = function.params[parameter];
    if(source.type.kind != lowir_model::LTK_PTR ||
       source.value >= result.size()) continue;
    AddressFact fact;
    fact.root_kind = ARK_PARAMETER;
    fact.identity = static_cast<std::uint32_t>(parameter);
    fact.address_identity = source.value;
    fact.precise = true;
    fact.bounded = true;
    result[source.value] = fact;
  }
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.dest.valid() && instruction.dest < ranges.size()) {
        IntegerRange range;
        if(instruction.kind == Instruction::IK_CONST)
          range = operand_range(instruction.first, ranges);
        else if(instruction.kind == Instruction::IK_COPY)
          range = operand_range(instruction.first, ranges);
        else if(instruction.kind == Instruction::IK_BINARY &&
                instruction.op.kind == lowir_model::LowOperation::LOP_AND) {
          const IntegerRange left = operand_range(instruction.first, ranges);
          const IntegerRange right = operand_range(instruction.second, ranges);
          if(right.known && right.minimum == right.maximum) {
            range.known = true;
            range.maximum = right.maximum;
          } else if(left.known && left.minimum == left.maximum) {
            range.known = true;
            range.maximum = left.maximum;
          }
        } else if(instruction.kind == Instruction::IK_BINARY &&
                  instruction.op.kind == lowir_model::LowOperation::LOP_MUL) {
          const IntegerRange left = operand_range(instruction.first, ranges);
          const IntegerRange right = operand_range(instruction.second, ranges);
          const std::size_t width =
            lowir_model::lowir_type_bit_width(instruction.type);
          const unsigned long long type_max = width >= 64 ?
            std::numeric_limits<unsigned long long>::max() :
            (1ULL << width) - 1;
          if(width && width <= 64 && left.known && right.known &&
             left.maximum <= type_max && right.maximum <= type_max &&
             (right.maximum == 0 ||
              left.maximum <= type_max / right.maximum) &&
             (right.maximum == 0 || left.maximum <=
                static_cast<unsigned long long>(
                  std::numeric_limits<long long>::max()) / right.maximum)) {
            range.known = true;
            range.minimum = left.minimum * right.minimum;
            range.maximum = left.maximum * right.maximum;
          }
        }
        ranges[instruction.dest] = range;
      }
      if(!instruction.dest.valid() || instruction.dest >= result.size())
        continue;
      AddressFact fact;
      if(instruction.kind == Instruction::IK_ADDR)
        fact = operand_address(instruction.first, result);
      else if(instruction.kind == Instruction::IK_INDEX)
        offset_address(
          operand_address(instruction.first, result), instruction,
          ranges, &fact);
      else if(instruction.kind == Instruction::IK_COPY)
        fact = operand_address(instruction.first, result);
      else if(instruction.kind == Instruction::IK_PHI &&
              instruction.args.size() >= 2) {
        fact = operand_address(instruction.args[1], result);
        for(std::size_t incoming = 3;
            fact.known() && incoming < instruction.args.size();
            incoming += 2)
          if(!same_address(fact,
               operand_address(instruction.args[incoming], result)))
            fact = AddressFact();
      }
      if(fact.known()) {
        // A copy and an equal-input phi preserve an unknown pointer's exact
        // identity.  An imprecise index already received its result identity
        // in offset_address above.
        result[instruction.dest] = fact;
      }
    }
  return result;
}

struct ProgramMemoryAnalysis
{
  std::vector<std::size_t> function_by_symbol;
  std::vector<std::vector<std::size_t> > object_bytes_by_symbol;
  std::vector<std::vector<unsigned char> > nocapture;
  std::vector<std::vector<unsigned char> > exclusive;
  std::vector<std::vector<MemoryParameterEffect> > effects;
  std::vector<std::vector<MemoryParameterCapture> > captures;
  std::size_t call_sites = 0;
  std::size_t nocapture_rounds = 0;
  std::size_t capture_rounds = 0;
  std::size_t exclusive_rounds = 0;
  std::size_t effect_rounds = 0;
};

struct ProgramCallSite
{
  std::size_t caller;
  std::size_t block;
  std::size_t instruction;
  std::size_t callee;
};

std::size_t call_object_bytes(
    const Instruction & instruction, std::size_t argument,
    const ProgramMemoryAnalysis & analysis)
{
  if(instruction.first.kind == Operand::OP_GLOBAL &&
     instruction.first.symbol < analysis.object_bytes_by_symbol.size() &&
     argument <
       analysis.object_bytes_by_symbol[instruction.first.symbol].size())
    return analysis.object_bytes_by_symbol[
      instruction.first.symbol][argument];
  if(instruction.first.kind != Operand::OP_GLOBAL &&
     instruction.has_call_signature && argument < instruction.call_params.size())
    return instruction.call_params[argument].metadata.object_bytes;
  return 0;
}

std::size_t direct_callee(
    const Instruction & instruction,
    const std::vector<std::size_t> & function_by_symbol)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL ||
     instruction.first.symbol >= function_by_symbol.size())
    return kNoIndex;
  return function_by_symbol[instruction.first.symbol];
}

bool fixed_pointer_use_is_nocapture(
    const Instruction & instruction, std::size_t operand)
{
  if(operand == 0) {
    switch(instruction.kind) {
    case Instruction::IK_ADDR:
    case Instruction::IK_COPY:
    case Instruction::IK_INDEX:
    case Instruction::IK_LOAD:
    case Instruction::IK_ATOMIC_LOAD:
    case Instruction::IK_ATOMIC_STORE:
    case Instruction::IK_ATOMIC_EXCHANGE:
    case Instruction::IK_ATOMIC_ADD_FETCH:
    case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
    case Instruction::IK_COPYOBJ:
    case Instruction::IK_ZEROINIT:
    case Instruction::IK_VA_START:
    case Instruction::IK_VA_ARG:
    case Instruction::IK_CMP:
    case Instruction::IK_BRANCH:
    case Instruction::IK_SWITCH:
      return true;
    default:
      return false;
    }
  }
  if(operand == 1)
    return instruction.kind == Instruction::IK_STORE ||
      instruction.kind == Instruction::IK_COPYOBJ ||
      instruction.kind == Instruction::IK_CMP;
  return false;
}

bool call_argument_is_nocapture(
    const Instruction & instruction, std::size_t argument,
    const ProgramMemoryAnalysis & analysis)
{
  const std::size_t callee = direct_callee(
    instruction, analysis.function_by_symbol);
  return (callee != kNoIndex && argument < analysis.nocapture[callee].size() &&
          analysis.nocapture[callee][argument]) ||
    call_object_bytes(instruction, argument, analysis) != 0;
}

bool parameter_is_captured(
    const Function & function, std::size_t parameter,
    const std::vector<AddressFact> & addresses,
    const ProgramMemoryAnalysis & analysis)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third};
      for(std::size_t operand = 0; operand < 3; ++operand) {
        const AddressFact fact = operand_address(*fixed[operand], addresses);
        const AddressFact store_destination =
          instruction.kind == Instruction::IK_STORE && operand == 0 ?
            operand_address(instruction.second, addresses) : AddressFact();
        const bool self_contained_store =
          store_destination.root_kind == ARK_PARAMETER &&
          store_destination.identity == parameter;
        if(fact.root_kind == ARK_PARAMETER &&
           fact.identity == parameter &&
           !fixed_pointer_use_is_nocapture(instruction, operand) &&
           !self_contained_store)
          return true;
      }
      for(std::size_t argument = 0;
          argument < instruction.args.size(); ++argument) {
        const AddressFact fact = operand_address(
          instruction.args[argument], addresses);
        if(fact.root_kind != ARK_PARAMETER || fact.identity != parameter)
          continue;
        if(instruction.kind == Instruction::IK_PHI) continue;
        if(instruction.kind != Instruction::IK_CALL ||
           !call_argument_is_nocapture(instruction, argument, analysis))
          return true;
      }
    }
  return false;
}

std::vector<unsigned char> private_slots(
    const Function & function, const std::vector<AddressFact> & addresses,
    const ProgramMemoryAnalysis & analysis)
{
  std::vector<unsigned char> result(function.slot_names.size(), 1);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third};
      for(std::size_t operand = 0; operand < 3; ++operand) {
        const AddressFact fact = operand_address(*fixed[operand], addresses);
        const AddressFact store_destination =
          instruction.kind == Instruction::IK_STORE && operand == 0 ?
            operand_address(instruction.second, addresses) : AddressFact();
        const bool self_contained_store =
          store_destination.root_kind == ARK_SLOT &&
          store_destination.identity == fact.identity;
        if(fact.root_kind == ARK_SLOT && fact.identity < result.size() &&
           !fixed_pointer_use_is_nocapture(instruction, operand) &&
           !self_contained_store)
          result[fact.identity] = 0;
      }
      for(std::size_t argument = 0;
          argument < instruction.args.size(); ++argument) {
        const AddressFact fact = operand_address(
          instruction.args[argument], addresses);
        if(fact.root_kind != ARK_SLOT || fact.identity >= result.size())
          continue;
        if(instruction.kind == Instruction::IK_PHI) continue;
        if(instruction.kind != Instruction::IK_CALL ||
           !call_argument_is_nocapture(instruction, argument, analysis))
          result[fact.identity] = 0;
      }
    }
  return result;
}

bool add_effect_range(MemoryParameterEffect * effect,
                      long long offset, std::size_t bytes)
{
  if(effect->unknown_write) return false;
  const __int128 end = static_cast<__int128>(offset) + bytes;
  if(end > std::numeric_limits<long long>::max()) {
    effect->unknown_write = true;
    return true;
  }
  const long long range_end = static_cast<long long>(end);
  if(!effect->has_write) {
    effect->has_write = true;
    effect->write_begin = offset;
    effect->write_end = range_end;
    return true;
  }
  const long long begin = std::min(effect->write_begin, offset);
  const long long finish = std::max(effect->write_end, range_end);
  if(begin == effect->write_begin && finish == effect->write_end)
    return false;
  effect->write_begin = begin;
  effect->write_end = finish;
  return true;
}

bool add_effect_write(MemoryParameterEffect * effect,
                      const AddressFact & address, std::size_t bytes)
{
  if(!address.precise) {
    if(effect->unknown_write) return false;
    effect->unknown_write = true;
    return true;
  }
  return add_effect_range(effect, address.byte_offset, bytes);
}

bool mark_unknown_capture(MemoryParameterCapture * capture)
{
  if(capture->unknown) return false;
  capture->unknown = true;
  capture->ranges.clear();
  return true;
}

bool add_capture_range(MemoryParameterCapture * capture,
                       long long begin, long long end)
{
  if(capture->unknown || end <= begin) return false;
  std::size_t first = 0;
  while(first < capture->ranges.size() &&
        capture->ranges[first].end < begin)
    ++first;
  if(first < capture->ranges.size() &&
     capture->ranges[first].begin <= begin &&
     end <= capture->ranges[first].end)
    return false;

  long long combined_begin = begin;
  long long combined_end = end;
  std::size_t last = first;
  while(last < capture->ranges.size() &&
        capture->ranges[last].begin <= combined_end) {
    combined_begin = std::min(combined_begin,
      capture->ranges[last].begin);
    combined_end = std::max(combined_end, capture->ranges[last].end);
    ++last;
  }
  if(first != last)
    capture->ranges.erase(capture->ranges.begin() + first,
      capture->ranges.begin() + last);
  MemoryParameterCapture::Range range = {combined_begin, combined_end};
  capture->ranges.insert(capture->ranges.begin() + first, range);
  if(capture->ranges.size() > kMaximumCaptureRanges)
    mark_unknown_capture(capture);
  return true;
}

bool add_bounded_capture(MemoryParameterCapture * capture,
                         const AddressFact & address, std::size_t bytes)
{
  if(!address.precise && !address.bounded)
    return mark_unknown_capture(capture);
  const long long begin = address.precise ?
    address.byte_offset : address.minimum_offset;
  const long long maximum = address.precise ?
    address.byte_offset : address.maximum_offset;
  const __int128 end = static_cast<__int128>(maximum) + bytes;
  if(end > std::numeric_limits<long long>::max())
    return mark_unknown_capture(capture);
  return add_capture_range(capture, begin, static_cast<long long>(end));
}

bool map_capture(MemoryParameterCapture * target,
                 const AddressFact & actual,
                 const MemoryParameterCapture & source)
{
  const bool source_unknown = source.unknown;
  const std::vector<MemoryParameterCapture::Range> source_ranges =
    source.ranges;
  if(source_unknown || (!actual.precise && !actual.bounded))
    return mark_unknown_capture(target);
  if(source_ranges.empty()) return false;
  const long long actual_begin = actual.precise ?
    actual.byte_offset : actual.minimum_offset;
  const long long actual_end = actual.precise ?
    actual.byte_offset : actual.maximum_offset;
  bool changed = false;
  for(std::size_t range = 0; range < source_ranges.size(); ++range) {
    const __int128 begin = static_cast<__int128>(actual_begin) +
      source_ranges[range].begin;
    const __int128 end = static_cast<__int128>(actual_end) +
      source_ranges[range].end;
    if(begin < std::numeric_limits<long long>::min() ||
       end > std::numeric_limits<long long>::max())
      return mark_unknown_capture(target) || changed;
    changed |= add_capture_range(target, static_cast<long long>(begin),
      static_cast<long long>(end));
  }
  return changed;
}

bool project_owner_capture(MemoryParameterCapture * target,
                           const AddressFact & actual,
                           std::size_t target_extent,
                           const MemoryParameterCapture & owner)
{
  const bool owner_unknown = owner.unknown;
  const std::vector<MemoryParameterCapture::Range> owner_ranges =
    owner.ranges;
  if(owner_unknown || !actual.precise || !target_extent)
    return !owner_ranges.empty() || owner_unknown ?
      mark_unknown_capture(target) : false;
  if(owner_ranges.empty()) return false;
  const __int128 actual_begin = actual.byte_offset;
  const __int128 actual_end = actual_begin + target_extent;
  bool changed = false;
  for(std::size_t range = 0; range < owner_ranges.size(); ++range) {
    const __int128 overlap_begin = std::max(actual_begin,
      static_cast<__int128>(owner_ranges[range].begin));
    const __int128 overlap_end = std::min(actual_end,
      static_cast<__int128>(owner_ranges[range].end));
    if(overlap_begin >= overlap_end) continue;
    const __int128 translated_begin = overlap_begin - actual_begin;
    const __int128 translated_end = overlap_end - actual_begin;
    if(translated_begin < std::numeric_limits<long long>::min() ||
       translated_end > std::numeric_limits<long long>::max())
      return mark_unknown_capture(target) || changed;
    changed |= add_capture_range(target,
      static_cast<long long>(translated_begin),
      static_cast<long long>(translated_end));
  }
  return changed;
}

std::vector<MemoryParameterCapture> analyze_slot_captures(
    const Function & function, const std::vector<AddressFact> & addresses,
    const std::vector<unsigned char> & private_slot,
    const ProgramMemoryAnalysis & analysis)
{
  std::vector<MemoryParameterCapture> result(function.slot_names.size());
  for(std::size_t slot = 0; slot < result.size(); ++slot)
    if(slot >= private_slot.size() || !private_slot[slot])
      result[slot].unknown = true;
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.kind == Instruction::IK_STORE &&
         instruction.type.kind == lowir_model::LTK_PTR) {
        const AddressFact source = operand_address(
          instruction.first, addresses);
        const AddressFact destination = operand_address(
          instruction.second, addresses);
        if(source.root_kind == ARK_SLOT &&
           destination.root_kind == ARK_SLOT &&
           source.identity == destination.identity &&
           source.identity < result.size()) {
          const std::size_t extent =
            source.identity < function.slot_types.size() ?
              function.slot_types[source.identity].storage_size : 0;
          const long long begin = source.precise ?
            source.byte_offset : source.minimum_offset;
          if(!extent || extent > static_cast<std::size_t>(
               std::numeric_limits<long long>::max()) ||
             (!source.precise && !source.bounded) || begin < 0 ||
             static_cast<unsigned long long>(begin) >= extent)
            mark_unknown_capture(&result[source.identity]);
          else
            add_capture_range(&result[source.identity], begin,
              static_cast<long long>(extent));
        }
      }
      if(instruction.kind != Instruction::IK_CALL) continue;
      const std::size_t callee = direct_callee(
        instruction, analysis.function_by_symbol);
      for(std::size_t argument = 0;
          argument < instruction.args.size(); ++argument) {
        const AddressFact actual = operand_address(
          instruction.args[argument], addresses);
        if(actual.root_kind != ARK_SLOT || actual.identity >= result.size())
          continue;
        if(callee != kNoIndex && argument < analysis.nocapture[callee].size() &&
           analysis.nocapture[callee][argument])
          map_capture(&result[actual.identity], actual,
            analysis.captures[callee][argument]);
        else {
          const std::size_t bytes =
            call_object_bytes(instruction, argument, analysis);
          if(bytes)
            add_bounded_capture(&result[actual.identity], actual, bytes);
          else
            mark_unknown_capture(&result[actual.identity]);
        }
      }
    }
  return result;
}

ProgramMemoryAnalysis analyze_program_memory(const lowir_model::Program & program)
{
  ProgramMemoryAnalysis result;
  result.function_by_symbol.assign(program.symbol_names.size(), kNoIndex);
  result.object_bytes_by_symbol.resize(program.symbol_names.size());
  result.nocapture.resize(program.functions.size());
  result.exclusive.resize(program.functions.size());
  result.effects.resize(program.functions.size());
  result.captures.resize(program.functions.size());
  std::vector<std::vector<AddressFact> > addresses(program.functions.size());
  for(std::size_t function = 0; function < program.functions.size(); ++function) {
    const Function & source = program.functions[function];
    result.function_by_symbol[source.symbol] = function;
    result.nocapture[function].resize(source.params.size(), 0);
    result.exclusive[function].resize(source.params.size(), 0);
    result.effects[function].resize(source.params.size());
    result.captures[function].resize(source.params.size());
    for(std::size_t parameter = 0;
        parameter < source.params.size(); ++parameter)
      if(source.params[parameter].type.kind == lowir_model::LTK_PTR)
        result.nocapture[function][parameter] = 1;
    addresses[function] = derive_addresses(source);
    result.object_bytes_by_symbol[source.symbol].resize(source.params.size());
    for(std::size_t parameter = 0;
        parameter < source.params.size(); ++parameter)
      result.object_bytes_by_symbol[source.symbol][parameter] =
        source.params[parameter].metadata.object_bytes;
  }
  for(std::size_t declaration = 0;
      declaration < program.function_declarations.size(); ++declaration) {
    const lowir_model::FunctionDeclaration & source =
      program.function_declarations[declaration];
    result.object_bytes_by_symbol[source.symbol].resize(source.params.size());
    for(std::size_t parameter = 0;
        parameter < source.params.size(); ++parameter)
      result.object_bytes_by_symbol[source.symbol][parameter] =
        source.params[parameter].metadata.object_bytes;
  }

  // The fixed-point analyses below revisit calls, not arbitrary
  // instructions.  Index them once so each round is proportional to the
  // call graph instead of rescanning the complete translation unit.  The
  // reverse index also avoids the former target-by-whole-program scan while
  // proving that every call of an internal function receives private storage.
  std::vector<ProgramCallSite> calls;
  std::vector<std::vector<std::size_t> > calls_by_callee(
    program.functions.size());
  for(std::size_t caller = 0; caller < program.functions.size(); ++caller)
    for(std::size_t block = 0;
        block < program.functions[caller].blocks.size(); ++block)
      for(std::size_t instruction = 0;
          instruction <
            program.functions[caller].blocks[block].instructions.size();
          ++instruction) {
        const Instruction & source =
          program.functions[caller].blocks[block].instructions[instruction];
        if(source.kind != Instruction::IK_CALL) continue;
        const std::size_t callee = direct_callee(
          source, result.function_by_symbol);
        const ProgramCallSite site = {caller, block, instruction, callee};
        calls.push_back(site);
        if(callee != kNoIndex)
          calls_by_callee[callee].push_back(calls.size() - 1);
      }
  result.call_sites = calls.size();

  bool changed = true;
  while(changed) {
    ++result.nocapture_rounds;
    changed = false;
    for(std::size_t function = 0;
        function < program.functions.size(); ++function)
      for(std::size_t parameter = 0;
          parameter < result.nocapture[function].size(); ++parameter)
        if(result.nocapture[function][parameter] &&
           parameter_is_captured(program.functions[function], parameter,
             addresses[function], result)) {
          result.nocapture[function][parameter] = 0;
          changed = true;
        }
  }

  for(std::size_t function = 0;
      function < program.functions.size(); ++function)
    for(std::size_t parameter = 0;
        parameter < result.nocapture[function].size(); ++parameter)
      if(!result.nocapture[function][parameter])
        result.captures[function][parameter].unknown = true;
  // A self pointer retained in one of the object's own fields is not an
  // external escape, but a later load through that field can alias the
  // object.  Record the possible pointed-to suffix so only disjoint fields
  // retain private-memory treatment.
  for(std::size_t function = 0;
      function < program.functions.size(); ++function)
    for(std::size_t block = 0;
        block < program.functions[function].blocks.size(); ++block)
      for(std::size_t index = 0;
          index < program.functions[function].blocks[block].instructions.size();
          ++index) {
        const Instruction & instruction =
          program.functions[function].blocks[block].instructions[index];
        if(instruction.kind != Instruction::IK_STORE ||
           instruction.type.kind != lowir_model::LTK_PTR)
          continue;
        const AddressFact source = operand_address(
          instruction.first, addresses[function]);
        const AddressFact destination = operand_address(
          instruction.second, addresses[function]);
        if(source.root_kind != ARK_PARAMETER ||
           destination.root_kind != ARK_PARAMETER ||
           source.identity != destination.identity ||
           source.identity >= result.captures[function].size())
          continue;
        MemoryParameterCapture & capture =
          result.captures[function][source.identity];
        const std::size_t extent = program.functions[function].params[
          source.identity].metadata.object_bytes;
        const long long begin = source.precise ?
          source.byte_offset : source.minimum_offset;
        if(!extent || extent > static_cast<std::size_t>(
             std::numeric_limits<long long>::max()) ||
           (!source.precise && !source.bounded) || begin < 0 ||
           static_cast<unsigned long long>(begin) >= extent)
          mark_unknown_capture(&capture);
        else
          add_capture_range(
            &capture, begin, static_cast<long long>(extent));
      }
  for(std::size_t iteration = 0;
      iteration <= program.functions.size(); ++iteration) {
    ++result.capture_rounds;
    changed = false;
    std::vector<std::vector<unsigned char> > changed_parameters(
      program.functions.size());
    for(std::size_t function = 0;
        function < program.functions.size(); ++function)
      changed_parameters[function].resize(
        program.functions[function].params.size(), 0);
    for(std::size_t call_index = 0;
        call_index < calls.size(); ++call_index) {
          const ProgramCallSite & site = calls[call_index];
          const std::size_t caller = site.caller;
          const std::size_t callee = site.callee;
          const Instruction & call = program.functions[caller].blocks[
            site.block].instructions[site.instruction];
          for(std::size_t argument = 0;
              argument < call.args.size(); ++argument) {
            const AddressFact actual = operand_address(
              call.args[argument], addresses[caller]);
            if(actual.root_kind != ARK_PARAMETER ||
               actual.identity >= result.captures[caller].size())
              continue;
            MemoryParameterCapture & target =
              result.captures[caller][actual.identity];
            bool updated = false;
            if(callee != kNoIndex &&
               argument < result.nocapture[callee].size() &&
               result.nocapture[callee][argument]) {
              updated = map_capture(
                &target, actual, result.captures[callee][argument]);
            } else {
              const std::size_t bytes =
                call_object_bytes(call, argument, result);
              updated = bytes ? add_bounded_capture(
                &target, actual, bytes) : mark_unknown_capture(&target);
            }
            if(updated) {
              changed = true;
              changed_parameters[caller][actual.identity] = 1;
            }
          }
        }
    if(!changed) break;
    if(iteration == program.functions.size())
      for(std::size_t function = 0;
          function < changed_parameters.size(); ++function)
        for(std::size_t parameter = 0;
            parameter < changed_parameters[function].size(); ++parameter)
          if(changed_parameters[function][parameter])
            result.captures[function][parameter].unknown = true;
  }

  std::vector<std::vector<unsigned char> > slots(program.functions.size());
  std::vector<std::vector<MemoryParameterCapture> > slot_captures(
    program.functions.size());
  for(std::size_t function = 0; function < program.functions.size(); ++function)
    slots[function] = private_slots(
      program.functions[function], addresses[function], result);
  for(std::size_t function = 0; function < program.functions.size(); ++function)
    slot_captures[function] = analyze_slot_captures(
      program.functions[function], addresses[function], slots[function], result);

  std::vector<unsigned char> address_taken(program.functions.size(), 0);
  std::vector<std::size_t> call_count(program.functions.size(), 0);
  for(std::size_t target = 0; target < calls_by_callee.size(); ++target)
    call_count[target] = calls_by_callee[target].size();
  for(std::size_t caller = 0; caller < program.functions.size(); ++caller)
    for(std::size_t block = 0;
        block < program.functions[caller].blocks.size(); ++block)
      for(std::size_t index = 0;
          index < program.functions[caller].blocks[block].instructions.size();
          ++index) {
        const Instruction & instruction =
          program.functions[caller].blocks[block].instructions[index];
        const Operand * fixed[] = {
          &instruction.first, &instruction.second, &instruction.third};
        for(std::size_t operand = 0; operand < 3; ++operand) {
          if(fixed[operand]->kind != Operand::OP_GLOBAL ||
             fixed[operand]->symbol >= result.function_by_symbol.size())
            continue;
          const std::size_t target =
            result.function_by_symbol[fixed[operand]->symbol];
          if(target != kNoIndex &&
             !(instruction.kind == Instruction::IK_CALL && operand == 0))
            address_taken[target] = 1;
        }
        for(std::size_t argument = 0;
            argument < instruction.args.size(); ++argument)
          if(instruction.args[argument].kind == Operand::OP_GLOBAL &&
             instruction.args[argument].symbol <
               result.function_by_symbol.size()) {
            const std::size_t target = result.function_by_symbol[
              instruction.args[argument].symbol];
            if(target != kNoIndex) address_taken[target] = 1;
          }
  }
  changed = true;
  for(std::size_t iteration = 0;
      changed && iteration <= program.functions.size(); ++iteration) {
    ++result.exclusive_rounds;
    changed = false;
    for(std::size_t target = 0; target < program.functions.size(); ++target) {
      const Function & function = program.functions[target];
      if(address_taken[target] || call_count[target] == 0 ||
         function.metadata.binding != lowir_model::SBM_INTERNAL)
        continue;
      for(std::size_t parameter = 0;
          parameter < function.params.size(); ++parameter) {
        if(!result.nocapture[target][parameter]) continue;
        bool all_private = true;
        for(std::size_t use = 0;
            use < calls_by_callee[target].size() && all_private; ++use) {
              const ProgramCallSite & site =
                calls[calls_by_callee[target][use]];
              const std::size_t caller = site.caller;
              const Instruction & call = program.functions[caller].blocks[
                site.block].instructions[site.instruction];
              if(parameter >= call.args.size()) {
                all_private = false;
                break;
              }
              const AddressFact actual = operand_address(
                call.args[parameter], addresses[caller]);
              const bool private_slot = actual.root_kind == ARK_SLOT &&
                actual.identity < slots[caller].size() &&
                slots[caller][actual.identity];
              const bool private_parameter =
                actual.root_kind == ARK_PARAMETER &&
                actual.identity < result.exclusive[caller].size() &&
                result.exclusive[caller][actual.identity];
              if(!private_slot && !private_parameter) {
                all_private = false;
                break;
              }
              const MemoryParameterCapture & owner = private_slot ?
                slot_captures[caller][actual.identity] :
                result.captures[caller][actual.identity];
              if(owner.unknown) {
                all_private = false;
                break;
              }
              if(project_owner_capture(
                   &result.captures[target][parameter], actual,
                   function.params[parameter].metadata.object_bytes,
                   owner))
                changed = true;
            }
        if(all_private && !result.exclusive[target][parameter]) {
          result.exclusive[target][parameter] = 1;
          changed = true;
        }
      }
    }
    if(changed && iteration == program.functions.size())
      for(std::size_t function = 0;
          function < result.exclusive.size(); ++function)
        for(std::size_t parameter = 0;
            parameter < result.exclusive[function].size(); ++parameter)
          if(result.exclusive[function][parameter])
            result.captures[function][parameter].unknown = true;
  }

  for(std::size_t function = 0; function < program.functions.size(); ++function) {
    for(std::size_t parameter = 0;
        parameter < result.effects[function].size(); ++parameter)
      result.effects[function][parameter].exclusive =
        result.exclusive[function][parameter] != 0;
    for(std::size_t block = 0;
        block < program.functions[function].blocks.size(); ++block)
      for(std::size_t index = 0;
          index < program.functions[function].blocks[block].instructions.size();
          ++index) {
        const Instruction & instruction =
          program.functions[function].blocks[block].instructions[index];
        const Operand * destination = 0;
        std::size_t bytes = 0;
        if(instruction.kind == Instruction::IK_STORE) {
          destination = &instruction.second;
          bytes = instruction.type.storage_size;
        } else if(instruction.kind == Instruction::IK_ATOMIC_STORE ||
                  instruction.kind == Instruction::IK_ATOMIC_EXCHANGE ||
                  instruction.kind == Instruction::IK_ATOMIC_ADD_FETCH ||
                  instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) {
          destination = &instruction.first;
          bytes = instruction.type.storage_size;
        } else if(instruction.kind == Instruction::IK_COPYOBJ) {
          destination = &instruction.second;
          bytes = instruction.byte_count;
        } else if(instruction.kind == Instruction::IK_ZEROINIT) {
          destination = &instruction.first;
          bytes = instruction.byte_count;
        } else if(instruction.kind == Instruction::IK_VA_START ||
                  instruction.kind == Instruction::IK_VA_ARG) {
          const AddressFact address = operand_address(
            instruction.first, addresses[function]);
          if(address.root_kind == ARK_PARAMETER &&
             address.identity < result.effects[function].size())
            result.effects[function][address.identity].unknown_write = true;
        }
        if(destination) {
          const AddressFact address = operand_address(
            *destination, addresses[function]);
          if(address.root_kind == ARK_PARAMETER &&
             address.identity < result.effects[function].size())
            add_effect_write(
              &result.effects[function][address.identity], address, bytes);
        }
      }
  }

  for(std::size_t iteration = 0;
      iteration <= program.functions.size(); ++iteration) {
    ++result.effect_rounds;
    changed = false;
    for(std::size_t call_index = 0;
        call_index < calls.size(); ++call_index) {
          const ProgramCallSite & site = calls[call_index];
          const std::size_t caller = site.caller;
          const Instruction & call = program.functions[caller].blocks[
            site.block].instructions[site.instruction];
          lowir_model::FunctionBoundaryMetadata boundary = call.call_boundary;
          const std::size_t callee = site.callee;
          if(callee != kNoIndex)
            boundary = program.functions[callee].boundary;
          if(boundary.effects == lowir_model::CFXM_READNONE ||
             boundary.effects == lowir_model::CFXM_READONLY) continue;
          for(std::size_t argument = 0;
              argument < call.args.size(); ++argument) {
            const AddressFact actual = operand_address(
              call.args[argument], addresses[caller]);
            if(actual.root_kind != ARK_PARAMETER ||
               actual.identity >= result.effects[caller].size()) continue;
            MemoryParameterEffect & target =
              result.effects[caller][actual.identity];
            const std::size_t bounded_bytes =
              call_object_bytes(call, argument, result);
            const bool precise_callee_effect = actual.precise &&
              callee != kNoIndex && callee < result.effects.size() &&
              argument < result.effects[callee].size() &&
              !result.effects[callee][argument].unknown_write;
            if(precise_callee_effect) {
              const MemoryParameterEffect & source =
                result.effects[callee][argument];
              if(source.has_write) {
                const __int128 begin =
                  static_cast<__int128>(actual.byte_offset) +
                    source.write_begin;
                const __int128 end =
                  static_cast<__int128>(actual.byte_offset) +
                    source.write_end;
                if(begin < std::numeric_limits<long long>::min() ||
                   end > std::numeric_limits<long long>::max()) {
                  if(!target.unknown_write) {
                    target.unknown_write = true;
                    changed = true;
                  }
                } else
                  changed |= add_effect_range(&target,
                    static_cast<long long>(begin),
                    static_cast<std::size_t>(end - begin));
              }
              continue;
            }
            if(bounded_bytes && actual.precise) {
              changed |= add_effect_range(
                &target, actual.byte_offset, bounded_bytes);
              continue;
            }
            if(callee == kNoIndex ||
               argument >= result.effects[callee].size() ||
               result.effects[callee][argument].unknown_write ||
               !actual.precise) {
              if(!target.unknown_write) {
                target.unknown_write = true;
                changed = true;
              }
              continue;
            }
          }
        }
    if(!changed) break;
    if(iteration == program.functions.size())
      for(std::size_t function = 0;
          function < result.effects.size(); ++function)
        for(std::size_t parameter = 0;
            parameter < result.effects[function].size(); ++parameter)
          if(result.effects[function][parameter].has_write)
            result.effects[function][parameter].unknown_write = true;
  }
  return result;
}

bool permitted_slot_address_use(const Instruction & instruction,
                                const Operand * operand)
{
  return (instruction.kind == Instruction::IK_LOAD &&
          operand == &instruction.first) ||
    (instruction.kind == Instruction::IK_STORE &&
     operand == &instruction.second) ||
    (instruction.kind == Instruction::IK_ADDR &&
     operand == &instruction.first) ||
    (instruction.kind == Instruction::IK_INDEX &&
     operand == &instruction.first) ||
    (instruction.kind == Instruction::IK_COPY &&
     operand == &instruction.first);
}

std::vector<unsigned char> find_escaped_slots(
    const Function & function, const std::vector<AddressFact> & addresses)
{
  std::vector<unsigned char> escaped(function.slot_names.size(), 0);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      const Operand * operands[] = {
        &instruction.first, &instruction.second, &instruction.third};
      for(std::size_t operand = 0; operand < 3; ++operand) {
        AddressFact fact = operand_address(*operands[operand], addresses);
        if(fact.root_kind == ARK_SLOT &&
           !permitted_slot_address_use(instruction, operands[operand]))
          escaped[fact.identity] = 1;
      }
      for(std::size_t operand = 0;
          operand < instruction.args.size(); ++operand) {
        AddressFact fact = operand_address(
          instruction.args[operand], addresses);
        if(fact.root_kind == ARK_SLOT) escaped[fact.identity] = 1;
      }
    }
  return escaped;
}

std::vector<std::size_t> layout_last_uses(const Function & function)
{
  std::vector<std::size_t> result(function.value_names.size(), kNoIndex);
  std::size_t position = 0;
  const auto record = [&result, &position](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP && operand.value < result.size())
      result[operand.value] = position;
  };
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      record(instruction.first);
      record(instruction.second);
      record(instruction.third);
      for(std::size_t argument = 0;
          argument < instruction.args.size(); ++argument)
        record(instruction.args[argument]);
      ++position;
    }
  return result;
}

struct LocationKey
{
  AddressRootKind root_kind;
  std::uint32_t identity;
  lowir_model::ValueId address_identity;
  long long byte_offset;
  lowir_model::LowTypeKind type_kind;
  std::size_t type_size;
  std::uint32_t type_alignment;
  bool precise;

  bool operator==(const LocationKey & other) const
  {
    return root_kind == other.root_kind && identity == other.identity &&
      address_identity == other.address_identity &&
      byte_offset == other.byte_offset && type_kind == other.type_kind &&
      type_size == other.type_size &&
      type_alignment == other.type_alignment && precise == other.precise;
  }
};

struct MemoryKey
{
  std::size_t memory_class;
  std::size_t version;
  std::size_t unknown_version;
  std::size_t eh_version;
  lowir_model::LowTypeKind type_kind;
  std::size_t type_size;
  std::uint32_t type_alignment;

  bool operator==(const MemoryKey & other) const
  {
    return memory_class == other.memory_class &&
      version == other.version &&
      unknown_version == other.unknown_version &&
      eh_version == other.eh_version &&
      type_kind == other.type_kind && type_size == other.type_size &&
      type_alignment == other.type_alignment;
  }
};

struct LocationKeyHash
{
  std::size_t operator()(const LocationKey & key) const
  {
    std::size_t result = static_cast<std::size_t>(key.root_kind);
    combine_hash(&result, key.identity);
    combine_hash(&result, static_cast<std::uint32_t>(key.address_identity));
    combine_hash(&result, std::hash<long long>()(key.byte_offset));
    combine_hash(&result, static_cast<std::size_t>(key.type_kind));
    combine_hash(&result, key.type_size);
    combine_hash(&result, key.type_alignment);
    combine_hash(&result, key.precise ? 1 : 0);
    return result;
  }
};

LocationKey location_key(const AddressFact & address, const LowType & type)
{
  LocationKey result;
  result.root_kind = address.root_kind;
  result.identity = address.identity;
  result.address_identity = address.precise ?
    lowir_model::ValueId() : address.address_identity;
  result.byte_offset = address.precise ? address.byte_offset : 0;
  result.type_kind = type.kind;
  result.type_size = type.storage_size;
  result.type_alignment = type.alignment;
  result.precise = address.precise;
  return result;
}

struct LocationInfo
{
  std::size_t load_count = 0;
  std::size_t memory_class = kNoIndex;
};

struct MemoryKeyHash
{
  std::size_t operator()(const MemoryKey & key) const
  {
    std::size_t result = key.memory_class;
    combine_hash(&result, key.version);
    combine_hash(&result, key.unknown_version);
    combine_hash(&result, key.eh_version);
    combine_hash(&result, static_cast<std::size_t>(key.type_kind));
    combine_hash(&result, key.type_size);
    combine_hash(&result, key.type_alignment);
    return result;
  }
};

struct BlockEvent
{
  std::size_t block;
  bool entering;
};

std::vector<BlockEvent> dominator_events(
    const lowir_analysis::DominatorTree & dominators,
    const std::vector<lowir_analysis::EdgeList> & children)
{
  struct Frame { std::size_t block; std::size_t child; };
  std::vector<BlockEvent> result;
  std::vector<Frame> stack;
  std::vector<unsigned char> scheduled(children.size(), 0);
  result.reserve(children.size() * 2);
  for(std::size_t root = 0; root < children.size(); ++root) {
    if(scheduled[root] ||
       (root != 0 && dominators.preorder[root] != 0)) continue;
    scheduled[root] = 1;
    result.push_back(BlockEvent{root, true});
    stack.push_back(Frame{root, 0});
    while(!stack.empty()) {
      Frame & frame = stack.back();
      if(frame.child < children[frame.block].size()) {
        const std::size_t child =
          children[frame.block][frame.child++];
        scheduled[child] = 1;
        result.push_back(BlockEvent{child, true});
        stack.push_back(Frame{child, 0});
      } else {
        result.push_back(BlockEvent{frame.block, false});
        stack.pop_back();
      }
    }
  }
  return result;
}

void add_definition(std::vector<std::vector<std::size_t> > * definitions,
                    std::size_t memory_class, std::size_t block)
{
  std::vector<std::size_t> & blocks = (*definitions)[memory_class];
  if(blocks.empty() || blocks.back() != block) blocks.push_back(block);
}

bool is_atomic_barrier(Instruction::Kind kind)
{
  return kind == Instruction::IK_ATOMIC_LOAD ||
    kind == Instruction::IK_ATOMIC_STORE ||
    kind == Instruction::IK_ATOMIC_EXCHANGE ||
    kind == Instruction::IK_ATOMIC_ADD_FETCH ||
    kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
    kind == Instruction::IK_ATOMIC_THREAD_FENCE ||
    kind == Instruction::IK_ATOMIC_SIGNAL_FENCE;
}

bool is_other_memory_write(Instruction::Kind kind)
{
  return kind == Instruction::IK_COPYOBJ ||
    kind == Instruction::IK_ZEROINIT ||
    kind == Instruction::IK_VA_START ||
    kind == Instruction::IK_VA_ARG;
}

bool memory_write_destination(const Instruction & instruction,
                              const Operand ** destination,
                              std::size_t * bytes, bool * unbounded)
{
  *destination = 0;
  *bytes = 0;
  *unbounded = false;
  if(instruction.kind == Instruction::IK_STORE) {
    *destination = &instruction.second;
    *bytes = instruction.type.storage_size;
  } else if(instruction.kind == Instruction::IK_ATOMIC_STORE ||
            instruction.kind == Instruction::IK_ATOMIC_EXCHANGE ||
            instruction.kind == Instruction::IK_ATOMIC_ADD_FETCH ||
            instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) {
    *destination = &instruction.first;
    *bytes = instruction.type.storage_size;
  } else if(instruction.kind == Instruction::IK_COPYOBJ) {
    *destination = &instruction.second;
    *bytes = instruction.byte_count;
  } else if(instruction.kind == Instruction::IK_ZEROINIT) {
    *destination = &instruction.first;
    *bytes = instruction.byte_count;
  } else if(instruction.kind == Instruction::IK_VA_START ||
            instruction.kind == Instruction::IK_VA_ARG) {
    *destination = &instruction.first;
    *unbounded = true;
  }
  return *destination != 0;
}

typedef std::unordered_map<LocationKey, LocationInfo, LocationKeyHash>
  LocationMap;

void collect_load_locations(
    const Function & function, const std::vector<AddressFact> & addresses,
    const std::vector<unsigned char> & escaped_slots,
    const std::vector<lowir_model::GlobalStorageMode> & global_storage,
    LocationMap * locations, std::vector<LocationKey> * location_order)
{
  locations->reserve(function.value_names.size() / 8 + 1);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.kind != Instruction::IK_LOAD ||
         instruction.volatile_access || instruction.debug_location.present() ||
         !scalar_load_type(instruction.type)) continue;
      const AddressFact address = operand_address(
        instruction.first, addresses);
      if(!address.known() ||
         (address.root_kind == ARK_SLOT &&
          escaped_slots[address.identity]) ||
         (address.root_kind == ARK_GLOBAL &&
          (address.identity >= global_storage.size() ||
           global_storage[address.identity] ==
             lowir_model::GSM_THREAD_LOCAL))) continue;
      const LocationKey key = location_key(address, instruction.type);
      const auto inserted = locations->emplace(key, LocationInfo());
      if(inserted.second) location_order->push_back(key);
      ++inserted.first->second.load_count;
    }
}

void record_elapsed(Stats * stats,
                    const std::chrono::steady_clock::time_point & started)
{
  if(stats) stats->memory_gvn_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
}

std::size_t load_class(const Instruction & instruction,
                       const std::vector<AddressFact> & addresses,
                       const LocationMap & locations)
{
  const AddressFact address = operand_address(instruction.first, addresses);
  if(!address.known()) return kNoIndex;
  const auto found = locations.find(location_key(address, instruction.type));
  return found == locations.end() ? kNoIndex : found->second.memory_class;
}

bool ranges_overlap(long long left_offset, std::size_t left_size,
                    long long right_offset, std::size_t right_size)
{
  const __int128 left_end = static_cast<__int128>(left_offset) + left_size;
  const __int128 right_end = static_cast<__int128>(right_offset) + right_size;
  return static_cast<__int128>(left_offset) < right_end &&
    static_cast<__int128>(right_offset) < left_end;
}

bool capture_overlaps(const MemoryParameterCapture & capture,
                      const LocationKey & location)
{
  if(capture.unknown) return true;
  if(capture.ranges.empty()) return false;
  if(!location.precise) return true;
  const __int128 location_end =
    static_cast<__int128>(location.byte_offset) + location.type_size;
  for(std::size_t range = 0; range < capture.ranges.size(); ++range)
    if(static_cast<__int128>(location.byte_offset) <
         capture.ranges[range].end &&
       static_cast<__int128>(capture.ranges[range].begin) < location_end)
      return true;
  return false;
}

bool store_overlaps(const AddressFact & store, std::size_t store_size,
                    const MemoryClass & memory_class)
{
  if(memory_class.kind == MCK_UNKNOWN_GLOBAL ||
     store.root_kind == ARK_NONE) return false;
  const MemoryClassKind store_kind = store.root_kind == ARK_SLOT ?
    MCK_SLOT : store.root_kind == ARK_GLOBAL ?
      MCK_GLOBAL : store.root_kind == ARK_PARAMETER ?
        MCK_PARAMETER : MCK_UNKNOWN_POINTER;
  if(store_kind != memory_class.kind ||
     store.identity != memory_class.identity) return false;
  if(store.precise && memory_class.precise)
    return ranges_overlap(store.byte_offset, store_size,
      memory_class.byte_offset, memory_class.byte_size);
  if(store.bounded && memory_class.precise) {
    const __int128 store_end = static_cast<__int128>(store.maximum_offset) +
      store_size;
    const __int128 class_end =
      static_cast<__int128>(memory_class.byte_offset) +
        memory_class.byte_size;
    return static_cast<__int128>(store.minimum_offset) < class_end &&
      static_cast<__int128>(memory_class.byte_offset) < store_end;
  }
  return store.precise == memory_class.precise ?
    store.address_identity == memory_class.address_identity : true;
}

template<typename Callback>
void visit_store_classes(
    const AddressFact & store, std::size_t store_size,
    const std::vector<MemoryClass> & classes,
    const std::vector<std::size_t> & first_slot_class,
    const std::vector<std::size_t> & first_parameter_class,
    const std::vector<std::uint32_t> & symbol_epochs,
    std::uint32_t function_epoch,
    const std::vector<std::size_t> & first_symbol_class,
    const Callback & callback)
{
  std::size_t memory_class = kNoIndex;
  if(store.root_kind == ARK_SLOT && store.identity < first_slot_class.size())
    memory_class = first_slot_class[store.identity];
  else if(store.root_kind == ARK_PARAMETER &&
          store.identity < first_parameter_class.size())
    memory_class = first_parameter_class[store.identity];
  else if(store.root_kind == ARK_GLOBAL &&
          store.identity < symbol_epochs.size() &&
          symbol_epochs[store.identity] == function_epoch)
    memory_class = first_symbol_class[store.identity];
  while(memory_class != kNoIndex) {
    if(classes[memory_class].enabled && !classes[memory_class].readonly &&
       store_overlaps(store, store_size, classes[memory_class]))
      callback(memory_class);
    memory_class = classes[memory_class].next_same_root;
  }
}

template<typename CallBoundary, typename CallWrites>
void collect_memory_definitions(
    const Function & function, const std::vector<AddressFact> & addresses,
    const std::vector<MemoryClass> & classes,
    const std::vector<std::size_t> & first_slot_class,
    const std::vector<std::size_t> & first_parameter_class,
    const std::vector<std::uint32_t> & symbol_epochs,
    std::uint32_t function_epoch,
    const std::vector<std::size_t> & first_symbol_class,
    std::size_t unknown_class, std::size_t eh_class,
    bool has_unknown_pointer,
    const lowir_eh_context::Context & eh_context,
    const CallBoundary & call_boundary,
    const CallWrites & call_writes,
    std::vector<std::vector<std::size_t> > * definitions)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    if(eh_class != kNoIndex && eh_context.entry_barriers[block])
      add_definition(definitions, eh_class, block);
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      bool unknown_write = is_atomic_barrier(instruction.kind) ||
        is_other_memory_write(instruction.kind);
      lowir_model::FunctionBoundaryMetadata boundary;
      if(instruction.kind == Instruction::IK_CALL) {
        boundary = call_boundary(instruction);
        unknown_write = boundary.effects != lowir_model::CFXM_READNONE &&
          boundary.effects != lowir_model::CFXM_READONLY;
        if(unknown_write)
          for(std::size_t memory_class = 0;
              memory_class < classes.size(); ++memory_class)
            if(classes[memory_class].enabled &&
               classes[memory_class].exclusive &&
               call_writes(instruction, classes[memory_class]))
              add_definition(definitions, memory_class, block);
      }
      if(eh_class != kNoIndex &&
         (lowir_eh_context::is_eh_instruction(instruction.kind) ||
          (instruction.kind == Instruction::IK_CALL &&
           boundary.unwind != lowir_model::CUM_NO)))
        add_definition(definitions, eh_class, block);
      const Operand * write_destination = 0;
      std::size_t write_bytes = 0;
      bool unbounded_write = false;
      if(memory_write_destination(
           instruction, &write_destination, &write_bytes, &unbounded_write)) {
        AddressFact store = operand_address(*write_destination, addresses);
        if(unbounded_write) {
          store.precise = false;
          store.bounded = false;
        }
        if(store.root_kind == ARK_SLOT || store.root_kind == ARK_GLOBAL ||
           store.root_kind == ARK_PARAMETER)
          visit_store_classes(store, write_bytes, classes,
            first_slot_class, first_parameter_class,
            symbol_epochs, function_epoch,
            first_symbol_class,
            [definitions, block](std::size_t memory_class) {
              add_definition(definitions, memory_class, block);
            });
        if(store.root_kind == ARK_UNKNOWN || !store.known() ||
           has_unknown_pointer) unknown_write = true;
      }
      if(unknown_write && unknown_class != kNoIndex)
        add_definition(definitions, unknown_class, block);
    }
  }
}

bool place_memory_merges(
    const std::vector<std::vector<std::size_t> > & definitions,
    const std::vector<lowir_analysis::EdgeList> & frontiers,
    std::vector<MemoryClass> * classes,
    std::vector<std::vector<std::size_t> > * block_merges,
    Stats * stats)
{
  std::vector<std::uint32_t> definition_epochs(frontiers.size(), 0);
  std::vector<std::uint32_t> merge_epochs(frontiers.size(), 0);
  std::vector<std::size_t> work;
  std::vector<std::size_t> pending;
  std::uint32_t epoch = 0;
  std::size_t total = 0;
  for(std::size_t memory_class = 0;
      memory_class < classes->size(); ++memory_class) {
    if(definitions[memory_class].empty()) continue;
    ++epoch;
    if(epoch == 0) {
      std::fill(definition_epochs.begin(), definition_epochs.end(), 0);
      std::fill(merge_epochs.begin(), merge_epochs.end(), 0);
      epoch = 1;
    }
    work = definitions[memory_class];
    for(std::size_t index = 0; index < work.size(); ++index)
      definition_epochs[work[index]] = epoch;
    pending.clear();
    for(std::size_t cursor = 0; cursor < work.size(); ++cursor) {
      const lowir_analysis::EdgeList & frontier = frontiers[work[cursor]];
      for(std::size_t edge = 0; edge < frontier.size(); ++edge) {
        const std::size_t block = frontier[edge];
        if(merge_epochs[block] == epoch) continue;
        merge_epochs[block] = epoch;
        pending.push_back(block);
        if(definition_epochs[block] != epoch) work.push_back(block);
        if(total + pending.size() > kMaximumMergeVersions) {
          (*classes)[memory_class].enabled = false;
          pending.clear();
          if(stats) {
            ++stats->memory_gvn_budget_skips;
            ++stats->budget_skips;
          }
          break;
        }
      }
      if(!(*classes)[memory_class].enabled) break;
    }
    if(!(*classes)[memory_class].enabled) continue;
    total += pending.size();
    for(std::size_t index = 0; index < pending.size(); ++index)
      (*block_merges)[pending[index]].push_back(memory_class);
  }
  if(stats) stats->memory_gvn_merge_versions += total;
  return total != 0;
}

struct VersionChange
{
  std::size_t memory_class;
  std::size_t previous;
};

struct AvailableLoad
{
  Operand value;
  std::size_t key;
  std::size_t previous;
};

Instruction load_replacement(const Instruction & load,
                             const Operand & value)
{
  Instruction result;
  result.kind = Instruction::IK_COPY;
  result.dest = load.dest;
  result.type = load.type;
  result.first = value;
  result.debug_location = load.debug_location;
  return result;
}

template<typename CallBoundary, typename CallWrites>
bool rewrite_redundant_loads(
    Function * function, lowir_analysis::FunctionAnalysis * analysis,
    Stats * stats, bool preserve_value_lifetimes,
    const std::vector<std::size_t> & final_uses,
    const std::vector<AddressFact> & addresses,
    const LocationMap & locations, std::vector<MemoryClass> * classes,
    const std::vector<std::size_t> & first_slot_class,
    const std::vector<std::size_t> & first_parameter_class,
    const std::vector<std::uint32_t> & symbol_epochs,
    std::uint32_t function_epoch,
    const std::vector<std::size_t> & symbol_classes,
    std::size_t unknown_class, std::size_t eh_class,
    bool has_unknown_pointer, const lowir_eh_context::Context & eh_context,
    const std::vector<std::vector<std::size_t> > & block_merges,
    const CallBoundary & call_boundary, const CallWrites & call_writes)
{
  const std::vector<BlockEvent> events = dominator_events(
    analysis->dominator_tree(), analysis->dominator_children());
  std::vector<std::size_t> versions(classes->size(), 0);
  std::size_t next_version = 0;
  std::vector<VersionChange> version_changes;
  std::vector<std::size_t> version_marks(function->blocks.size(), 0);
  std::unordered_map<MemoryKey, std::size_t, MemoryKeyHash> key_ids;
  std::vector<std::size_t> heads;
  std::vector<AvailableLoad> available;
  std::vector<std::size_t> available_marks(function->blocks.size(), 0);
  key_ids.reserve(function->value_names.size() / 8 + 1);
  heads.reserve(function->value_names.size() / 8 + 1);
  available.reserve(function->value_names.size() / 8 + 1);
  const auto assign_version = [&versions, &version_changes, &next_version](
      std::size_t memory_class) {
    version_changes.push_back(VersionChange{
      memory_class, versions[memory_class]});
    versions[memory_class] = ++next_version;
  };
  bool changed = false;
  for(std::size_t event = 0; event < events.size(); ++event) {
    const std::size_t block = events[event].block;
    if(!events[event].entering) {
      while(available.size() > available_marks[block]) {
        const AvailableLoad & load = available.back();
        heads[load.key] = load.previous;
        available.pop_back();
      }
      while(version_changes.size() > version_marks[block]) {
        const VersionChange & change = version_changes.back();
        versions[change.memory_class] = change.previous;
        version_changes.pop_back();
      }
      continue;
    }
    version_marks[block] = version_changes.size();
    available_marks[block] = available.size();
    for(std::size_t merge = 0;
        merge < block_merges[block].size(); ++merge)
      if((*classes)[block_merges[block][merge]].enabled)
        assign_version(block_merges[block][merge]);
    if(eh_class != kNoIndex && eh_context.entry_barriers[block])
      assign_version(eh_class);
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = 0; index < instructions.size(); ++index) {
      Instruction & instruction = instructions[index];
      if(stats) ++stats->instruction_visits;
      lowir_model::FunctionBoundaryMetadata boundary;
      const bool call = instruction.kind == Instruction::IK_CALL;
      if(call) boundary = call_boundary(instruction);
      if(eh_class != kNoIndex &&
         (lowir_eh_context::is_eh_instruction(instruction.kind) ||
          (call && boundary.unwind != lowir_model::CUM_NO))) {
        assign_version(eh_class);
        if(stats && call) ++stats->memory_gvn_eh_barriers;
      }
      if(call && boundary.effects != lowir_model::CFXM_READNONE &&
         boundary.effects != lowir_model::CFXM_READONLY)
        for(std::size_t memory_class = 0;
            memory_class < classes->size(); ++memory_class)
          if((*classes)[memory_class].enabled &&
             (*classes)[memory_class].exclusive &&
             call_writes(instruction, (*classes)[memory_class]))
            assign_version(memory_class);
      if(instruction.kind == Instruction::IK_LOAD &&
         !instruction.volatile_access &&
         !instruction.debug_location.present() &&
         scalar_load_type(instruction.type)) {
        const std::size_t memory_class =
          load_class(instruction, addresses, locations);
        if(memory_class != kNoIndex && (*classes)[memory_class].enabled) {
          const MemoryClass & load_memory = (*classes)[memory_class];
          const std::size_t unknown_version =
            ((load_memory.kind == MCK_GLOBAL && !load_memory.readonly) ||
             load_memory.kind == MCK_UNKNOWN_POINTER ||
             (load_memory.kind == MCK_PARAMETER && !load_memory.exclusive)) &&
            unknown_class != kNoIndex ? versions[unknown_class] : 0;
          const MemoryKey key = {
            memory_class, versions[memory_class], unknown_version,
            eh_class == kNoIndex ? 0 : versions[eh_class],
            instruction.type.kind, instruction.type.storage_size,
            instruction.type.alignment};
          if(stats) ++stats->memory_gvn_load_probes;
          const auto inserted = key_ids.emplace(key, heads.size());
          if(inserted.second) heads.push_back(kNoIndex);
          const std::size_t key_id = inserted.first->second;
          bool reusable = heads[key_id] != kNoIndex;
          if(reusable && preserve_value_lifetimes) {
            const Operand & available_value = available[heads[key_id]].value;
            reusable = available_value.kind == Operand::OP_TEMP &&
              available_value.value < final_uses.size() &&
              instruction.dest < final_uses.size() &&
              final_uses[available_value.value] != kNoIndex &&
              final_uses[available_value.value] >= final_uses[instruction.dest];
          }
          if(reusable) {
            instruction = load_replacement(
              instruction, available[heads[key_id]].value);
            changed = true;
            if(stats) {
              ++stats->memory_gvn_loads_eliminated;
              ++stats->rewrites;
            }
          } else {
            Operand value;
            value.kind = Operand::OP_TEMP;
            value.value = instruction.dest;
            available.push_back(AvailableLoad{value, key_id, heads[key_id]});
            heads[key_id] = available.size() - 1;
          }
        }
      }

      bool unknown_write = is_atomic_barrier(instruction.kind) ||
        is_other_memory_write(instruction.kind);
      if(call) unknown_write = boundary.effects != lowir_model::CFXM_READNONE &&
        boundary.effects != lowir_model::CFXM_READONLY;
      const Operand * write_destination = 0;
      std::size_t write_bytes = 0;
      bool unbounded_direct_write = false;
      if(memory_write_destination(instruction, &write_destination,
           &write_bytes, &unbounded_direct_write)) {
        AddressFact store = operand_address(*write_destination, addresses);
        if(unbounded_direct_write) {
          store.precise = false;
          store.bounded = false;
        }
        if(store.root_kind == ARK_SLOT || store.root_kind == ARK_GLOBAL ||
           store.root_kind == ARK_PARAMETER)
          visit_store_classes(store, write_bytes, *classes,
            first_slot_class, first_parameter_class, symbol_epochs,
            function_epoch, symbol_classes, assign_version);
        if(store.root_kind == ARK_UNKNOWN || !store.known() ||
           has_unknown_pointer) unknown_write = true;
      }
      if(unknown_write && unknown_class != kNoIndex &&
         (*classes)[unknown_class].enabled) {
        assign_version(unknown_class);
        if(stats) ++stats->memory_gvn_unknown_barriers;
      }
    }
  }
  return changed;
}

}  // namespace

MemoryGVNSession::MemoryGVNSession(const lowir_model::Program & program,
                                   bool analyze_parameters, Stats * stats)
  : boundaries_(program.symbol_names.size()),
    known_boundaries_(program.symbol_names.size(), 0),
    global_storage_(program.symbol_names.size(), lowir_model::GSM_DEFAULT),
    symbol_epochs_(program.symbol_names.size(), 0),
    symbol_classes_(program.symbol_names.size(), kNoIndex), function_epoch_(0)
{
  if(analyze_parameters) {
    const std::chrono::steady_clock::time_point started = stats ?
      std::chrono::steady_clock::now() :
      std::chrono::steady_clock::time_point();
    ProgramMemoryAnalysis memory = analyze_program_memory(program);
    if(stats) {
      record_elapsed(stats, started);
      stats->memory_gvn_program_call_sites += memory.call_sites;
      stats->memory_gvn_nocapture_rounds += memory.nocapture_rounds;
      stats->memory_gvn_capture_rounds += memory.capture_rounds;
      stats->memory_gvn_exclusive_rounds += memory.exclusive_rounds;
      stats->memory_gvn_effect_rounds += memory.effect_rounds;
      for(std::size_t function = 0;
          function < program.functions.size(); ++function)
        for(std::size_t parameter = 0;
            parameter < program.functions[function].params.size(); ++parameter) {
          if(program.functions[function].params[parameter].metadata.object_bytes)
            ++stats->memory_gvn_object_extent_parameters;
          if(function < memory.exclusive.size() &&
             parameter < memory.exclusive[function].size() &&
             memory.exclusive[function][parameter])
            ++stats->memory_gvn_exclusive_parameters;
          if(function < memory.captures.size() &&
             parameter < memory.captures[function].size())
            stats->memory_gvn_capture_ranges +=
              memory.captures[function][parameter].ranges.size();
        }
    }
    function_by_symbol_.swap(memory.function_by_symbol);
    parameter_effects_.swap(memory.effects);
    parameter_captures_.swap(memory.captures);
    parameter_object_bytes_.swap(memory.object_bytes_by_symbol);
  }
  for(std::size_t index = 0;
      index < program.function_declarations.size(); ++index) {
    const lowir_model::SymbolId symbol =
      program.function_declarations[index].symbol;
    boundaries_[symbol] = program.function_declarations[index].boundary;
    known_boundaries_[symbol] = 1;
  }
  for(std::size_t index = 0; index < program.functions.size(); ++index) {
    const lowir_model::SymbolId symbol = program.functions[index].symbol;
    boundaries_[symbol] = program.functions[index].boundary;
    known_boundaries_[symbol] = 1;
  }
  for(std::size_t index = 0;
      index < program.global_declarations.size(); ++index)
    global_storage_[program.global_declarations[index].symbol] =
      program.global_declarations[index].storage;
  for(std::size_t index = 0; index < program.globals.size(); ++index)
    global_storage_[program.globals[index].symbol] =
      program.globals[index].storage;
}

lowir_model::FunctionBoundaryMetadata MemoryGVNSession::call_boundary(
    const Instruction & instruction) const
{
  lowir_model::FunctionBoundaryMetadata result = instruction.call_boundary;
  if(instruction.first.kind == Operand::OP_GLOBAL &&
     instruction.first.symbol < known_boundaries_.size() &&
     known_boundaries_[instruction.first.symbol])
    result = boundaries_[instruction.first.symbol];
  return result;
}

void MemoryGVNSession::begin_function()
{
  ++function_epoch_;
  if(function_epoch_ == 0) {
    std::fill(symbol_epochs_.begin(), symbol_epochs_.end(), 0);
    function_epoch_ = 1;
  }
}

void MemoryGVNSession::touch_symbol(lowir_model::SymbolId symbol)
{
  if(symbol_epochs_[symbol] == function_epoch_) return;
  symbol_epochs_[symbol] = function_epoch_;
  symbol_classes_[symbol] = kNoIndex;
}

bool MemoryGVNSession::eliminate_redundant_loads(
    Function * function, lowir_analysis::FunctionAnalysis * analysis, Stats * stats,
    bool preserve_value_lifetimes)
{
  const std::chrono::steady_clock::time_point started =
    stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
  if(stats) ++stats->memory_gvn_runs;
  if(function->blocks.empty()) return false;
  const std::vector<std::size_t> final_uses = preserve_value_lifetimes ? layout_last_uses(*function) : std::vector<std::size_t>();
  const lowir_eh_context::Context eh_context = lowir_eh_context::analyze(*function, analysis->graph());
  if(eh_context.has_eh && stats) {
    ++stats->memory_gvn_eh_functions;
    stats->memory_gvn_eh_barriers += eh_context.barrier_count;
  }
  if(eh_context.conflicting) {
    if(stats) {
      ++stats->memory_gvn_eh_skips;
      record_elapsed(stats, started);
    }
    return false;
  }

  begin_function();
  const std::vector<AddressFact> addresses = derive_addresses(*function);
  const std::vector<unsigned char> escaped_slots = find_escaped_slots(*function, addresses);
  LocationMap locations;
  std::vector<LocationKey> location_order;
  collect_load_locations(*function, addresses, escaped_slots,
    global_storage_, &locations, &location_order);

  const std::size_t function_index =
    function->symbol < function_by_symbol_.size() ?
      function_by_symbol_[function->symbol] : kNoIndex;

  std::vector<MemoryClass> classes;
  std::vector<std::size_t> first_slot_class(function->slot_names.size(), kNoIndex);
  std::vector<std::size_t> first_parameter_class(
    function->params.size(), kNoIndex);
  for(std::size_t index = 0; index < location_order.size(); ++index) {
    LocationInfo & info = locations.find(location_order[index])->second;
    if(info.load_count < 2) continue;
    const LocationKey & key = location_order[index];
    const bool slot = key.root_kind == ARK_SLOT;
    const bool global = key.root_kind == ARK_GLOBAL;
    const bool parameter = key.root_kind == ARK_PARAMETER;
    const bool private_parameter = parameter && function_index != kNoIndex &&
      key.identity < parameter_effects_[function_index].size() &&
      parameter_effects_[function_index][key.identity].exclusive;
    const bool exclusive = private_parameter &&
      function_index < parameter_captures_.size() &&
      key.identity < parameter_captures_[function_index].size() &&
      !capture_overlaps(
        parameter_captures_[function_index][key.identity], key);
    if(parameter && stats) ++stats->memory_gvn_parameter_classes;
    std::size_t previous = kNoIndex;
    if(slot) previous = first_slot_class[key.identity];
    else if(parameter) previous = first_parameter_class[key.identity];
    else if(global) {
      touch_symbol(key.identity);
      previous = symbol_classes_[key.identity];
    }
    info.memory_class = classes.size();
    classes.push_back(MemoryClass{
      slot ? MCK_SLOT : global ? MCK_GLOBAL :
        parameter ? MCK_PARAMETER : MCK_UNKNOWN_POINTER,
      key.identity, key.address_identity,
      key.byte_offset, key.type_size, previous, key.precise,
      global && global_storage_[key.identity] == lowir_model::GSM_READONLY,
      exclusive, true});
    if(slot) first_slot_class[key.identity] = info.memory_class;
    else if(parameter) first_parameter_class[key.identity] = info.memory_class;
    else if(global) symbol_classes_[key.identity] = info.memory_class;
  }
  if(classes.empty()) {
    record_elapsed(stats, started);
    return false;
  }
  if(classes.size() > kMaximumClasses) {
    if(stats) {
      ++stats->memory_gvn_budget_skips;
      ++stats->budget_skips;
      record_elapsed(stats, started);
    }
    return false;
  }

  std::size_t unknown_class = kNoIndex;
  for(std::size_t index = 0; index < classes.size(); ++index)
    if((classes[index].kind == MCK_GLOBAL && !classes[index].readonly) ||
       classes[index].kind == MCK_UNKNOWN_POINTER ||
       (classes[index].kind == MCK_PARAMETER && !classes[index].exclusive)) {
      unknown_class = classes.size();
      classes.push_back(MemoryClass{
        MCK_UNKNOWN_GLOBAL, 0, lowir_model::ValueId(), 0, 0, kNoIndex,
        false, false, false, true});
      break;
  }
  const bool has_unknown_pointer = std::find_if(
    classes.begin(), classes.end(), [](const MemoryClass & memory_class) {
      return memory_class.kind == MCK_UNKNOWN_POINTER ||
        (memory_class.kind == MCK_PARAMETER && !memory_class.exclusive);
    }) != classes.end();
  const std::size_t eh_class = eh_context.has_eh ? classes.size() : kNoIndex;
  if(eh_class != kNoIndex)
    classes.push_back(MemoryClass{
      MCK_EH_BARRIER, 0, lowir_model::ValueId(), 0, 0, kNoIndex,
      false, false, false, true});
  const auto call_writes = [this, function_index, &addresses](
      const Instruction & instruction, const MemoryClass & memory_class) {
    if(!memory_class.exclusive ||
       memory_class.kind != MCK_PARAMETER || function_index == kNoIndex)
      return true;
    const std::size_t callee = direct_callee(
      instruction, function_by_symbol_);
    if(instruction.first.kind == Operand::OP_TEMP) {
      const AddressFact target = operand_address(instruction.first, addresses);
      if(target.root_kind == ARK_PARAMETER &&
         target.identity == memory_class.identity) return true;
    }
    for(std::size_t argument = 0;
        argument < instruction.args.size(); ++argument) {
      const AddressFact actual = operand_address(
        instruction.args[argument], addresses);
      if(actual.root_kind != ARK_PARAMETER ||
         actual.identity != memory_class.identity) continue;
      std::size_t bounded_bytes = 0;
      if(instruction.first.kind == Operand::OP_GLOBAL &&
         instruction.first.symbol < parameter_object_bytes_.size() &&
         argument < parameter_object_bytes_[instruction.first.symbol].size())
        bounded_bytes =
          parameter_object_bytes_[instruction.first.symbol][argument];
      else if(instruction.first.kind != Operand::OP_GLOBAL &&
              instruction.has_call_signature &&
              argument < instruction.call_params.size())
        bounded_bytes =
          instruction.call_params[argument].metadata.object_bytes;
      const bool precise_callee_effect = actual.precise &&
        callee != kNoIndex && callee < parameter_effects_.size() &&
        argument < parameter_effects_[callee].size() &&
        !parameter_effects_[callee][argument].unknown_write;
      if(precise_callee_effect) {
        const MemoryParameterEffect & effect =
          parameter_effects_[callee][argument];
        if(!effect.has_write) continue;
        const __int128 begin = static_cast<__int128>(actual.byte_offset) +
          effect.write_begin;
        const __int128 end = static_cast<__int128>(actual.byte_offset) +
          effect.write_end;
        if(begin < std::numeric_limits<long long>::min() ||
           end > std::numeric_limits<long long>::max()) return true;
        if(ranges_overlap(static_cast<long long>(begin),
             static_cast<std::size_t>(end - begin),
             memory_class.byte_offset, memory_class.byte_size)) return true;
        continue;
      }
      if(bounded_bytes && actual.precise) {
        if(ranges_overlap(actual.byte_offset, bounded_bytes,
             memory_class.byte_offset, memory_class.byte_size)) return true;
        continue;
      }
      return true;
    }
    return false;
  };
  std::vector<std::vector<std::size_t> > definitions(classes.size());
  collect_memory_definitions(*function, addresses, classes,
    first_slot_class, first_parameter_class,
    symbol_epochs_, function_epoch_, symbol_classes_,
    unknown_class, eh_class, has_unknown_pointer, eh_context,
    [this](const Instruction & instruction) {
      return call_boundary(instruction);
    }, call_writes, &definitions);
  const std::vector<lowir_analysis::EdgeList> & frontiers = analysis->dominance_frontier();
  std::vector<std::vector<std::size_t> > block_merges(function->blocks.size());
  place_memory_merges(definitions, frontiers, &classes, &block_merges, stats);
  if(eh_class != kNoIndex && !classes[eh_class].enabled) {
    if(stats) {
      ++stats->memory_gvn_eh_skips;
      record_elapsed(stats, started);
    }
    return false;
  }
  if(unknown_class != kNoIndex && !classes[unknown_class].enabled)
    for(std::size_t index = 0; index < classes.size(); ++index)
      if((classes[index].kind == MCK_GLOBAL && !classes[index].readonly) ||
         classes[index].kind == MCK_UNKNOWN_POINTER ||
         (classes[index].kind == MCK_PARAMETER && !classes[index].exclusive))
        classes[index].enabled = false;
  if(stats) stats->memory_gvn_classes += classes.size() -
    (unknown_class == kNoIndex ? 0 : 1) -
    (eh_class == kNoIndex ? 0 : 1);

  const bool changed = rewrite_redundant_loads(function, analysis, stats,
    preserve_value_lifetimes, final_uses, addresses, locations, &classes,
    first_slot_class, first_parameter_class, symbol_epochs_, function_epoch_,
    symbol_classes_, unknown_class, eh_class, has_unknown_pointer, eh_context,
    block_merges,
    [this](const Instruction & instruction) {
      return call_boundary(instruction);
    }, call_writes);
  record_elapsed(stats, started);
  return changed;
}

bool rematerialize_parameter_addresses(Function * function, Stats * stats)
{
  if(function->blocks.empty() || function->params.empty()) return false;
  const std::vector<AddressFact> addresses = derive_addresses(*function);
  const std::size_t original_values = function->value_names.size();
  std::vector<std::size_t> definition_block(original_values, kNoIndex);
  std::vector<std::size_t> definition_instruction(original_values, kNoIndex);
  std::vector<unsigned char> eligible(original_values, 0);
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function->blocks[block].instructions[index];
      if(!instruction.dest.valid() || instruction.dest >= original_values)
        continue;
      definition_block[instruction.dest] = block;
      definition_instruction[instruction.dest] = index;
    }
  for(std::size_t value = 0; value < original_values; ++value) {
    if(value >= addresses.size() ||
       addresses[value].root_kind != ARK_PARAMETER ||
       !addresses[value].precise || addresses[value].byte_offset <= 0 ||
       addresses[value].identity >= function->params.size() ||
       !function->params[addresses[value].identity].metadata.object_bytes ||
       value >= function->value_types.size() ||
       function->value_types[value].kind != lowir_model::LTK_PTR ||
       definition_block[value] == kNoIndex)
      continue;
    const Instruction & definition = function->blocks[
      definition_block[value]].instructions[definition_instruction[value]];
    if((definition.kind == Instruction::IK_INDEX ||
        definition.kind == Instruction::IK_COPY) &&
       !definition.debug_location.present())
      eligible[value] = 1;
  }

  std::size_t rematerialized = 0;
  bool budget_skipped = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> source =
      std::move(function->blocks[block].instructions);
    std::vector<Instruction> rebuilt;
    rebuilt.reserve(source.size());
    std::unordered_map<std::uint32_t, Operand> segment_clones;
    std::size_t segment = 0;
    std::vector<std::size_t> definition_segments(source.size(), 0);
    for(std::size_t index = 0; index < source.size(); ++index) {
      definition_segments[index] = segment;
      if(source[index].kind == Instruction::IK_CALL) ++segment;
    }
    segment = 0;
    for(std::size_t index = 0; index < source.size(); ++index) {
      const bool call = source[index].kind == Instruction::IK_CALL;
      Instruction instruction = std::move(source[index]);
      if(instruction.kind != Instruction::IK_PHI) {
        const auto replace = [&](Operand * operand) {
          if(operand->kind != Operand::OP_TEMP ||
             operand->value >= eligible.size() ||
             !eligible[operand->value])
            return;
          const std::size_t value = operand->value;
          const bool other_block = definition_block[value] != block;
          const bool crosses_call = !other_block &&
            definition_instruction[value] < definition_segments.size() &&
            definition_segments[definition_instruction[value]] < segment;
          if(!other_block && !crosses_call) return;
          const auto found = segment_clones.find(
            static_cast<std::uint32_t>(value));
          if(found != segment_clones.end()) {
            *operand = found->second;
            return;
          }
          if(rematerialized >=
               kMaximumParameterAddressRematerializations) {
            budget_skipped = true;
            return;
          }
          Instruction clone;
          clone.kind = Instruction::IK_INDEX;
          clone.dest = lowir_model::append_lowir_fresh_generated_value(
            *function,
            lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
          clone.type = lowir_model::builtin_lowir_type(lowir_model::LTK_I8);
          clone.first.kind = Operand::OP_TEMP;
          clone.first.value = function->params[
            addresses[value].identity].value;
          clone.second.kind = Operand::OP_INTEGER;
          clone.second.has_int_value = true;
          clone.second.int_value = addresses[value].byte_offset;
          clone.second.int_high = 0;
          Operand replacement;
          replacement.kind = Operand::OP_TEMP;
          replacement.value = clone.dest;
          rebuilt.push_back(std::move(clone));
          segment_clones.emplace(
            static_cast<std::uint32_t>(value), replacement);
          *operand = replacement;
          ++rematerialized;
        };
        replace(&instruction.first);
        replace(&instruction.second);
        replace(&instruction.third);
        for(std::size_t argument = 0;
            argument < instruction.args.size(); ++argument)
          replace(&instruction.args[argument]);
      }
      rebuilt.push_back(std::move(instruction));
      if(call) {
        ++segment;
        segment_clones.clear();
      }
    }
    function->blocks[block].instructions.swap(rebuilt);
  }
  if(rematerialized && stats) {
    stats->parameter_address_rematerializations += rematerialized;
    stats->rewrites += rematerialized;
  }
  if(budget_skipped && stats) {
    ++stats->parameter_address_rematerialization_budget_skips;
    ++stats->budget_skips;
  }
  return rematerialized != 0;
}

}  // namespace lowir_opt
