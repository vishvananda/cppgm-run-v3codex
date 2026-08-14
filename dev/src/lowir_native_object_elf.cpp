#include "lowir_native_object_elf.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace lowir_native {
namespace object_elf_detail {

std::string native_object_symbol(const std::string & symbol)
{
  return symbol.empty() || symbol[0] == '@' ? symbol : "@" + symbol;
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
  std::size_t offset = 0;
  std::string target;
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

EncodedLabelIndex index_encoded_labels(
    const EncodedSection & text,
    const std::vector<EncodedSection> & data_sections)
{
  std::size_t label_count = text.labels.size();
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    label_count += data_sections[i].labels.size();
  EncodedLabelIndex result;
  result.reserve(label_count);
  for(std::unordered_map<std::string, std::size_t>::const_iterator label =
        text.labels.begin(); label != text.labels.end(); ++label) {
    EncodedLabelLocation location;
    location.text = true;
    location.offset = label->second;
    result.insert(std::make_pair(label->first, location));
  }
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    for(std::unordered_map<std::string, std::size_t>::const_iterator label =
          data_sections[i].labels.begin();
        label != data_sections[i].labels.end(); ++label) {
      EncodedLabelLocation location;
      location.section = i;
      location.offset = label->second;
      result.insert(std::make_pair(label->first, location));
    }
  return result;
}

void publish_object_aliases(
    const lowir_model::LowirProgram & program,
    EncodedSection & text,
    std::vector<EncodedSection> & data_sections,
    EncodedLabelIndex & labels)
{
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    const std::string alias = native_object_symbol(
      program.object_aliases[i].object_symbol);
    const EncodedLabelIndex::const_iterator target =
      labels.find(program.object_aliases[i].target);
    if(target == labels.end()) throw std::runtime_error(
      "native alias has undefined target: " + program.object_aliases[i].target);
    const EncodedLabelLocation location = target->second;
    if(location.text) text.labels[alias] = location.offset;
    else data_sections[location.section].labels[alias] = location.offset;
    labels[alias] = location;
  }
}

struct HostSection
{
  std::string name;
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
    lowir_model::SymbolRole role, const std::string & name,
    std::uint32_t type, std::vector<HostRelocation> & relocations)
{
  HostSection section;
  section.name = name;
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
    relocation.target = function.name;
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

std::string host_symbol_spelling(const std::string & raw);

HostSection make_host_lsda(
    std::vector<HostFunctionLayout> & functions,
    const EncodedSection & text,
    const std::unordered_map<std::string, std::string> & declarations,
    std::vector<HostRelocation> & relocations)
{
  HostSection section;
  section.name = ".gcc_except_table";
  section.flags = 2;
  section.alignment = 4;
  for(std::size_t i = 0; i < functions.size(); ++i) {
    HostFunctionLayout & function = functions[i];
    if(function.call_sites.empty()) continue;
    function.lsda_offset = section.bytes.size();
    std::vector<unsigned char> call_table;
    std::vector<unsigned char> actions;
    std::map<std::string, std::size_t> action_offsets;
    std::map<long long, std::string> type_symbols;
    std::map<std::string, long long> selectors_by_type;
    std::map<std::vector<std::string>, long long> filter_selectors;
    std::vector<unsigned char> exception_specs;
    long long max_selector = 0;
    for(std::map<std::string,
          std::vector<mir_model::MirHostEhClause> >::const_iterator clauses =
          function.clauses.begin(); clauses != function.clauses.end();
        ++clauses) {
      const std::vector<mir_model::MirHostEhClause> & list = clauses->second;
      for(std::size_t clause = 0; clause < list.size(); ++clause) {
        if(list[clause].kind != mir_model::MirHostEhClause::HC_CATCH) continue;
        max_selector = std::max(max_selector, list[clause].selector);
        type_symbols[list[clause].selector] = list[clause].catch_all ?
          std::string() : list[clause].type_symbol;
        if(!list[clause].catch_all)
          selectors_by_type[list[clause].type_symbol] = list[clause].selector;
      }
    }
    for(std::map<std::string,
          std::vector<mir_model::MirHostEhClause> >::const_iterator clauses =
          function.clauses.begin(); clauses != function.clauses.end();
        ++clauses) {
      const std::vector<mir_model::MirHostEhClause> & list = clauses->second;
      for(std::size_t clause = 0; clause < list.size(); ++clause) {
        if(list[clause].kind != mir_model::MirHostEhClause::HC_FILTER)
          continue;
        if(filter_selectors.count(list[clause].filter_type_symbols)) continue;
        const long long selector = -static_cast<long long>(
          exception_specs.size() + 1);
        filter_selectors[list[clause].filter_type_symbols] = selector;
        for(std::size_t type = 0;
            type < list[clause].filter_type_symbols.size(); ++type) {
          const std::string & symbol =
            list[clause].filter_type_symbols[type];
          std::map<std::string, long long>::const_iterator selected =
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
    for(std::map<std::string,
          std::vector<mir_model::MirHostEhClause> >::const_iterator clauses =
          function.clauses.begin(); clauses != function.clauses.end();
        ++clauses) {
      const std::vector<mir_model::MirHostEhClause> & list = clauses->second;
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
      action_offsets[clauses->first] = next_action + 1;
    }

    std::vector<HostFunctionLayout::CallSite> sites = function.call_sites;
    std::sort(sites.begin(), sites.end(),
      [](const HostFunctionLayout::CallSite & left,
         const HostFunctionLayout::CallSite & right) {
        return left.start < right.start;
      });
    std::size_t cursor = 0;
    for(std::size_t site = 0; site < sites.size(); ++site) {
      if(sites[site].start > cursor) {
        append_uleb128(call_table, cursor);
        append_uleb128(call_table, sites[site].start - cursor);
        append_uleb128(call_table, 0);
        append_uleb128(call_table, 0);
      } else if(site == 0 && cursor == 0 && sites[site].start == 0) {
        append_uleb128(call_table, 0);
        append_uleb128(call_table, 0);
        append_uleb128(call_table, 0);
        append_uleb128(call_table, 0);
      }
      const std::string landing_name = function.internal_symbol + "::" +
        sites[site].landing_pad;
      const std::unordered_map<std::string, std::size_t>::const_iterator landing =
        text.labels.find(landing_name);
      if(landing == text.labels.end())
        throw std::logic_error("host EH landing pad has no encoded label");
      append_uleb128(call_table, sites[site].start);
      append_uleb128(call_table, sites[site].length);
      append_uleb128(call_table, landing->second - function.offset);
      const std::map<std::string, std::size_t>::const_iterator action =
        action_offsets.find(sites[site].landing_pad);
      append_uleb128(call_table,
        action == action_offsets.end() ? 0 : action->second);
      cursor = std::max(cursor, sites[site].start + sites[site].length);
    }
    if(cursor < function.size) {
      append_uleb128(call_table, cursor);
      append_uleb128(call_table, function.size - cursor);
      append_uleb128(call_table, 0);
      append_uleb128(call_table, 0);
    }

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
        const std::map<long long, std::string>::const_iterator type =
          type_symbols.find(selector);
        const std::string symbol = type == type_symbols.end() ?
          std::string() : type->second;
        if(!symbol.empty()) {
          HostRelocation relocation;
          relocation.kind = HostRelocation::HR_PC32;
          relocation.offset = section.bytes.size();
          const std::unordered_map<std::string, std::string>::const_iterator named =
            declarations.find(symbol);
          relocation.target = "DW.ref." + (named == declarations.end() ?
            host_symbol_spelling(symbol) : named->second);
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
    personality.target = "DW.ref.__gxx_personality_v0";
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
  section.name = ".eh_frame";
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
      lsda.offset = section.bytes.size();
      lsda.target = ".gcc_except_table";
      lsda.addend = static_cast<long long>(function.lsda_offset);
      relocations.push_back(lsda);
      append_little(section.bytes, 0, 4);
    }
    append_cfi_advance(section.bytes, 1);
    section.bytes.push_back(0x0e);
    append_uleb128(section.bytes, 16);
    section.bytes.push_back(0x80 | dwarf_register(XR_RBP));
    append_uleb128(section.bytes, 2);
    append_cfi_advance(section.bytes, 3);
    section.bytes.push_back(0x0d);
    append_uleb128(section.bytes, dwarf_register(XR_RBP));
    for(std::size_t saved = 0; saved < function.callee_saved_regs.size(); ++saved) {
      const X64Register reg = function.callee_saved_regs[saved];
      append_cfi_advance(section.bytes, reg >= XR_R8 ? 2 : 1);
      section.bytes.push_back(static_cast<unsigned char>(
        0x80 | dwarf_register(reg)));
      append_uleb128(section.bytes, 3 + saved);
    }
    while((section.bytes.size() - fde_start) % 4) section.bytes.push_back(0);
    put_little(section.bytes, fde_start,
               section.bytes.size() - fde_start - 4, 4);
    HostRelocation relocation;
    relocation.kind = HostRelocation::HR_PC32;
    relocation.offset = initial_location;
    relocation.target = ".text";
    relocation.addend = static_cast<long long>(function.offset);
    relocations.push_back(relocation);
  }
  return section;
}

std::string host_symbol_spelling(const std::string & raw)
{
  return !raw.empty() && raw[0] == '@' ? raw.substr(1) : raw;
}

std::string host_runtime_object_symbol(
    const lowir_model::SymbolMetadata & metadata)
{
  static const char * const memory_symbols[] = {
    "bzero", "memchr", "memcmp", "memcpy", "memmove", "memset", "strchr",
    "strcmp", "strlen", "vsnprintf"
  };
  const std::string prefix = "cppgm_builtin_";
  if(metadata.object_symbol.compare(0, prefix.size(), prefix) == 0) {
    const std::string suffix = metadata.object_symbol.substr(prefix.size());
    if(suffix == "unreachable") return "abort";
    for(std::size_t i = 0;
        i < sizeof(memory_symbols) / sizeof(memory_symbols[0]); ++i)
      if(suffix == memory_symbols[i]) return suffix;
  }
  if(metadata.role == lowir_model::SR_ALLOCATE_MEMORY) {
    if(metadata.object_symbol == "cppgm_builtin_operator_new") return "_Znwm";
    if(metadata.object_symbol == "cppgm_builtin_operator_new_array")
      return "_Znam";
  }
  if(metadata.role == lowir_model::SR_FREE_MEMORY) {
    if(metadata.object_symbol == "cppgm_builtin_operator_delete")
      return "_ZdlPv";
    if(metadata.object_symbol == "cppgm_builtin_operator_delete_array")
      return "_ZdaPv";
  }
  return metadata.object_symbol;
}

std::unordered_map<std::string, std::string> declaration_object_symbols(
    const lowir_model::LowirProgram & program)
{
  std::unordered_map<std::string, std::string> result;
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i)
    if(!program.exported_symbols[i].object_symbol.empty())
      result[program.exported_symbols[i].internal_symbol] =
        program.exported_symbols[i].object_symbol;
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    if(!program.function_declarations[i].metadata.object_symbol.empty())
      result[program.function_declarations[i].name] =
        host_runtime_object_symbol(
          program.function_declarations[i].metadata);
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    if(!program.global_declarations[i].metadata.object_symbol.empty())
      result[program.global_declarations[i].name] =
        program.global_declarations[i].metadata.object_symbol;
  return result;
}

std::string relocation_target(
    const std::string & raw,
    const EncodedLabelIndex & labels,
    const std::unordered_map<std::string, std::string> & declarations)
{
  const std::unordered_map<std::string, std::string>::const_iterator found =
    declarations.find(raw);
  if(found != declarations.end()) return found->second;
  if(labels.count(raw)) return raw;
  return host_symbol_spelling(raw);
}

std::vector<HostRelocation> host_relocations(
    EncodedSection & source,
    const EncodedLabelIndex & labels,
    const std::unordered_map<std::string, std::string> & declarations)
{
  std::vector<HostRelocation> result;
  result.reserve(source.fixups.size());
  for(std::size_t i = 0; i < source.fixups.size(); ++i) {
    const EncodedFixup & fixup = source.fixups[i];
    HostRelocation relocation;
    if(fixup.kind == EncodedFixup::EF_TLS_OFFSET32) {
      relocation.kind = HostRelocation::HR_TPOFF32;
    } else if(fixup.kind == EncodedFixup::EF_ADDRESS32) {
      const bool local_address = fixup.address_binding ==
        mir_model::MirOperand::ADDRESS_LOCAL;
      relocation.kind = local_address ? HostRelocation::HR_PC32 :
        HostRelocation::HR_GOTPCRELX;
      if(!local_address) {
        if(fixup.offset < 2 || fixup.offset > source.bytes.size() ||
           source.bytes[fixup.offset - 2] != 0x8d)
          throw std::logic_error("symbol-address fixup is not RIP-relative LEA");
        source.bytes[fixup.offset - 2] = 0x8b;
      }
    } else {
      relocation.kind = fixup.kind == EncodedFixup::EF_ABSOLUTE64 ?
        HostRelocation::HR_ABSOLUTE64 : HostRelocation::HR_PLT32;
    }
    relocation.offset = fixup.offset;
    relocation.target = relocation_target(
      fixup.target, labels, declarations);
    relocation.addend = fixup.kind == EncodedFixup::EF_RELATIVE32 ||
      fixup.kind == EncodedFixup::EF_ADDRESS32 ?
      fixup.addend - 4 : fixup.addend;
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
    if(prior.section == 0 && symbol.section != 0) prior = symbol;
    return;
  }
  index[symbol.name] = symbols.size();
  symbols.push_back(symbol);
}

std::uint64_t function_size_at(
    const std::unordered_map<std::size_t, std::uint64_t> & function_sizes,
    std::size_t offset)
{
  const std::unordered_map<std::size_t, std::uint64_t>::const_iterator found =
    function_sizes.find(offset);
  return found == function_sizes.end() ? 0 : found->second;
}

void collect_host_symbols(
    const lowir_model::LowirProgram & program,
    const EncodedSection & text,
    std::uint16_t text_section_index,
    const std::vector<EncodedSection> & data_sections,
    const std::vector<std::uint16_t> & data_section_indexes,
    const EncodedLabelIndex & encoded_labels,
    const std::vector<HostFunctionLayout> & functions,
    const std::vector<HostRelocation> & text_relocations,
    const std::vector<std::vector<HostRelocation> > & data_relocations,
    const std::vector<HostRelocation> & lsda_relocations,
    const std::vector<HostRelocation> & eh_relocations,
    std::vector<HostSymbol> & locals,
    std::vector<HostSymbol> & globals)
{
  std::unordered_map<std::string, std::size_t> local_index;
  std::unordered_map<std::string, std::size_t> global_index;
  std::unordered_map<std::size_t, std::uint64_t> function_sizes;
  function_sizes.reserve(functions.size());
  for(std::size_t i = 0; i < functions.size(); ++i)
    function_sizes[functions[i].offset] = functions[i].size;
  for(std::unordered_map<std::string, std::size_t>::const_iterator it =
        text.labels.begin(); it != text.labels.end(); ++it) {
    HostSymbol symbol;
    symbol.name = it->first;
    symbol.section = text_section_index;
    symbol.value = it->second;
    symbol.size = function_size_at(function_sizes, it->second);
    symbol.type = symbol.size ? 2 : 0;
    add_unique_symbol(locals, local_index, symbol);
  }
  for(std::size_t section = 0; section < data_sections.size(); ++section) {
    for(std::unordered_map<std::string, std::size_t>::const_iterator it =
          data_sections[section].labels.begin();
        it != data_sections[section].labels.end(); ++it) {
      HostSymbol symbol;
      symbol.name = it->first;
      symbol.section = data_section_indexes[section];
      symbol.value = it->second;
      symbol.type = (data_sections[section].flags & 0x400) ? 6 : 1;
      add_unique_symbol(locals, local_index, symbol);
    }
  }

  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i) {
    const ir_model::ExportedSymbol & exported = program.exported_symbols[i];
    if(exported.object_symbol.empty()) continue;
    const std::string object_label = native_object_symbol(exported.object_symbol);
    const EncodedLabelIndex::const_iterator location =
      encoded_labels.find(object_label);
    if(location == encoded_labels.end()) continue;
    const std::uint16_t section = location->second.text ? text_section_index :
      data_section_indexes[location->second.section];
    const std::size_t value = location->second.offset;
    const unsigned type = location->second.text ? 2 :
      (data_sections[location->second.section].flags & 0x400) ? 6 : 1;
    HostSymbol symbol;
    symbol.name = exported.object_symbol;
    symbol.section = section;
    symbol.value = value;
    symbol.type = type;
    symbol.size = symbol.type == 2 ? function_size_at(function_sizes, value) : 0;
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
    const std::unordered_map<std::string, std::size_t>::const_iterator at =
      text.labels.find(program.functions[i].name);
    if(at == text.labels.end())
      throw std::logic_error("entry function has no encoded symbol");
    HostSymbol symbol;
    symbol.name = "main";
    symbol.binding = 1;
    symbol.type = 2;
    symbol.section = text_section_index;
    symbol.value = at->second;
    symbol.size = function_size_at(function_sizes, at->second);
    add_unique_symbol(globals, global_index, symbol);
  }

  std::unordered_map<std::string, bool> defined;
  for(std::size_t i = 0; i < locals.size(); ++i) defined[locals[i].name] = true;
  for(std::size_t i = 0; i < globals.size(); ++i) defined[globals[i].name] = true;
  std::unordered_set<std::string> declared_tls_symbols;
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    if(program.global_declarations[i].storage == lowir_model::GSM_THREAD_LOCAL &&
       !program.global_declarations[i].metadata.object_symbol.empty())
      declared_tls_symbols.insert(
        program.global_declarations[i].metadata.object_symbol);
  std::unordered_set<std::string> section_symbols;
  section_symbols.insert(text.name);
  section_symbols.insert(".gcc_except_table");
  section_symbols.insert(".eh_frame");
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    section_symbols.insert(data_sections[i].name);
  std::vector<const std::vector<HostRelocation> *> relocation_groups;
  relocation_groups.push_back(&text_relocations);
  for(std::size_t i = 0; i < data_relocations.size(); ++i)
    relocation_groups.push_back(&data_relocations[i]);
  relocation_groups.push_back(&lsda_relocations);
  relocation_groups.push_back(&eh_relocations);
  for(std::size_t group = 0; group < relocation_groups.size(); ++group) {
    const std::vector<HostRelocation> & relocations = *relocation_groups[group];
    for(std::size_t i = 0; i < relocations.size(); ++i) {
      if(section_symbols.count(relocations[i].target) ||
         defined.count(relocations[i].target)) continue;
      HostSymbol symbol;
      symbol.name = relocations[i].target;
      symbol.binding = 1;
      if(declared_tls_symbols.count(symbol.name)) symbol.type = 6;
      add_unique_symbol(globals, global_index, symbol);
      defined[symbol.name] = true;
    }
  }
  const auto stable_symbol_order = [](const HostSymbol & left,
                                      const HostSymbol & right) {
    if(left.name != right.name) return left.name < right.name;
    if(left.section != right.section) return left.section < right.section;
    return left.value < right.value;
  };
  std::sort(locals.begin(), locals.end(), stable_symbol_order);
  std::sort(globals.begin(), globals.end(), stable_symbol_order);
}

std::unordered_set<std::string> host_external_global_definitions(
    const lowir_model::LowirProgram & source,
    const mir_model::MirProgram & program)
{
  std::unordered_set<std::string> external_objects;
  for(std::size_t i = 0; i < source.global_declarations.size(); ++i)
    if(source.global_declarations[i].metadata.role ==
         lowir_model::SR_RTTI_DATA &&
       !source.global_declarations[i].metadata.object_symbol.empty())
      external_objects.insert(
        source.global_declarations[i].metadata.object_symbol);
  std::unordered_set<std::string> suppressed;
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const mir_model::MirGlobalDefinition & global = program.globals[i];
    if(!external_objects.count(global.object_symbol)) continue;
    suppressed.insert(global.name);
    for(std::size_t j = 0; j < global.data_items.size(); ++j)
      if(global.data_items[j].kind ==
           mir_model::MirGlobalDefinition::DataItem::ITEM_ADDR)
        suppressed.insert(global.data_items[j].symbol);
  }
  return suppressed;
}

std::size_t add_string(std::vector<unsigned char> & strings,
                       const std::string & value)
{
  const std::size_t offset = strings.size();
  strings.insert(strings.end(), value.begin(), value.end());
  strings.push_back(0);
  return offset;
}

std::vector<unsigned char> make_symbol_table(
    const std::vector<HostSymbol> & locals,
    const std::vector<HostSymbol> & globals,
    const std::vector<std::pair<std::string, std::uint16_t> > & section_symbols,
    std::vector<unsigned char> & strings,
    std::unordered_map<std::string, std::size_t> & indexes)
{
  std::vector<unsigned char> table(24, 0);
  for(std::size_t i = 0; i < section_symbols.size(); ++i) {
    const std::size_t index = table.size() / 24;
    indexes[section_symbols[i].first] = index;
    append_little(table, 0, 4);
    table.push_back(3);
    table.push_back(0);
    append_little(table, section_symbols[i].second, 2);
    append_little(table, 0, 8);
    append_little(table, 0, 8);
  }
  for(unsigned group = 0; group < 2; ++group) {
    const std::vector<HostSymbol> & symbols = group == 0 ? locals : globals;
    for(std::size_t i = 0; i < symbols.size(); ++i) {
      const HostSymbol & symbol = symbols[i];
      const std::size_t index = table.size() / 24;
      if(!indexes.count(symbol.name)) indexes[symbol.name] = index;
      append_little(table, add_string(strings, symbol.name), 4);
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
    const std::unordered_map<std::string, std::size_t> & symbols)
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
    const std::unordered_map<std::string, std::size_t>::const_iterator symbol =
      symbols.find(relocation.target);
    if(symbol == symbols.end())
      throw std::logic_error("ELF relocation has no symbol: " +
                             relocation.target);
    const unsigned type = relocation.kind == HostRelocation::HR_ABSOLUTE64 ? 1 :
      relocation.kind == HostRelocation::HR_PC32 ? 2 :
      relocation.kind == HostRelocation::HR_GOTPCRELX ? 42 :
      relocation.kind == HostRelocation::HR_TPOFF32 ? 23 : 4;
    append_little(table, relocation.offset, 8);
    append_little(table, (static_cast<std::uint64_t>(symbol->second) << 32) |
                         type, 8);
    append_little(table, static_cast<std::uint64_t>(relocation.addend), 8);
  }
  return table;
}

std::vector<unsigned char> make_linux_relocatable_image(
    const lowir_model::LowirProgram & program,
    EncodedSection mutable_text,
    std::vector<EncodedSection> mutable_data,
    std::vector<HostFunctionLayout> & functions,
    const std::vector<unsigned char> & compiler_payload,
    std::size_t & relocation_count)
{
  EncodedLabelIndex encoded_labels =
    index_encoded_labels(mutable_text, mutable_data);
  publish_object_aliases(program, mutable_text, mutable_data, encoded_labels);
  const std::unordered_map<std::string, std::string> declarations =
    declaration_object_symbols(program);
  const std::vector<HostRelocation> text_relocations = host_relocations(
    mutable_text, encoded_labels, declarations);
  std::vector<std::vector<HostRelocation> > data_relocations(
    mutable_data.size());
  for(std::size_t i = 0; i < mutable_data.size(); ++i)
    data_relocations[i] = host_relocations(
      mutable_data[i], encoded_labels, declarations);
  std::vector<HostRelocation> lsda_relocations;
  HostSection lsda = make_host_lsda(
    functions, mutable_text, declarations, lsda_relocations);
  std::vector<HostRelocation> eh_relocations;
  HostSection eh = make_host_eh_frame(functions, eh_relocations);

  struct PendingRelocations
  {
    std::uint16_t section;
    const std::vector<HostRelocation> * relocations;
  };
  std::vector<HostSection> sections(1);
  std::vector<PendingRelocations> pending_relocations;
  const auto append_section = [&sections](const HostSection & section)
    -> std::uint16_t {
      if(sections.size() >= 0xffff)
        throw std::runtime_error("too many ELF sections");
      sections.push_back(section);
      return static_cast<std::uint16_t>(sections.size() - 1);
    };
  const auto append_relocations =
    [&append_section, &pending_relocations](
      const std::string & name, std::uint16_t target,
      const std::vector<HostRelocation> & relocations) {
      if(relocations.empty()) return;
      HostSection section;
      section.name = ".rela" + name;
      section.type = 4;
      section.alignment = 8;
      section.entry_size = 24;
      section.info = target;
      PendingRelocations pending;
      pending.section = append_section(section);
      pending.relocations = &relocations;
      pending_relocations.push_back(pending);
    };

  HostSection text_section;
  text_section.name = mutable_text.name;
  text_section.flags = mutable_text.flags;
  text_section.alignment = mutable_text.alignment;
  text_section.bytes = mutable_text.bytes;
  const std::uint16_t text_index = append_section(text_section);
  append_relocations(text_section.name, text_index, text_relocations);

  std::vector<std::uint16_t> data_indexes;
  data_indexes.reserve(mutable_data.size());
  for(std::size_t i = 0; i < mutable_data.size(); ++i) {
    HostSection section;
    section.name = mutable_data[i].name;
    section.flags = mutable_data[i].flags;
    section.alignment = mutable_data[i].alignment;
    section.bytes = mutable_data[i].bytes;
    const std::uint16_t index = append_section(section);
    data_indexes.push_back(index);
    append_relocations(section.name, index, data_relocations[i]);
  }

  std::vector<HostRelocation> init_array_relocations;
  HostSection init_array = make_host_lifecycle_array(
    program, lowir_model::SR_INIT, ".init_array", 14,
    init_array_relocations);
  std::uint16_t init_array_index = 0;
  if(!init_array.bytes.empty()) {
    init_array_index = append_section(init_array);
    append_relocations(init_array.name, init_array_index,
                       init_array_relocations);
  }
  std::vector<HostRelocation> fini_array_relocations;
  HostSection fini_array = make_host_lifecycle_array(
    program, lowir_model::SR_FINI, ".fini_array", 15,
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
  const std::uint16_t eh_index = append_section(eh);
  append_relocations(eh.name, eh_index, eh_relocations);

  std::vector<HostSymbol> locals;
  std::vector<HostSymbol> globals;
  collect_host_symbols(program, mutable_text, text_index,
                       mutable_data, data_indexes, encoded_labels,
                       functions, text_relocations,
                       data_relocations, lsda_relocations, eh_relocations,
                       locals, globals);

  std::vector<std::pair<std::string, std::uint16_t> > section_symbols;
  section_symbols.push_back(std::make_pair(text_section.name, text_index));
  for(std::size_t i = 0; i < mutable_data.size(); ++i)
    section_symbols.push_back(std::make_pair(mutable_data[i].name,
                                             data_indexes[i]));
  if(init_array_index != 0)
    section_symbols.push_back(std::make_pair(init_array.name,
                                             init_array_index));
  if(fini_array_index != 0)
    section_symbols.push_back(std::make_pair(fini_array.name,
                                             fini_array_index));
  if(lsda_index != 0)
    section_symbols.push_back(std::make_pair(lsda.name, lsda_index));
  section_symbols.push_back(std::make_pair(eh.name, eh_index));

  std::vector<unsigned char> strings(1, 0);
  std::unordered_map<std::string, std::size_t> symbol_indexes;
  std::vector<unsigned char> symbol_table = make_symbol_table(
    locals, globals, section_symbols, strings, symbol_indexes);

  HostSection note;
  note.name = ".note.GNU-stack";
  append_section(note);
  HostSection payload;
  payload.name = ".cppgm_object";
  payload.bytes = compiler_payload;
  append_section(payload);
  HostSection symtab;
  symtab.name = ".symtab";
  symtab.type = 2;
  symtab.alignment = 8;
  symtab.entry_size = 24;
  symtab.info = static_cast<std::uint32_t>(
    1 + section_symbols.size() + locals.size());
  symtab.bytes.swap(symbol_table);
  const std::uint16_t symtab_index = append_section(symtab);
  HostSection strtab;
  strtab.name = ".strtab";
  strtab.type = 3;
  strtab.bytes.swap(strings);
  const std::uint16_t strtab_index = append_section(strtab);
  sections[symtab_index].link = strtab_index;
  HostSection shstrtab;
  shstrtab.name = ".shstrtab";
  shstrtab.type = 3;
  shstrtab.bytes.push_back(0);
  const std::uint16_t shstrtab_index = append_section(shstrtab);

  for(std::size_t i = 0; i < pending_relocations.size(); ++i) {
    HostSection & section = sections[pending_relocations[i].section];
    section.link = symtab_index;
    section.bytes = make_relocation_table(
      *pending_relocations[i].relocations, symbol_indexes);
  }

  // STB_WEAK is the executable coalescing rule for the current monolithic
  // text/data sections.  Also publish one standards-shaped COMDAT group per
  // weak ODR root.  Its tiny SHF_GROUP marker gives object inspectors and the
  // host linker a canonical group signature without incorrectly placing the
  // whole translation-unit .text/.data section in a discardable group.
  const std::size_t first_global_symbol =
    1 + section_symbols.size() + locals.size();
  for(std::size_t i = 0; i < globals.size(); ++i) {
    const HostSymbol & symbol = globals[i];
    if(symbol.binding != 2 || symbol.section == 0) continue;
    HostSection marker;
    marker.name = ".cppgm.odr." + std::to_string(i);
    marker.flags = 0x200;
    const std::size_t marker_index = sections.size();
    sections.push_back(marker);
    HostSection group;
    group.name = ".group";
    group.type = 17;
    group.alignment = 4;
    group.entry_size = 4;
    group.link = symtab_index;
    group.info = static_cast<std::uint32_t>(first_global_symbol + i);
    append_little(group.bytes, 1, 4);
    append_little(group.bytes, marker_index, 4);
    sections.push_back(group);
  }
  for(std::size_t i = 1; i < sections.size(); ++i)
    sections[i].name_offset = add_string(
      sections[shstrtab_index].bytes, sections[i].name);

  std::vector<unsigned char> image(64, 0);
  image[0] = 0x7f;
  image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
  image[4] = 2; image[5] = 1; image[6] = 1;
  put_little(image, 16, 1, 2);
  put_little(image, 18, 62, 2);
  put_little(image, 20, 1, 4);
  put_little(image, 52, 64, 2);
  put_little(image, 58, 64, 2);
  put_little(image, 60, sections.size(), 2);
  put_little(image, 62, shstrtab_index, 2);
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
  relocation_count = text_relocations.size() + lsda_relocations.size() +
    eh_relocations.size();
  for(std::size_t i = 0; i < data_relocations.size(); ++i)
    relocation_count += data_relocations[i].size();
  return image;
}

}  // namespace object_elf_detail
}  // namespace lowir_native
