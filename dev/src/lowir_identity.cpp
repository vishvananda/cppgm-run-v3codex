#include "lowir_model.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace lowir_model {

namespace {

const std::uint32_t kPresentationKindMask = 0xc0000000U;
const std::uint32_t kPresentationPayloadMask = 0x3fffffffU;
const std::uint32_t kPresentationPreserveCopy = 0x40000000U;
const std::uint32_t kPresentationGeneratedValue = 0x80000000U;

}  // namespace

PresentationName::PresentationName() : encoded_(kInvalidCompactId) {}
PresentationName::PresentationName(std::uint32_t encoded) : encoded_(encoded) {}

PresentationName PresentationName::pooled(StringId spelling,
                                           bool preserve_copy)
{
  const std::uint32_t id = spelling;
  if(!spelling.valid() || id > kPresentationPayloadMask)
    throw std::runtime_error("invalid pooled presentation identity");
  return PresentationName(
    id | (preserve_copy ? kPresentationPreserveCopy : 0));
}

PresentationName PresentationName::generated_value(std::uint32_t ordinal)
{
  if(ordinal > kPresentationPayloadMask)
    throw std::runtime_error("too many generated presentation identities");
  return PresentationName(kPresentationGeneratedValue | ordinal);
}

bool PresentationName::valid() const
{
  return encoded_ != kInvalidCompactId;
}

bool PresentationName::generated() const
{
  return valid() &&
    (encoded_ & kPresentationKindMask) == kPresentationGeneratedValue;
}

bool PresentationName::preserves_copy() const
{
  return valid() &&
    (encoded_ & kPresentationKindMask) == kPresentationPreserveCopy;
}

StringId PresentationName::spelling() const
{
  if(!valid() || generated())
    throw std::logic_error("presentation identity has no pooled spelling");
  return StringId(encoded_ & kPresentationPayloadMask);
}

std::uint32_t PresentationName::generated_ordinal() const
{
  if(!generated())
    throw std::logic_error("presentation identity has no generated ordinal");
  return encoded_ & kPresentationPayloadMask;
}

namespace {

std::string generated_literal_spelling(const Operand & operand)
{
  if(operand.kind == Operand::OP_INTEGER)
    return std::to_string(operand.int_value);
  if(operand.kind != Operand::OP_FLOAT)
    throw std::logic_error("named LowIR operand has no presentation identity");
  if(std::isinf(operand.float_value))
    return operand.float_value < 0 ? "-inf" : "inf";
  if(std::isnan(operand.float_value)) return "nan";
  std::ostringstream text;
  text.precision(20);
  text << operand.float_value;
  if(operand.literal_type.kind == LTK_F32) text << 'f';
  else if(operand.literal_type.kind == LTK_F80) text << 'L';
  return text.str();
}

void intern_operand_literal(Program & program, Operand & operand)
{
  if(operand.kind != Operand::OP_INTEGER &&
     operand.kind != Operand::OP_FLOAT) return;
  if(!operand.has_spelling) {
    operand.literal = program.strings.intern(
      generated_literal_spelling(operand));
    operand.has_spelling = true;
  }
}

void intern_instruction_literals(Program & program, Instruction & instruction)
{
  intern_operand_literal(program, instruction.first);
  intern_operand_literal(program, instruction.second);
  intern_operand_literal(program, instruction.third);
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    intern_operand_literal(program, instruction.args[i]);
}

std::size_t hash_range(const std::string & text, std::size_t first,
                       std::size_t count)
{
  std::size_t value = sizeof(std::size_t) == 8 ?
    static_cast<std::size_t>(1469598103934665603ULL) :
    static_cast<std::size_t>(2166136261U);
  const std::size_t prime = sizeof(std::size_t) == 8 ?
    static_cast<std::size_t>(1099511628211ULL) :
    static_cast<std::size_t>(16777619U);
  for(std::size_t i = first; i < first + count; ++i) {
    value ^= static_cast<unsigned char>(text[i]);
    value *= prime;
  }
  return value;
}

}  // namespace

