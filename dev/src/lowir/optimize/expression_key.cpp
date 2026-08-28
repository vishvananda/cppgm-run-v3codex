#include "lowir/optimize/expression_key.h"

#include "lowir/optimize/support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace lowir_opt {
namespace {

using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::Operand;
using optimizer_support::combine_hash;

bool commutative(LowOperation operation)
{
  return operation.kind == LowOperation::LOP_ADD ||
    operation.kind == LowOperation::LOP_MUL ||
    operation.kind == LowOperation::LOP_AND ||
    operation.kind == LowOperation::LOP_OR ||
    operation.kind == LowOperation::LOP_XOR;
}

LowOperation reverse_compare(LowOperation operation)
{
  if(operation.kind == LowOperation::LOP_LT) return LowOperation::LOP_GT;
  if(operation.kind == LowOperation::LOP_LE) return LowOperation::LOP_GE;
  if(operation.kind == LowOperation::LOP_GT) return LowOperation::LOP_LT;
  if(operation.kind == LowOperation::LOP_GE) return LowOperation::LOP_LE;
  if(operation.kind == LowOperation::LOP_ULT) return LowOperation::LOP_UGT;
  if(operation.kind == LowOperation::LOP_ULE) return LowOperation::LOP_UGE;
  if(operation.kind == LowOperation::LOP_UGT) return LowOperation::LOP_ULT;
  if(operation.kind == LowOperation::LOP_UGE) return LowOperation::LOP_ULE;
  return operation;
}

bool operand_less(const Operand & left, const Operand & right)
{
  if(left.kind != right.kind) return left.kind < right.kind;
  if(left.kind == Operand::OP_SLOT) return left.slot < right.slot;
  if(left.kind == Operand::OP_LABEL) return left.block < right.block;
  if(left.kind == Operand::OP_TEMP) return left.value < right.value;
  if(left.kind == Operand::OP_GLOBAL) return left.symbol < right.symbol;
  if(left.kind == Operand::OP_INTEGER)
    return left.int_high != right.int_high ?
      left.int_high < right.int_high :
      static_cast<std::uint64_t>(left.int_value) <
        static_cast<std::uint64_t>(right.int_value);
  if(left.kind == Operand::OP_FLOAT)
    return left.literal_high != right.literal_high ?
      left.literal_high < right.literal_high :
      left.literal_low < right.literal_low;
  return false;
}

ExpressionOperandKey operand_key(const Operand & operand)
{
  ExpressionOperandKey key;
  key.kind = operand.kind;
  key.identity = lowir_model::kInvalidCompactId;
  key.int_value = 0;
  key.int_high = 0;
  if(operand.kind == Operand::OP_SLOT) key.identity = operand.slot;
  else if(operand.kind == Operand::OP_LABEL) key.identity = operand.block;
  else if(operand.kind == Operand::OP_TEMP) key.identity = operand.value;
  else if(operand.kind == Operand::OP_GLOBAL) key.identity = operand.symbol;
  else if(operand.kind == Operand::OP_INTEGER ||
          operand.kind == Operand::OP_FLOAT) {
    key.int_value = operand.int_value;
    key.int_high = operand.int_high;
  }
  return key;
}

}  // namespace

bool ExpressionOperandKey::operator==(
    const ExpressionOperandKey & other) const
{
  return kind == other.kind && identity == other.identity &&
    int_value == other.int_value && int_high == other.int_high;
}

bool ExpressionKey::operator==(const ExpressionKey & other) const
{
  return kind == other.kind && op == other.op &&
    type_kind == other.type_kind && type_size == other.type_size &&
    type_alignment == other.type_alignment &&
    source_type_kind == other.source_type_kind &&
    source_type_size == other.source_type_size &&
    source_type_alignment == other.source_type_alignment &&
    index_projection == other.index_projection &&
    first == other.first && second == other.second;
}

std::size_t ExpressionKeyHash::operator()(const ExpressionKey & key) const
{
  std::size_t result = static_cast<std::size_t>(key.kind);
  combine_hash(&result, lowir_model::lowir_operation_hash(key.op));
  combine_hash(&result, static_cast<std::size_t>(key.type_kind));
  combine_hash(&result, key.type_size);
  combine_hash(&result, key.type_alignment);
  combine_hash(&result, static_cast<std::size_t>(key.source_type_kind));
  combine_hash(&result, key.source_type_size);
  combine_hash(&result, key.source_type_alignment);
  combine_hash(&result, static_cast<std::size_t>(key.index_projection));
  const ExpressionOperandKey operands[] = {key.first, key.second};
  for(std::size_t index = 0; index < 2; ++index) {
    combine_hash(&result, static_cast<std::size_t>(operands[index].kind));
    combine_hash(&result, operands[index].identity);
    combine_hash(&result, std::hash<long long>()(operands[index].int_value));
    combine_hash(&result,
      std::hash<std::uint64_t>()(operands[index].int_high));
  }
  return result;
}

bool cse_eligible(Instruction::Kind kind)
{
  return kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

ExpressionKey expression_key(const Instruction & instruction)
{
  const Operand * first = &instruction.first;
  const Operand * second = &instruction.second;
  LowOperation operation = instruction.op;
  if((instruction.kind == Instruction::IK_BINARY &&
      commutative(operation)) ||
     (instruction.kind == Instruction::IK_CMP &&
      (operation.kind == LowOperation::LOP_EQ ||
       operation.kind == LowOperation::LOP_NE))) {
    if(operand_less(*second, *first)) std::swap(first, second);
  } else if(instruction.kind == Instruction::IK_CMP &&
            operand_less(*second, *first)) {
    std::swap(first, second);
    operation = reverse_compare(operation);
  }
  ExpressionKey key;
  key.kind = instruction.kind;
  key.op = operation;
  key.type_kind = instruction.type.kind;
  key.type_size = instruction.type.storage_size;
  key.type_alignment = instruction.type.alignment;
  key.source_type_kind = instruction.source_type.kind;
  key.source_type_size = instruction.source_type.storage_size;
  key.source_type_alignment = instruction.source_type.alignment;
  key.index_projection = instruction.index_projection;
  key.first = operand_key(*first);
  key.second = operand_key(*second);
  return key;
}

}  // namespace lowir_opt
