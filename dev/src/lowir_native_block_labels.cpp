#include "lowir_native_block_labels.h"

#include "lowir_native_mir.h"

namespace lowir_native {

mir_model::MirOperand native_block_operand(
    const lowir_model::Function & function,
    const lowir_model::Operand & block)
{
  (void)function;
  mir_model::MirOperand result;
  result.kind = mir_model::MirOperand::OP_LABEL;
  result.block = block.block;
  return result;
}

}  // namespace lowir_native
