#include "native/object/lowir_native_object_elf.h"
#include "native/object/lowir_native_elf_string_table.h"
#include "native/eh/lowir_native_lsda.h"
#include "native/object/lowir_native_object_fixups.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>

namespace lowir_native {
namespace object_elf_detail {

std::string native_object_symbol(const std::string & symbol)
{
  return !symbol.empty() && symbol[0] == '@' ? symbol.substr(1) : symbol;
}

const std::string & exported_internal_symbol(
    const lowir_model::LowirProgram & program,
    const lowir_model::ExportedSymbol & symbol)
{
  return lowir_model::lowir_symbol_name(program, symbol.internal_symbol);
}

const std::string & exported_object_symbol(
    const lowir_model::LowirProgram & program,
    const lowir_model::ExportedSymbol & symbol)
{
  if(!symbol.object_symbol.valid()) {
    static const std::string empty;
    return empty;
  }
  return program.strings.get(symbol.object_symbol);
}

void put_little(std::vector<unsigned char> & out, std::size_t offset,
                std::uint64_t value, unsigned count)
{
  if(offset + count > out.size())
    throw std::logic_error("invalid ELF header field");
  for(unsigned i = 0; i < count; ++i)
    out[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}

struct HostRelocation
{
  enum Kind { HR_ABSOLUTE64, HR_PC32, HR_PLT32, HR_GOTPCRELX, HR_TPOFF32 }
    kind = HR_ABSOLUTE64;
  enum SectionTarget { HST_NONE, HST_TEXT, HST_LSDA }
    section_target = HST_NONE;
  std::size_t offset = 0;
  std::string target;
  lowir_model::SymbolId program_symbol;
  lowir_model::StringId object_symbol;
  lowir_model::SymbolId eh_type_ref_symbol;
  bool eh_personality_ref = false;
  std::size_t text_section = 0;
  std::uint16_t section_symbol = 0;
  long long addend = 0;
};

struct EncodedLabelLocation
{
  bool text = false;
  std::size_t section = 0;
  std::size_t offset = 0;
};

typedef std::unordered_map<std::string, EncodedLabelLocation>
  EncodedLabelIndex;

struct EncodedEhTypeRefLocation
{
  lowir_model::SymbolId symbol;
  EncodedLabelLocation location;
};

struct EncodedLabels
{
  EncodedLabelIndex named;
  std::vector<EncodedLabelLocation> symbols;
  std::vector<unsigned char> symbol_known;
  std::vector<EncodedLabelLocation> objects;
  std::vector<unsigned char> object_known;
  std::vector<EncodedEhTypeRefLocation> eh_type_refs;
  EncodedLabelLocation eh_personality_ref;
  bool eh_personality_ref_known = false;
};

void index_symbol_labels(EncodedLabels & result,
    const EncodedSection & source, bool text, std::size_t section)
{
  for(std::size_t i = 0; i < source.symbol_labels.size(); ++i) {
    const EncodedSymbolLabel & label = source.symbol_labels[i];
    const std::uint32_t symbol = label.symbol;
    if(!label.symbol.valid() || symbol >= result.symbols.size())
      throw std::logic_error("invalid encoded symbol label identity");
    // Some TLS wrapper identities are also published at their backing
    // storage address.  Preserve the historical text-before-data choice.
    if(result.symbol_known[symbol]) continue;
    result.symbols[symbol].text = text;
    result.symbols[symbol].section = section;
    result.symbols[symbol].offset = label.offset;
    result.symbol_known[symbol] = 1;
  }
}

void index_object_labels(EncodedLabels & result,
    const EncodedSection & source, bool text, std::size_t section)
{
  for(std::size_t i = 0; i < source.object_labels.size(); ++i) {
    const EncodedObjectLabel & label = source.object_labels[i];
    const std::uint32_t symbol = label.symbol;
    if(!label.symbol.valid() || symbol >= result.objects.size())
      throw std::logic_error("invalid encoded object-symbol identity");
    if(result.object_known[symbol]) continue;
    result.objects[symbol].text = text;
    result.objects[symbol].section = section;
    result.objects[symbol].offset = label.offset;
    result.object_known[symbol] = 1;
  }
}

void index_eh_type_ref_labels(EncodedLabels & result,
    const EncodedSection & source, bool text, std::size_t section)
{
  for(std::size_t i = 0; i < source.eh_type_ref_labels.size(); ++i) {
    const EncodedEhTypeRefLabel & label = source.eh_type_ref_labels[i];
    const std::uint32_t symbol = label.symbol;
    if(!label.symbol.valid())
      throw std::logic_error("invalid encoded EH type-reference identity");
    bool known = false;
    for(std::size_t prior = 0; prior < result.eh_type_refs.size(); ++prior)
      known = known || result.eh_type_refs[prior].symbol == label.symbol;
    if(known) continue;
    EncodedEhTypeRefLocation location;
    location.symbol = lowir_model::SymbolId(symbol);
    location.location.text = text;
    location.location.section = section;
    location.location.offset = label.offset;
    result.eh_type_refs.push_back(location);
  }
}

const EncodedLabelLocation * find_eh_type_ref_label(
    const EncodedLabels & labels, lowir_model::SymbolId symbol)
{
  for(std::size_t i = 0; i < labels.eh_type_refs.size(); ++i)
    if(labels.eh_type_refs[i].symbol == symbol)
      return &labels.eh_type_refs[i].location;
  return 0;
}

void index_eh_personality_ref_label(EncodedLabels & result,
    const EncodedSection & source, bool text, std::size_t section)
{
  if(!source.has_eh_personality_ref_label ||
     result.eh_personality_ref_known) return;
  result.eh_personality_ref.text = text;
  result.eh_personality_ref.section = section;
  result.eh_personality_ref.offset =
    source.eh_personality_ref_label_offset;
  result.eh_personality_ref_known = true;
}

EncodedLabels index_encoded_labels(
    const lowir_model::LowirProgram & program,
    const std::vector<EncodedSection> & text_sections,
    const std::vector<EncodedSection> & data_sections)
{
  std::size_t label_count = 0;
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    label_count += text_sections[i].labels.size();
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    label_count += data_sections[i].labels.size();
  EncodedLabels result;
  result.named.reserve(label_count);
  result.symbols.resize(program.symbol_names.size());
  result.symbol_known.assign(program.symbol_names.size(), 0);
  result.objects.resize(program.strings.size() + 1);
  result.object_known.assign(program.strings.size() + 1, 0);
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    for(std::unordered_map<std::string, std::size_t>::const_iterator label =
          text_sections[i].labels.begin();
        label != text_sections[i].labels.end(); ++label) {
      EncodedLabelLocation location;
      location.text = true;
      location.section = i;
      location.offset = label->second;
      result.named.insert(std::make_pair(label->first, location));
    }
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    for(std::unordered_map<std::string, std::size_t>::const_iterator label =
          data_sections[i].labels.begin();
        label != data_sections[i].labels.end(); ++label) {
      EncodedLabelLocation location;
      location.section = i;
      location.offset = label->second;
      result.named.insert(std::make_pair(label->first, location));
    }
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    index_symbol_labels(result, text_sections[i], true, i);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    index_symbol_labels(result, data_sections[i], false, i);
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    index_object_labels(result, text_sections[i], true, i);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    index_object_labels(result, data_sections[i], false, i);
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    index_eh_type_ref_labels(result, text_sections[i], true, i);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    index_eh_type_ref_labels(result, data_sections[i], false, i);
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    index_eh_personality_ref_label(result, text_sections[i], true, i);
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    index_eh_personality_ref_label(result, data_sections[i], false, i);
  return result;
}

void publish_object_aliases(
    const lowir_model::LowirProgram & program,
    std::vector<EncodedSection> & text_sections,
    std::vector<EncodedSection> & data_sections,
    EncodedLabels & labels)
{
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    const lowir_model::StringId alias =
      program.object_aliases[i].object_symbol;
    const lowir_model::SymbolId target_id =
      program.object_aliases[i].target_id;
    const std::uint32_t target = target_id;
    if(!target_id.valid() || target >= labels.symbols.size() ||
       !labels.symbol_known[target])
      throw std::runtime_error("native alias has undefined target: " +
        lowir_model::lowir_symbol_name(program, target_id));
    const EncodedLabelLocation location = labels.symbols[target];
    EncodedObjectLabel label;
    label.symbol = alias;
    label.offset = location.offset;
    if(location.text)
      text_sections[location.section].object_labels.push_back(label);
    else data_sections[location.section].object_labels.push_back(label);
    const std::uint32_t object = alias;
    if(!alias.valid() || object >= labels.objects.size())
      throw std::logic_error("invalid object-alias identity");
    labels.objects[object] = location;
    labels.object_known[object] = 1;
  }
}

struct TextSlice
{
  std::size_t old_start = 0;
  std::size_t old_end = 0;
  std::size_t section = 0;
  std::size_t new_start = 0;
};

std::size_t text_slice_at(const std::vector<TextSlice> & slices,
                          std::size_t offset)
{
  const std::vector<TextSlice>::const_iterator after = std::upper_bound(
    slices.begin(), slices.end(), offset,
    [](std::size_t value, const TextSlice & slice) {
      return value < slice.old_start;
    });
  if(after == slices.begin())
    throw std::logic_error("encoded text item precedes every function");
  const std::size_t index = static_cast<std::size_t>(after - slices.begin() - 1);
  if(offset > slices[index].old_end)
    throw std::logic_error("encoded text item is outside a function");
  return index;
}

std::vector<EncodedSection> partition_weak_text(
    const lowir_model::LowirProgram & program,
    EncodedSection source,
    std::vector<HostFunctionLayout> & functions)
{
  std::vector<lowir_model::StringId> weak_symbols(
    program.symbol_names.size());
  std::vector<unsigned char> weak_objects(program.strings.size() + 1, 0);
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i) {
    const lowir_model::ExportedSymbol & symbol = program.exported_symbols[i];
    if(symbol.linkage != ir_model::SL_WEAK || !symbol.object_symbol.valid())
      continue;
    const std::uint32_t object = symbol.object_symbol;
    const std::uint32_t internal = symbol.internal_symbol;
    if(object >= weak_objects.size() ||
       !symbol.internal_symbol.valid() || internal >= weak_symbols.size())
      throw std::logic_error("invalid weak symbol identity");
    weak_objects[object] = 1;
    if(!weak_symbols[internal].valid())
      weak_symbols[internal] = symbol.object_symbol;
  }

