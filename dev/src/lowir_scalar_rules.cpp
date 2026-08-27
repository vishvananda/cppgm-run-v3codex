#include "lowir_scalar_rules.h"

#include "lowir_function_analysis.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace lowir_opt {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::LowType;
using lowir_model::Operand;

// Rewrite an unsigned divide, remainder, or multiply whose right operand is
// a positive power of two into the equivalent shift or mask.  Signed division
// keeps its rounding semantics and is not rewritten.
bool strength_reduce_binary(Instruction * ins, const LowType & type)
{
  if(ins->kind != Instruction::IK_BINARY ||
     ins->second.kind != Operand::OP_INTEGER ||
     ins->second.int_high != 0 ||
     type.kind == lowir_model::LTK_I128) return false;
  const unsigned long long value = ins->second.int_value;
  if(value == 0 || (value & (value - 1)) != 0 || value == 1) return false;
  unsigned shift = 0;
  while((value >> shift) != 1) ++shift;
  if(ins->op.kind == LowOperation::LOP_UDIV) {
    ins->op.kind = LowOperation::LOP_USHR;
    ins->second.int_value = shift;
  } else if(ins->op.kind == LowOperation::LOP_UMOD) {
    ins->op.kind = LowOperation::LOP_AND;
    ins->second.int_value = value - 1;
  } else if(ins->op.kind == LowOperation::LOP_MUL) {
    ins->op.kind = LowOperation::LOP_SHL;
    ins->second.int_value = shift;
  } else return false;
  ins->second.int_high = 0;
  ins->second.has_spelling = false;
  return true;
}

// A readonly scalar global with a literal initializer always observes that
// literal, so a typed direct load of it is the constant.
ReadonlyGlobalIndex::ReadonlyGlobalIndex(
    const lowir_model::LowirProgram & program, bool populate)
  : known(program.symbol_names.size(), 0),
    constants(program.symbol_names.size()),
    types(program.symbol_names.size())
{
  if(!populate) return;
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const lowir_model::GlobalDefinition & global = program.globals[i];
    if(global.structured ||
       global.storage != lowir_model::GSM_READONLY ||
       global.init_kind != lowir_model::GlobalDefinition::INIT_INTEGER ||
       global.init_operand.kind != Operand::OP_INTEGER) continue;
    known[global.symbol] = 1;
    constants[global.symbol] = global.init_operand;
    types[global.symbol] = global.type;
  }
}

namespace {

bool compatible_strlen_signature(
    const std::vector<lowir_model::Parameter> & params,
    const LowType & result,
    const lowir_model::FunctionBoundaryMetadata & boundary)
{
  return params.size() == 1 &&
    params[0].type.kind == lowir_model::LTK_PTR &&
    result.kind == lowir_model::LTK_I64 &&
    boundary.arity == lowir_model::CAM_FIXED;
}

bool readonly_byte_string_length(
    const lowir_model::GlobalDefinition & global, std::size_t * length)
{
  if(!global.structured || global.storage != lowir_model::GSM_READONLY)
    return false;
  std::size_t offset = 0;
  for(std::size_t i = 0; i < global.data_items.size(); ++i) {
    const lowir_model::GlobalDefinition::DataItem & item =
      global.data_items[i];
    if(item.kind == lowir_model::GlobalDefinition::DataItem::ITEM_ZERO) {
      if(item.zero_bytes == 0) continue;
      *length = offset;
      return true;
    }
    if(item.kind != lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER ||
       item.type.storage_size != 1 ||
       item.literal_operand.kind != Operand::OP_INTEGER ||
       !item.literal_operand.has_int_value)
      return false;
    if(static_cast<unsigned char>(item.literal_operand.int_value) == 0) {
      *length = offset;
      return true;
    }
    ++offset;
  }
  return false;
}

}  // namespace

