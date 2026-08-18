#include "lowir_native_global_encoding.h"

#include "lowir_native.h"
#include "lowir_native_data_layout.h"
#include "lowir_native_float_bits.h"

#include <cstdint>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace lowir_native {
namespace global_encoding {
namespace {

using elf_detail::CodeBuffer;
using data_layout::global_alignment;
using data_layout::type_size;
using float_bits::extended;
using float_bits::scalar;

std::string native_object_symbol(
    const CodeBuffer & out, lowir_model::StringId symbol)
{
  if(!symbol.valid()) return std::string();
  const std::string & spelling = out.literal_spelling(symbol);
  return spelling.empty() || spelling[0] == '@' ? spelling : "@" + spelling;
}

void emit_integer_data(CodeBuffer & out, long long value, std::size_t size,
                       lowir_model::StringId literal)
{
  if(size <= 8) {
    out.little(static_cast<std::uint64_t>(value), static_cast<unsigned>(size));
    return;
  }
  if(size != 16) throw std::logic_error("unsupported wide integer data size");
  const std::string text = literal.valid() ? out.literal_spelling(literal) :
    std::to_string(value);
  std::uint64_t low = 0;
  std::uint64_t high = 0;
	const std::chrono::steady_clock::time_point started = out.collects_stats() ?
		std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
  parse_wide_literal_words(text, &low, &high);
	if(out.collects_stats()) out.note_literal_text_parse(
		static_cast<std::uint64_t>(std::chrono::duration_cast<
			std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count()));
  out.little(low, 8);
  out.little(high, 8);
}

void emit_float_data(CodeBuffer & out, lowir_model::StringId literal,
                     const lowir_model::LowType & type)
{
  const std::string & text = out.literal_spelling(literal);
	const std::chrono::steady_clock::time_point started = out.collects_stats() ?
		std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
  if(type.kind == lowir_model::LTK_F80) {
    const std::pair<std::uint64_t, std::uint64_t> words = extended(text);
		if(out.collects_stats()) out.note_literal_text_parse(
			static_cast<std::uint64_t>(std::chrono::duration_cast<
				std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count()));
    out.little(words.first, 8);
    out.little(words.second, 8);
    return;
  }
	const std::uint64_t bits = scalar(text, type);
	if(out.collects_stats()) out.note_literal_text_parse(
		static_cast<std::uint64_t>(std::chrono::duration_cast<
			std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count()));
  out.little(bits, static_cast<unsigned>(type_size(type)));
}

}  // namespace

void emit_global(CodeBuffer & out,
                 const mir_model::MirGlobalDefinition & global)
{
  out.align(global_alignment(global));
  const std::string & global_name = out.symbol_name(global.symbol);
  out.label(global.symbol);
  const std::string object_symbol =
    native_object_symbol(out, global.object_symbol);
  if(!object_symbol.empty() && object_symbol != global_name)
    out.label(object_symbol);
  if(global.thread_local_storage && global.thread_local_wrapper_symbol.valid())
    out.label(global.thread_local_wrapper_symbol);
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR) {
    const std::size_t size = type_size(global.type);
    if(global.init_kind == mir_model::MirGlobalDefinition::GI_ADDR)
      out.absolute64(global.init_symbol, global.addr_addend);
    else if(global.init_kind == mir_model::MirGlobalDefinition::GI_FLOAT)
      emit_float_data(out, global.literal, global.type);
    else
      emit_integer_data(out, global.int_value, size, global.literal);
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
      emit_integer_data(out, item.int_value, size, item.literal);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT)
      emit_float_data(out, item.literal, item.type);
    else
      throw std::logic_error("unsupported native global data item");
  }
}

}  // namespace global_encoding
}  // namespace lowir_native
