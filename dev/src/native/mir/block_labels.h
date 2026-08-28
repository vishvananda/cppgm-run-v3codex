#pragma once

#include "lowir/model/program.h"
#include "native/mir/model.h"

namespace lowir_native {

mir_model::MirOperand native_block_operand(
    const lowir_model::Function & function,
    const lowir_model::Operand & block);
void sort_blocks_by_presentation_order(
    std::vector<lowir_model::BlockId> * blocks,
    const std::vector<std::uint32_t> & presentation_order);

}  // namespace lowir_native