ReadonlyByteStringIndex::ReadonlyByteStringIndex(
    const lowir_model::LowirProgram & program)
  : known(program.symbol_names.size(), 0),
    lengths(program.symbol_names.size()), strlen_symbol()
{
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & declaration =
      program.function_declarations[i];
    if(compatible_strlen_signature(declaration.params,
         declaration.return_type, declaration.boundary) &&
       declaration.metadata.object_symbol.valid() &&
       program.strings.get(declaration.metadata.object_symbol) ==
         "cppgm_builtin_strlen") {
      strlen_symbol = declaration.symbol;
      break;
    }
  }
  for(std::size_t i = 0;
      !strlen_symbol.valid() && i < program.functions.size(); ++i) {
    const lowir_model::Function & function = program.functions[i];
    if(compatible_strlen_signature(
         function.params, function.return_type, function.boundary) &&
       function.metadata.object_symbol.valid() &&
       program.strings.get(function.metadata.object_symbol) ==
         "cppgm_builtin_strlen")
      strlen_symbol = function.symbol;
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const lowir_model::GlobalDefinition & global = program.globals[i];
    std::size_t length = 0;
    if(readonly_byte_string_length(global, &length) &&
       length <= static_cast<std::size_t>(
         std::numeric_limits<long long>::max())) {
      known[global.symbol] = 1;
      lengths[global.symbol] = length;
    }
  }
}

bool fold_readonly_byte_string_lengths(Function * function,
    const ReadonlyByteStringIndex & strings, Stats * stats)
{
  if(!strings.strlen_symbol.valid()) return false;
  std::vector<lowir_model::SymbolId> address_origins(
    function->value_names.size());
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind == Instruction::IK_ADDR && ins.dest.valid() &&
         ins.first.kind == Operand::OP_GLOBAL)
        address_origins[ins.dest] = ins.first.symbol;
      if(ins.kind != Instruction::IK_CALL ||
         ins.first.kind != Operand::OP_GLOBAL ||
         ins.first.symbol != strings.strlen_symbol || ins.args.size() != 1 ||
         ins.args[0].kind != Operand::OP_TEMP) continue;
      const lowir_model::ValueId argument = ins.args[0].value;
      if(static_cast<std::uint32_t>(argument) >= address_origins.size())
        continue;
      const lowir_model::SymbolId global = address_origins[argument];
      if(!global.valid() || static_cast<std::uint32_t>(global) >=
           strings.known.size() || !strings.known[global]) continue;
      Operand result;
      result.kind = Operand::OP_INTEGER;
      result.has_int_value = true;
      result.int_value = static_cast<long long>(strings.lengths[global]);
      result.int_high = 0;
      result.literal_type = ins.type;
      ins.kind = Instruction::IK_CONST;
      ins.first = result;
      ins.second = Operand();
      ins.third = Operand();
      ins.args.clear();
      ins.call_params.clear();
      ins.has_call_signature = false;
      changed = true;
      if(stats) ++stats->rewrites;
    }
  return changed;
}

namespace {

const std::size_t kNoInstruction = static_cast<std::size_t>(-1);

struct DefinitionLocation
{
  const Instruction * instruction = 0;
  std::size_t block = kNoInstruction;
  std::size_t index = kNoInstruction;
};

struct SlotAddressFact
{
  bool known = false;
  bool dynamic = false;
  lowir_model::SlotId slot;
  std::size_t offset = 0;
  std::size_t stride = 0;
  Operand index;
};

class TableAddressAnalysis
{
public:
  TableAddressAnalysis(const Function & function,
      const std::vector<DefinitionLocation> & definitions)
    : definitions_(definitions), facts_(function.value_names.size()),
      states_(function.value_names.size(), 0)
  {}