  std::vector<std::size_t> order(functions.size());
  for(std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(),
    [&functions](std::size_t left, std::size_t right) {
      return functions[left].offset < functions[right].offset;
    });

  std::vector<EncodedSection> result(1);
  result[0].name = source.name;
  result[0].flags = source.flags;
  result[0].alignment = source.alignment;
  std::vector<TextSlice> slices;
  slices.reserve(functions.size());
  std::vector<unsigned char> emitted_weak_objects(
    program.strings.size() + 1, 0);
  for(std::size_t position = 0; position < order.size(); ++position) {
    HostFunctionLayout & function = functions[order[position]];
    if(function.offset > source.bytes.size() ||
       function.size > source.bytes.size() - function.offset)
      throw std::logic_error("encoded host function is out of bounds");
    if(position && slices.back().old_end > function.offset)
      throw std::logic_error("overlapping encoded host functions");
    lowir_model::StringId signature;
    const std::uint32_t program_symbol = function.program_symbol;
    if(function.program_symbol.valid() &&
       program_symbol < weak_symbols.size())
      signature = weak_symbols[program_symbol];
    if(function.object_symbol.valid()) {
      const std::uint32_t object = function.object_symbol;
      if(object >= weak_objects.size())
        throw std::logic_error("invalid function object symbol identity");
      if(weak_objects[object]) signature = function.object_symbol;
    }

    TextSlice slice;
    slice.old_start = function.offset;
    slice.old_end = function.offset + function.size;
    if(!signature.valid()) {
      EncodedSection & ordinary = result[0];
      while(ordinary.bytes.size() % 2) ordinary.bytes.push_back(0);
      slice.new_start = ordinary.bytes.size();
      slice.section = 0;
      ordinary.bytes.insert(ordinary.bytes.end(),
        source.bytes.begin() + function.offset,
        source.bytes.begin() + function.offset + function.size);
    } else {
      const std::uint32_t object = signature;
      if(object >= emitted_weak_objects.size())
        throw std::logic_error("invalid weak signature identity");
      EncodedSection grouped;
      grouped.name = SectionIdentity(SK_TEXT_COMDAT, signature);
      grouped.comdat_signature = signature;
      grouped.flags = source.flags | 0x200;
      grouped.alignment = 2;
      if(emitted_weak_objects[object])
        throw std::logic_error("duplicate function COMDAT section name");
      emitted_weak_objects[object] = 1;
      grouped.bytes.insert(grouped.bytes.end(),
        source.bytes.begin() + function.offset,
        source.bytes.begin() + function.offset + function.size);
      result.push_back(std::move(grouped));
      slice.section = result.size() - 1;
    }
    function.text_section = slice.section;
    function.offset = slice.new_start;
    slices.push_back(slice);
  }

  for(std::unordered_map<std::string, std::size_t>::const_iterator label =
        source.labels.begin(); label != source.labels.end(); ++label) {
    const TextSlice & slice = slices[text_slice_at(slices, label->second)];
    result[slice.section].labels[label->first] =
      slice.new_start + label->second - slice.old_start;
  }
  for(std::size_t i = 0; i < source.symbol_labels.size(); ++i) {
    const EncodedSymbolLabel & label = source.symbol_labels[i];
    const TextSlice & slice = slices[text_slice_at(slices, label.offset)];
    EncodedSymbolLabel moved = label;
    moved.offset = slice.new_start + label.offset - slice.old_start;
    result[slice.section].symbol_labels.push_back(moved);
  }
  for(std::size_t i = 0; i < source.object_labels.size(); ++i) {
    const EncodedObjectLabel & label = source.object_labels[i];
    const TextSlice & slice = slices[text_slice_at(slices, label.offset)];
    EncodedObjectLabel moved = label;
    moved.offset = slice.new_start + label.offset - slice.old_start;
    result[slice.section].object_labels.push_back(moved);
  }
  for(std::size_t i = 0; i < source.eh_type_ref_labels.size(); ++i) {
    const EncodedEhTypeRefLabel & label = source.eh_type_ref_labels[i];
    const TextSlice & slice = slices[text_slice_at(slices, label.offset)];
    EncodedEhTypeRefLabel moved = label;
    moved.offset = slice.new_start + label.offset - slice.old_start;
    result[slice.section].eh_type_ref_labels.push_back(moved);
  }
  if(source.has_eh_personality_ref_label) {
    const TextSlice & slice = slices[text_slice_at(
      slices, source.eh_personality_ref_label_offset)];
    EncodedSection & target = result[slice.section];
    target.has_eh_personality_ref_label = true;
    target.eh_personality_ref_label_offset = slice.new_start +
      source.eh_personality_ref_label_offset - slice.old_start;
  }
  for(std::size_t i = 0; i < source.fixups.size(); ++i) {
    const TextSlice & slice = slices[text_slice_at(
      slices, source.fixups[i].offset)];
    if(source.fixups[i].offset >= slice.old_end)
      throw std::logic_error("encoded text fixup is outside a function");
    EncodedFixup fixup = source.fixups[i];
    fixup.offset = slice.new_start + fixup.offset - slice.old_start;
    result[slice.section].fixups.push_back(fixup);
  }
  for(std::size_t i = 0; i < source.symbol_fixups.size(); ++i) {
    const TextSlice & slice = slices[text_slice_at(
      slices, source.symbol_fixups[i].offset)];
    if(source.symbol_fixups[i].offset >= slice.old_end)
      throw std::logic_error("encoded text symbol fixup is outside a function");
    EncodedSymbolFixup fixup = source.symbol_fixups[i];
    fixup.offset = slice.new_start + fixup.offset - slice.old_start;
    result[slice.section].symbol_fixups.push_back(fixup);
  }
  return result;
}

struct HostSection
{
  SectionIdentity name;
  std::uint32_t type = 1;
  std::uint64_t flags = 0;
  std::size_t alignment = 1;
  std::size_t entry_size = 0;
  std::uint32_t link = 0;
  std::uint32_t info = 0;
  std::vector<unsigned char> bytes;
  std::size_t file_offset = 0;
  std::size_t name_offset = 0;
};

struct HostSymbol
{
  std::string name;
  bool internal_program_symbol = false;
  bool named_lookup = false;
  lowir_model::SymbolId program_symbol;
  lowir_model::StringId object_symbol;
  lowir_model::SymbolId eh_type_ref_symbol;
  bool eh_personality_ref = false;
  unsigned binding = 0;
  unsigned type = 0;
  std::uint16_t section = 0;
  std::uint64_t value = 0;
  std::uint64_t size = 0;
};

void append_little(std::vector<unsigned char> & out, std::uint64_t value,
                   unsigned count)
{
  for(unsigned i = 0; i < count; ++i)
    out.push_back(static_cast<unsigned char>(value >> (i * 8)));
}

HostSection make_host_lifecycle_array(
    const lowir_model::LowirProgram & program,
    lowir_model::SymbolRole role, SectionKind kind,
    std::uint32_t type, std::vector<HostRelocation> & relocations)
{
  HostSection section;
  section.name = SectionIdentity(kind);
  section.type = type;
  section.flags = 3;
  section.alignment = 8;
  section.entry_size = 8;
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const lowir_model::Function & function = program.functions[i];
    if(function.metadata.role != role) continue;
    HostRelocation relocation;
    relocation.kind = HostRelocation::HR_ABSOLUTE64;
    relocation.offset = section.bytes.size();
    relocation.program_symbol = function.symbol;
    relocations.push_back(relocation);
    append_little(section.bytes, 0, 8);
  }
  return section;
}

void append_uleb128(std::vector<unsigned char> & out, std::uint64_t value)
{
  do {
    unsigned byte = static_cast<unsigned>(value & 0x7f);
    value >>= 7;
    if(value) byte |= 0x80;
    out.push_back(static_cast<unsigned char>(byte));
  } while(value);
}

void append_sleb128(std::vector<unsigned char> & out, std::int64_t value)
{
  bool more = true;
  while(more) {
    unsigned byte = static_cast<unsigned>(value) & 0x7f;
    const bool sign = (byte & 0x40) != 0;
    value >>= 7;
    more = !((value == 0 && !sign) || (value == -1 && sign));
    if(more) byte |= 0x80;
    out.push_back(static_cast<unsigned char>(byte));
  }
}

void align_bytes(std::vector<unsigned char> & out, std::size_t alignment)
{
  if(!alignment) throw std::logic_error("zero ELF section alignment");
  while(out.size() % alignment) out.push_back(0);
}

unsigned dwarf_register(X64Register reg)
{
  switch(reg) {
  case XR_RAX: return 0;
  case XR_RDX: return 1;
  case XR_RCX: return 2;
  case XR_RBX: return 3;
  case XR_RSI: return 4;
  case XR_RDI: return 5;
  case XR_RBP: return 6;
  case XR_RSP: return 7;
  default: return static_cast<unsigned>(reg);
  }
}

void append_cfi_advance(std::vector<unsigned char> & out, std::size_t amount)
{
  if(amount <= 0x3f) {
    out.push_back(static_cast<unsigned char>(0x40 | amount));
  } else if(amount <= 0xff) {
    out.push_back(0x02);
    out.push_back(static_cast<unsigned char>(amount));
  } else if(amount <= 0xffff) {
    out.push_back(0x03);
    append_little(out, amount, 2);
  } else {
    out.push_back(0x04);
    append_little(out, amount, 4);
  }
}

