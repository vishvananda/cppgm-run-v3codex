#include "native/object/string_table.h"
#include "native/errors.h"

#include <limits>
#include <utility>

namespace lowir_native {
namespace object_elf_detail {
namespace {

const std::uint32_t kMissingOffset =
  std::numeric_limits<std::uint32_t>::max();

const char * fixed_section_name(SectionKind kind)
{
  switch(kind) {
  case SK_TEXT: return ".text";
  case SK_DATA: return ".data";
  case SK_TDATA: return ".tdata";
  case SK_INIT_ARRAY: return ".init_array";
  case SK_FINI_ARRAY: return ".fini_array";
  case SK_GCC_EXCEPT_TABLE: return ".gcc_except_table";
  case SK_EH_FRAME: return ".eh_frame";
  case SK_NOTE_GNU_STACK: return ".note.GNU-stack";
  case SK_SYMTAB: return ".symtab";
  case SK_STRTAB: return ".strtab";
  case SK_SHSTRTAB: return ".shstrtab";
  case SK_GROUP: return ".group";
  default: return 0;
  }
}

}  // namespace

ElfStringTableBuilder::ElfStringTableBuilder(
    const lowir_model::StringPool & spellings,
    std::size_t program_symbol_count)
  : spellings_(spellings), bytes_(1, 0),
    eh_personality_ref_offset_(kMissingOffset), entries_(0),
    section_requests_(0), section_reuses_(0), symbol_requests_(0),
    symbol_reuses_(0), suffix_aliases_(0)
{
  for(std::size_t kind = 0; kind < kKindCount; ++kind)
    for(unsigned relocation = 0; relocation < 2; ++relocation)
      fixed_offsets_[kind][relocation] = kMissingOffset;
  for(unsigned relocation = 0; relocation < 2; ++relocation) {
    comdat_offsets_[relocation].assign(
      spellings_.size() + 1, kMissingOffset);
    custom_offsets_[relocation].assign(
      spellings_.size() + 1, kMissingOffset);
  }
  object_offsets_.assign(spellings_.size() + 1, kMissingOffset);
  for(unsigned internal = 0; internal < 2; ++internal)
    program_offsets_[internal].assign(program_symbol_count, kMissingOffset);
  eh_type_ref_offsets_.assign(program_symbol_count, kMissingOffset);
}

void ElfStringTableBuilder::Append(const char * text)
{
  while(*text) bytes_.push_back(static_cast<unsigned char>(*text++));
}

void ElfStringTableBuilder::Append(const std::string & text)
{
  bytes_.insert(bytes_.end(), text.begin(), text.end());
}

std::uint32_t & ElfStringTableBuilder::SectionOffset(
    const SectionIdentity & identity)
{
  if(identity.kind == SK_NONE || identity.kind >= kKindCount)
    native_errors::ThrowInternal("ELF section has no identity");
  const unsigned relocation = identity.relocation ? 1 : 0;
  if(identity.kind != SK_TEXT_COMDAT && identity.kind != SK_CUSTOM)
    return fixed_offsets_[identity.kind][relocation];
  if(!identity.spelling.valid())
    native_errors::ThrowInternal("dynamic ELF section has no spelling identity");
  const std::uint32_t spelling = identity.spelling;
  std::vector<std::uint32_t> & offsets = identity.kind == SK_TEXT_COMDAT ?
    comdat_offsets_[relocation] : custom_offsets_[relocation];
  if(spelling >= offsets.size())
    native_errors::ThrowInternal("ELF section spelling identity is out of range");
  return offsets[spelling];
}

void ElfStringTableBuilder::RegisterObjectSuffix(
    lowir_model::StringId spelling, std::uint32_t offset, bool suffix)
{
  const std::uint32_t identity = spelling;
  if(!spelling.valid() || identity >= object_offsets_.size())
    native_errors::ThrowInternal("ELF string spelling identity is out of range");
  if(object_offsets_[identity] != kMissingOffset) return;
  object_offsets_[identity] = offset;
  if(suffix) ++suffix_aliases_;
}

std::uint32_t ElfStringTableBuilder::InternSection(
    const SectionIdentity & identity)
{
  ++section_requests_;
  std::uint32_t & known = SectionOffset(identity);
  if(known != kMissingOffset) {
    ++section_reuses_;
    return known;
  }

  const std::uint32_t offset = CurrentOffset();
  if(identity.relocation) Append(".rela");
  const std::uint32_t base_offset = CurrentOffset();
  std::uint32_t spelling_offset = kMissingOffset;
  if(identity.kind == SK_TEXT_COMDAT) {
    Append(".text.");
    spelling_offset = CurrentOffset();
    Append(spellings_.get(identity.spelling));
  } else if(identity.kind == SK_CUSTOM) {
    spelling_offset = CurrentOffset();
    Append(spellings_.get(identity.spelling));
  } else {
    const char * name = fixed_section_name(identity.kind);
    if(!name) native_errors::ThrowInternal("invalid fixed ELF section identity");
    Append(name);
  }
  bytes_.push_back(0);
  ++entries_;
  known = offset;

  if(identity.relocation) {
    SectionIdentity base = identity;
    base.relocation = false;
    std::uint32_t & base_known = SectionOffset(base);
    if(base_known == kMissingOffset) {
      base_known = base_offset;
      ++suffix_aliases_;
    }
  }
  if(spelling_offset != kMissingOffset)
    RegisterObjectSuffix(identity.spelling, spelling_offset,
      spelling_offset != offset);
  return offset;
}

std::uint32_t ElfStringTableBuilder::InternSymbol(const std::string & name,
    bool internal_program_symbol, lowir_model::SymbolId program_symbol,
    lowir_model::StringId object_symbol,
    lowir_model::SymbolId eh_type_ref_symbol, bool eh_personality_ref)
{
  ++symbol_requests_;
  std::uint32_t * typed_offset = TypedSymbolOffset(internal_program_symbol,
    program_symbol, object_symbol, eh_type_ref_symbol, eh_personality_ref);
  if(typed_offset && *typed_offset != kMissingOffset) {
    ++symbol_reuses_;
    return *typed_offset;
  }

  std::unordered_map<std::string, std::uint32_t> & offsets =
    internal_program_symbol ? internal_offsets_ : plain_offsets_;
  const std::unordered_map<std::string, std::uint32_t>::const_iterator found =
    offsets.find(name);
  if(found != offsets.end()) {
    if(typed_offset) *typed_offset = found->second;
    ++symbol_reuses_;
    return found->second;
  }

  const std::uint32_t offset = CurrentOffset();
  if(internal_program_symbol) bytes_.push_back('@');
  Append(name);
  bytes_.push_back(0);
  ++entries_;
  if(typed_offset) *typed_offset = offset;
  else offsets.insert(std::make_pair(name, offset));
  return offset;
}

std::uint32_t * ElfStringTableBuilder::TypedSymbolOffset(
    bool internal_program_symbol, lowir_model::SymbolId program_symbol,
    lowir_model::StringId object_symbol,
    lowir_model::SymbolId eh_type_ref_symbol, bool eh_personality_ref)
{
  if(!internal_program_symbol && object_symbol.valid()) {
    const std::uint32_t identity = object_symbol;
    if(identity >= object_offsets_.size())
      native_errors::ThrowInternal("ELF object-symbol identity is out of range");
    return &object_offsets_[identity];
  }
  if(program_symbol.valid()) {
    const std::uint32_t identity = program_symbol;
    if(identity >= program_offsets_[0].size())
      native_errors::ThrowInternal("ELF program-symbol identity is out of range");
    return &program_offsets_[internal_program_symbol ? 1 : 0][identity];
  }
  if(eh_type_ref_symbol.valid()) {
    const std::uint32_t identity = eh_type_ref_symbol;
    if(identity >= eh_type_ref_offsets_.size())
      native_errors::ThrowInternal("ELF EH-reference identity is out of range");
    return &eh_type_ref_offsets_[identity];
  }
  if(eh_personality_ref) return &eh_personality_ref_offset_;
  return 0;
}

std::uint32_t ElfStringTableBuilder::CurrentOffset() const
{
  if(bytes_.size() >= kMissingOffset)
    native_errors::ThrowResourceLimit(
      "ELF string table exceeds 32-bit offsets");
  return static_cast<std::uint32_t>(bytes_.size());
}

std::vector<unsigned char> ElfStringTableBuilder::ReleaseBytes()
{
  return std::move(bytes_);
}

}  // namespace object_elf_detail
}  // namespace lowir_native