bool parse_lowir_integer_literal(const std::string & text,
                                 long long * low, std::uint64_t * high)
{
  if(!low || !high) return false;
  if(text == "nullptr") {
    *low = 0;
    *high = 0;
    return true;
  }
  if(text.empty()) return false;
  std::size_t at = 0;
  bool negative = false;
  if(text[at] == '+' || text[at] == '-') {
    negative = text[at] == '-';
    if(++at == text.size()) return false;
  }
  unsigned base = 10;
  if(at + 2 <= text.size() && text[at] == '0' &&
     (text[at + 1] == 'x' || text[at + 1] == 'X')) {
    base = 16;
    at += 2;
  } else if(at + 1 < text.size() && text[at] == '0') {
    base = 8;
    ++at;
  }
  typedef unsigned __int128 WideUnsigned;
  WideUnsigned value = 0;
  for(; at < text.size(); ++at) {
    unsigned digit;
    if(text[at] >= '0' && text[at] <= '9')
      digit = static_cast<unsigned>(text[at] - '0');
    else if(text[at] >= 'a' && text[at] <= 'f')
      digit = static_cast<unsigned>(text[at] - 'a' + 10);
    else if(text[at] >= 'A' && text[at] <= 'F')
      digit = static_cast<unsigned>(text[at] - 'A' + 10);
    else return false;
    if(digit >= base) return false;
    value = value * base + digit;
  }
  if(negative) value = -value;
  *low = static_cast<long long>(static_cast<std::uint64_t>(value));
  *high = static_cast<std::uint64_t>(value >> 64);
  return true;
}

StringPool::StringPool() : slots_(32, 0), spelling_bytes_(0)
{
  strings_.push_back(std::string());
}

StringId StringPool::intern(const std::string & text, StringPoolStats * stats)
{
  return intern_range(text, 0, text.size(), stats);
}

StringId StringPool::intern_range(const std::string & text, std::size_t first,
                                  std::size_t count, StringPoolStats * stats)
{
  if(first > text.size() || count > text.size() - first)
    throw std::logic_error("invalid LowIR string-pool range");
  if(stats) {
    ++stats->intern_calls;
    stats->hash_bytes += count;
  }
  if((strings_.size() + 1) * 10 > slots_.size() * 7)
    rehash(slots_.size() * 2);
  const std::size_t mask = slots_.size() - 1;
  std::size_t slot = hash_range(text, first, count) & mask;
  while(slots_[slot] != 0) {
    if(stats) ++stats->slot_probes;
    const std::uint32_t id = slots_[slot];
    if(strings_[id].size() == count &&
       text.compare(first, count, strings_[id]) == 0) {
      if(stats) ++stats->intern_hits;
      return StringId(id);
    }
    slot = (slot + 1) & mask;
  }
  if(strings_.size() >= kInvalidCompactId)
    throw std::runtime_error("too many pooled LowIR strings");
  const std::uint32_t id = static_cast<std::uint32_t>(strings_.size());
  strings_.push_back(text.substr(first, count));
  spelling_bytes_ += count;
  slots_[slot] = id;
  if(stats) ++stats->intern_misses;
  return StringId(id);
}

void StringPool::reserve(std::size_t expected_strings)
{
  if(expected_strings >= kInvalidCompactId)
    expected_strings = kInvalidCompactId - 1;
  strings_.reserve(expected_strings + 1);
  std::size_t capacity = slots_.size();
  while(expected_strings + 1 > capacity * 7 / 10) capacity *= 2;
  if(capacity > slots_.size()) rehash(capacity);
}

const std::string & StringPool::get(StringId id) const
{
  const std::uint32_t index = id;
  if(index >= strings_.size())
    throw std::logic_error("invalid pooled LowIR string identity " +
      std::to_string(index) + " for " + std::to_string(strings_.size()) +
      " entries");
  return strings_[index];
}

std::size_t StringPool::size() const { return strings_.size() - 1; }
std::size_t StringPool::spelling_bytes() const { return spelling_bytes_; }

std::size_t StringPool::storage_bytes() const
{
  std::size_t bytes = strings_.capacity() * sizeof(std::string) +
    slots_.capacity() * sizeof(std::uint32_t);
  for(std::size_t i = 1; i < strings_.size(); ++i)
    bytes += strings_[i].capacity();
  return bytes;
}

void StringPool::rehash(std::size_t capacity)
{
  std::vector<std::uint32_t> replacement(capacity, 0);
  const std::size_t mask = capacity - 1;
  for(std::uint32_t id = 1; id < strings_.size(); ++id) {
    std::size_t slot = hash_range(strings_[id], 0, strings_[id].size()) & mask;
    while(replacement[slot] != 0) slot = (slot + 1) & mask;
    replacement[slot] = id;
  }
  slots_.swap(replacement);
}