void append_cfi_cfa_offset(std::vector<unsigned char> & out,
                           std::size_t offset)
{
  out.push_back(0x0e);  // DW_CFA_def_cfa_offset
  append_uleb128(out, offset);
}

std::string host_symbol_spelling(const std::string & raw);

HostSection make_host_lsda(
    std::vector<HostFunctionLayout> & functions,
    std::vector<HostRelocation> & relocations, Stats * stats)
{
  HostSection section;
  section.name = SectionIdentity(SK_GCC_EXCEPT_TABLE);
  section.flags = 2;
  section.alignment = 4;
  std::vector<lsda_detail::CallSiteTableEntry> sites;
  for(std::size_t i = 0; i < functions.size(); ++i) {
    HostFunctionLayout & function = functions[i];
    if(function.call_sites.empty()) continue;
    function.lsda_offset = section.bytes.size();
    std::vector<unsigned char> call_table;
    std::vector<unsigned char> actions;
    const std::size_t no_action = static_cast<std::size_t>(-1);
    std::vector<std::size_t> action_offsets(
      function.clauses.size(), no_action);
    std::map<long long, lowir_model::SymbolId> type_symbols;
    std::map<lowir_model::SymbolId, long long> selectors_by_type;
    std::map<std::vector<lowir_model::SymbolId>, long long> filter_selectors;
    std::vector<unsigned char> exception_specs;
    long long max_selector = 0;
    for(std::size_t ordered = 0;
        ordered < function.clause_order.size(); ++ordered) {
      const std::uint32_t block = function.clause_order[ordered];
      if(block >= function.clauses.size())
        throw std::logic_error("invalid host EH clause identity");
      const std::vector<mir_model::MirHostEhClause> & list =
        function.clauses[block];
      for(std::size_t clause = 0; clause < list.size(); ++clause) {
        if(list[clause].kind != mir_model::MirHostEhClause::HC_CATCH) continue;
        max_selector = std::max(max_selector, list[clause].selector);
        type_symbols[list[clause].selector] = list[clause].catch_all ?
          lowir_model::SymbolId() : list[clause].type_symbol;
        if(!list[clause].catch_all)
          selectors_by_type[list[clause].type_symbol] = list[clause].selector;
      }
    }
    for(std::size_t ordered = 0;
        ordered < function.clause_order.size(); ++ordered) {
      const std::uint32_t block = function.clause_order[ordered];
      const std::vector<mir_model::MirHostEhClause> & list =
        function.clauses[block];
      for(std::size_t clause = 0; clause < list.size(); ++clause) {
        if(list[clause].kind != mir_model::MirHostEhClause::HC_FILTER)
          continue;
        if(filter_selectors.count(list[clause].filter_type_symbols)) continue;
        const long long selector = -static_cast<long long>(
          exception_specs.size() + 1);
        filter_selectors[list[clause].filter_type_symbols] = selector;
        for(std::size_t type = 0;
            type < list[clause].filter_type_symbols.size(); ++type) {
          const lowir_model::SymbolId symbol =
            list[clause].filter_type_symbols[type];
          std::map<lowir_model::SymbolId, long long>::const_iterator selected =
            selectors_by_type.find(symbol);
          if(selected == selectors_by_type.end()) {
            const long long index = ++max_selector;
            type_symbols[index] = symbol;
            selectors_by_type[symbol] = index;
            append_uleb128(exception_specs, index);
          } else append_uleb128(exception_specs, selected->second);
        }
        append_uleb128(exception_specs, 0);
      }
    }
    for(std::size_t ordered = 0;
        ordered < function.clause_order.size(); ++ordered) {
      const std::uint32_t block = function.clause_order[ordered];
      const std::vector<mir_model::MirHostEhClause> & list =
        function.clauses[block];
      bool catches = false;
      for(std::size_t clause = 0; clause < list.size(); ++clause)
        catches = catches ||
          list[clause].kind == mir_model::MirHostEhClause::HC_CATCH ||
          list[clause].kind == mir_model::MirHostEhClause::HC_FILTER;
      if(!catches) continue;
      std::size_t next_action = static_cast<std::size_t>(-1);
      for(std::size_t clause = list.size(); clause != 0; --clause) {
        const mir_model::MirHostEhClause & item = list[clause - 1];
        const std::size_t action = actions.size();
        long long selector = 0;
        if(item.kind == mir_model::MirHostEhClause::HC_CATCH)
          selector = item.selector > 0 ? item.selector : 1;
        else if(item.kind == mir_model::MirHostEhClause::HC_FILTER)
          selector = filter_selectors[item.filter_type_symbols];
        append_sleb128(actions, selector);
        const std::size_t displacement = actions.size();
        append_sleb128(actions, next_action == static_cast<std::size_t>(-1) ?
          0 : static_cast<std::int64_t>(next_action) -
                static_cast<std::int64_t>(displacement));
        next_action = action;
      }
      action_offsets[block] = next_action + 1;
    }

    lsda_detail::sparse_call_site_table(function, sites, stats);
    for(std::size_t site = 0; site < sites.size(); ++site) {
      const std::size_t entry_start = call_table.size();
      append_uleb128(call_table, sites[site].start);
      append_uleb128(call_table, sites[site].length);
      append_uleb128(call_table, sites[site].has_landing_pad ?
        sites[site].landing_pad_offset : 0);
      const std::uint32_t action_block = sites[site].action_block;
      const std::size_t action = sites[site].has_landing_pad &&
        action_block < action_offsets.size() ?
        action_offsets[action_block] : no_action;
      append_uleb128(call_table, action == no_action ? 0 : action);
      if(stats && !sites[site].has_landing_pad)
        stats->eh_lsda_unprotected_call_site_bytes +=
          call_table.size() - entry_start;
    }
    if(stats) stats->eh_lsda_call_site_table_bytes += call_table.size();

    std::vector<unsigned char> body;
    body.push_back(0x01);
    append_uleb128(body, call_table.size());
    body.insert(body.end(), call_table.begin(), call_table.end());
    body.insert(body.end(), actions.begin(), actions.end());
    const std::size_t type_bytes = static_cast<std::size_t>(max_selector) * 4;
    const std::size_t type_offset = body.size() + type_bytes;
    section.bytes.push_back(0xff);
    const bool has_type_table = max_selector || !exception_specs.empty();
    section.bytes.push_back(has_type_table ? 0x9b : 0xff);
    if(has_type_table) append_uleb128(section.bytes, type_offset);
    section.bytes.insert(section.bytes.end(), body.begin(), body.end());
    if(max_selector) {
      const std::size_t types_begin = section.bytes.size();
      for(long long selector = max_selector; selector != 0; --selector) {
        const std::map<long long, lowir_model::SymbolId>::const_iterator type =
          type_symbols.find(selector);
        const lowir_model::SymbolId symbol = type == type_symbols.end() ?
          lowir_model::SymbolId() : type->second;
        if(symbol.valid()) {
          HostRelocation relocation;
          relocation.kind = HostRelocation::HR_PC32;
          relocation.offset = section.bytes.size();
          relocation.eh_type_ref_symbol = symbol;
          relocations.push_back(relocation);
        }
        append_little(section.bytes, 0, 4);
      }
      if(section.bytes.size() - types_begin != type_bytes)
        throw std::logic_error("invalid host EH type table size");
    }
    section.bytes.insert(section.bytes.end(),
      exception_specs.begin(), exception_specs.end());
  }
  return section;
}

std::size_t append_cie(HostSection & section, bool with_personality,
                       std::vector<HostRelocation> & relocations)
{
  const std::size_t start = section.bytes.size();
  append_little(section.bytes, 0, 4);
  append_little(section.bytes, 0, 4);
  section.bytes.push_back(1);
  section.bytes.push_back('z');
  if(with_personality) {
    section.bytes.push_back('P');
    section.bytes.push_back('L');
  }
  section.bytes.push_back('R');
  section.bytes.push_back(0);
  append_uleb128(section.bytes, 1);
  section.bytes.push_back(0x78);
  append_uleb128(section.bytes, 16);
  append_uleb128(section.bytes, with_personality ? 7 : 1);
  if(with_personality) {
    section.bytes.push_back(0x9b);
    HostRelocation personality;
    personality.kind = HostRelocation::HR_PC32;
    personality.offset = section.bytes.size();
    personality.eh_personality_ref = true;
    relocations.push_back(personality);
    append_little(section.bytes, 0, 4);
    section.bytes.push_back(0x1b);
  }
  section.bytes.push_back(0x1b);
  section.bytes.push_back(0x0c);
  append_uleb128(section.bytes, 7);
  append_uleb128(section.bytes, 8);
  section.bytes.push_back(0x90);
  append_uleb128(section.bytes, 1);
  while((section.bytes.size() - start) % 4) section.bytes.push_back(0);
  put_little(section.bytes, start, section.bytes.size() - start - 4, 4);
  return start;
}

