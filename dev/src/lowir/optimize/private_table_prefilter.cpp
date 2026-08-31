#include "lowir/optimize/private_table_prefilter.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::LowType;
using lowir_model::LowirProgram;
using lowir_model::Operand;

std::size_t direct_target(const Instruction & instruction,
                          const InlineCallGraph & graph)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL)
    return InlineCallGraph::no_function();
  const std::uint32_t symbol = instruction.first.symbol;
  return symbol < graph.definition_by_symbol.size() ?
    graph.definition_by_symbol[symbol] : InlineCallGraph::no_function();
}

bool signed_scalar_integer(const LowType & type)
{
  return type.kind == lowir_model::LTK_I8 ||
    type.kind == lowir_model::LTK_I16 ||
    type.kind == lowir_model::LTK_I32 ||
    type.kind == lowir_model::LTK_I64;
}

std::size_t greatest_common_divisor(std::size_t left, std::size_t right)
{
  while(right) {
    const std::size_t remainder = left % right;
    left = right;
    right = remainder;
  }
  return left;
}

std::size_t literal_multiple(const Operand & operand, std::size_t cap)
{
  if(operand.kind != Operand::OP_INTEGER || !operand.has_int_value ||
     operand.int_high != 0)
    return 1;
  const unsigned long long value =
    static_cast<unsigned long long>(operand.int_value);
  return greatest_common_divisor(
    static_cast<std::size_t>(value % cap), cap);
}

std::size_t operand_multiple(
    const Operand & operand, const std::vector<std::size_t> & multiples,
    std::size_t cap)
{
  if(operand.kind == Operand::OP_INTEGER)
    return literal_multiple(operand, cap);
  if(operand.kind == Operand::OP_TEMP &&
     operand.value < multiples.size())
    return multiples[operand.value];
  return 1;
}

std::size_t multiplied_multiple(std::size_t left, std::size_t right,
                                std::size_t cap)
{
  if(left >= cap || right >= cap || left > cap / right) return cap;
  return std::min(cap, left * right);
}

std::vector<std::size_t> integer_multiples(const Function & function,
                                           std::size_t cap)
{
  std::vector<std::size_t> result(function.value_names.size(), 1);
  bool changed = true;
  while(changed) {
    changed = false;
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function.blocks[block].instructions[index];
        if(!instruction.dest.valid() ||
           instruction.dest >= result.size())
          continue;
        std::size_t multiple = 1;
        if(instruction.kind == Instruction::IK_CONST)
          multiple = literal_multiple(instruction.first, cap);
        else if(instruction.kind == Instruction::IK_COPY)
          multiple = operand_multiple(instruction.first, result, cap);
        else if(instruction.kind == Instruction::IK_BINARY) {
          const std::size_t left =
            operand_multiple(instruction.first, result, cap);
          const std::size_t right =
            operand_multiple(instruction.second, result, cap);
          if(instruction.op.kind == LowOperation::LOP_ADD ||
             instruction.op.kind == LowOperation::LOP_SUB)
            multiple = greatest_common_divisor(left, right);
          else if(instruction.op.kind == LowOperation::LOP_MUL)
            multiple = multiplied_multiple(left, right, cap);
          else if(instruction.op.kind == LowOperation::LOP_SHL &&
                  instruction.second.kind == Operand::OP_INTEGER &&
                  instruction.second.has_int_value &&
                  instruction.second.int_high == 0 &&
                  instruction.second.int_value >= 0) {
            const unsigned long long shift = static_cast<unsigned long long>(
              instruction.second.int_value);
            multiple = shift >= 63 ? cap : multiplied_multiple(
              left, static_cast<std::size_t>(1) << shift, cap);
          }
        }
        if(multiple > result[instruction.dest]) {
          result[instruction.dest] = multiple;
          changed = true;
        }
      }
  }
  return result;
}

