#pragma once

#include "native/object/elf_format.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native {
namespace object_elf_detail {

class ElfStringTableBuilder
{
public:
  explicit ElfStringTableBuilder(
    const lowir_model::StringPool & spellings,
    std::size_t program_symbol_count);

  std::uint32_t InternSection(const SectionIdentity & identity);
  std::uint32_t InternSymbol(const std::string & name,
    bool internal_program_symbol, lowir_model::SymbolId program_symbol,
    lowir_model::StringId object_symbol,
    lowir_model::SymbolId eh_type_ref_symbol, bool eh_personality_ref);

  const std::vector<unsigned char> & bytes() const { return bytes_; }
  std::vector<unsigned char> ReleaseBytes();
  std::size_t entries() const { return entries_; }
  std::size_t section_requests() const { return section_requests_; }
  std::size_t section_reuses() const { return section_reuses_; }
  std::size_t symbol_requests() const { return symbol_requests_; }
  std::size_t symbol_reuses() const { return symbol_reuses_; }
  std::size_t suffix_aliases() const { return suffix_aliases_; }

private:
  static const std::size_t kKindCount = SK_GROUP + 1;

  const lowir_model::StringPool & spellings_;
  std::vector<unsigned char> bytes_;
  std::uint32_t fixed_offsets_[kKindCount][2];
  std::vector<std::uint32_t> comdat_offsets_[2];
  std::vector<std::uint32_t> custom_offsets_[2];
  std::vector<std::uint32_t> object_offsets_;
  std::vector<std::uint32_t> program_offsets_[2];
  std::vector<std::uint32_t> eh_type_ref_offsets_;
  std::uint32_t eh_personality_ref_offset_;
  std::unordered_map<std::string, std::uint32_t> plain_offsets_;
  std::unordered_map<std::string, std::uint32_t> internal_offsets_;
  std::size_t entries_;
  std::size_t section_requests_;
  std::size_t section_reuses_;
  std::size_t symbol_requests_;
  std::size_t symbol_reuses_;
  std::size_t suffix_aliases_;

  std::uint32_t & SectionOffset(const SectionIdentity & identity);
  std::uint32_t * TypedSymbolOffset(bool internal_program_symbol,
    lowir_model::SymbolId program_symbol,
    lowir_model::StringId object_symbol,
    lowir_model::SymbolId eh_type_ref_symbol, bool eh_personality_ref);
  void RegisterObjectSuffix(lowir_model::StringId spelling,
    std::uint32_t offset, bool suffix);
  std::uint32_t CurrentOffset() const;
  void Append(const char * text);
  void Append(const std::string & text);
};

}  // namespace object_elf_detail
}  // namespace lowir_native
