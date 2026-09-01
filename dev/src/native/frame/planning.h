#pragma once

#include "native/analysis/function.h"
#include "native/errors.h"
#include "native/frame/layout.h"
#include "native/eh/host_regions.h"
#include "native/lowering/varargs.h"

#include <cstddef>

namespace lowir_native {
namespace frame_planning_detail {

template <class Derived>
class FramePlanning
{
protected:
  void plan_variadic_register_save()
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(!lowerer.facts_.has_va_start) return;
    if(lowerer.source_.boundary.arity != lowir_model::CAM_VARIADIC)
      native_errors::ThrowLowirInput("va_start in non-variadic function");
    lowerer.variadic_state_ = abi::variadic_state(lowerer.source_.params);
    lowerer.variadic_register_save_offset_ = lowerer.allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::FPN_VA_REGISTER_SAVE,
      varargs::register_save_type());
    varargs::append_register_save(
      lowerer.variadic_register_save_offset_, lowerer.parameter_moves_);
    lowerer.uses_scalar_float_ = true;
  }

  void plan_slots()
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    for(std::size_t i = 0; i < lowerer.source_.slots.size(); ++i) {
      const lowir_model::SlotId slot = lowerer.source_.slots[i];
      const lowir_model::LowType & type =
        lowir_model::lowir_slot_type(lowerer.source_, slot);
      if(lowerer.storage_facts_.parameter_slot_aliases[slot].valid() ||
         lowerer.storage_facts_.promoted_parameter_slots[slot].valid())
        continue;
      if(lowerer.storage_facts_.forwarded_parameter_slots[slot].valid() ||
         lowerer.storage_facts_.dead_store_slots[slot]) {
        lowerer.discarded_slots_[slot] = 1;
        continue;
      }
      lowerer.slot_offsets_[slot] = lowerer.allocate_frame_binding(
        mir_model::MirFrameBinding::FB_SLOT,
        lowerer.source_.slot_names[slot].valid() ?
          lowir_model::PresentationName::pooled(
            lowerer.source_.slot_names[slot]) :
          lowir_model::PresentationName(), type);
      lowerer.slot_offset_known_[slot] = 1;
    }
  }

  void plan_host_eh()
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(!host_eh_detail::requires_host_eh_storage(lowerer.source_)) return;
    lowerer.target_.host_eh_enabled = true;
    lowerer.target_.host_eh_exception_offset = lowerer.allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::FPN_HOST_EH_EXCEPTION,
      lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
    lowerer.target_.host_eh_selector_offset = lowerer.allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::FPN_HOST_EH_SELECTOR,
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
  }
};

}  // namespace frame_planning_detail
}  // namespace lowir_native