bool table_minimum(const lowir_model::GlobalDefinition & global,
                   long long * minimum, LowType * type)
{
  if(global.data_items.empty()) return false;
  bool first = true;
  for(std::size_t item = 0; item < global.data_items.size(); ++item) {
    const lowir_model::GlobalDefinition::DataItem & data =
      global.data_items[item];
    const Operand & literal = data.literal_operand;
    if(data.kind != lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER ||
       literal.kind != Operand::OP_INTEGER || !literal.has_int_value ||
       literal.int_high != 0 ||
       !lowir_model::same_lowir_type(data.type, literal.literal_type) ||
       (!first &&
        !lowir_model::same_lowir_type(*type, literal.literal_type)))
      return false;
    if(first) {
      *minimum = literal.int_value;
      *type = literal.literal_type;
      first = false;
    } else
      *minimum = std::min(*minimum, literal.int_value);
  }
  return !first && *minimum > 0 && signed_scalar_integer(*type) &&
    type->storage_size != 0;
}

bool harmless_instruction(const Instruction & instruction)
{
  if(instruction.volatile_access) return false;
  switch(instruction.kind) {
  case Instruction::IK_CONST:
  case Instruction::IK_COPY:
  case Instruction::IK_ADDR:
  case Instruction::IK_LOAD:
  case Instruction::IK_INDEX:
  case Instruction::IK_UNARY:
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
  case Instruction::IK_CONVERT:
  case Instruction::IK_PHI:
  case Instruction::IK_JUMP:
  case Instruction::IK_BRANCH:
  case Instruction::IK_RETURN:
    return true;
  default:
    return false;
  }
}

bool aligned_index(const Instruction & instruction,
                   const std::vector<std::size_t> & multiples,
                   std::size_t alignment)
{
  if(instruction.kind != Instruction::IK_INDEX ||
     instruction.type.storage_size == 0)
    return false;
  const std::size_t scale = instruction.type.storage_size % alignment;
  const std::size_t multiple =
    operand_multiple(instruction.second, multiples, alignment);
  return (scale * (multiple % alignment)) % alignment == 0;
}

bool forced_below_table_branch(
    const Instruction & branch,
    const std::vector<const Instruction *> & definitions,
    const std::vector<unsigned char> & table_loads,
    lowir_model::ValueId key, bool * result)
{
  if(branch.kind != Instruction::IK_BRANCH ||
     branch.first.kind != Operand::OP_TEMP ||
     branch.first.value >= definitions.size())
    return false;
  const Instruction * comparison = definitions[branch.first.value];
  if(!comparison || comparison->kind != Instruction::IK_CMP) return false;
  const bool key_left = comparison->first.kind == Operand::OP_TEMP &&
    comparison->first.value == key &&
    comparison->second.kind == Operand::OP_TEMP &&
    comparison->second.value < table_loads.size() &&
    table_loads[comparison->second.value];
  const bool key_right = comparison->second.kind == Operand::OP_TEMP &&
    comparison->second.value == key &&
    comparison->first.kind == Operand::OP_TEMP &&
    comparison->first.value < table_loads.size() &&
    table_loads[comparison->first.value];
  if(!key_left && !key_right) return false;
  if(key_left && (comparison->op.kind == LowOperation::LOP_LT ||
                  comparison->op.kind == LowOperation::LOP_LE)) {
    *result = true;
    return true;
  }
  if(key_left && (comparison->op.kind == LowOperation::LOP_GT ||
                  comparison->op.kind == LowOperation::LOP_GE)) {
    *result = false;
    return true;
  }
  if(key_right && (comparison->op.kind == LowOperation::LOP_GT ||
                   comparison->op.kind == LowOperation::LOP_GE)) {
    *result = true;
    return true;
  }
  if(key_right && (comparison->op.kind == LowOperation::LOP_LT ||
                   comparison->op.kind == LowOperation::LOP_LE)) {
    *result = false;
    return true;
  }
  return false;
}

}  // namespace

