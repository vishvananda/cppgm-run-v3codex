#pragma once

#include "native/lowering/lowir_native.h"
#include "native/mir/construction.h"
#include "native/allocation/registers.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lowir_native {
namespace object_elf_detail {

struct EncodedFixup
{
  enum Kind { EF_RELATIVE32, EF_ABSOLUTE64, EF_ADDRESS32, EF_TLS_OFFSET32 }
    kind = EF_RELATIVE32;
  mir_model::MirOperand::AddressBinding address_binding =
    mir_model::MirOperand::ADDRESS_LOCAL;
  std::size_t offset = 0;
  std::string target;
  long long addend = 0;
};

struct EncodedSymbolFixup
{
  EncodedFixup::Kind kind = EncodedFixup::EF_RELATIVE32;
  mir_model::MirOperand::AddressBinding address_binding =
    mir_model::MirOperand::ADDRESS_LOCAL;
  std::size_t offset = 0;
  lowir_model::SymbolId target;
  long long addend = 0;
};

struct EncodedSymbolLabel
{
  lowir_model::SymbolId symbol;
  std::size_t offset = 0;
};

struct EncodedObjectLabel
{
  lowir_model::StringId symbol;
  std::size_t offset = 0;
};

enum SectionKind : std::uint8_t
{
  SK_NONE,
  SK_TEXT,
  SK_TEXT_COMDAT,
  SK_DATA,
  SK_TDATA,
  SK_CUSTOM,
  SK_INIT_ARRAY,
  SK_FINI_ARRAY,
  SK_GCC_EXCEPT_TABLE,
  SK_EH_FRAME,
  SK_NOTE_GNU_STACK,
  SK_SYMTAB,
  SK_STRTAB,
  SK_SHSTRTAB,
  SK_GROUP
};

struct SectionIdentity
{
  SectionKind kind = SK_NONE;
  bool relocation = false;
  lowir_model::StringId spelling;

  SectionIdentity() {}
  explicit SectionIdentity(SectionKind section_kind)
    : kind(section_kind) {}
  SectionIdentity(SectionKind section_kind,
                  lowir_model::StringId section_spelling)
    : kind(section_kind), spelling(section_spelling) {}

  SectionIdentity relocation_identity() const
  {
    SectionIdentity result = *this;
    result.relocation = true;
    return result;
  }
};

static_assert(sizeof(SectionIdentity) <= 8,
  "ELF section identity must remain compact");

struct EncodedEhTypeRefLabel
{
  lowir_model::SymbolId symbol;
  std::size_t offset = 0;
};

struct EncodedSection
{
  SectionIdentity name;
  lowir_model::StringId comdat_signature;
  std::uint64_t flags = 0;
  std::size_t alignment = 1;
  std::vector<unsigned char> bytes;
  std::unordered_map<std::string, std::size_t> labels;
  std::vector<EncodedSymbolLabel> symbol_labels;
  std::vector<EncodedObjectLabel> object_labels;
  std::vector<EncodedEhTypeRefLabel> eh_type_ref_labels;
  bool has_eh_personality_ref_label = false;
  std::size_t eh_personality_ref_label_offset = 0;
  std::vector<EncodedFixup> fixups;
  std::vector<EncodedSymbolFixup> symbol_fixups;
};

struct HostFunctionLayout
{
  struct UnwindRange
  {
    std::size_t start = 0;
    std::size_t length = 0;
  };

  struct CallSite
  {
    std::size_t start = 0;
    std::size_t length = 0;
    lowir_model::LocalLabelId landing_label;
    std::size_t landing_pad_offset = 0;
    lowir_model::BlockId action_block;
  };

  lowir_model::SymbolId program_symbol;
  lowir_model::StringId object_symbol;
  std::size_t text_section = 0;
  std::size_t offset = 0;
  std::size_t size = 0;
  bool omit_frame_pointer = false;
  std::size_t stack_adjustment = 0;
  std::vector<X64Register> callee_saved_regs;
  std::vector<std::vector<mir_model::MirHostEhClause> > clauses;
  std::vector<lowir_model::BlockId> clause_order;
  std::vector<CallSite> call_sites;
  std::vector<UnwindRange> unprotected_unwind_ranges;
  std::size_t lsda_offset = 0;
};

struct DeclarationObjectSymbol
{
  lowir_model::StringId identity;
  const std::string * spelling = 0;
};

struct DeclarationObjectSymbols
{
  std::vector<DeclarationObjectSymbol> typed;

  const DeclarationObjectSymbol * find(lowir_model::SymbolId symbol) const
  {
    const std::uint32_t index = symbol;
    return symbol.valid() && index < typed.size() && typed[index].spelling ?
      &typed[index] : 0;
  }
};

std::string host_symbol_spelling(const std::string & raw);
DeclarationObjectSymbols declaration_object_symbols(
  const lowir_model::LowirProgram & program);
std::vector<unsigned char> host_external_global_definitions(
  const lowir_model::LowirProgram & source,
  const mir_model::MirProgram & program);

std::vector<unsigned char> make_linux_relocatable_image(
  const lowir_model::LowirProgram & program,
  DeclarationObjectSymbols declarations,
  EncodedSection text,
  std::vector<EncodedSection> data_sections,
  std::vector<HostFunctionLayout> & functions,
  std::size_t & relocation_count,
  Stats * stats = 0);

}  // namespace object_elf_detail
}  // namespace lowir_native