  SlotAddressFact resolve(lowir_model::ValueId value)
  {
    const std::uint32_t id = value;
    if(id >= definitions_.size()) return SlotAddressFact();
    if(states_[id] == 2) return facts_[id];
    if(states_[id] == 1) return SlotAddressFact();
    states_[id] = 1;
    const Instruction * definition = definitions_[id].instruction;
    SlotAddressFact fact;
    if(definition && definition->kind == Instruction::IK_ADDR &&
       definition->first.kind == Operand::OP_SLOT) {
      fact.known = true;
      fact.slot = definition->first.slot;
    } else if(definition && definition->kind == Instruction::IK_COPY &&
              definition->first.kind == Operand::OP_TEMP) {
      fact = resolve(definition->first.value);
    } else if(definition && definition->kind == Instruction::IK_INDEX &&
              definition->first.kind == Operand::OP_TEMP) {
      const SlotAddressFact base = resolve(definition->first.value);
      const std::size_t scale = definition->type.storage_size;
      if(base.known && !base.dynamic && scale != 0) {
        if(definition->second.kind == Operand::OP_INTEGER &&
           definition->second.has_int_value &&
           definition->second.int_high == 0 &&
           definition->second.int_value >= 0) {
          const unsigned long long index = static_cast<unsigned long long>(
            definition->second.int_value);
          if(index <= (std::numeric_limits<std::size_t>::max() -
                       base.offset) / scale) {
            fact = base;
            fact.offset += static_cast<std::size_t>(index) * scale;
          }
        } else if(definition->second.kind == Operand::OP_TEMP &&
                  definition->index_projection ==
                    lowir_model::IPK_ARRAY_ELEMENT) {
          fact = base;
          fact.dynamic = true;
          fact.stride = scale;
          fact.index = definition->second;
        }
      }
    }
    facts_[id] = fact;
    states_[id] = 2;
    return fact;
  }

private:
  const std::vector<DefinitionLocation> & definitions_;
  std::vector<SlotAddressFact> facts_;
  std::vector<unsigned char> states_;
};

lowir_model::SymbolId direct_global_address_origin(
    lowir_model::ValueId value,
    const std::vector<DefinitionLocation> & definitions,
    std::vector<lowir_model::SymbolId> * origins,
    std::vector<unsigned char> * states)
{
  const std::uint32_t id = value;
  if(id >= definitions.size()) return lowir_model::SymbolId();
  if((*states)[id] == 2) return (*origins)[id];
  if((*states)[id] == 1) return lowir_model::SymbolId();
  (*states)[id] = 1;
  const Instruction * definition = definitions[id].instruction;
  lowir_model::SymbolId result;
  if(definition && definition->kind == Instruction::IK_ADDR &&
     definition->first.kind == Operand::OP_GLOBAL)
    result = definition->first.symbol;
  else if(definition && definition->kind == Instruction::IK_COPY &&
          definition->first.kind == Operand::OP_TEMP)
    result = direct_global_address_origin(definition->first.value,
      definitions, origins, states);
  (*origins)[id] = result;
  (*states)[id] = 2;
  return result;
}

bool table_pointer_load_origin(lowir_model::ValueId value,
    const std::vector<DefinitionLocation> & definitions,
    TableAddressAnalysis * addresses, SlotAddressFact * address,
    lowir_model::ValueId * loaded_pointer)
{
  for(std::size_t steps = 0; steps <= definitions.size(); ++steps) {
    const std::uint32_t id = value;
    if(id >= definitions.size()) return false;
    const Instruction * definition = definitions[id].instruction;
    if(!definition) return false;
    if(definition->kind == Instruction::IK_COPY &&
       definition->first.kind == Operand::OP_TEMP) {
      value = definition->first.value;
      continue;
    }
    if(definition->kind != Instruction::IK_LOAD ||
       definition->volatile_access ||
       definition->type.kind != lowir_model::LTK_PTR ||
       definition->first.kind != Operand::OP_TEMP)
      return false;
    *address = addresses->resolve(definition->first.value);
    *loaded_pointer = definition->dest;
    return address->known && address->dynamic;
  }
  return false;
}

bool location_dominates(std::size_t definition_block,
    std::size_t definition_index, std::size_t use_block,
    std::size_t use_index,
    const lowir_analysis::DominatorTree & dominators)
{
  return definition_block == use_block ? definition_index < use_index :
    dominators.dominates(definition_block, use_block);
}

struct TableUseLocation
{
  std::size_t block;
  std::size_t index;
  Operand element_index;
  lowir_model::ValueId loaded_pointer;
};

struct StringTableCandidate
{
  bool considered = false;
  bool eligible = false;
  std::size_t count = 0;
  std::vector<std::size_t> lengths;
  std::vector<lowir_model::SymbolId> symbols;
  std::vector<TableUseLocation> stores;
  std::vector<TableUseLocation> loads;
  lowir_model::SymbolId table_symbol;
};

struct StringTableRewrite
{
  std::size_t block;
  std::size_t index;
  lowir_model::SlotId slot;
  Operand element_index;
  lowir_model::ValueId loaded_pointer;
};

struct PointerTableRewrite
{
  std::size_t block;
  std::size_t index;
  lowir_model::SlotId slot;
  Operand element_index;
  lowir_model::ValueId record_address;
};

bool operand_uses_candidate_slot(const Operand & operand,
    const std::vector<StringTableCandidate> & candidates,
    TableAddressAnalysis * addresses, SlotAddressFact * fact)
{
  if(operand.kind != Operand::OP_TEMP) return false;
  *fact = addresses->resolve(operand.value);
  const std::uint32_t slot = fact->slot;
  return fact->known && slot < candidates.size() &&
    candidates[slot].considered;
}

lowir_model::SymbolId append_string_table(
    lowir_model::LowirProgram * program,
    const std::vector<lowir_model::SymbolId> & symbols,
    const std::vector<std::size_t> & lengths)
{
  const std::string prefix = "__o1_string_records_";
  std::size_t suffix = program->symbol_names.size();
  std::string name;
  for(;; ++suffix) {
    name = prefix + std::to_string(suffix);
    bool used = false;
    for(std::size_t i = 0; i < program->symbol_names.size(); ++i)
      if(program->strings.get(program->symbol_names[i]) == name) {
        used = true;
        break;
      }
    if(!used) break;
  }

  lowir_model::GlobalDefinition table;
  table.symbol = lowir_model::append_lowir_symbol(*program, name);
  table.structured = true;
  table.storage = lowir_model::GSM_READONLY;
  table.metadata.binding = lowir_model::SBM_INTERNAL;
  table.data_items.reserve(lengths.size() * 2);
  const LowType & pointer =
    lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
  const LowType & i64 = lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  for(std::size_t i = 0; i < lengths.size(); ++i) {
    lowir_model::GlobalDefinition::DataItem pointer_item;
    pointer_item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
    pointer_item.type = pointer;
    pointer_item.symbol_id = symbols[i];
    table.data_items.push_back(pointer_item);

    lowir_model::GlobalDefinition::DataItem item;
    item.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
    item.type = i64;
    item.literal_operand.kind = Operand::OP_INTEGER;
    item.literal_operand.has_int_value = true;
    item.literal_operand.int_value = static_cast<long long>(lengths[i]);
    item.literal_operand.int_high = 0;
    item.literal_operand.literal_type = i64;
    table.data_items.push_back(item);
  }
  program->globals.push_back(std::move(table));
  return program->globals.back().symbol;
}

Operand temporary_operand(lowir_model::ValueId value)
{
  Operand result;
  result.kind = Operand::OP_TEMP;
  result.value = value;
  return result;
}

}  // namespace