DirectGlobalAliases direct_global_aliases(const Function & function)
{
  DirectGlobalAliases result;
  result.known.assign(function.value_names.size(), 0);
  result.values.resize(function.value_names.size());

  // Block storage order is not a dominance order.  Seed every direct global
  // address first, then close copy chains so privacy checks cannot miss an
  // alias merely because its defining block is stored later.
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.kind == Instruction::IK_ADDR &&
         instruction.first.kind == Operand::OP_GLOBAL &&
         instruction.dest.valid() && instruction.dest < result.known.size()) {
        result.known[instruction.dest] = 1;
        result.values[instruction.dest] = instruction.first;
      }
    }
  bool changed = true;
  while(changed) {
    changed = false;
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function.blocks[block].instructions[index];
        if(instruction.kind != Instruction::IK_COPY ||
           !instruction.dest.valid() ||
           instruction.dest >= result.known.size() ||
           result.known[instruction.dest] ||
           instruction.first.kind != Operand::OP_TEMP ||
           instruction.first.value >= result.known.size() ||
           !result.known[instruction.first.value])
          continue;
        result.known[instruction.dest] = 1;
        result.values[instruction.dest] =
          result.values[instruction.first.value];
        changed = true;
      }
  }
  return result;
}

Operand normalize_direct_global(const Operand & operand,
                                const DirectGlobalAliases & aliases)
{
  if(operand.kind == Operand::OP_TEMP &&
     operand.value < aliases.known.size() && aliases.known[operand.value])
    return aliases.values[operand.value];
  return operand;
}

const lowir_model::GlobalDefinition * structured_internal_integer_table(
    const LowirProgram & program, const Operand & operand)
{
  if(operand.kind != Operand::OP_GLOBAL) return 0;
  for(std::size_t global = 0; global < program.globals.size(); ++global) {
    const lowir_model::GlobalDefinition & candidate = program.globals[global];
    if(candidate.symbol != operand.symbol) continue;
    if(!candidate.structured || candidate.data_items.empty() ||
       candidate.metadata.binding != lowir_model::SBM_INTERNAL ||
       candidate.storage == lowir_model::GSM_THREAD_LOCAL ||
       candidate.metadata.keep_internal_alias ||
       candidate.metadata.object_output_root ||
       candidate.metadata.tls_for_symbol_id.valid())
      return 0;
    for(std::size_t item = 0; item < candidate.data_items.size(); ++item) {
      const lowir_model::GlobalDefinition::DataItem & value =
        candidate.data_items[item];
      if(value.kind != lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER ||
         value.literal_operand.kind != Operand::OP_INTEGER ||
         !value.literal_operand.has_int_value)
        return 0;
    }
    return &candidate;
  }
  return 0;
}