BlockId allocate_lowir_block_id(Function & function, StringId label)
{
  if(function.next_block_id == kInvalidCompactId)
    throw std::runtime_error("too many LowIR blocks");
  const BlockId result(function.next_block_id++);
  function.block_labels.push_back(label);
  return result;
}

const std::string & lowir_block_label(const StringPool & strings,
                                      const Function & function,
                                      BlockId block)
{
  const std::uint32_t id = block;
  if(id >= function.block_labels.size())
    throw std::logic_error("invalid LowIR block identity");
  return strings.get(function.block_labels[id]);
}

SlotId append_lowir_slot(Function & function, StringId name,
                         const LowType & type)
{
  if(function.slot_names.size() == kInvalidCompactId)
    throw std::runtime_error("too many LowIR slots");
  const SlotId result(static_cast<std::uint32_t>(function.slot_names.size()));
  function.slot_names.push_back(name);
  function.slot_types.push_back(type);
  function.slot_parameter_values.push_back(ValueId());
  function.slots.push_back(result);
  return result;
}

const std::string & lowir_slot_name(const StringPool & strings,
                                    const Function & function, SlotId slot)
{
  const std::uint32_t id = slot;
  if(id >= function.slot_names.size())
    throw std::logic_error("invalid LowIR slot identity");
  return strings.get(function.slot_names[id]);
}

const LowType & lowir_slot_type(const Function & function, SlotId slot)
{
  const std::uint32_t id = slot;
  if(id >= function.slot_types.size())
    throw std::logic_error("invalid LowIR slot identity");
  return function.slot_types[id];
}

namespace {

ValueId append_value_identity(Function & function, PresentationName name,
                              const LowType & type)
{
  if(function.value_names.size() == kInvalidCompactId)
    throw std::runtime_error("too many LowIR values");
  const ValueId result(static_cast<std::uint32_t>(function.value_names.size()));
  function.value_names.push_back(name);
  function.value_types.push_back(type);
  return result;
}

}  // namespace

ValueId append_lowir_value(Function & function, StringId name,
                           const LowType & type, bool preserve_copy)
{
  if(!name.valid()) throw std::logic_error("empty named LowIR value");
  return append_value_identity(
    function, PresentationName::pooled(name, preserve_copy), type);
}

ValueId append_lowir_generated_value(Function & function,
                                     std::uint32_t ordinal,
                                     const LowType & type)
{
  return append_value_identity(
    function, PresentationName::generated_value(ordinal), type);
}

std::string lowir_value_name(const StringPool & strings,
                             const Function & function, ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_names.size())
    throw std::logic_error("invalid LowIR value identity");
  const PresentationName name = function.value_names[id];
  if(!name.valid())
    throw std::logic_error("LowIR value has no presentation identity");
  if(!name.generated()) return strings.get(name.spelling());
  return "%t" + std::to_string(name.generated_ordinal());
}

const LowType & lowir_value_type(const Function & function, ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_types.size())
    throw std::logic_error("invalid LowIR value type identity");
  return function.value_types[id];
}

bool lowir_value_preserves_copy(const Function & function, ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_names.size())
    throw std::logic_error("invalid LowIR value identity");
  return function.value_names[id].preserves_copy();
}

PresentationName lowir_value_presentation(const Function & function,
                                          ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_names.size())
    throw std::logic_error("invalid LowIR value identity");
  return function.value_names[id];
}

const std::string & lowir_parameter_name(const Program & program,
                                         const Parameter & parameter)
{
  if(!parameter.name.valid())
    throw std::logic_error("LowIR parameter has no presentation identity");
  return program.strings.get(parameter.name);
}

SymbolId append_lowir_symbol(Program & program, const std::string & name)
{
  return append_lowir_symbol(program, program.strings.intern(name));
}

SymbolId append_lowir_symbol(Program & program, StringId name)
{
  if(program.symbol_names.size() == kInvalidCompactId)
    throw std::runtime_error("too many LowIR symbols");
  if(!name.valid()) throw std::logic_error("empty LowIR symbol presentation");
  const SymbolId result(
    static_cast<std::uint32_t>(program.symbol_names.size()));
  program.symbol_names.push_back(name);
  return result;
}

