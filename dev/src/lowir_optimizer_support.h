#pragma once

#include "lowir_model.h"

#include <cstddef>

namespace lowir_opt {
namespace optimizer_support {

// A role-neutral view of first, second, third, then args.  It is allocation
// free and preserves serialized operand order.  Callers that interpret roles
// (notably phi value/label pairs) must keep their specialized traversal.
inline std::size_t all_operand_count(
    const lowir_model::Instruction & instruction)
{
  return 3 + instruction.args.size();
}

inline const lowir_model::Operand & all_operand_at(
    const lowir_model::Instruction & instruction, std::size_t index)
{
  if(index == 0) return instruction.first;
  if(index == 1) return instruction.second;
  if(index == 2) return instruction.third;
  return instruction.args[index - 3];
}

inline lowir_model::Operand & all_operand_at(
    lowir_model::Instruction & instruction, std::size_t index)
{
  if(index == 0) return instruction.first;
  if(index == 1) return instruction.second;
  if(index == 2) return instruction.third;
  return instruction.args[index - 3];
}

// The exact hash combiner used by LowIR optimizer keys.  Key field choice and
// ordering remain with each key owner; this helper defines only the mixer.
inline void combine_hash(std::size_t * seed, std::size_t value)
{
  *seed ^= value + static_cast<std::size_t>(0x9e3779b9U) +
    (*seed << 6) + (*seed >> 2);
}

// Exact identity of a LowIR operand that denotes a load/store location.
// This intentionally accepts only temporaries, slots, and globals; global
// identity includes the serialized address binding.  It does not implement
// general operand equality and does not compare access type or volatility.
inline bool same_storage_location(const lowir_model::Operand & left,
                                  const lowir_model::Operand & right)
{
  if(left.kind != right.kind) return false;
  if(left.kind == lowir_model::Operand::OP_TEMP)
    return left.value == right.value;
  if(left.kind == lowir_model::Operand::OP_SLOT)
    return left.slot == right.slot;
  if(left.kind == lowir_model::Operand::OP_GLOBAL)
    return left.symbol == right.symbol &&
      left.address_binding == right.address_binding;
  return false;
}

}  // namespace optimizer_support
}  // namespace lowir_opt