HostSection make_host_eh_frame(
    const std::vector<HostFunctionLayout> & functions,
    std::vector<HostRelocation> & relocations)
{
  HostSection section;
  section.name = SectionIdentity(SK_EH_FRAME);
  section.flags = 2;
  section.alignment = 8;
  const std::size_t basic_cie = append_cie(section, false, relocations);
  bool needs_personality = false;
  for(std::size_t i = 0; i < functions.size(); ++i)
    needs_personality = needs_personality || !functions[i].call_sites.empty();
  const std::size_t personality_cie = needs_personality ?
    append_cie(section, true, relocations) : 0;

  for(std::size_t i = 0; i < functions.size(); ++i) {
    const HostFunctionLayout & function = functions[i];
    const bool with_personality = !function.call_sites.empty();
    const std::size_t cie = with_personality ? personality_cie : basic_cie;
    const std::size_t fde_start = section.bytes.size();
    append_little(section.bytes, 0, 4);
    append_little(section.bytes, fde_start + 4 - cie, 4);
    const std::size_t initial_location = section.bytes.size();
    append_little(section.bytes, 0, 4);
    append_little(section.bytes, function.size, 4);
    append_uleb128(section.bytes, with_personality ? 4 : 0);
    if(with_personality) {
      HostRelocation lsda;
      lsda.kind = HostRelocation::HR_PC32;
      lsda.section_target = HostRelocation::HST_LSDA;
      lsda.offset = section.bytes.size();
      lsda.addend = static_cast<long long>(function.lsda_offset);
      relocations.push_back(lsda);
      append_little(section.bytes, 0, 4);
    }
    if(function.omit_frame_pointer) {
      std::size_t cfa_offset = 8;
      for(std::size_t saved = 0;
          saved < function.callee_saved_regs.size(); ++saved) {
        const X64Register reg = function.callee_saved_regs[saved];
        append_cfi_advance(section.bytes, reg >= XR_R8 ? 2 : 1);
        cfa_offset += 8;
        append_cfi_cfa_offset(section.bytes, cfa_offset);
        section.bytes.push_back(static_cast<unsigned char>(
          0x80 | dwarf_register(reg)));
        append_uleb128(section.bytes, 2 + saved);
      }
      if(function.stack_adjustment) {
        // Frameless call alignment is one imm8 stack adjustment (four bytes).
        append_cfi_advance(section.bytes, 4);
        cfa_offset += function.stack_adjustment;
        append_cfi_cfa_offset(section.bytes, cfa_offset);
      }
    } else {
      append_cfi_advance(section.bytes, 1);
      append_cfi_cfa_offset(section.bytes, 16);
      section.bytes.push_back(0x80 | dwarf_register(XR_RBP));
      append_uleb128(section.bytes, 2);
      append_cfi_advance(section.bytes, 3);
      section.bytes.push_back(0x0d);
      append_uleb128(section.bytes, dwarf_register(XR_RBP));
      for(std::size_t saved = 0;
          saved < function.callee_saved_regs.size(); ++saved) {
        const X64Register reg = function.callee_saved_regs[saved];
        append_cfi_advance(section.bytes, reg >= XR_R8 ? 2 : 1);
        section.bytes.push_back(static_cast<unsigned char>(
          0x80 | dwarf_register(reg)));
        append_uleb128(section.bytes, 3 + saved);
      }
    }
    while((section.bytes.size() - fde_start) % 4) section.bytes.push_back(0);
    put_little(section.bytes, fde_start,
               section.bytes.size() - fde_start - 4, 4);
    HostRelocation relocation;
    relocation.kind = HostRelocation::HR_PC32;
    relocation.section_target = HostRelocation::HST_TEXT;
    relocation.offset = initial_location;
    relocation.text_section = function.text_section;
    relocation.addend = static_cast<long long>(function.offset);
    relocations.push_back(relocation);
  }
  return section;
}

void bind_host_section_targets(
    std::vector<HostRelocation> & relocations,
    const std::vector<std::uint16_t> & text_section_indexes,
    std::uint16_t lsda_index)
{
  for(std::size_t i = 0; i < relocations.size(); ++i) {
    HostRelocation & relocation = relocations[i];
    if(relocation.section_target == HostRelocation::HST_NONE) continue;
    if(relocation.section_target == HostRelocation::HST_TEXT) {
      if(relocation.text_section >= text_section_indexes.size())
        throw std::logic_error("host function has invalid text section");
      relocation.section_symbol =
        text_section_indexes[relocation.text_section];
    } else {
      if(!lsda_index)
        throw std::logic_error("host EH relocation has no LSDA section");
      relocation.section_symbol = lsda_index;
    }
    relocation.section_target = HostRelocation::HST_NONE;
  }
}

std::string host_symbol_spelling(const std::string & raw)
{
  return !raw.empty() && raw[0] == '@' ? raw.substr(1) : raw;
}

const std::string & host_runtime_object_symbol(
    const lowir_model::LowirProgram & program,
    const lowir_model::SymbolMetadata & metadata)
{
  static const std::string memory_symbols[] = {
    "bzero", "memchr", "memcmp", "memcpy", "memmove", "memset", "strchr",
    "strcmp", "strlen", "vsnprintf"
  };
  static const std::string abort_name = "abort";
  static const std::string operator_new_name = "_Znwm";
  static const std::string operator_new_array_name = "_Znam";
  static const std::string operator_delete_name = "_ZdlPv";
  static const std::string operator_delete_array_name = "_ZdaPv";
  const std::string prefix = "cppgm_builtin_";
  const std::string & object_symbol =
    program.strings.get(metadata.object_symbol);
  if(object_symbol.compare(0, prefix.size(), prefix) == 0) {
    const std::string suffix = object_symbol.substr(prefix.size());
    if(suffix == "unreachable") return abort_name;
    for(std::size_t i = 0;
        i < sizeof(memory_symbols) / sizeof(memory_symbols[0]); ++i)
      if(suffix == memory_symbols[i]) return memory_symbols[i];
  }
  if(metadata.role == lowir_model::SR_ALLOCATE_MEMORY) {
    if(object_symbol == "cppgm_builtin_operator_new") return operator_new_name;
    if(object_symbol == "cppgm_builtin_operator_new_array")
      return operator_new_array_name;
  }
  if(metadata.role == lowir_model::SR_FREE_MEMORY) {
    if(object_symbol == "cppgm_builtin_operator_delete")
      return operator_delete_name;
    if(object_symbol == "cppgm_builtin_operator_delete_array")
      return operator_delete_array_name;
  }
  return object_symbol;
}

DeclarationObjectSymbols declaration_object_symbols(
    const lowir_model::LowirProgram & program)
{
  DeclarationObjectSymbols result;
  result.typed.resize(program.symbol_names.size());
  const auto add = [&result](
      lowir_model::SymbolId symbol, lowir_model::StringId object_identity,
      const std::string & object) {
    const std::uint32_t index = symbol;
    if(!symbol.valid() || index >= result.typed.size())
      throw std::logic_error("invalid declaration symbol identity");
    result.typed[index].identity = object_identity;
    result.typed[index].spelling = &object;
  };
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i)
    if(program.exported_symbols[i].object_symbol.valid())
      add(program.exported_symbols[i].internal_symbol,
        program.exported_symbols[i].object_symbol,
        exported_object_symbol(program, program.exported_symbols[i]));
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & declaration =
      program.function_declarations[i];
    if(!declaration.metadata.object_symbol.valid()) continue;
    const std::string & object = host_runtime_object_symbol(
      program, declaration.metadata);
    const std::string & pooled = program.strings.get(
      declaration.metadata.object_symbol);
    add(declaration.symbol, &object == &pooled ?
      declaration.metadata.object_symbol : lowir_model::StringId(), object);
  }
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    if(program.global_declarations[i].metadata.object_symbol.valid())
      add(program.global_declarations[i].symbol,
        program.global_declarations[i].metadata.object_symbol,
        program.strings.get(
          program.global_declarations[i].metadata.object_symbol));
  return result;
}

std::string host_eh_type_ref_symbol(
    const lowir_model::LowirProgram & program,
    const DeclarationObjectSymbols & declarations,
    lowir_model::SymbolId symbol)
{
  const DeclarationObjectSymbol * object = declarations.find(symbol);
  return "DW.ref." + (object ? *object->spelling :
    host_symbol_spelling(lowir_model::lowir_symbol_name(program, symbol)));
}

void assign_relocation_target(
    HostRelocation & relocation,
    const std::string & raw,
    const EncodedLabels & labels)
{
  if(labels.named.count(raw)) {
    relocation.target = raw;
  } else {
    relocation.target = host_symbol_spelling(raw);
  }
}

HostRelocation host_relocation_envelope(
    EncodedFixup::Kind kind,
    mir_model::MirOperand::AddressBinding address_binding,
    std::size_t offset, long long addend, EncodedSection & source)
{
  HostRelocation relocation;
  if(kind == EncodedFixup::EF_TLS_OFFSET32) {
    relocation.kind = HostRelocation::HR_TPOFF32;
  } else if(kind == EncodedFixup::EF_ADDRESS32) {
    const bool local_address =
      address_binding == mir_model::MirOperand::ADDRESS_LOCAL;
    relocation.kind = local_address ? HostRelocation::HR_PC32 :
      HostRelocation::HR_GOTPCRELX;
    if(!local_address) {
      if(offset < 2 || offset > source.bytes.size() ||
         source.bytes[offset - 2] != 0x8d)
        throw std::logic_error("symbol-address fixup is not RIP-relative LEA");
      source.bytes[offset - 2] = 0x8b;
    }
  } else {
    relocation.kind = kind == EncodedFixup::EF_ABSOLUTE64 ?
      HostRelocation::HR_ABSOLUTE64 : HostRelocation::HR_PLT32;
  }
  relocation.offset = offset;
  relocation.addend = kind == EncodedFixup::EF_RELATIVE32 ||
    kind == EncodedFixup::EF_ADDRESS32 ? addend - 4 : addend;
  return relocation;
}

std::vector<HostRelocation> host_relocations(
    EncodedSection & source,
    const EncodedLabels & labels,
    const lowir_model::LowirProgram & program,
    const DeclarationObjectSymbols & declarations)
{
  std::vector<HostRelocation> result;
  result.reserve(source.fixups.size() + source.symbol_fixups.size());
  for(std::size_t i = 0; i < source.fixups.size(); ++i) {
    const EncodedFixup & fixup = source.fixups[i];
    HostRelocation relocation = host_relocation_envelope(
      fixup.kind, fixup.address_binding, fixup.offset, fixup.addend, source);
    assign_relocation_target(relocation, fixup.target, labels);
    result.push_back(relocation);
  }
  for(std::size_t i = 0; i < source.symbol_fixups.size(); ++i) {
    const EncodedSymbolFixup & fixup = source.symbol_fixups[i];
    const std::uint32_t symbol = fixup.target;
    if(!fixup.target.valid() || symbol >= program.symbol_names.size())
      throw std::logic_error("invalid host relocation symbol identity");
    HostRelocation relocation = host_relocation_envelope(
      fixup.kind, fixup.address_binding, fixup.offset, fixup.addend, source);
    const DeclarationObjectSymbol * declared =
      declarations.find(fixup.target);
    if(declared) {
      if(declared->identity.valid())
        relocation.object_symbol = declared->identity;
      else relocation.target = *declared->spelling;
    } else if(symbol < labels.symbol_known.size() &&
              labels.symbol_known[symbol]) {
      relocation.program_symbol = fixup.target;
    } else {
      relocation.target = host_symbol_spelling(
        lowir_model::lowir_symbol_name(program, fixup.target));
    }
    result.push_back(relocation);
  }
  return result;
}

