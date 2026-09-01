#include "native/frame/home_planning.h"

#include "native/errors.h"
#include "native/lowering/abi.h"
#include "native/frame/layout.h"
#include "native/mir/construction.h"

#include <algorithm>
#include <limits>

namespace lowir_native {
namespace frame_home_planning {

long long allocate_binding(
    mir_model::MirFunction & target,
    std::size_t & frame_bytes,
    mir_model::MirFrameBinding::Kind kind,
    lowir_model::PresentationName name,
    const lowir_model::LowType & type)
{
  return frame_layout::append_binding(
    target, frame_bytes, kind, name, type);
}

std::uint32_t append_binding(
    mir_model::MirFunction & target,
    mir_model::MirFrameBinding::Kind kind,
    lowir_model::PresentationName name,
    const lowir_model::LowType & type,
    long long offset)
{
  if(target.frame_bindings.size() >=
     std::numeric_limits<std::uint32_t>::max())
    native_errors::ThrowResourceLimit("too many native frame bindings");
  mir_model::MirFrameBinding binding;
  binding.kind = kind;
  binding.name = name;
  binding.offset = offset;
  binding.type = type;
  target.frame_bindings.push_back(binding);
  return static_cast<std::uint32_t>(target.frame_bindings.size());
}

namespace {

TemporaryHomeReason infer_reason(
    const analysis::FunctionFacts & facts,
    lowir_model::ValueId value,
    const lowir_model::LowType & type,
    bool crosses_call)
{
  if(type.kind == lowir_model::LTK_OBJECT) return THR_OBJECT_VALUE;
  if(type.kind == lowir_model::LTK_I128 || type.kind == lowir_model::LTK_F80)
    return THR_EXTENDED_REPRESENTATION;
  if(facts.has(value, analysis::FunctionFacts::VF_EDGE_LIVE))
    return THR_EDGE_LIVE;
  if(crosses_call) return THR_LIVE_ACROSS_CALL;
  return THR_SCALAR_VALUE;
}

}  // namespace

mir_model::MirOperand allocate_temporary(
    mir_model::MirFunction & target,
    std::size_t & frame_bytes,
    spill_slots::Pool & spill_slots,
    location_planning::GeneratedFrameNames & generated_names,
    const analysis::FunctionFacts & facts,
    lowir_model::ValueId value,
    const lowir_model::LowType & type,
    std::size_t position,
    TemporaryHomeReason reason,
    bool crosses_call,
    Stats * stats)
{
  if(reason == THR_COUNT)
    reason = infer_reason(facts, value, type, crosses_call);
  if(stats) ++stats->temporary_home_requests_by_reason[reason];

  const std::size_t size = abi::frame_storage_size(type);
  std::size_t available_after =
    facts.last_use[value] == analysis::FunctionFacts::missing_position() ?
      position : facts.last_use[value];
  if(facts.shared_storage_last_use[value] !=
     analysis::FunctionFacts::missing_position())
    available_after = std::max(
      available_after, facts.shared_storage_last_use[value]);

  const lowir_model::PresentationName name = generated_names.name(value);
  const bool reusable = type.kind != lowir_model::LTK_OBJECT;
  long long offset = 0;
  if(reusable && spill_slots.acquire(
       size, type.alignment, position, available_after, &offset)) {
    if(stats) {
      ++stats->temporary_frame_homes_reused;
      ++stats->temporary_home_reuses_by_reason[reason];
    }
    const std::uint32_t binding = append_binding(
      target, mir_model::MirFrameBinding::FB_TEMP, name, type, offset);
    return build::frame_operand(offset, binding);
  }

  if(stats) {
    ++stats->temporary_frame_homes_created;
    ++stats->temporary_home_creations_by_reason[reason];
  }
  offset = allocate_binding(
    target, frame_bytes, mir_model::MirFrameBinding::FB_TEMP, name, type);
  if(reusable)
    spill_slots.remember(size, type.alignment, available_after, offset);
  return build::frame_operand(
    offset, static_cast<std::uint32_t>(target.frame_bindings.size()));
}

}  // namespace frame_home_planning
}  // namespace lowir_native
