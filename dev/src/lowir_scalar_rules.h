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

}