bool fold_readonly_byte_string_table_lengths(
    lowir_model::LowirProgram * program,
    const ReadonlyByteStringIndex & strings, Stats * stats,
    std::vector<unsigned char> * changed_functions)
{
  if(!strings.strlen_symbol.valid()) return false;
  if(changed_functions)
    changed_functions->assign(program->functions.size(), 0);
  bool changed = false;
  const LowType & pointer =
    lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
  const LowType & i64 = lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  LowType record_type;
  record_type.storage_size = pointer.storage_size + i64.storage_size;
  record_type.alignment = std::max(pointer.alignment, i64.alignment);
  record_type.kind = lowir_model::LTK_OBJECT;
  const std::size_t pointer_size = pointer.storage_size;

  for(std::size_t function_index = 0;
      function_index < program->functions.size(); ++function_index) {
    Function & function = program->functions[function_index];
    bool function_changed = false;
    std::vector<DefinitionLocation> definitions(function.value_names.size());
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & ins = function.blocks[block].instructions[index];
        if(ins.dest.valid() &&
           static_cast<std::uint32_t>(ins.dest) < definitions.size())
        {
          definitions[ins.dest].instruction = &ins;
          definitions[ins.dest].block = block;
          definitions[ins.dest].index = index;
        }
      }
    TableAddressAnalysis addresses(function, definitions);
    std::vector<StringTableCandidate> candidates(function.slot_names.size());
    std::vector<StringTableRewrite> rewrites;

    // First discover strlen calls reached through a variable-indexed local
    // pointer table.  The complete initialization and non-escape proof below
    // decides whether each discovered slot is actually eligible.
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & ins = function.blocks[block].instructions[index];
        if(ins.kind != Instruction::IK_CALL || !ins.dest.valid() ||
           ins.type.kind != lowir_model::LTK_I64 ||
           ins.first.kind != Operand::OP_GLOBAL ||
           ins.first.symbol != strings.strlen_symbol || ins.args.size() != 1 ||
           ins.args[0].kind != Operand::OP_TEMP)
          continue;
        SlotAddressFact address;
        lowir_model::ValueId loaded_pointer;
        if(!table_pointer_load_origin(ins.args[0].value, definitions,
             &addresses, &address, &loaded_pointer))
          continue;
        const std::uint32_t slot = address.slot;
        if(slot >= candidates.size()) continue;
        candidates[slot].considered = true;
        rewrites.push_back(StringTableRewrite{
          block, index, address.slot, address.index, loaded_pointer});
      }
    if(rewrites.empty()) continue;

    for(std::size_t slot = 0; slot < candidates.size(); ++slot) {
      StringTableCandidate & candidate = candidates[slot];
      if(!candidate.considered) continue;
      const LowType & type = lowir_model::lowir_slot_type(
        function, lowir_model::SlotId(static_cast<std::uint32_t>(slot)));
      if(pointer_size == 0 || type.kind != lowir_model::LTK_OBJECT ||
         type.alignment < pointer.alignment ||
         type.storage_size < pointer_size * 2 ||
         type.storage_size % pointer_size != 0 ||
         type.storage_size / pointer_size > 256)
        continue;
      candidate.count = type.storage_size / pointer_size;
      candidate.lengths.resize(candidate.count);
      candidate.symbols.resize(candidate.count);
      candidate.stores.resize(candidate.count,
        TableUseLocation{kNoInstruction, kNoInstruction, Operand(),
          lowir_model::ValueId()});
      candidate.eligible = true;
    }

    std::vector<lowir_model::SymbolId> global_origins(
      function.value_names.size());
    std::vector<unsigned char> global_origin_states(
      function.value_names.size(), 0);

    // Every use of an address derived from a candidate slot must remain an
    // ordinary address copy/index, a known initialization store, or a dynamic
    // pointer load.  Any escape or unmodelled access rejects the entire slot.
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & ins = function.blocks[block].instructions[index];
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t operand_index = 0; operand_index < 3; ++operand_index) {
          const Operand & operand = *operands[operand_index];
          if(operand.kind == Operand::OP_SLOT) {
            const std::uint32_t slot = operand.slot;
            if(slot < candidates.size() && candidates[slot].considered &&
               !(ins.kind == Instruction::IK_ADDR && operand_index == 0))
              candidates[slot].eligible = false;
            continue;
          }
          SlotAddressFact address;
          if(!operand_uses_candidate_slot(
               operand, candidates, &addresses, &address))
            continue;
          StringTableCandidate & candidate = candidates[address.slot];
          if(!candidate.eligible) continue;
          const bool address_plumbing =
            (ins.kind == Instruction::IK_COPY && operand_index == 0) ||
            (ins.kind == Instruction::IK_INDEX && operand_index == 0);
          if(address_plumbing) continue;
          if(ins.kind == Instruction::IK_STORE && operand_index == 1 &&
             !ins.volatile_access && !address.dynamic &&
             ins.type.kind == lowir_model::LTK_PTR &&
             address.offset % pointer_size == 0 &&
             address.offset / pointer_size < candidate.count &&
             ins.first.kind == Operand::OP_TEMP) {
            const std::size_t element = address.offset / pointer_size;
            TableUseLocation & store = candidate.stores[element];
            const lowir_model::SymbolId global =
              direct_global_address_origin(ins.first.value, definitions,
                &global_origins, &global_origin_states);
            const std::uint32_t global_index = global;
            if(store.block != kNoInstruction || !global.valid() ||
               global_index >= strings.known.size() ||
               !strings.known[global_index]) {
              candidate.eligible = false;
              continue;
            }
            candidate.lengths[element] = strings.lengths[global_index];
            candidate.symbols[element] = global;
            store = TableUseLocation{block, index, Operand(),
              lowir_model::ValueId()};
            continue;
          }
          if(ins.kind == Instruction::IK_LOAD && operand_index == 0 &&
             !ins.volatile_access && ins.type.kind == lowir_model::LTK_PTR &&
             address.dynamic && address.offset == 0 &&
             address.stride == pointer_size) {
            candidate.loads.push_back(
              TableUseLocation{block, index, address.index, ins.dest});
            continue;
          }
          candidate.eligible = false;
        }
        for(std::size_t argument = 0; argument < ins.args.size(); ++argument) {
          SlotAddressFact address;
          if(operand_uses_candidate_slot(
               ins.args[argument], candidates, &addresses, &address))
            candidates[address.slot].eligible = false;
        }
      }

    bool need_dominators = false;
    for(std::size_t slot = 0; slot < candidates.size(); ++slot)
      if(candidates[slot].considered && candidates[slot].eligible) {
        need_dominators = true;
      }
    if(!need_dominators) continue;
    lowir_analysis::FunctionAnalysis analysis(function, stats);
    const lowir_analysis::DominatorTree & dominators =
      analysis.dominator_tree();
    for(std::size_t slot = 0; slot < candidates.size(); ++slot) {
      StringTableCandidate & candidate = candidates[slot];
      if(!candidate.considered || !candidate.eligible) continue;
      for(std::size_t element = 0; element < candidate.count; ++element)
        if(candidate.stores[element].block == kNoInstruction)
          candidate.eligible = false;
      for(std::size_t load = 0;
          load < candidate.loads.size() && candidate.eligible; ++load)
        for(std::size_t element = 0; element < candidate.count; ++element)
          if(!location_dominates(candidate.stores[element].block,
               candidate.stores[element].index,
               candidate.loads[load].block, candidate.loads[load].index,
               dominators)) {
            candidate.eligible = false;
            break;
          }
      for(std::size_t rewrite = 0;
          rewrite < rewrites.size() && candidate.eligible; ++rewrite) {
        if(rewrites[rewrite].slot !=
           lowir_model::SlotId(static_cast<std::uint32_t>(slot))) continue;
        bool found = false;
        for(std::size_t load = 0; load < candidate.loads.size(); ++load)
          if(candidate.loads[load].loaded_pointer ==
             rewrites[rewrite].loaded_pointer) {
            found = true;
            break;
          }
        if(!found) candidate.eligible = false;
      }
      if(candidate.eligible) {
        candidate.table_symbol = append_string_table(
          program, candidate.symbols, candidate.lengths);
      }
    }

    std::vector<std::vector<StringTableRewrite> > rewrites_by_block(
      function.blocks.size());
    std::vector<std::vector<PointerTableRewrite> > pointer_rewrites_by_block(
      function.blocks.size());
    std::vector<std::vector<std::size_t> > removals_by_block(
      function.blocks.size());
    std::vector<lowir_model::ValueId> record_addresses(
      function.value_names.size());
    for(std::size_t slot = 0; slot < candidates.size(); ++slot) {
      const StringTableCandidate & candidate = candidates[slot];
      if(!candidate.eligible) continue;
      for(std::size_t load = 0; load < candidate.loads.size(); ++load) {
        const TableUseLocation & location = candidate.loads[load];
        const lowir_model::ValueId record_address =
          lowir_model::append_lowir_fresh_generated_value(function, pointer);
        record_addresses[location.loaded_pointer] = record_address;
        pointer_rewrites_by_block[location.block].push_back(
          PointerTableRewrite{location.block, location.index,
            lowir_model::SlotId(static_cast<std::uint32_t>(slot)),
            location.element_index, record_address});
      }
      for(std::size_t store = 0; store < candidate.stores.size(); ++store)
        removals_by_block[candidate.stores[store].block].push_back(
          candidate.stores[store].index);
    }
    for(std::size_t i = 0; i < rewrites.size(); ++i)
      if(candidates[rewrites[i].slot].eligible) {
        rewrites[i].loaded_pointer =
          record_addresses[rewrites[i].loaded_pointer];
        rewrites_by_block[rewrites[i].block].push_back(rewrites[i]);
      }
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      std::sort(pointer_rewrites_by_block[block].begin(),
        pointer_rewrites_by_block[block].end(),
        [](const PointerTableRewrite & left,
           const PointerTableRewrite & right) {
          return left.index < right.index;
        });
      std::sort(removals_by_block[block].begin(),
        removals_by_block[block].end());
    }

    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      if(rewrites_by_block[block].empty() &&
         pointer_rewrites_by_block[block].empty() &&
         removals_by_block[block].empty()) continue;
      std::vector<Instruction> replacement;
      replacement.reserve(function.blocks[block].instructions.size() +
        (rewrites_by_block[block].size() +
         pointer_rewrites_by_block[block].size()) * 2);
      std::size_t rewrite_index = 0;
      std::size_t pointer_rewrite_index = 0;
      std::size_t removal_index = 0;
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        Instruction ins = std::move(
          function.blocks[block].instructions[index]);
        if(removal_index < removals_by_block[block].size() &&
           removals_by_block[block][removal_index] == index) {
          ++removal_index;
          changed = function_changed = true;
          if(stats) ++stats->rewrites;
          continue;
        }
        if(pointer_rewrite_index <
             pointer_rewrites_by_block[block].size() &&
           pointer_rewrites_by_block[block][pointer_rewrite_index].index ==
             index) {
          const PointerTableRewrite & rewrite =
            pointer_rewrites_by_block[block][pointer_rewrite_index++];
          Instruction address;
          address.kind = Instruction::IK_ADDR;
          address.dest = lowir_model::append_lowir_fresh_generated_value(
            function, pointer);
          address.first.kind = Operand::OP_GLOBAL;
          address.first.symbol = candidates[rewrite.slot].table_symbol;
          address.debug_location = ins.debug_location;
          replacement.push_back(address);

          Instruction element;
          element.kind = Instruction::IK_INDEX;
          element.dest = rewrite.record_address;
          element.type = record_type;
          element.index_projection = lowir_model::IPK_ARRAY_ELEMENT;
          element.first = temporary_operand(address.dest);
          element.second = rewrite.element_index;
          element.debug_location = ins.debug_location;
          replacement.push_back(element);
          ins.first = temporary_operand(element.dest);
          replacement.push_back(std::move(ins));
          changed = function_changed = true;
          if(stats) ++stats->rewrites;
          continue;
        }
        if(rewrite_index >= rewrites_by_block[block].size() ||
           rewrites_by_block[block][rewrite_index].index != index) {
          replacement.push_back(std::move(ins));
          continue;
        }
        const StringTableRewrite & rewrite =
          rewrites_by_block[block][rewrite_index++];

        Instruction element;
        element.kind = Instruction::IK_INDEX;
        element.dest = lowir_model::append_lowir_fresh_generated_value(
          function, pointer);
        element.type = i64;
        element.index_projection = lowir_model::IPK_FIELD;
        element.first = temporary_operand(rewrite.loaded_pointer);
        element.second.kind = Operand::OP_INTEGER;
        element.second.has_int_value = true;
        element.second.int_value = 1;
        element.second.int_high = 0;
        element.second.literal_type = i64;
        element.debug_location = ins.debug_location;
        replacement.push_back(element);

        ins.kind = Instruction::IK_LOAD;
        ins.first = temporary_operand(element.dest);
        ins.second = Operand();
        ins.third = Operand();
        ins.args.clear();
        ins.call_params.clear();
        ins.has_call_signature = false;
        ins.volatile_access = false;
        replacement.push_back(std::move(ins));
        changed = function_changed = true;
        if(stats) ++stats->rewrites;
      }
      function.blocks[block].instructions.swap(replacement);
    }
    if(changed_functions && function_changed)
      (*changed_functions)[function_index] = 1;
  }
  return changed;
}