void add_unique_symbol(std::vector<HostSymbol> & symbols,
                       std::unordered_map<std::string, std::size_t> & index,
                       const HostSymbol & symbol)
{
  if(symbol.name.empty()) return;
  const std::unordered_map<std::string, std::size_t>::const_iterator found =
    index.find(symbol.name);
  if(found != index.end()) {
    HostSymbol & prior = symbols[found->second];
    const bool named_lookup = prior.named_lookup || symbol.named_lookup;
    lowir_model::StringId object_symbol = prior.object_symbol.valid() ?
      prior.object_symbol : symbol.object_symbol;
    if(prior.object_symbol.valid() && symbol.object_symbol.valid() &&
       prior.object_symbol != symbol.object_symbol)
      throw std::logic_error("conflicting ELF object-symbol identities");
    if(prior.section == 0 && symbol.section != 0) prior = symbol;
    prior.named_lookup = named_lookup;
    if(!prior.object_symbol.valid()) prior.object_symbol = object_symbol;
    return;
  }
  index[symbol.name] = symbols.size();
  symbols.push_back(symbol);
}

std::uint64_t function_size_at(
    const std::map<std::pair<std::size_t, std::size_t>, std::uint64_t> &
      function_sizes,
    std::size_t section, std::size_t offset)
{
  const std::map<std::pair<std::size_t, std::size_t>,
    std::uint64_t>::const_iterator found = function_sizes.find(
      std::make_pair(section, offset));
  return found == function_sizes.end() ? 0 : found->second;
}

void collect_host_symbols(
    const lowir_model::LowirProgram & program,
    const std::vector<EncodedSection> & text_sections,
    const std::vector<std::uint16_t> & text_section_indexes,
    const std::vector<EncodedSection> & data_sections,
    const std::vector<std::uint16_t> & data_section_indexes,
    const EncodedLabels & encoded_labels,
    const DeclarationObjectSymbols & declarations,
    const std::vector<HostFunctionLayout> & functions,
    const std::vector<std::vector<HostRelocation> > & text_relocations,
    const std::vector<std::vector<HostRelocation> > & data_relocations,
    const std::vector<HostRelocation> & init_array_relocations,
    const std::vector<HostRelocation> & fini_array_relocations,
    const std::vector<HostRelocation> & lsda_relocations,
    const std::vector<HostRelocation> & eh_relocations,
    std::vector<HostSymbol> & locals,
    std::vector<HostSymbol> & globals)
{
  std::unordered_map<std::string, std::size_t> local_index;
  std::unordered_map<std::string, std::size_t> global_index;
  std::unordered_set<std::string> required_local_labels;
  std::vector<unsigned char> required_program_symbols(
    program.symbol_names.size(), 0);
  std::vector<lowir_model::SymbolId> required_eh_type_refs;
  bool required_eh_personality_ref = false;
  const auto require_program_symbol =
    [&required_program_symbols](lowir_model::SymbolId symbol) {
      const std::uint32_t index = symbol;
      if(!symbol.valid() || index >= required_program_symbols.size())
        throw std::logic_error("invalid required program symbol identity");
      required_program_symbols[index] = 1;
    };
  const auto require_relocation_labels =
    [&encoded_labels, &required_local_labels, &require_program_symbol,
     &required_program_symbols, &required_eh_type_refs,
     &required_eh_personality_ref](
      const std::vector<HostRelocation> & relocations) {
      for(std::size_t i = 0; i < relocations.size(); ++i) {
        if(relocations[i].section_symbol) {
          continue;
        } else if(relocations[i].program_symbol.valid()) {
          require_program_symbol(relocations[i].program_symbol);
        } else if(relocations[i].eh_type_ref_symbol.valid()) {
          const std::uint32_t symbol = relocations[i].eh_type_ref_symbol;
          if(symbol >= required_program_symbols.size())
            throw std::logic_error(
              "invalid required EH type-reference identity");
          if(std::find(required_eh_type_refs.begin(),
                       required_eh_type_refs.end(),
                       relocations[i].eh_type_ref_symbol) ==
             required_eh_type_refs.end())
            required_eh_type_refs.push_back(
              relocations[i].eh_type_ref_symbol);
        } else if(relocations[i].eh_personality_ref) {
          required_eh_personality_ref = true;
        } else if(encoded_labels.named.count(relocations[i].target)) {
          required_local_labels.insert(relocations[i].target);
        }
      }
    };
  for(std::size_t i = 0; i < text_relocations.size(); ++i)
    require_relocation_labels(text_relocations[i]);
  for(std::size_t i = 0; i < data_relocations.size(); ++i)
    require_relocation_labels(data_relocations[i]);
  require_relocation_labels(init_array_relocations);
  require_relocation_labels(fini_array_relocations);
  require_relocation_labels(lsda_relocations);
  require_relocation_labels(eh_relocations);
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i)
    if(program.exported_symbols[i].keep_internal_alias)
      require_program_symbol(program.exported_symbols[i].internal_symbol);
  for(std::size_t i = 0; i < functions.size(); ++i)
    if(!functions[i].object_symbol.valid())
      require_program_symbol(functions[i].program_symbol);
  std::map<std::pair<std::size_t, std::size_t>, std::uint64_t> function_sizes;
  for(std::size_t i = 0; i < functions.size(); ++i)
    function_sizes[std::make_pair(
      functions[i].text_section, functions[i].offset)] = functions[i].size;
  for(std::size_t section = 0; section < text_sections.size(); ++section)
    for(std::unordered_map<std::string, std::size_t>::const_iterator it =
          text_sections[section].labels.begin();
        it != text_sections[section].labels.end(); ++it) {
      if(!required_local_labels.count(it->first)) continue;
      HostSymbol symbol;
      symbol.name = it->first;
      symbol.section = text_section_indexes[section];
      symbol.value = it->second;
      symbol.size = function_size_at(function_sizes, section, it->second);
      symbol.type = symbol.size ? 2 : 0;
      add_unique_symbol(locals, local_index, symbol);
    }
  for(std::size_t section = 0; section < data_sections.size(); ++section) {
    for(std::unordered_map<std::string, std::size_t>::const_iterator it =
          data_sections[section].labels.begin();
        it != data_sections[section].labels.end(); ++it) {
      if(!required_local_labels.count(it->first)) continue;
      HostSymbol symbol;
      symbol.name = it->first;
      symbol.section = data_section_indexes[section];
      symbol.value = it->second;
      symbol.type = (data_sections[section].flags & 0x400) ? 6 : 1;
      add_unique_symbol(locals, local_index, symbol);
    }
  }
  for(std::size_t i = 0; i < required_program_symbols.size(); ++i) {
    if(!required_program_symbols[i] || !encoded_labels.symbol_known[i])
      continue;
    const lowir_model::SymbolId identity(static_cast<std::uint32_t>(i));
    const EncodedLabelLocation & location = encoded_labels.symbols[i];
    HostSymbol symbol;
    symbol.name = lowir_model::lowir_symbol_name(program, identity);
    symbol.internal_program_symbol = true;
    symbol.program_symbol = identity;
    symbol.section = location.text ?
      text_section_indexes[location.section] :
      data_section_indexes[location.section];
    symbol.value = location.offset;
    symbol.size = location.text ? function_size_at(
      function_sizes, location.section, location.offset) : 0;
    symbol.type = location.text ? (symbol.size ? 2 : 0) :
      ((data_sections[location.section].flags & 0x400) ? 6 : 1);
    add_unique_symbol(locals, local_index, symbol);
  }
  for(std::size_t i = 0; i < required_eh_type_refs.size(); ++i) {
    const lowir_model::SymbolId identity = required_eh_type_refs[i];
    const EncodedLabelLocation * location =
      find_eh_type_ref_label(encoded_labels, identity);
    if(!location)
      throw std::logic_error("required EH type reference has no label");
    HostSymbol symbol;
    symbol.name = host_eh_type_ref_symbol(program, declarations, identity);
    symbol.eh_type_ref_symbol = identity;
    symbol.section = location->text ?
      text_section_indexes[location->section] :
      data_section_indexes[location->section];
    symbol.value = location->offset;
    symbol.size = location->text ? function_size_at(
      function_sizes, location->section, location->offset) : 0;
    symbol.type = location->text ? (symbol.size ? 2 : 0) :
      ((data_sections[location->section].flags & 0x400) ? 6 : 1);
    add_unique_symbol(locals, local_index, symbol);
  }
  if(required_eh_personality_ref) {
    if(!encoded_labels.eh_personality_ref_known)
      throw std::logic_error(
        "required EH personality reference has no label");
    const EncodedLabelLocation & location =
      encoded_labels.eh_personality_ref;
    HostSymbol symbol;
    symbol.name = "DW.ref.__gxx_personality_v0";
    symbol.eh_personality_ref = true;
    symbol.section = location.text ?
      text_section_indexes[location.section] :
      data_section_indexes[location.section];
    symbol.value = location.offset;
    symbol.size = location.text ? function_size_at(
      function_sizes, location.section, location.offset) : 0;
    symbol.type = location.text ? (symbol.size ? 2 : 0) :
      ((data_sections[location.section].flags & 0x400) ? 6 : 1);
    add_unique_symbol(locals, local_index, symbol);
  }

  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i) {
    const lowir_model::ExportedSymbol & exported = program.exported_symbols[i];
    if(!exported.object_symbol.valid()) continue;
    const std::string & object_symbol = exported_object_symbol(program, exported);
    const std::uint32_t internal = exported.internal_symbol;
    if(!exported.internal_symbol.valid() ||
       internal >= encoded_labels.symbol_known.size() ||
       !encoded_labels.symbol_known[internal]) continue;
    const EncodedLabelLocation & location = encoded_labels.symbols[internal];
    const std::uint16_t section = location.text ?
      text_section_indexes[location.section] :
      data_section_indexes[location.section];
    const std::size_t value = location.offset;
    const unsigned type = location.text ? 2 :
      (data_sections[location.section].flags & 0x400) ? 6 : 1;
    HostSymbol symbol;
    symbol.name = object_symbol;
    symbol.object_symbol = exported.object_symbol;
    symbol.section = section;
    symbol.value = value;
    symbol.type = type;
    symbol.size = symbol.type == 2 ? function_size_at(
      function_sizes, location.section, value) : 0;
    symbol.binding = exported.linkage == ir_model::SL_WEAK ? 2 :
      exported.linkage == ir_model::SL_INTERNAL ||
      exported.prefer_local_object_binding ? 0 : 1;
    if(symbol.binding == 0)
      add_unique_symbol(locals, local_index, symbol);
    else
      add_unique_symbol(globals, global_index, symbol);
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    if(program.functions[i].metadata.role != lowir_model::SR_ENTRY) continue;
    const std::uint32_t entry = program.functions[i].symbol;
    if(!program.functions[i].symbol.valid() ||
       entry >= encoded_labels.symbol_known.size() ||
       !encoded_labels.symbol_known[entry] ||
       !encoded_labels.symbols[entry].text)
      throw std::logic_error("entry function has no encoded symbol");
    const EncodedLabelLocation & at = encoded_labels.symbols[entry];
    HostSymbol symbol;
    symbol.name = "main";
    symbol.binding = 1;
    symbol.type = 2;
    symbol.section = text_section_indexes[at.section];
    symbol.value = at.offset;
    symbol.size = function_size_at(
      function_sizes, at.section, at.offset);
    add_unique_symbol(globals, global_index, symbol);
  }

  std::unordered_set<std::string> defined;
  std::vector<unsigned char> defined_objects(program.strings.size() + 1, 0);
  const auto mark_defined = [&defined, &defined_objects](
      const HostSymbol & symbol) {
    if(!symbol.object_symbol.valid()) {
      if(!symbol.program_symbol.valid() && !symbol.eh_type_ref_symbol.valid() &&
         !symbol.eh_personality_ref)
        defined.insert(symbol.name);
      return;
    }
    const std::uint32_t object = symbol.object_symbol;
    if(object >= defined_objects.size())
      throw std::logic_error("invalid defined object-symbol identity");
    defined_objects[object] = 1;
  };
  for(std::size_t i = 0; i < locals.size(); ++i) mark_defined(locals[i]);
  for(std::size_t i = 0; i < globals.size(); ++i) mark_defined(globals[i]);
  std::unordered_set<std::string> declared_tls_symbols;
  std::vector<unsigned char> declared_tls_objects(
    program.strings.size() + 1, 0);
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    if(program.global_declarations[i].storage == lowir_model::GSM_THREAD_LOCAL) {
      const DeclarationObjectSymbol * object = declarations.find(
        program.global_declarations[i].symbol);
      if(object && object->identity.valid()) {
        const std::uint32_t identity = object->identity;
        if(identity >= declared_tls_objects.size())
          throw std::logic_error("invalid TLS object-symbol identity");
        declared_tls_objects[identity] = 1;
      } else if(object) declared_tls_symbols.insert(*object->spelling);
    }
  std::vector<const std::vector<HostRelocation> *> relocation_groups;
  for(std::size_t i = 0; i < text_relocations.size(); ++i)
    relocation_groups.push_back(&text_relocations[i]);
  for(std::size_t i = 0; i < data_relocations.size(); ++i)
    relocation_groups.push_back(&data_relocations[i]);
  relocation_groups.push_back(&init_array_relocations);
  relocation_groups.push_back(&fini_array_relocations);
  relocation_groups.push_back(&lsda_relocations);
  relocation_groups.push_back(&eh_relocations);
  for(std::size_t group = 0; group < relocation_groups.size(); ++group) {
    const std::vector<HostRelocation> & relocations = *relocation_groups[group];
    for(std::size_t i = 0; i < relocations.size(); ++i) {
      if(relocations[i].section_symbol ||
         relocations[i].program_symbol.valid() ||
         relocations[i].eh_type_ref_symbol.valid() ||
         relocations[i].eh_personality_ref) continue;
      if(relocations[i].object_symbol.valid()) {
        const std::uint32_t object = relocations[i].object_symbol;
        if(object >= defined_objects.size())
          throw std::logic_error("invalid relocation object-symbol identity");
        if(defined_objects[object]) continue;
        HostSymbol symbol;
        symbol.object_symbol = relocations[i].object_symbol;
        symbol.name = program.strings.get(relocations[i].object_symbol);
        symbol.binding = 1;
        if(declared_tls_objects[object]) symbol.type = 6;
        add_unique_symbol(globals, global_index, symbol);
        defined_objects[object] = 1;
        continue;
      }
      if(defined.count(relocations[i].target)) continue;
      HostSymbol symbol;
      symbol.name = relocations[i].target;
      symbol.named_lookup = true;
      symbol.binding = 1;
      if(declared_tls_symbols.count(symbol.name)) symbol.type = 6;
      add_unique_symbol(globals, global_index, symbol);
      defined.insert(symbol.name);
    }
  }
  const auto stable_symbol_order = [](const HostSymbol & left,
                                      const HostSymbol & right) {
    const std::size_t left_size = left.name.size() +
      (left.internal_program_symbol ? 1 : 0);
    const std::size_t right_size = right.name.size() +
      (right.internal_program_symbol ? 1 : 0);
    const std::size_t common = std::min(left_size, right_size);
    for(std::size_t i = 0; i < common; ++i) {
      const char left_char = left.internal_program_symbol && i == 0 ? '@' :
        left.name[i - (left.internal_program_symbol ? 1 : 0)];
      const char right_char = right.internal_program_symbol && i == 0 ? '@' :
        right.name[i - (right.internal_program_symbol ? 1 : 0)];
      if(left_char != right_char) return left_char < right_char;
    }
    if(left_size != right_size) return left_size < right_size;
    if(left.section != right.section) return left.section < right.section;
    return left.value < right.value;
  };
  std::sort(locals.begin(), locals.end(), stable_symbol_order);
  std::sort(globals.begin(), globals.end(), stable_symbol_order);
}

