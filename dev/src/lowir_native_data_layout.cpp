#include "lowir_native_data_layout.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lowir_native {
namespace data_layout {

std::size_t type_size(const std::string & type)
{
  if(type == "i1" || type == "i8" || type == "u8") return 1;
  if(type == "i16" || type == "u16") return 2;
  if(type == "i32" || type == "u32" || type == "f32") return 4;
  if(type == "i64" || type == "f64" || type == "ptr") return 8;
  if(type == "i128" || type == "f80") return 16;
  throw std::logic_error("unsupported native data type: " + type);
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
      throw std::logic_error("native zero-only global size overflows");
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