bool fold_readonly_global_loads(Function * function,
    const std::vector<unsigned char> & readonly_known,
    const std::vector<Operand> & readonly_constants,
    const std::vector<LowType> & readonly_types, Stats * stats)
{
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind != Instruction::IK_LOAD || ins.volatile_access ||
         ins.first.kind != Operand::OP_GLOBAL ||
         static_cast<std::uint32_t>(ins.first.symbol) >=
           readonly_known.size() ||
         !readonly_known[ins.first.symbol] ||
         !lowir_model::same_lowir_type(
           ins.type, readonly_types[ins.first.symbol]))
        continue;
      ins.kind = Instruction::IK_CONST;
      ins.first = readonly_constants[ins.first.symbol];
      ins.second = Operand();
      changed = true;
      if(stats) ++stats->rewrites;
    }
  return changed;
}


bool same_operand(const Operand & a, const Operand & b)
{
  if(a.kind != b.kind) return false;
  if(a.kind == Operand::OP_LABEL) return a.block == b.block;
  if(a.kind == Operand::OP_SLOT) return a.slot == b.slot;
  if(a.kind == Operand::OP_TEMP) return a.value == b.value;
  if(a.kind == Operand::OP_GLOBAL) return a.symbol == b.symbol;
  if(a.kind == Operand::OP_INTEGER) {
    return a.has_int_value == b.has_int_value &&
      a.int_value == b.int_value && a.int_high == b.int_high;
  }
  if(a.kind == Operand::OP_FLOAT) {
    return a.has_float_bits == b.has_float_bits &&
      a.literal_low == b.literal_low && a.literal_high == b.literal_high &&
      lowir_model::same_lowir_type(a.literal_type, b.literal_type);
  }
  return true;
}

