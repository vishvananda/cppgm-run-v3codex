#include "native/frame/layout.h"

#include <algorithm>

namespace lowir_native {
namespace frame_layout {

void finalize_function(
    mir_model::MirFunction & target,
    const lowir_model::LowirFunction & source,
    const analysis::FunctionFacts & function_facts,
    const analysis::StorageFacts & storage_facts,
    std::size_t frame_bytes, bool uses_scalar_float,
    bool constrained_wide_pressure)
{
  target.has_dynamic_stack = function_facts.has_dynamic_stack;
  const bool needs_call_scratch = uses_scalar_float ||
    (source.metadata.keep_internal_alias && !function_facts.calls.empty());
  target.scratch_bytes = needs_call_scratch ? 48 : 0;
  const std::size_t direct_parameter_bytes =
    !storage_facts.has_promoted_parameter_slots &&
    !function_facts.has_direct_branch_parameter ?
      abi::direct_parameter_bytes(source.params) : 0;
  if(needs_call_scratch) {
    const bool has_zero_index_parameter = std::find_if(
      function_facts.value_flags.begin(), function_facts.value_flags.end(),
      [](unsigned flags) {
        return (flags & analysis::FunctionFacts::VF_ZERO_INDEX_PARAMETER) != 0;
      }) != function_facts.value_flags.end();
    const std::size_t float_frame_bytes =
      source.params.empty() || has_zero_index_parameter ?
        frame_bytes : std::max<std::size_t>(frame_bytes, 16);
    target.stack_frame_bytes = float_frame_bytes + target.scratch_bytes;
  } else {
    target.stack_frame_bytes = frame_bytes;
    target.stack_floor_bytes = direct_parameter_bytes;
  }
  if(constrained_wide_pressure) {
    target.stack_frame_bytes += 16;
    target.stack_floor_bytes += 16;
  }
  target.stack_size = selection::align_up(
    std::max(target.stack_floor_bytes,
             target.stack_frame_bytes + target.callee_saved_regs.size() * 8),
    16);
}

}  // namespace frame_layout
}  // namespace lowir_native
