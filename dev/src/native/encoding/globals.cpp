#include "native/encoding/globals.h"

#include "native/driver/stats.h"
#include "native/errors.h"
#include "native/analysis/data_layout.h"

#include <cstdint>
#include <string>

namespace lowir_native {
namespace global_encoding {
namespace {

using elf_detail::CodeBuffer;
using data_layout::global_alignment;
using data_layout::type_size;

void emit_integer_data(CodeBuffer & out, long long value,
                       std::uint64_t high, std::size_t size)
{
  if(size <= 8) {
    out.little(static_cast<std::uint64_t>(value), static_cast<unsigned>(size));
    return;
  }
  if(size != 16)
    native_errors::ThrowSource("unsupported wide integer data size");
  out.little(static_cast<std::uint64_t>(value), 8);
  out.little(high, 8);
}

void emit_float_data(CodeBuffer & out, std::uint64_t low,
                     std::uint64_t high, const lowir_model::LowType & type)
{
  if(type.kind == lowir_model::LTK_F80) {
    out.little(low, 8);
    out.little(high, 8);
    return;
  }
  out.little(low, static_cast<unsigned>(type_size(type)));
}

}  // namespace

void emit_global(CodeBuffer & out,
                 const mir_model::MirGlobalDefinition & global)
{
  out.align(global_alignment(global));
  out.label(global.symbol);
  if(global.object_symbol.valid()) out.label_object(global.object_symbol);
  if(global.thread_local_storage && global.thread_local_wrapper_symbol.valid())
    out.label(global.thread_local_wrapper_symbol);
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR) {
    const std::size_t size = type_size(global.type);
    if(global.init_kind == mir_model::MirGlobalDefinition::GI_ADDR)
      out.absolute64(global.init_symbol, global.addr_addend);
    else if(global.init_kind == mir_model::MirGlobalDefinition::GI_FLOAT)
      emit_float_data(
        out, global.literal_low, global.literal_high, global.type);
    else
      emit_integer_data(out, global.int_value, global.literal_high, size);
    return;
  }
  for(std::size_t i = 0; i < global.data_items.size(); ++i) {
    const mir_model::MirGlobalDefinition::DataItem & item = global.data_items[i];
    if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO) {
      out.zeros(item.zero_bytes);
      continue;
    }
    const std::size_t size = type_size(item.type);
    out.align(size);
    if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_ADDR)
      out.absolute64(item.symbol, item.addr_addend);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_INTEGER)
      emit_integer_data(out, item.int_value, item.literal_high, size);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT)
      emit_float_data(out, item.literal_low, item.literal_high, item.type);
    else
      native_errors::ThrowSource("unsupported native global data item");
  }
}

}  // namespace global_encoding
}  // namespace lowir_native
