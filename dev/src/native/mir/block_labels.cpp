#include "native/mir/block_labels.h"

#include "native/mir/construction.h"

#include <algorithm>

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

void sort_blocks_by_presentation_order(
    std::vector<lowir_model::BlockId> * blocks,
    const std::vector<std::uint32_t> & presentation_order)
{
  std::sort(blocks->begin(), blocks->end(),
    [&presentation_order](lowir_model::BlockId left,
                          lowir_model::BlockId right) {
      const std::uint32_t left_id = left;
      const std::uint32_t right_id = right;
      const std::uint32_t left_rank = left_id < presentation_order.size() ?
        presentation_order[left_id] : left_id;
      const std::uint32_t right_rank = right_id < presentation_order.size() ?
        presentation_order[right_id] : right_id;
      return left_rank < right_rank;
    });
}

}  // namespace lowir_native