StringId lowir_symbol_spelling(const Program & program, SymbolId symbol)
{
  const std::uint32_t id = symbol;
  if(id >= program.symbol_names.size())
    throw std::logic_error("invalid LowIR symbol identity");
  return program.symbol_names[id];
}

const std::string & lowir_symbol_name(const Program & program, SymbolId symbol)
{
  return program.strings.get(lowir_symbol_spelling(program, symbol));
}

void resolve_lowir_function_operands(Function & function,
                                     const StringPool & strings)
{
  std::unordered_map<std::string, BlockId> blocks;
  blocks.reserve(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    Block & block = function.blocks[i];
    if(!block.id.valid()) block.id = allocate_lowir_block_id(function);
    blocks.emplace(lowir_block_label(strings, function, block.id), block.id);
  }
  std::unordered_map<std::string, SlotId> slots;
  slots.reserve(function.slots.size());
  for(std::size_t i = 0; i < function.slots.size(); ++i)
    slots.emplace(lowir_slot_name(strings, function, function.slots[i]),
                  function.slots[i]);
  std::unordered_map<std::string, ValueId> values;
  values.reserve(function.value_names.size());
  for(std::size_t i = 0; i < function.value_names.size(); ++i) {
    const ValueId value(static_cast<std::uint32_t>(i));
    values.emplace(lowir_value_name(strings, function, value), value);
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    std::vector<Instruction> & instructions = function.blocks[i].instructions;
    for(std::size_t j = 0; j < instructions.size(); ++j) {
      Instruction & instruction = instructions[j];
      Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < 3; ++k) {
        if(!fixed[k]->has_spelling) continue;
        const std::string & spelling = strings.get(fixed[k]->literal);
        if(fixed[k]->kind == Operand::OP_LABEL) {
          const std::unordered_map<std::string, BlockId>::const_iterator found =
            blocks.find(spelling);
          if(found == blocks.end()) throw ParseError("undefined block target");
          fixed[k]->block = found->second;
        } else if(fixed[k]->kind == Operand::OP_SLOT) {
          const std::unordered_map<std::string, SlotId>::const_iterator found =
            slots.find(spelling);
          if(found == slots.end()) throw ParseError("undefined slot operand");
          fixed[k]->slot = found->second;
        } else if(fixed[k]->kind == Operand::OP_TEMP) {
          const std::unordered_map<std::string, ValueId>::const_iterator found =
            values.find(spelling);
          if(found == values.end()) throw ParseError("undefined value operand");
          fixed[k]->value = found->second;
        } else continue;
        fixed[k]->has_spelling = false;
      }
      for(std::size_t a = 0; a < instruction.args.size(); ++a) {
        Operand & operand = instruction.args[a];
        if(!operand.has_spelling) continue;
        const std::string & spelling = strings.get(operand.literal);
        if(operand.kind == Operand::OP_LABEL) {
          const std::unordered_map<std::string, BlockId>::const_iterator found =
            blocks.find(spelling);
          if(found == blocks.end()) throw ParseError("undefined block target");
          operand.block = found->second;
        } else if(operand.kind == Operand::OP_SLOT) {
          const std::unordered_map<std::string, SlotId>::const_iterator found =
            slots.find(spelling);
          if(found == slots.end()) throw ParseError("undefined slot operand");
          operand.slot = found->second;
        } else if(operand.kind == Operand::OP_TEMP) {
          const std::unordered_map<std::string, ValueId>::const_iterator found =
            values.find(spelling);
          if(found == values.end()) throw ParseError("undefined value operand");
          operand.value = found->second;
        } else continue;
        operand.has_spelling = false;
      }
    }
  }
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const SlotId slot = function.slots[s];
    if(lowir_slot_type(function, slot).kind != LTK_OBJECT) continue;
    const std::string & slot_name = lowir_slot_name(strings, function, slot);
    for(std::size_t p = 0; p < function.params.size(); ++p) {
      const std::string & parameter_name =
        strings.get(function.params[p].name);
      if(slot_name.size() > 1 && parameter_name.size() > 1 &&
         slot_name.compare(1, std::string::npos,
                           parameter_name, 1,
                           std::string::npos) == 0 &&
         same_lowir_type(lowir_slot_type(function, slot),
                         function.params[p].type)) {
        function.slot_parameter_values[slot] = function.params[p].value;
        break;
      }
    }
  }
}