bool is_zero(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == 0;
}

bool is_one(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == 1;
}

bool is_minus_one(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == -1;
}

bool rewrite_inverted_compare(Instruction * ins,
    LowOperation::Kind inner_compare,
    lowir_model::LowTypeKind inner_type,
    const Operand & inner_first,
    const Operand & inner_second)
{
  if(inner_type == lowir_model::LTK_F32 ||
     inner_type == lowir_model::LTK_F64 ||
     inner_type == lowir_model::LTK_F80 ||
     inner_type == lowir_model::LTK_OBJECT ||
     inner_type == lowir_model::LTK_VOID ||
     inner_type == lowir_model::LTK_INVALID)
    return false;
  LowOperation::Kind inverted;
  switch(inner_compare) {
  case LowOperation::LOP_EQ: inverted = LowOperation::LOP_NE; break;
  case LowOperation::LOP_NE: inverted = LowOperation::LOP_EQ; break;
  case LowOperation::LOP_LT: inverted = LowOperation::LOP_GE; break;
  case LowOperation::LOP_GE: inverted = LowOperation::LOP_LT; break;
  case LowOperation::LOP_LE: inverted = LowOperation::LOP_GT; break;
  case LowOperation::LOP_GT: inverted = LowOperation::LOP_LE; break;
  case LowOperation::LOP_ULT: inverted = LowOperation::LOP_UGE; break;
  case LowOperation::LOP_UGE: inverted = LowOperation::LOP_ULT; break;
  case LowOperation::LOP_ULE: inverted = LowOperation::LOP_UGT; break;
  case LowOperation::LOP_UGT: inverted = LowOperation::LOP_ULE; break;
  default: return false;
  }
  ins->op.kind = inverted;
  ins->first = inner_first;
  ins->second = inner_second;
  ins->type = lowir_model::builtin_lowir_type(inner_type);
  return true;
}

