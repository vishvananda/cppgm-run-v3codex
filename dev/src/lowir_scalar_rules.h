#pragma once

#include "lowir_model.h"
#include "lowir_opt.h"

#include <vector>

namespace lowir_opt {

// Rewrite an unsigned divide, remainder, or multiply whose right operand is
// a positive power of two into the equivalent shift or mask.  Signed
// division keeps its rounding semantics and is not rewritten.
bool strength_reduce_binary(lowir_model::Instruction * ins,
    const lowir_model::LowType & type);

// The readonly scalar globals of one program with literal initializers,
// indexed densely by symbol.
struct ReadonlyGlobalIndex
{
  std::vector<unsigned char> known;
  std::vector<lowir_model::Operand> constants;
  std::vector<lowir_model::LowType> types;

  ReadonlyGlobalIndex(const lowir_model::LowirProgram & program,
                      bool populate);
};

// Readonly structured byte globals whose first zero byte is known, together
// with the ABI-designated strlen declaration used by ordinary LowIR calls.
struct ReadonlyByteStringIndex
{
  std::vector<unsigned char> known;
  std::vector<std::size_t> lengths;
  lowir_model::SymbolId strlen_symbol;

  explicit ReadonlyByteStringIndex(
    const lowir_model::LowirProgram & program);
};

// A readonly scalar global with a literal initializer always observes that
// literal, so a typed direct load of it is the constant.
bool fold_readonly_global_loads(lowir_model::Function * function,
    const std::vector<unsigned char> & readonly_known,
    const std::vector<lowir_model::Operand> & readonly_constants,
    const std::vector<lowir_model::LowType> & readonly_types,
    Stats * stats);

// Replace strlen of the direct address of a known readonly byte string by
// its first-NUL length.  The proof uses only serialized LowIR facts.
bool fold_readonly_byte_string_lengths(lowir_model::Function * function,
    const ReadonlyByteStringIndex & strings, Stats * stats);

// When a local pointer table is completely initialized with direct addresses
// of known readonly byte strings, replace strlen of a variable-indexed table
// element with an indexed load from a synthesized readonly length table.  The
// local table remains in place for its other users; only the redundant scan is
// removed.
bool fold_readonly_byte_string_table_lengths(
    lowir_model::LowirProgram * program,
    const ReadonlyByteStringIndex & strings, Stats * stats);

bool same_operand(const lowir_model::Operand & a,
                  const lowir_model::Operand & b);
bool is_zero(const lowir_model::Operand & value);
bool is_one(const lowir_model::Operand & value);
bool is_minus_one(const lowir_model::Operand & value);

// A zero-equality test of a comparison result is the inverted comparison.
// Rewrites *ins in place when the inner comparison's operand type permits an
// exact predicate inversion (integers and pointers; floats keep their NaN
// behavior and are not rewritten).  Returns whether *ins changed.
bool rewrite_inverted_compare(lowir_model::Instruction * ins,
    lowir_model::LowOperation::Kind inner_compare,
    lowir_model::LowTypeKind inner_type,
    const lowir_model::Operand & inner_first,
    const lowir_model::Operand & inner_second);

// Replace a load whose address and type match an earlier load in the same
// block, with no possibly-writing instruction between them, by a copy of the
// earlier result.
bool eliminate_duplicate_block_loads(lowir_model::Function * function,
    Stats * stats);

}
