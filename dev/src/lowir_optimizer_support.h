#pragma once

#include "lowir_model.h"

namespace lowir_opt {
namespace optimizer_support {

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