bool private_call_table(
    const LowirProgram & program, const InlineCallGraph & call_graph,
    lowir_model::SymbolId symbol, std::size_t selected_target,
    std::size_t selected_parameter)
{
  for(std::size_t alias = 0; alias < program.object_aliases.size(); ++alias)
    if(program.object_aliases[alias].target_id == symbol)
      return false;
  for(std::size_t exported = 0;
      exported < program.exported_symbols.size(); ++exported)
    if(program.exported_symbols[exported].internal_symbol == symbol &&
       program.exported_symbols[exported].linkage != ir_model::SL_INTERNAL)
      return false;
  for(std::size_t declaration = 0;
      declaration < program.global_declarations.size(); ++declaration)
    if(program.global_declarations[declaration].metadata.tls_for_symbol_id ==
         symbol)
      return false;
  for(std::size_t global = 0; global < program.globals.size(); ++global) {
    if(program.globals[global].metadata.tls_for_symbol_id == symbol)
      return false;
    if(program.globals[global].init_operand.kind == Operand::OP_GLOBAL &&
       program.globals[global].init_operand.symbol == symbol)
      return false;
    for(std::size_t item = 0;
        item < program.globals[global].data_items.size(); ++item)
      if(program.globals[global].data_items[item].kind ==
           lowir_model::GlobalDefinition::DataItem::ITEM_ADDR &&
         program.globals[global].data_items[item].symbol_id == symbol)
        return false;
  }
  for(std::size_t function_index = 0;
      function_index < program.functions.size(); ++function_index) {
    const Function & function = program.functions[function_index];
    const DirectGlobalAliases aliases = direct_global_aliases(function);
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function.blocks[block].instructions[index];
        const Operand * fixed[] = {
          &instruction.first, &instruction.second, &instruction.third
        };
        for(std::size_t operand = 0; operand < 3; ++operand) {
          if(fixed[operand]->kind == Operand::OP_GLOBAL &&
             fixed[operand]->symbol == symbol &&
             !(instruction.kind == Instruction::IK_ADDR && operand == 0))
            return false;
          if(fixed[operand]->kind != Operand::OP_TEMP ||
             fixed[operand]->value >= aliases.known.size() ||
             !aliases.known[fixed[operand]->value] ||
             aliases.values[fixed[operand]->value].symbol != symbol)
            continue;
          if(instruction.kind == Instruction::IK_COPY && operand == 0)
            continue;
          return false;
        }
        for(std::size_t argument = 0;
            argument < instruction.args.size(); ++argument) {
          const Operand normalized =
            normalize_direct_global(instruction.args[argument], aliases);
          if(normalized.kind != Operand::OP_GLOBAL ||
             normalized.symbol != symbol)
            continue;
          if(instruction.kind != Instruction::IK_CALL ||
             direct_target(instruction, call_graph) != selected_target ||
             argument != selected_parameter)
            return false;
        }
      }
  }
  return true;
}

