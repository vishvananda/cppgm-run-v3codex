#pragma once

#include "lowir/model/program.h"

#include <cstddef>

namespace lowir_model {
namespace operand_view {

// A role-neutral view of first, second, third, then args.  It is allocation
// free and preserves serialized operand order.  Callers that interpret roles
// (notably phi value/label pairs) must keep their specialized traversal.
inline std::size_t all_operand_count(const Instruction & instruction)
{
  return 3 + instruction.args.size();
}

inline const Operand & all_operand_at(
    const Instruction & instruction, std::size_t index)
{
  if(index == 0) return instruction.first;
  if(index == 1) return instruction.second;
  if(index == 2) return instruction.third;
  return instruction.args[index - 3];
}

inline Operand & all_operand_at(Instruction & instruction, std::size_t index)
{
  if(index == 0) return instruction.first;
  if(index == 1) return instruction.second;
  if(index == 2) return instruction.third;
  return instruction.args[index - 3];
}

}  // namespace operand_view
}  // namespace lowir_model
