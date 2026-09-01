#include "native/analysis/data_layout.h"
#include "native/errors.h"

#include <algorithm>
#include <limits>

namespace lowir_native {
namespace data_layout {

std::size_t type_size(const lowir_model::LowType & type)
{
  if(type.kind == lowir_model::LTK_INVALID ||
     type.kind == lowir_model::LTK_VOID ||
     type.kind == lowir_model::LTK_OBJECT)
    native_errors::ThrowInternal("unsupported native data type");
  return type.storage_size;
}

unsigned type_width(const lowir_model::LowType & type)
{
  if(type.kind == lowir_model::LTK_INVALID ||
     type.kind == lowir_model::LTK_VOID ||
     type.kind == lowir_model::LTK_OBJECT ||
     type.kind == lowir_model::LTK_I128)
    native_errors::ThrowInternal("unsupported native scalar type");
  return type.kind == lowir_model::LTK_I1 ? 8 :
    static_cast<unsigned>(lowir_model::lowir_type_bit_width(type));
}

std::size_t global_alignment(const mir_model::MirGlobalDefinition & global)
{
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR)
    return type_size(global.type);

  std::size_t alignment = 1;
  std::size_t zero_bytes = 0;
  bool has_typed_item = false;
  for(std::size_t i = 0; i < global.data_items.size(); ++i) {
    const mir_model::MirGlobalDefinition::DataItem & item = global.data_items[i];
    if(item.kind != mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO) {
      has_typed_item = true;
      alignment = std::max(alignment, type_size(item.type));
      continue;
    }
    if(zero_bytes > std::numeric_limits<std::size_t>::max() - item.zero_bytes)
      native_errors::ThrowResourceLimit(
        "native zero-only global size overflows");
    zero_bytes += item.zero_bytes;
  }

  // Zero runs are padding in mixed data.  Wholly zero-filled storage has no
  // typed item to carry alignment, so use the natural power-of-two divisor of
  // its total size, capped at the x86-64 aggregate alignment.
  if(!has_typed_item && zero_bytes != 0) {
    alignment = 16;
    while(alignment > 1 && zero_bytes % alignment != 0) alignment /= 2;
  }
  return alignment;
}

}  // namespace data_layout
}  // namespace lowir_native