bool add_private_table_lower_prefilter(
    Function * function, const lowir_model::GlobalDefinition & global,
    const std::vector<unsigned char> & fixed_parameters)
{
  long long minimum = 0;
  LowType table_type;
  if(!table_minimum(global, &minimum, &table_type) ||
     function->blocks.empty())
    return false;

  std::vector<const Instruction *> definitions(
    function->value_names.size(), static_cast<const Instruction *>(0));
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function->blocks[block].instructions[index];
      if(!harmless_instruction(instruction)) return false;
      if(instruction.dest.valid() && instruction.dest < definitions.size())
        definitions[instruction.dest] = &instruction;
    }

  const std::size_t element_size = table_type.storage_size;
  const std::vector<std::size_t> multiples =
    integer_multiples(*function, element_size);
  std::vector<unsigned char> table_addresses(function->value_names.size(), 0);
  bool changed = true;
  while(changed) {
    changed = false;
    for(std::size_t block = 0; block < function->blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function->blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function->blocks[block].instructions[index];
        if(!instruction.dest.valid() ||
           instruction.dest >= table_addresses.size() ||
           table_addresses[instruction.dest])
          continue;
        bool address = instruction.kind == Instruction::IK_ADDR &&
          instruction.first.kind == Operand::OP_GLOBAL &&
          instruction.first.symbol == global.symbol;
        if(instruction.kind == Instruction::IK_COPY &&
           instruction.first.kind == Operand::OP_TEMP &&
           instruction.first.value < table_addresses.size())
          address = table_addresses[instruction.first.value];
        if(instruction.kind == Instruction::IK_INDEX &&
           aligned_index(instruction, multiples, element_size)) {
          const bool direct = instruction.first.kind == Operand::OP_GLOBAL &&
            instruction.first.symbol == global.symbol;
          const bool derived = instruction.first.kind == Operand::OP_TEMP &&
            instruction.first.value < table_addresses.size() &&
            table_addresses[instruction.first.value];
          address = direct || derived;
        }
        if(address) {
          table_addresses[instruction.dest] = 1;
          changed = true;
        }
      }
  }
  std::vector<unsigned char> table_loads(function->value_names.size(), 0);
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function->blocks[block].instructions[index];
      if(instruction.kind == Instruction::IK_LOAD &&
         instruction.dest.valid() && instruction.dest < table_loads.size() &&
         instruction.first.kind == Operand::OP_TEMP &&
         instruction.first.value < table_addresses.size() &&
         table_addresses[instruction.first.value] &&
         lowir_model::same_lowir_type(instruction.type, table_type))
        table_loads[instruction.dest] = 1;
    }

  std::vector<std::size_t> block_by_id(
    function->next_block_id, static_cast<std::size_t>(-1));
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    block_by_id[function->blocks[block].id] = block;
  for(std::size_t parameter = 0; parameter < function->params.size();
      ++parameter) {
    if(parameter < fixed_parameters.size() && fixed_parameters[parameter])
      continue;
    if(!lowir_model::same_lowir_type(
         function->params[parameter].type, table_type))
      continue;
    const lowir_model::ValueId key = function->params[parameter].value;
    std::vector<unsigned char> reachable(function->blocks.size(), 0);
    std::deque<std::size_t> pending;
    pending.push_back(0);
    std::size_t false_return = static_cast<std::size_t>(-1);
    bool valid = true;
    while(!pending.empty() && valid) {
      const std::size_t block = pending.front();
      pending.pop_front();
      if(reachable[block]) continue;
      reachable[block] = 1;
      const std::vector<Instruction> & instructions =
        function->blocks[block].instructions;
      if(instructions.empty()) { valid = false; break; }
      const Instruction & terminal = instructions.back();
      if(terminal.kind == Instruction::IK_RETURN) {
        if(terminal.first.kind != Operand::OP_INTEGER ||
           !terminal.first.has_int_value || terminal.first.int_high != 0 ||
           terminal.first.int_value != 0) {
          valid = false;
          break;
        }
        // The new entry edge must not bypass definitions used by its target.
        if(instructions.size() == 1) false_return = block;
        continue;
      }
      const auto enqueue = [&](const Operand & target) {
        if(target.kind != Operand::OP_LABEL ||
           target.block >= block_by_id.size() ||
           block_by_id[target.block] == static_cast<std::size_t>(-1)) {
          valid = false;
          return;
        }
        pending.push_back(block_by_id[target.block]);
      };
      if(terminal.kind == Instruction::IK_JUMP) {
        enqueue(terminal.first);
      } else if(terminal.kind == Instruction::IK_BRANCH) {
        bool condition = false;
        if(forced_below_table_branch(
             terminal, definitions, table_loads, key, &condition))
          enqueue(condition ? terminal.second : terminal.third);
        else {
          enqueue(terminal.second);
          enqueue(terminal.third);
        }
      } else valid = false;
    }
    if(!valid || false_return == static_cast<std::size_t>(-1)) continue;
    std::vector<Instruction> & entry = function->blocks[0].instructions;
    if(entry.empty() || entry.back().kind != Instruction::IK_JUMP) continue;
    const Operand ordinary = entry.back().first;
    Instruction compare;
    compare.kind = Instruction::IK_CMP;
    compare.dest = lowir_model::append_lowir_fresh_generated_value(
      *function, lowir_model::builtin_lowir_type(lowir_model::LTK_U8));
    compare.type = table_type;
    compare.op.kind = LowOperation::LOP_LT;
    compare.first.kind = Operand::OP_TEMP;
    compare.first.value = key;
    compare.second.kind = Operand::OP_INTEGER;
    compare.second.has_int_value = true;
    compare.second.int_value = minimum;
    compare.second.int_high = 0;
    compare.second.literal_type = table_type;
    Instruction branch;
    branch.kind = Instruction::IK_BRANCH;
    branch.first.kind = Operand::OP_TEMP;
    branch.first.value = compare.dest;
    branch.second.kind = Operand::OP_LABEL;
    branch.second.block = function->blocks[false_return].id;
    branch.third = ordinary;
    entry.back() = compare;
    entry.push_back(branch);
    return true;
  }
  return false;
}

}  // namespace lowir_opt