void resolve_lowir_program_symbols(Program & program)
{
  // Every spelling has already been interned at the input boundary.  Resolve
  // through that compact identity instead of owning and hashing the text a
  // second time.
  std::vector<SymbolId> symbols(program.strings.size() + 1);
  for(std::size_t i = 0; i < program.symbol_names.size(); ++i) {
    const std::uint32_t spelling = program.symbol_names[i];
    if(spelling >= symbols.size())
      throw std::logic_error("invalid LowIR symbol presentation identity");
    symbols[spelling] = SymbolId(static_cast<std::uint32_t>(i));
  }

  const auto resolve_spelling = [&symbols](StringId spelling,
                                           const char * error) -> SymbolId {
    const std::uint32_t id = spelling;
    if(!spelling.valid() || id >= symbols.size() || !symbols[id].valid())
      throw ParseError(error);
    return symbols[id];
  };

  const auto resolve_tls = [&program, &resolve_spelling](
      SymbolMetadata & metadata) {
    if(!metadata.tls_for_spelling.valid()) return;
    metadata.tls_for_symbol_id = resolve_spelling(
      metadata.tls_for_spelling, "undefined TLS target");
    metadata.tls_for_spelling = StringId();
  };
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    resolve_tls(program.global_declarations[i].metadata);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    resolve_tls(program.function_declarations[i].metadata);
  for(std::size_t i = 0; i < program.globals.size(); ++i)
    resolve_tls(program.globals[i].metadata);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    resolve_tls(program.functions[i].metadata);

  const auto resolve_operand = [&resolve_spelling](Operand & operand) {
    if(operand.kind != Operand::OP_GLOBAL) return;
    if(!operand.has_spelling) return;
    operand.symbol = resolve_spelling(
      operand.literal, "undefined top-level symbol");
    operand.has_spelling = false;
  };
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    GlobalDefinition & global = program.globals[i];
    resolve_operand(global.init_operand);
    for(std::size_t j = 0; j < global.data_items.size(); ++j) {
      GlobalDefinition::DataItem & item = global.data_items[j];
      resolve_operand(item.literal_operand);
      if(item.kind != GlobalDefinition::DataItem::ITEM_ADDR) continue;
      if(item.symbol_id.valid()) continue;
      item.symbol_id = resolve_spelling(
        item.symbol_spelling, "undefined data symbol");
      item.symbol_spelling = StringId();
    }
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f)
    for(std::size_t b = 0; b < program.functions[f].blocks.size(); ++b) {
      std::vector<Instruction> & instructions =
        program.functions[f].blocks[b].instructions;
      for(std::size_t i = 0; i < instructions.size(); ++i) {
        resolve_operand(instructions[i].first);
        resolve_operand(instructions[i].second);
        resolve_operand(instructions[i].third);
        for(std::size_t a = 0; a < instructions[i].args.size(); ++a)
          resolve_operand(instructions[i].args[a]);
      }
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    if(program.object_aliases[i].target_id.valid()) continue;
    program.object_aliases[i].target_id = resolve_spelling(
      program.object_aliases[i].target_spelling, "undefined alias target");
    program.object_aliases[i].target_spelling = StringId();
  }
}

void intern_lowir_program_literals(Program & program)
{
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    GlobalDefinition & global = program.globals[i];
    intern_operand_literal(program, global.init_operand);
    for(std::size_t j = 0; j < global.data_items.size(); ++j)
      intern_operand_literal(program, global.data_items[j].literal_operand);
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f)
    for(std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
      for(std::size_t i = 0;
          i < program.functions[f].blocks[b].instructions.size(); ++i)
        intern_instruction_literals(
          program, program.functions[f].blocks[b].instructions[i]);
}

