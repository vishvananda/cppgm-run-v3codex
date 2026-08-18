#include "lowir_native_block_labels.h"

#include "lowir_native_mir.h"

namespace lowir_native {

mir_model::MirOperand native_block_operand(
    const lowir_model::Function & function,
    const lowir_model::Operand & block)
{
  return build::named_operand(
    mir_model::MirOperand::OP_LABEL,
    lowir_model::lowir_block_label(function, block.block));
}

}  // namespace lowir_native
