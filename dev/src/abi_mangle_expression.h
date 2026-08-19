#pragma once

// Compact operation vocabulary for dependent expressions in ABI facts.

#include <cstdint>
#include <string>

namespace abi_mangle {

enum AbiExpressionOperationKind : std::uint8_t
{
  ABI_EXPRESSION_OPERATION_TEXT,
  ABI_EXPRESSION_OPERATION_DEREFERENCE,
  ABI_EXPRESSION_OPERATION_SUBTRACT,
  ABI_EXPRESSION_OPERATION_MEMBER,
  ABI_EXPRESSION_OPERATION_INDIRECT_MEMBER
};

// Unknown PA14 operation codes remain textual adapter data.  Integrated
// compilation constructs one of the fixed operation kinds directly.
AbiExpressionOperationKind abi_expression_operation_kind(
  const std::string & word);
const char * abi_expression_operation_code(AbiExpressionOperationKind kind);

}  // namespace abi_mangle
