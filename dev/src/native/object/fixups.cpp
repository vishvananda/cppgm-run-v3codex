#include "native/object/fixups.h"
#include "native/errors.h"

#include <climits>
#include <cstdint>
#include <utility>

namespace lowir_native {
namespace object_elf_detail {
namespace {

struct LabelLocation
{
  bool text = false;
  std::size_t section = 0;
  std::size_t offset = 0;
};

typedef std::unordered_map<std::string, LabelLocation> LabelIndex;

void add_labels(LabelIndex & labels, const EncodedSection & source,
                bool text, std::size_t section)
{
  for(std::unordered_map<std::string, std::size_t>::const_iterator label =
        source.labels.begin(); label != source.labels.end(); ++label) {
    LabelLocation location;
    location.text = text;
    location.section = section;
    location.offset = label->second;
    labels.insert(std::make_pair(label->first, location));
  }
}

void add_symbol_labels(
    std::vector<LabelLocation> & locations,
    std::vector<unsigned char> & known,
    const EncodedSection & source, bool text, std::size_t section)
{
  for(std::size_t i = 0; i < source.symbol_labels.size(); ++i) {
    const EncodedSymbolLabel & label = source.symbol_labels[i];
    const std::uint32_t symbol = label.symbol;
    if(!label.symbol.valid() || symbol >= locations.size())
      native_errors::ThrowInternal("invalid encoded symbol label identity");
    // Some TLS wrapper identities are also published at their backing
    // storage address.  Preserve the text-before-data definition.
    if(known[symbol]) continue;
    locations[symbol].text = text;
    locations[symbol].section = section;
    locations[symbol].offset = label.offset;
    known[symbol] = 1;
  }
}

void patch_relative32(EncodedSection & source, const EncodedFixup & fixup,
                      std::size_t target)
{
  if(fixup.offset > source.bytes.size() ||
     4 > source.bytes.size() - fixup.offset)
    native_errors::ThrowInternal("native object relative fixup is out of bounds");
  if(target > static_cast<std::size_t>(LLONG_MAX) ||
     fixup.offset > static_cast<std::size_t>(LLONG_MAX - 4))
    native_errors::ThrowResourceLimit(
      "native object relative fixup exceeds rel32");
  const long long base = static_cast<long long>(target) -
    static_cast<long long>(fixup.offset + 4);
  if((fixup.addend > 0 && base > LLONG_MAX - fixup.addend) ||
     (fixup.addend < 0 && base < LLONG_MIN - fixup.addend))
    native_errors::ThrowResourceLimit(
      "native object relative fixup exceeds rel32");
  const long long delta = base + fixup.addend;
  if(delta < INT32_MIN || delta > INT32_MAX)
    native_errors::ThrowResourceLimit(
      "native object relative fixup exceeds rel32");
  const std::uint32_t encoded = static_cast<std::uint32_t>(delta);
  for(unsigned byte = 0; byte < 4; ++byte)
    source.bytes[fixup.offset + byte] =
      static_cast<unsigned char>(encoded >> (byte * 8));
}

bool is_same_section(const LabelLocation & target, bool source_text,
                     std::size_t source_section)
{
  return target.text == source_text &&
    target.section == source_section;
}

void resolve_section_fixups(
    EncodedSection & source, bool source_text, std::size_t source_section,
    const LabelIndex & labels,
    const std::vector<LabelLocation> & symbol_locations,
    const std::vector<unsigned char> & symbol_location_known,
    const std::vector<unsigned char> & symbol_external)
{
  std::vector<EncodedFixup> unresolved;
  unresolved.reserve(source.fixups.size());
  for(std::size_t i = 0; i < source.fixups.size(); ++i) {
    const EncodedFixup & fixup = source.fixups[i];
    const LabelIndex::const_iterator target = labels.find(fixup.target);
    const bool relative = fixup.kind == EncodedFixup::EF_RELATIVE32 ||
      (fixup.kind == EncodedFixup::EF_ADDRESS32 &&
       fixup.address_binding == mir_model::MirOperand::ADDRESS_LOCAL);
    if(!relative || target == labels.end() ||
       !is_same_section(target->second, source_text, source_section)) {
      unresolved.push_back(fixup);
      continue;
    }
    patch_relative32(source, fixup, target->second.offset);
  }
  source.fixups.swap(unresolved);
  std::vector<EncodedSymbolFixup> unresolved_symbols;
  unresolved_symbols.reserve(source.symbol_fixups.size());
  for(std::size_t i = 0; i < source.symbol_fixups.size(); ++i) {
    const EncodedSymbolFixup & fixup = source.symbol_fixups[i];
    const std::uint32_t symbol = fixup.target;
    if(!fixup.target.valid() || symbol >= symbol_locations.size())
      native_errors::ThrowInternal("invalid encoded symbol fixup identity");
    const bool relative = fixup.kind == EncodedFixup::EF_RELATIVE32 ||
      (fixup.kind == EncodedFixup::EF_ADDRESS32 &&
       fixup.address_binding == mir_model::MirOperand::ADDRESS_LOCAL);
    if(!relative || symbol_external[symbol] || !symbol_location_known[symbol] ||
       !is_same_section(symbol_locations[symbol], source_text, source_section)) {
      unresolved_symbols.push_back(fixup);
      continue;
    }
    EncodedFixup numeric;
    numeric.kind = fixup.kind;
    numeric.address_binding = fixup.address_binding;
    numeric.offset = fixup.offset;
    numeric.addend = fixup.addend;
    patch_relative32(source, numeric, symbol_locations[symbol].offset);
  }
  source.symbol_fixups.swap(unresolved_symbols);
}

}  // namespace

void resolve_same_section_local_fixups(
    std::vector<EncodedSection> & text_sections,
    std::vector<EncodedSection> & data_sections,
    const lowir_model::LowirProgram & program,
    const DeclarationObjectSymbols & declarations)
{
  std::size_t label_count = 0;
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    label_count += text_sections[i].labels.size();
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    label_count += data_sections[i].labels.size();
  LabelIndex labels;
  labels.reserve(label_count);
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    add_labels(labels, text_sections[i], true, i);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    add_labels(labels, data_sections[i], false, i);
  std::vector<LabelLocation> symbol_locations(program.symbol_names.size());
  std::vector<unsigned char> symbol_location_known(
    program.symbol_names.size(), 0);
  std::vector<unsigned char> symbol_external(program.symbol_names.size(), 0);
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    add_symbol_labels(symbol_locations, symbol_location_known,
      text_sections[i], true, i);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    add_symbol_labels(symbol_locations, symbol_location_known,
      data_sections[i], false, i);
  for(std::size_t i = 0; i < declarations.typed.size(); ++i)
    symbol_external[i] = declarations.typed[i].spelling != 0;
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    resolve_section_fixups(
      text_sections[i], true, i, labels, symbol_locations,
      symbol_location_known, symbol_external);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    resolve_section_fixups(
      data_sections[i], false, i, labels, symbol_locations,
      symbol_location_known, symbol_external);
}

}  // namespace object_elf_detail
}  // namespace lowir_native