void remap_lowir_program_strings(Program & program,
                                 StringPool & destination)
{
  const auto remap_string = [&program, &destination](StringId & string) {
    if(string.valid()) string = destination.intern(program.strings.get(string));
  };
  const auto remap = [&program, &destination](Operand & operand) {
    if(!operand.has_spelling) return;
    operand.literal = destination.intern(program.strings.get(operand.literal));
  };
  const auto remap_parameters = [&remap_string](
      std::vector<Parameter> & parameters) {
    for(std::size_t i = 0; i < parameters.size(); ++i)
      remap_string(parameters[i].name);
  };
  for(std::size_t i = 0; i < program.symbol_names.size(); ++i)
    remap_string(program.symbol_names[i]);
  const auto remap_metadata = [&remap_string](SymbolMetadata & metadata) {
    remap_string(metadata.object_symbol);
    remap_string(metadata.tls_for_spelling);
    remap_string(metadata.section_segment);
    remap_string(metadata.section_name);
  };
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    remap_metadata(program.global_declarations[i].metadata);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
  {
    remap_metadata(program.function_declarations[i].metadata);
    remap_parameters(program.function_declarations[i].params);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    remap_metadata(program.globals[i].metadata);
    remap(program.globals[i].init_operand);
    for(std::size_t j = 0; j < program.globals[i].data_items.size(); ++j) {
      remap(program.globals[i].data_items[j].literal_operand);
      remap_string(program.globals[i].data_items[j].symbol_spelling);
    }
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f)
  {
    Function & function = program.functions[f];
    remap_metadata(function.metadata);
    remap_parameters(function.params);
    for(std::size_t i = 0; i < function.slot_names.size(); ++i)
      remap_string(function.slot_names[i]);
    for(std::size_t i = 0; i < function.value_names.size(); ++i) {
      PresentationName & name = function.value_names[i];
      if(name.valid() && !name.generated())
        name = PresentationName::pooled(
          destination.intern(program.strings.get(name.spelling())),
          name.preserves_copy());
    }
    for(std::size_t i = 0; i < function.block_labels.size(); ++i)
      if(function.block_labels[i].valid()) remap_string(function.block_labels[i]);
    remap_string(function.debug_location.file);
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0;
          i < function.blocks[b].instructions.size(); ++i) {
        Instruction & instruction =
          function.blocks[b].instructions[i];
        remap_parameters(instruction.call_params);
        remap_string(instruction.debug_location.file);
        remap(instruction.first);
        remap(instruction.second);
        remap(instruction.third);
        for(std::size_t a = 0; a < instruction.args.size(); ++a)
          remap(instruction.args[a]);
      }
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    remap_string(program.object_aliases[i].object_symbol);
    remap_string(program.object_aliases[i].target_spelling);
  }
}

void remap_lowir_program_symbols(
    Program & program, const std::vector<SymbolId> & symbols)
{
  if(symbols.size() != program.symbol_names.size())
    throw std::logic_error("invalid LowIR symbol remap");
  const auto remap_symbol = [&symbols](SymbolId & symbol) {
    if(!symbol.valid() || static_cast<std::uint32_t>(symbol) >= symbols.size())
      throw std::logic_error("invalid LowIR symbol identity during remap");
    symbol = symbols[symbol];
  };
  const auto remap_metadata = [&remap_symbol](SymbolMetadata & metadata) {
    if(metadata.tls_for_symbol_id.valid())
      remap_symbol(metadata.tls_for_symbol_id);
  };
  const auto remap_operand = [&remap_symbol](Operand & operand) {
    if(operand.kind == Operand::OP_GLOBAL && !operand.has_spelling)
      remap_symbol(operand.symbol);
  };
  const auto remap_instruction = [&remap_operand](Instruction & instruction) {
    remap_operand(instruction.first);
    remap_operand(instruction.second);
    remap_operand(instruction.third);
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      remap_operand(instruction.args[i]);
  };
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i) {
    remap_symbol(program.global_declarations[i].symbol);
    remap_metadata(program.global_declarations[i].metadata);
  }
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    remap_symbol(program.function_declarations[i].symbol);
    remap_metadata(program.function_declarations[i].metadata);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    GlobalDefinition & global = program.globals[i];
    remap_symbol(global.symbol);
    remap_metadata(global.metadata);
    remap_operand(global.init_operand);
    for(std::size_t j = 0; j < global.data_items.size(); ++j) {
      remap_operand(global.data_items[j].literal_operand);
      if(global.data_items[j].symbol_id.valid())
        remap_symbol(global.data_items[j].symbol_id);
    }
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    Function & function = program.functions[i];
    remap_symbol(function.symbol);
    remap_metadata(function.metadata);
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j)
        remap_instruction(function.blocks[b].instructions[j]);
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i)
    remap_symbol(program.object_aliases[i].target_id);
}

}  // namespace lowir_model
