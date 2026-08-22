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

// A readonly scalar global with a literal initializer always observes that
// literal, so a typed direct load of it is the constant.
bool fold_readonly_global_loads(lowir_model::Function * function,
    const std::vector<unsigned char> & readonly_known,
    const std::vector<lowir_model::Operand> & readonly_constants,
    const std::vector<lowir_model::LowType> & readonly_types,
    Stats * stats);

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
