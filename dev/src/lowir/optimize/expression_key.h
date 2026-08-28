#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <cstdint>

namespace lowir_opt {

struct ExpressionOperandKey
{
  lowir_model::Operand::Kind kind;
  std::uint32_t identity;
  long long int_value;
  std::uint64_t int_high;

  bool operator==(const ExpressionOperandKey & other) const;
};

struct ExpressionKey
{
  lowir_model::Instruction::Kind kind;
  lowir_model::LowOperation op;
  lowir_model::LowTypeKind type_kind;
  std::size_t type_size;
  std::size_t type_alignment;
  lowir_model::LowTypeKind source_type_kind;
  std::size_t source_type_size;
  std::size_t source_type_alignment;
  lowir_model::IndexProjectionKind index_projection;
  ExpressionOperandKey first;
  ExpressionOperandKey second;

  bool operator==(const ExpressionKey & other) const;
};

struct ExpressionKeyHash
{
  std::size_t operator()(const ExpressionKey & key) const;
};

bool cse_eligible(lowir_model::Instruction::Kind kind);
ExpressionKey expression_key(const lowir_model::Instruction & instruction);

}  // namespace lowir_opt