namespace {

bool same_load_address(const Operand & left, const Operand & right)
{
  if(left.kind != right.kind) return false;
  if(left.kind == Operand::OP_TEMP) return left.value == right.value;
  if(left.kind == Operand::OP_SLOT) return left.slot == right.slot;
  if(left.kind == Operand::OP_GLOBAL)
    return left.symbol == right.symbol &&
      left.address_binding == right.address_binding;
  return false;
}

bool keeps_memory(const Instruction & ins)
{
  switch(ins.kind) {
  case Instruction::IK_CONST:
  case Instruction::IK_COPY:
  case Instruction::IK_ADDR:
  case Instruction::IK_INDEX:
  case Instruction::IK_UNARY:
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
  case Instruction::IK_CONVERT:
  case Instruction::IK_PHI:
  case Instruction::IK_JUMP:
  case Instruction::IK_BRANCH:
  case Instruction::IK_SWITCH:
  case Instruction::IK_RETURN:
    return true;
  default:
    return false;
  }
}

}  // namespace

bool eliminate_duplicate_block_loads(Function * function, Stats * stats)
{
  struct TrackedLoad
  {
    Operand address;
    LowType type;
    lowir_model::ValueId dest;
  };
  bool changed = false;
  std::vector<TrackedLoad> live;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    live.clear();
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind == Instruction::IK_LOAD && !ins.volatile_access) {
        bool matched = false;
        for(std::size_t i = 0; i < live.size() && !matched; ++i) {
          if(!same_load_address(live[i].address, ins.first) ||
             !lowir_model::same_lowir_type(live[i].type, ins.type))
            continue;
          ins.kind = Instruction::IK_COPY;
          Operand prior;
          prior.kind = Operand::OP_TEMP;
          prior.value = live[i].dest;
          ins.first = prior;
          ins.second = Operand();
          changed = true;
          matched = true;
          if(stats) ++stats->duplicate_block_loads_removed;
        }
        if(!matched && ins.dest.valid()) {
          TrackedLoad tracked;
          tracked.address = ins.first;
          tracked.type = ins.type;
          tracked.dest = ins.dest;
          live.push_back(tracked);
        }
        continue;
      }
      if(!keeps_memory(ins)) live.clear();
    }
  }
  return changed;
}

}