std::vector<unsigned char> host_external_global_definitions(
    const lowir_model::LowirProgram & source,
    const mir_model::MirProgram & program)
{
  std::vector<unsigned char> external_objects(source.strings.size() + 1, 0);
  for(std::size_t i = 0; i < source.global_declarations.size(); ++i)
    if(source.global_declarations[i].metadata.role ==
         lowir_model::SR_RTTI_DATA &&
       source.global_declarations[i].metadata.object_symbol.valid()) {
      const std::uint32_t object =
        source.global_declarations[i].metadata.object_symbol;
      if(object >= external_objects.size())
        throw std::logic_error("invalid external RTTI object identity");
      external_objects[object] = 1;
    }
  std::vector<unsigned char> suppressed(program.symbol_names.size(), 0);
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const mir_model::MirGlobalDefinition & global = program.globals[i];
    const std::uint32_t object = global.object_symbol;
    if(!global.object_symbol.valid() || object >= external_objects.size() ||
       !external_objects[object]) continue;
    suppressed[global.symbol] = 1;
    for(std::size_t j = 0; j < global.data_items.size(); ++j)
      if(global.data_items[j].kind ==
           mir_model::MirGlobalDefinition::DataItem::ITEM_ADDR)
        suppressed[global.data_items[j].symbol] = 1;
  }
  return suppressed;
}

std::vector<unsigned char> make_symbol_table(
    const std::vector<HostSymbol> & locals,
    const std::vector<HostSymbol> & globals,
    const std::vector<std::uint16_t> & section_symbols,
    ElfStringTableBuilder & strings,
    std::unordered_map<std::string, std::size_t> & indexes,
    std::vector<std::size_t> & section_indexes,
    std::vector<std::size_t> & program_indexes,
    std::vector<std::size_t> & object_indexes,
    std::vector<std::pair<lowir_model::SymbolId, std::size_t> > &
      eh_type_ref_indexes,
    std::size_t & eh_personality_ref_index)
{
  std::vector<unsigned char> table(24, 0);
  for(std::size_t i = 0; i < section_symbols.size(); ++i) {
    const std::size_t index = table.size() / 24;
    const std::uint16_t section = section_symbols[i];
    if(section >= section_indexes.size())
      throw std::logic_error("invalid ELF section-symbol identity");
    section_indexes[section] = index;
    append_little(table, 0, 4);
    table.push_back(3);
    table.push_back(0);
    append_little(table, section, 2);
    append_little(table, 0, 8);
    append_little(table, 0, 8);
  }
  for(unsigned group = 0; group < 2; ++group) {
    const std::vector<HostSymbol> & symbols = group == 0 ? locals : globals;
    for(std::size_t i = 0; i < symbols.size(); ++i) {
      const HostSymbol & symbol = symbols[i];
      const std::size_t index = table.size() / 24;
      if((symbol.named_lookup ||
          (!symbol.program_symbol.valid() && !symbol.object_symbol.valid() &&
           !symbol.eh_type_ref_symbol.valid() &&
           !symbol.eh_personality_ref)) && !indexes.count(symbol.name))
        indexes[symbol.name] = index;
      if(symbol.program_symbol.valid()) {
        const std::uint32_t identity = symbol.program_symbol;
        if(identity >= program_indexes.size())
          throw std::logic_error("invalid ELF program symbol identity");
        program_indexes[identity] = index;
      }
      if(symbol.object_symbol.valid()) {
        const std::uint32_t identity = symbol.object_symbol;
        if(identity >= object_indexes.size())
          throw std::logic_error("invalid ELF object-symbol identity");
        object_indexes[identity] = index;
      }
      if(symbol.eh_type_ref_symbol.valid()) {
        eh_type_ref_indexes.push_back(std::make_pair(
          symbol.eh_type_ref_symbol, index));
      }
      if(symbol.eh_personality_ref) eh_personality_ref_index = index;
      append_little(table, strings.InternSymbol(symbol.name,
        symbol.internal_program_symbol, symbol.program_symbol,
        symbol.object_symbol, symbol.eh_type_ref_symbol,
        symbol.eh_personality_ref), 4);
      table.push_back(static_cast<unsigned char>((symbol.binding << 4) |
                                                 symbol.type));
      table.push_back(0);
      append_little(table, symbol.section, 2);
      append_little(table, symbol.value, 8);
      append_little(table, symbol.size, 8);
    }
  }
  return table;
}

