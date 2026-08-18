#pragma once

#include "lowir_native_abi.h"
#include "lowir_native_selection.h"

namespace lowir_native {
namespace frame_layout {

inline long long append_binding(
    mir_model::MirFunction & function, std::size_t & frame_bytes,
    mir_model::MirFrameBinding::Kind kind, lowir_model::StringId name,
    const lowir_model::LowType & type)
{
  frame_bytes = selection::align_up(frame_bytes, type.alignment);
  frame_bytes += abi::frame_storage_size(type);
  mir_model::MirFrameBinding binding;
  binding.kind = kind;
  binding.name = name;
  binding.offset = -static_cast<long long>(frame_bytes);
  binding.type = type;
  function.frame_bindings.push_back(binding);
  return binding.offset;
}

}  // namespace frame_layout
}  // namespace lowir_native
