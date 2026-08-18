#pragma once

#include "lowir_model.h"
#include "mir_model.h"

namespace lowir_native {

mir_model::MirOperand native_block_operand(
    const lowir_model::Function & function,
    const lowir_model::Operand & block);

}  // namespace lowir_native