std::vector<unsigned char> make_relocation_table(
    const std::vector<HostRelocation> & relocations,
    const std::unordered_map<std::string, std::size_t> & symbols,
    const std::vector<std::size_t> & section_symbols,
    const std::vector<std::size_t> & program_symbols,
    const std::vector<std::size_t> & object_symbols,
    const std::vector<std::pair<lowir_model::SymbolId, std::size_t> > &
      eh_type_ref_symbols,
    std::size_t eh_personality_ref_symbol)
{
  std::vector<HostRelocation> ordered = relocations;
  std::sort(ordered.begin(), ordered.end(),
    [](const HostRelocation & left, const HostRelocation & right) {
      return left.offset < right.offset;
    });
  std::vector<unsigned char> table;
  table.reserve(ordered.size() * 24);
  for(std::size_t i = 0; i < ordered.size(); ++i) {
    const HostRelocation & relocation = ordered[i];
    std::size_t symbol_index = lowir_model::kInvalidCompactId;
    if(relocation.section_symbol) {
      if(relocation.section_symbol < section_symbols.size())
        symbol_index = section_symbols[relocation.section_symbol];
      if(symbol_index == lowir_model::kInvalidCompactId)
        throw std::logic_error("ELF relocation has no section symbol");
    } else if(relocation.program_symbol.valid()) {
      const std::uint32_t identity = relocation.program_symbol;
      if(identity < program_symbols.size())
        symbol_index = program_symbols[identity];
      if(symbol_index == lowir_model::kInvalidCompactId)
        throw std::logic_error("ELF relocation has no program symbol");
    } else if(relocation.object_symbol.valid()) {
      const std::uint32_t identity = relocation.object_symbol;
      if(identity < object_symbols.size())
        symbol_index = object_symbols[identity];
      if(symbol_index == lowir_model::kInvalidCompactId)
        throw std::logic_error("ELF relocation has no object symbol");
    } else if(relocation.eh_type_ref_symbol.valid()) {
      for(std::size_t ref = 0; ref < eh_type_ref_symbols.size(); ++ref)
        if(eh_type_ref_symbols[ref].first ==
           relocation.eh_type_ref_symbol) {
          symbol_index = eh_type_ref_symbols[ref].second;
          break;
        }
      if(symbol_index == lowir_model::kInvalidCompactId)
        throw std::logic_error("ELF relocation has no EH type reference");
    } else if(relocation.eh_personality_ref) {
      symbol_index = eh_personality_ref_symbol;
      if(symbol_index == lowir_model::kInvalidCompactId)
        throw std::logic_error(
          "ELF relocation has no EH personality reference");
    } else {
      const std::unordered_map<std::string, std::size_t>::const_iterator symbol =
        symbols.find(relocation.target);
      if(symbol == symbols.end())
        throw std::logic_error("ELF relocation has no symbol: " +
                               relocation.target);
      symbol_index = symbol->second;
    }
    const unsigned type = relocation.kind == HostRelocation::HR_ABSOLUTE64 ? 1 :
      relocation.kind == HostRelocation::HR_PC32 ? 2 :
      relocation.kind == HostRelocation::HR_GOTPCRELX ? 42 :
      relocation.kind == HostRelocation::HR_TPOFF32 ? 23 : 4;
    append_little(table, relocation.offset, 8);
    append_little(table, (static_cast<std::uint64_t>(symbol_index) << 32) |
                         type, 8);
    append_little(table, static_cast<std::uint64_t>(relocation.addend), 8);
  }
  return table;
}

std::size_t relocatable_image_size(
    const std::vector<HostSection> & sections)
{
  std::size_t size = 64;
  for(std::size_t i = 1; i < sections.size(); ++i) {
    const std::size_t alignment = sections[i].alignment;
    const std::size_t remainder = alignment > 1 ? size % alignment : 0;
    if(remainder) size += alignment - remainder;
    size += sections[i].bytes.size();
  }
  const std::size_t header_remainder = size % 8;
  if(header_remainder) size += 8 - header_remainder;
  return size + sections.size() * 64;
}

void append_function_comdat_groups(
    std::vector<HostSection> & sections,
    const std::vector<EncodedSection> & text_sections,
    const std::vector<std::uint16_t> & text_indexes,
    const std::vector<std::uint16_t> & text_relocation_indexes,
    std::uint16_t symtab_index,
    const std::vector<std::size_t> & object_symbol_indexes)
{
  for(std::size_t i = 0; i < text_sections.size(); ++i) {
    const lowir_model::StringId signature =
      text_sections[i].comdat_signature;
    if(!signature.valid()) continue;
    const std::uint32_t object = signature;
    if(object >= object_symbol_indexes.size() ||
       object_symbol_indexes[object] == lowir_model::kInvalidCompactId)
      throw std::logic_error("function COMDAT has no signature symbol");
    HostSection group;
    group.name = SectionIdentity(SK_GROUP);
    group.type = 17;
    group.alignment = 4;
    group.entry_size = 4;
    group.link = symtab_index;
    group.info = static_cast<std::uint32_t>(object_symbol_indexes[object]);
    append_little(group.bytes, 1, 4);
    append_little(group.bytes, text_indexes[i], 4);
    if(text_relocation_indexes[i])
      append_little(group.bytes, text_relocation_indexes[i], 4);
    sections.push_back(group);
  }
}

namespace {

void record_encoded_storage(Stats * stats, const EncodedSection & text,
    const std::vector<EncodedSection> & data)
{
  if(!stats) return;
  stats->encoded_section_bytes = text.bytes.size();
  for(std::size_t i = 0; i < data.size(); ++i)
    stats->encoded_section_bytes += data[i].bytes.size();
}

void record_string_table(Stats * stats,
    const std::unordered_map<std::string, std::size_t> & indexes,
    const ElfStringTableBuilder & strings,
    std::size_t symbol_name_entries, std::size_t section_name_entries,
    const std::chrono::steady_clock::time_point & started)
{
  if(!stats) return;
  stats->elf_internal_string_entries = 0;
  stats->elf_imported_string_entries = indexes.size();
  stats->final_strtab_entries = symbol_name_entries;
  stats->final_strtab_bytes = strings.bytes().size();
  stats->final_shstrtab_entries = section_name_entries;
  stats->final_shstrtab_bytes = 0;
  stats->final_shared_string_entries = strings.entries();
  stats->final_section_string_reuses = strings.section_reuses();
  stats->final_symbol_string_reuses = strings.symbol_reuses();
  stats->final_string_suffix_aliases = strings.suffix_aliases();
  stats->elf_string_table_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
}

void record_final_elf_storage(Stats * stats,
    const std::vector<HostSection> & sections,
    const std::vector<unsigned char> & image, std::size_t relocations,
    std::size_t symbol_entries)
{
  if(!stats) return;
  stats->elf_string_map_probes = symbol_entries + relocations;
  stats->final_elf_live_bytes = image.capacity() +
    sections.capacity() * sizeof(HostSection);
  for(std::size_t i = 1; i < sections.size(); ++i)
    stats->final_elf_live_bytes += sections[i].bytes.capacity();
}

}  // namespace

