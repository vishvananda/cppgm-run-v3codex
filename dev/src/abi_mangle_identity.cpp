#include "abi_mangle.h"

#include <limits>
#include <stdexcept>

namespace abi_mangle {

AbiTerminalKind abi_terminal_kind(const std::string & word)
{
  static const struct Entry
  {
    const char * word;
    AbiTerminalKind kind;
  } entries[] = {
    {"constructor-complete", ABI_TERMINAL_CONSTRUCTOR_COMPLETE},
    {"constructor-base", ABI_TERMINAL_CONSTRUCTOR_BASE},
    {"destructor-deleting", ABI_TERMINAL_DESTRUCTOR_DELETING},
    {"destructor-complete", ABI_TERMINAL_DESTRUCTOR_COMPLETE},
    {"destructor-base", ABI_TERMINAL_DESTRUCTOR_BASE},
    {"literal", ABI_TERMINAL_LITERAL},
    {"plus", ABI_TERMINAL_PLUS},
    {"minus", ABI_TERMINAL_MINUS},
    {"unary-plus", ABI_TERMINAL_UNARY_PLUS},
    {"binary-plus", ABI_TERMINAL_BINARY_PLUS},
    {"unary-minus", ABI_TERMINAL_UNARY_MINUS},
    {"binary-minus", ABI_TERMINAL_BINARY_MINUS},
    {"address-of", ABI_TERMINAL_ADDRESS_OF},
    {"deref", ABI_TERMINAL_DEREFERENCE},
    {"new", ABI_TERMINAL_NEW},
    {"new-array", ABI_TERMINAL_NEW_ARRAY},
    {"delete", ABI_TERMINAL_DELETE},
    {"delete-array", ABI_TERMINAL_DELETE_ARRAY},
    {"multiply", ABI_TERMINAL_MULTIPLY},
    {"divide", ABI_TERMINAL_DIVIDE},
    {"remainder", ABI_TERMINAL_REMAINDER},
    {"bit-and", ABI_TERMINAL_BIT_AND},
    {"bit-or", ABI_TERMINAL_BIT_OR},
    {"bit-xor", ABI_TERMINAL_BIT_XOR},
    {"assign", ABI_TERMINAL_ASSIGN},
    {"plus-assign", ABI_TERMINAL_PLUS_ASSIGN},
    {"minus-assign", ABI_TERMINAL_MINUS_ASSIGN},
    {"multiply-assign", ABI_TERMINAL_MULTIPLY_ASSIGN},
    {"divide-assign", ABI_TERMINAL_DIVIDE_ASSIGN},
    {"remainder-assign", ABI_TERMINAL_REMAINDER_ASSIGN},
    {"and-assign", ABI_TERMINAL_AND_ASSIGN},
    {"or-assign", ABI_TERMINAL_OR_ASSIGN},
    {"xor-assign", ABI_TERMINAL_XOR_ASSIGN},
    {"left-shift", ABI_TERMINAL_LEFT_SHIFT},
    {"right-shift", ABI_TERMINAL_RIGHT_SHIFT},
    {"left-shift-assign", ABI_TERMINAL_LEFT_SHIFT_ASSIGN},
    {"right-shift-assign", ABI_TERMINAL_RIGHT_SHIFT_ASSIGN},
    {"equal", ABI_TERMINAL_EQUAL},
    {"not-equal", ABI_TERMINAL_NOT_EQUAL},
    {"less", ABI_TERMINAL_LESS},
    {"greater", ABI_TERMINAL_GREATER},
    {"less-equal", ABI_TERMINAL_LESS_EQUAL},
    {"greater-equal", ABI_TERMINAL_GREATER_EQUAL},
    {"logical-not", ABI_TERMINAL_LOGICAL_NOT},
    {"logical-and", ABI_TERMINAL_LOGICAL_AND},
    {"logical-or", ABI_TERMINAL_LOGICAL_OR},
    {"increment", ABI_TERMINAL_INCREMENT},
    {"decrement", ABI_TERMINAL_DECREMENT},
    {"comma", ABI_TERMINAL_COMMA},
    {"member-pointer", ABI_TERMINAL_MEMBER_POINTER},
    {"arrow", ABI_TERMINAL_ARROW},
    {"call", ABI_TERMINAL_CALL},
    {"operator-call", ABI_TERMINAL_CALL},
    {"index", ABI_TERMINAL_INDEX}
  };
  for(const Entry & entry : entries)
    if(word == entry.word) return entry.kind;
  throw std::logic_error("unknown ABI terminal '" + word + "'");
}

const char * abi_terminal_code(AbiTerminalKind kind, bool member,
                               std::size_t parameter_count)
{
  static const char * codes[] = {
    nullptr, "C1", "C2", "D0", "D1", "D2", nullptr,
    nullptr, nullptr, "ps", "pl", "ng", "mi", "ad", "de",
    "nw", "na", "dl", "da", "ml", "dv", "rm", "an", "or",
    "eo", "aS", "pL", "mI", "mL", "dV", "rM", "aN", "oR",
    "eO", "ls", "rs", "lS", "rS", "eq", "ne", "lt", "gt",
    "le", "ge", "nt", "aa", "oo", "pp", "mm", "cm", "pm",
    "pt", "cl", "ix"
  };
  static_assert(sizeof(codes) / sizeof(codes[0]) == ABI_TERMINAL_INDEX + 1,
                "ABI terminal code table is incomplete");
  if(kind == ABI_TERMINAL_PLUS)
    return (member ? parameter_count == 0 : parameter_count == 1) ? "ps" : "pl";
  if(kind == ABI_TERMINAL_MINUS)
    return (member ? parameter_count == 0 : parameter_count == 1) ? "ng" : "mi";
  if(kind > ABI_TERMINAL_INDEX || codes[kind] == nullptr)
    throw std::logic_error("ABI terminal has no fixed encoding");
  return codes[kind];
}

size_t make_semantic_substitution(AbiSemanticSubstitutionKind kind,
                                  size_t identity)
{
  const size_t kind_count = 3;
  const size_t kind_value = static_cast<size_t>(kind);
  if(kind_value >= kind_count ||
     identity > (std::numeric_limits<size_t>::max() - kind_value - 1) /
       kind_count)
    throw std::logic_error("semantic ABI substitution identity is invalid");
  return identity * kind_count + kind_value;
}

bool AbiFunctionTarget::has_resolved_source_name() const
{
  return resolved_context == ABI_NO_RESOLVED_REFERENCE &&
    resolved_context_identity != ABI_NO_RESOLVED_REFERENCE;
}

void AbiFunctionTarget::set_resolved_source_name(size_t name)
{
  resolved_context_identity = name;
}

size_t AbiFunctionTarget::resolved_source_name() const
{
  return resolved_context_identity;
}

}  // namespace abi_mangle