std::vector<unsigned char> make_linux_relocatable_image(
    const lowir_model::LowirProgram & program,
    DeclarationObjectSymbols declarations,
    EncodedSection mutable_text,
    std::vector<EncodedSection> mutable_data,
    std::vector<HostFunctionLayout> & functions,
    std::size_t & relocation_count,
    Stats * stats)
{
	record_encoded_storage(stats, mutable_text, mutable_data);
  std::vector<EncodedSection> text_sections;
  text_sections.push_back(std::move(mutable_text));
  EncodedLabels encoded_labels =
    index_encoded_labels(program, text_sections, mutable_data);
  publish_object_aliases(
    program, text_sections, mutable_data, encoded_labels);
  text_sections = partition_weak_text(
    program, std::move(text_sections[0]), functions);
  encoded_labels = index_encoded_labels(program, text_sections, mutable_data);
  if(stats) stats->elf_imported_string_entries = 0;
  resolve_same_section_local_fixups(
    text_sections, mutable_data, program, declarations);
  std::vector<std::vector<HostRelocation> > text_relocations(
    text_sections.size());
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    text_relocations[i] = host_relocations(
      text_sections[i], encoded_labels, program, declarations);
  std::vector<std::vector<HostRelocation> > data_relocations(
    mutable_data.size());
  for(std::size_t i = 0; i < mutable_data.size(); ++i)
    data_relocations[i] = host_relocations(
      mutable_data[i], encoded_labels, program, declarations);
  std::vector<HostRelocation> lsda_relocations;
  HostSection lsda = make_host_lsda(functions, lsda_relocations, stats);
  std::vector<HostRelocation> eh_relocations;
  HostSection eh = make_host_eh_frame(functions, eh_relocations);
  struct PendingRelocations
  {
    std::uint16_t section;
    const std::vector<HostRelocation> * relocations;
  };
  std::vector<HostSection> sections(1);
  std::vector<PendingRelocations> pending_relocations;
  const auto append_section = [&sections](HostSection section)
    -> std::uint16_t {
      if(sections.size() >= 0xffff)
        throw std::runtime_error("too many ELF sections");
      sections.push_back(std::move(section));
      return static_cast<std::uint16_t>(sections.size() - 1);
    };
  const auto append_relocations =
    [&append_section, &pending_relocations, &sections](
      SectionIdentity name, std::uint16_t target,
      const std::vector<HostRelocation> & relocations) -> std::uint16_t {
      if(relocations.empty()) return 0;
      HostSection section;
      section.name = name.relocation_identity();
      section.type = 4;
      section.flags = 0x40 |
        ((sections[target].flags & 0x200) ? 0x200 : 0);
      section.alignment = 8;
      section.entry_size = 24;
      section.info = target;
      PendingRelocations pending;
      pending.section = append_section(std::move(section));
      pending.relocations = &relocations;
      pending_relocations.push_back(pending);
      return pending.section;
    };
  std::vector<std::uint16_t> text_indexes;
  std::vector<std::uint16_t> text_relocation_indexes;
  text_indexes.reserve(text_sections.size());
  text_relocation_indexes.reserve(text_sections.size());
  for(std::size_t i = 0; i < text_sections.size(); ++i) {
    HostSection section;
    section.name = text_sections[i].name;
    section.flags = text_sections[i].flags;
    section.alignment = text_sections[i].alignment;
    section.bytes = std::move(text_sections[i].bytes);
    const std::uint16_t index = append_section(std::move(section));
    text_indexes.push_back(index);
    text_relocation_indexes.push_back(append_relocations(
      text_sections[i].name, index, text_relocations[i]));
  }
  std::vector<std::uint16_t> data_indexes;
  data_indexes.reserve(mutable_data.size());
  for(std::size_t i = 0; i < mutable_data.size(); ++i) {
    HostSection section;
    section.name = mutable_data[i].name;
    section.flags = mutable_data[i].flags;
    section.alignment = mutable_data[i].alignment;
    section.bytes = std::move(mutable_data[i].bytes);
    const std::uint16_t index = append_section(std::move(section));
    data_indexes.push_back(index);
    append_relocations(mutable_data[i].name, index, data_relocations[i]);
  }
  std::vector<HostRelocation> init_array_relocations;
  HostSection init_array = make_host_lifecycle_array(
    program, lowir_model::SR_INIT, SK_INIT_ARRAY, 14,
    init_array_relocations);
  std::uint16_t init_array_index = 0;
  if(!init_array.bytes.empty()) {
    init_array_index = append_section(init_array);
    append_relocations(init_array.name, init_array_index,
                       init_array_relocations);
  }
  std::vector<HostRelocation> fini_array_relocations;
  HostSection fini_array = make_host_lifecycle_array(
    program, lowir_model::SR_FINI, SK_FINI_ARRAY, 15,
    fini_array_relocations);
  std::uint16_t fini_array_index = 0;
  if(!fini_array.bytes.empty()) {
    fini_array_index = append_section(fini_array);
    append_relocations(fini_array.name, fini_array_index,
                       fini_array_relocations);
  }

  std::uint16_t lsda_index = 0;
  if(!lsda.bytes.empty()) {
    lsda_index = append_section(lsda);
    append_relocations(lsda.name, lsda_index, lsda_relocations);
  }
  bind_host_section_targets(eh_relocations, text_indexes, lsda_index);
  const std::uint16_t eh_index = append_section(eh);
  append_relocations(eh.name, eh_index, eh_relocations);

  std::vector<HostSymbol> locals;
  std::vector<HostSymbol> globals;
  collect_host_symbols(program, text_sections, text_indexes,
                       mutable_data, data_indexes, encoded_labels,
                       declarations,
                       functions, text_relocations,
                       data_relocations, init_array_relocations,
                       fini_array_relocations, lsda_relocations,
                       eh_relocations,
                       locals, globals);

  std::vector<std::uint16_t> section_symbols;
  for(std::size_t i = 0; i < text_sections.size(); ++i)
    section_symbols.push_back(text_indexes[i]);
  for(std::size_t i = 0; i < mutable_data.size(); ++i)
    section_symbols.push_back(data_indexes[i]);
  if(init_array_index != 0)
    section_symbols.push_back(init_array_index);
  if(fini_array_index != 0)
    section_symbols.push_back(fini_array_index);
  if(lsda_index != 0)
    section_symbols.push_back(lsda_index);
  section_symbols.push_back(eh_index);

  std::unordered_map<std::string, std::size_t> symbol_indexes;
  std::vector<std::size_t> section_symbol_indexes(
    sections.size(), lowir_model::kInvalidCompactId);
  std::vector<std::size_t> program_symbol_indexes(
    program.symbol_names.size(), lowir_model::kInvalidCompactId);
  std::vector<std::size_t> object_symbol_indexes(
    program.strings.size() + 1, lowir_model::kInvalidCompactId);
  std::vector<std::pair<lowir_model::SymbolId, std::size_t> >
    eh_type_ref_symbol_indexes;
  eh_type_ref_symbol_indexes.reserve(encoded_labels.eh_type_refs.size());
  std::size_t eh_personality_ref_symbol_index =
    lowir_model::kInvalidCompactId;

  HostSection note;
  note.name = SectionIdentity(SK_NOTE_GNU_STACK);
  append_section(std::move(note));
  HostSection symtab;
  symtab.name = SectionIdentity(SK_SYMTAB);
  symtab.type = 2;
  symtab.alignment = 8;
  symtab.entry_size = 24;
  symtab.info = static_cast<std::uint32_t>(
    1 + section_symbols.size() + locals.size());
  const std::uint16_t symtab_index = append_section(std::move(symtab));
  HostSection strtab;
  strtab.name = SectionIdentity(SK_STRTAB);
  strtab.type = 3;
  const std::uint16_t strtab_index = append_section(std::move(strtab));
  sections[symtab_index].link = strtab_index;

	const std::chrono::steady_clock::time_point string_table_started = stats ?
		std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
  ElfStringTableBuilder strings(program.strings, program.symbol_names.size());
  for(std::size_t i = 1; i < sections.size(); ++i)
    if(sections[i].name.relocation)
      strings.InternSection(sections[i].name);
  for(std::size_t i = 1; i < sections.size(); ++i)
    if(!sections[i].name.relocation)
      strings.InternSection(sections[i].name);
  sections[symtab_index].bytes = make_symbol_table(
    locals, globals, section_symbols, strings, symbol_indexes,
    section_symbol_indexes, program_symbol_indexes, object_symbol_indexes,
    eh_type_ref_symbol_indexes, eh_personality_ref_symbol_index);

  append_function_comdat_groups(
    sections, text_sections, text_indexes, text_relocation_indexes,
    symtab_index, object_symbol_indexes);

  for(std::size_t i = 0; i < pending_relocations.size(); ++i) {
    HostSection & section = sections[pending_relocations[i].section];
    section.link = symtab_index;
    section.bytes = make_relocation_table(
      *pending_relocations[i].relocations, symbol_indexes,
      section_symbol_indexes, program_symbol_indexes, object_symbol_indexes,
      eh_type_ref_symbol_indexes, eh_personality_ref_symbol_index);
  }

  for(std::size_t i = 1; i < sections.size(); ++i)
    sections[i].name_offset = strings.InternSection(sections[i].name);
	record_string_table(stats, symbol_indexes, strings,
	  locals.size() + globals.size(), sections.size() - 1,
	  string_table_started);
  sections[strtab_index].bytes = strings.ReleaseBytes();

  std::vector<unsigned char> image;
  image.reserve(relocatable_image_size(sections));
  image.resize(64, 0);
  image[0] = 0x7f;
  image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
  image[4] = 2; image[5] = 1; image[6] = 1;
  put_little(image, 16, 1, 2);
  put_little(image, 18, 62, 2);
  put_little(image, 20, 1, 4);
  put_little(image, 52, 64, 2);
  put_little(image, 58, 64, 2);
  put_little(image, 60, sections.size(), 2);
  put_little(image, 62, strtab_index, 2);
  for(std::size_t i = 1; i < sections.size(); ++i) {
    align_bytes(image, sections[i].alignment);
    sections[i].file_offset = image.size();
    image.insert(image.end(), sections[i].bytes.begin(), sections[i].bytes.end());
  }
  align_bytes(image, 8);
  const std::size_t section_headers = image.size();
  put_little(image, 40, section_headers, 8);
  image.resize(image.size() + sections.size() * 64, 0);
  for(std::size_t i = 1; i < sections.size(); ++i) {
    const std::size_t at = section_headers + i * 64;
    put_little(image, at, sections[i].name_offset, 4);
    put_little(image, at + 4, sections[i].type, 4);
    put_little(image, at + 8, sections[i].flags, 8);
    put_little(image, at + 24, sections[i].file_offset, 8);
    put_little(image, at + 32, sections[i].bytes.size(), 8);
    put_little(image, at + 40, sections[i].link, 4);
    put_little(image, at + 44, sections[i].info, 4);
    put_little(image, at + 48, sections[i].alignment, 8);
    put_little(image, at + 56, sections[i].entry_size, 8);
  }
  relocation_count = lsda_relocations.size() + eh_relocations.size();
  for(std::size_t i = 0; i < text_relocations.size(); ++i)
    relocation_count += text_relocations[i].size();
  for(std::size_t i = 0; i < data_relocations.size(); ++i)
    relocation_count += data_relocations[i].size();
	record_final_elf_storage(
		stats, sections, image, relocation_count, symbol_indexes.size());
  return image;
}

}  // namespace object_elf_detail
}  // namespace lowir_native
