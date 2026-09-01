#include "lowir/optimize/interprocedural_specialization.h"

#include "lowir/analysis/function.h"
#include "lowir/optimize/inline_o1.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/private_table_prefilter.h"
#include "lowir/optimize/scalar_rules.h"
#include "lowir/optimize/unreachable.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::LowirProgram;
using lowir_model::Operand;

const std::size_t kNoParameter = static_cast<std::size_t>(-1);
const std::size_t kMaximumSpecializedClones = 256;
const std::size_t kMaximumClonedInstructions = 8192;
const std::size_t kO3MinimumGroupedCalls = 8;
const std::size_t kO3MaximumGroupedTargetInstructions = 128;
const std::size_t kO3MaximumStringGroupedTargetInstructions = 192;
const std::size_t kO3MaximumConstantGroupsPerTarget = 64;
const std::size_t kO3MaximumGroupedClones = 24;
const std::size_t kO3MaximumGroupedInstructions = 1536;
const std::size_t kO3MaximumStringGroupsPerTarget = 12;

enum AgreementState
{
  AS_UNSEEN,
  AS_UNIFORM,
  AS_DIFFERENT,
  AS_UNSUPPORTED
};

struct ArgumentAgreement
{
  Operand value;
  AgreementState state = AS_UNSEEN;
};

bool supported_argument(const Operand & operand)
{
  return (operand.kind == Operand::OP_INTEGER && operand.has_int_value) ||
    (operand.kind == Operand::OP_FLOAT && operand.has_float_bits) ||
    operand.kind == Operand::OP_GLOBAL;
}

bool same_argument(const Operand & left, const Operand & right)
{
  if(left.kind != right.kind ||
     left.address_binding != right.address_binding ||
     !lowir_model::same_lowir_type(left.literal_type, right.literal_type))
    return false;
  if(left.kind == Operand::OP_INTEGER)
    return left.has_int_value == right.has_int_value &&
      left.int_value == right.int_value && left.int_high == right.int_high;
  if(left.kind == Operand::OP_FLOAT)
    return left.has_float_bits == right.has_float_bits &&
      left.literal_low == right.literal_low &&
      left.literal_high == right.literal_high;
  return left.kind == Operand::OP_GLOBAL && left.symbol == right.symbol;
}

bool removable_parameter(const lowir_model::Parameter & parameter)
{
  if(parameter.metadata.passing != lowir_model::PPM_DIRECT) return false;
  const LowType & type = parameter.type;
  return type.kind != lowir_model::LTK_OBJECT &&
    type.kind != lowir_model::LTK_I128 &&
    type.kind != lowir_model::LTK_F80;
}

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

void mark_reference(const Operand & operand,
                    const InlineCallGraph & graph,
                    std::vector<unsigned char> * escaped)
{
  if(operand.kind != Operand::OP_GLOBAL) return;
  const std::uint32_t symbol = operand.symbol;
  if(symbol >= graph.definition_by_symbol.size()) return;
  const std::size_t target = graph.definition_by_symbol[symbol];
  if(target != InlineCallGraph::no_function()) (*escaped)[target] = 1;
}

template <class T>
void remove_masked(std::vector<T> * values,
                   const unsigned char * removed,
                   std::size_t removed_count)
{
  if(values->size() != removed_count)
    throw std::logic_error("interprocedural signature shape mismatch");
  std::size_t output = 0;
  for(std::size_t input = 0; input < values->size(); ++input) {
    if(removed[input]) continue;
    if(output != input) (*values)[output] = std::move((*values)[input]);
    ++output;
  }
  values->resize(output);
}

bool has_removed_parameter(const unsigned char * removed,
                           std::size_t parameter_count)
{
  return std::find(removed, removed + parameter_count, 1) !=
    removed + parameter_count;
}

void update_agreement(ArgumentAgreement * agreement,
                      const Operand & argument)
{
  if(agreement->state == AS_DIFFERENT ||
     agreement->state == AS_UNSUPPORTED) return;
  if(!supported_argument(argument)) {
    agreement->state = AS_UNSUPPORTED;
    return;
  }
  if(agreement->state == AS_UNSEEN) {
    agreement->value = argument;
    agreement->state = AS_UNIFORM;
  } else if(!same_argument(agreement->value, argument)) {
    agreement->state = AS_DIFFERENT;
  }
}

std::size_t instruction_count(const Function & function)
{
  std::size_t result = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    result += function.blocks[block].instructions.size();
  return result;
}

bool uniform_parameter_controls_cfg(
    const Function & function,
    const ArgumentAgreement * agreements,
    std::size_t agreement_count)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.kind != Instruction::IK_BRANCH &&
         instruction.kind != Instruction::IK_SWITCH)
        continue;
      if(instruction.first.kind != Operand::OP_TEMP) continue;
      for(std::size_t parameter = 0; parameter < agreement_count;
          ++parameter)
        if(agreements[parameter].state == AS_UNIFORM &&
           function.params[parameter].value == instruction.first.value)
          return true;
    }
  return false;
}

lowir_model::SymbolId allocate_clone_symbol(
    LowirProgram * program,
    std::vector<unsigned char> * used_names,
    std::size_t * ordinal,
    const char * prefix)
{
  for(;;) {
    const lowir_model::StringId name = program->strings.intern(
      std::string(prefix) + std::to_string((*ordinal)++));
    const std::uint32_t id = name;
    if(id >= used_names->size()) used_names->resize(id + 1, 0);
    if((*used_names)[id]) continue;
    (*used_names)[id] = 1;
    return lowir_model::append_lowir_symbol(*program, name);
  }
}

void make_internal_clone(Function * function, lowir_model::SymbolId symbol)
{
  function->symbol = symbol;
  function->metadata.role = lowir_model::SR_NONE;
  function->metadata.binding = lowir_model::SBM_INTERNAL;
  function->metadata.object_symbol = lowir_model::StringId();
  function->metadata.tls_for_spelling = lowir_model::StringId();
  function->metadata.tls_for_symbol_id = lowir_model::SymbolId();
  function->metadata.section_name = lowir_model::StringId();
  function->metadata.keep_internal_alias = false;
  function->metadata.prefer_local_object_binding = false;
  function->metadata.object_output_root = false;
  function->metadata.force_inline = false;
  function->metadata.inline_hint = false;
  function->metadata.no_inline = false;
}

void rewrite_operand(
    Operand * operand,
    const std::vector<std::size_t> & parameter_by_value,
    const ArgumentAgreement * agreements,
    std::vector<unsigned char> * used,
    bool * substituted,
    Stats * stats)
{
  if(operand->kind != Operand::OP_TEMP) return;
  const std::uint32_t value = operand->value;
  if(value >= parameter_by_value.size()) return;
  const std::size_t parameter = parameter_by_value[value];
  if(parameter == kNoParameter) return;
  if(agreements[parameter].state == AS_UNIFORM) {
    *operand = agreements[parameter].value;
    *substituted = true;
    if(stats) ++stats->ipa_substituted_operands;
  } else {
    (*used)[parameter] = 1;
  }
}

bool specialize_function(
    Function * function,
    const ArgumentAgreement * agreements,
    std::size_t agreement_count,
    unsigned char * removed,
    std::vector<std::size_t> * parameter_by_value,
    std::vector<unsigned char> * used,
    Stats * stats)
{
  parameter_by_value->assign(function->value_names.size(), kNoParameter);
  for(std::size_t parameter = 0; parameter < function->params.size();
      ++parameter)
    (*parameter_by_value)[function->params[parameter].value] = parameter;

  if(function->params.size() != agreement_count)
    throw std::logic_error("interprocedural parameter agreement mismatch");
  used->assign(function->params.size(), 0);
  bool substituted = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & instruction =
        function->blocks[block].instructions[index];
      if(stats) ++stats->ipa_instruction_visits;
      Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t operand = 0; operand < 3; ++operand)
        rewrite_operand(fixed[operand], *parameter_by_value, agreements,
                        used, &substituted, stats);
      for(std::size_t operand = 0; operand < instruction.args.size(); ++operand)
        rewrite_operand(&instruction.args[operand], *parameter_by_value,
                        agreements, used, &substituted, stats);
    }
  }
  std::fill(removed, removed + function->params.size(), 0);
  bool removed_any = false;
  for(std::size_t parameter = 0; parameter < function->params.size();
      ++parameter) {
    if(!(*used)[parameter] && removable_parameter(function->params[parameter])) {
      removed[parameter] = 1;
      removed_any = true;
      if(stats) ++stats->ipa_dead_parameters;
    }
  }
  return substituted || removed_any;
}

void append_specialized_clones(
    LowirProgram * program,
    std::size_t original_function_count,
    std::vector<Function> * clones,
    std::vector<unsigned char> * rewritten_symbols)
{
  if(clones->empty()) return;
  program->functions.reserve(program->functions.size() + clones->size());
  for(std::size_t clone = 0; clone < clones->size(); ++clone)
    program->functions.push_back(std::move((*clones)[clone]));
  if(!rewritten_symbols) return;
  rewritten_symbols->resize(program->symbol_names.size(), 0);
  for(std::size_t function = original_function_count;
      function < program->functions.size(); ++function)
    (*rewritten_symbols)[program->functions[function].symbol] = 1;
}

struct ConstantGroup
{
  std::size_t parameter = 0;
  Operand value;
  std::size_t calls = 0;
};

struct GroupSelection
{
  std::size_t parameter = 0;
  Operand value;
  lowir_model::SymbolId replacement;
  std::vector<unsigned char> removed;
};

bool readonly_byte_string_argument(
    const std::vector<unsigned char> & strings, const Operand & value)
{
  return value.kind == Operand::OP_GLOBAL &&
    static_cast<std::uint32_t>(value.symbol) < strings.size() &&
    strings[value.symbol];
}

std::vector<unsigned char> internal_readonly_byte_strings(
    const LowirProgram & program, const ReadonlyByteStringIndex & strings)
{
  std::vector<unsigned char> result(strings.known.size(), 0);
  for(std::size_t global = 0; global < program.globals.size(); ++global) {
    const lowir_model::GlobalDefinition & definition =
      program.globals[global];
    const std::uint32_t symbol = definition.symbol;
    if(symbol < strings.known.size() && strings.known[symbol] &&
       definition.metadata.binding == lowir_model::SBM_INTERNAL)
      result[symbol] = 1;
  }
  return result;
}

bool readonly_byte_at(const LowirProgram & program,
                      lowir_model::SymbolId symbol,
                      std::size_t offset,
                      unsigned char * value)
{
  for(std::size_t global = 0; global < program.globals.size(); ++global) {
    const lowir_model::GlobalDefinition & definition =
      program.globals[global];
    if(definition.symbol != symbol) continue;
    if(!definition.structured ||
       definition.storage != lowir_model::GSM_READONLY ||
       definition.metadata.binding != lowir_model::SBM_INTERNAL)
      return false;
    std::size_t position = 0;
    for(std::size_t item = 0; item < definition.data_items.size(); ++item) {
      const lowir_model::GlobalDefinition::DataItem & data =
        definition.data_items[item];
      if(data.kind == lowir_model::GlobalDefinition::DataItem::ITEM_ZERO) {
        if(offset >= position && offset - position < data.zero_bytes) {
          *value = 0;
          return true;
        }
        if(data.zero_bytes > std::numeric_limits<std::size_t>::max() - position)
          return false;
        position += data.zero_bytes;
        continue;
      }
      if(data.kind !=
           lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER ||
         data.type.storage_size != 1 ||
         data.literal_operand.kind != Operand::OP_INTEGER ||
         !data.literal_operand.has_int_value)
        return false;
      if(offset == position) {
        *value = static_cast<unsigned char>(data.literal_operand.int_value);
        return true;
      }
      if(position == std::numeric_limits<std::size_t>::max()) return false;
      ++position;
    }
    return false;
  }
  return false;
}

struct ReadonlyByteAddress
{
  lowir_model::SymbolId symbol;
  std::size_t offset = 0;
  bool known = false;
};

bool fold_readonly_byte_loads(Function * function,
                              const LowirProgram & program)
{
  std::vector<ReadonlyByteAddress> addresses(function->value_names.size());
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & instruction =
        function->blocks[block].instructions[index];
      ReadonlyByteAddress address;
      if(instruction.first.kind == Operand::OP_GLOBAL) {
        address.symbol = instruction.first.symbol;
        address.known = true;
      } else if(instruction.first.kind == Operand::OP_TEMP &&
                instruction.first.value < addresses.size()) {
        address = addresses[instruction.first.value];
      }
      if(instruction.dest.valid() && instruction.dest < addresses.size()) {
        if(instruction.kind == Instruction::IK_ADDR ||
           instruction.kind == Instruction::IK_COPY) {
          addresses[instruction.dest] = address;
        } else if(instruction.kind == Instruction::IK_INDEX && address.known &&
                  instruction.second.kind == Operand::OP_INTEGER &&
                  instruction.second.has_int_value &&
                  instruction.second.int_high == 0 &&
                  instruction.second.int_value >= 0) {
          const unsigned long long element =
            static_cast<unsigned long long>(instruction.second.int_value);
          const unsigned long long stride = instruction.type.storage_size;
          if(stride == 0 || element >
               std::numeric_limits<std::size_t>::max() / stride ||
             element * stride >
               std::numeric_limits<std::size_t>::max() - address.offset)
            addresses[instruction.dest] = ReadonlyByteAddress();
          else {
            address.offset += static_cast<std::size_t>(element * stride);
            addresses[instruction.dest] = address;
          }
        }
      }
      if(instruction.kind != Instruction::IK_LOAD ||
         instruction.volatile_access || !address.known ||
         (instruction.type.kind != lowir_model::LTK_I8 &&
          instruction.type.kind != lowir_model::LTK_U8))
        continue;
      unsigned char byte = 0;
      if(!readonly_byte_at(program, address.symbol, address.offset, &byte))
        continue;
      Operand constant;
      constant.kind = Operand::OP_INTEGER;
      constant.has_int_value = true;
      constant.int_value = instruction.type.kind == lowir_model::LTK_I8 &&
        byte >= 128 ? static_cast<long long>(byte) - 256 : byte;
      constant.int_high = 0;
      constant.literal_type = instruction.type;
      instruction.kind = Instruction::IK_CONST;
      instruction.first = constant;
      instruction.second = Operand();
      instruction.third = Operand();
      instruction.args.clear();
      instruction.call_params.clear();
      instruction.has_call_signature = false;
      changed = true;
    }
  return changed;
}

bool fold_literal_control_with_phi_repair(Function * function)
{
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    if(instructions.empty()) continue;
    Instruction & terminal = instructions.back();
    Operand selected;
    if(terminal.kind == Instruction::IK_BRANCH &&
       terminal.first.kind == Operand::OP_INTEGER &&
       terminal.first.has_int_value) {
      selected = terminal.first.int_value ? terminal.second : terminal.third;
    } else if(terminal.kind == Instruction::IK_SWITCH &&
              terminal.first.kind == Operand::OP_INTEGER &&
              terminal.first.has_int_value) {
      selected = terminal.second;
      for(std::size_t argument = 0;
          argument + 1 < terminal.args.size(); argument += 2)
        if(terminal.args[argument].kind == Operand::OP_INTEGER &&
           terminal.args[argument].has_int_value &&
           terminal.args[argument].int_value == terminal.first.int_value) {
          selected = terminal.args[argument + 1];
          break;
        }
    } else {
      continue;
    }
    const lowir_model::InstructionDebugLocation debug =
      terminal.debug_location;
    terminal = Instruction();
    terminal.kind = Instruction::IK_JUMP;
    terminal.first = selected;
    terminal.debug_location = debug;
    changed = true;
  }
  if(!changed) return false;

  const lowir_analysis::Graph graph =
    lowir_analysis::build_graph(*function, 0);
  std::vector<unsigned char> reachable(function->blocks.size(), 0);
  std::deque<std::size_t> work;
  reachable[0] = 1;
  work.push_back(0);
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    for(std::size_t edge = 0;
        edge < graph.successors[block].size(); ++edge) {
      const std::size_t successor = graph.successors[block][edge];
      if(reachable[successor]) continue;
      reachable[successor] = 1;
      work.push_back(successor);
    }
  }
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    if(!reachable[block]) continue;
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = 0; index < instructions.size(); ++index) {
      Instruction & phi = instructions[index];
      if(phi.kind != Instruction::IK_PHI) continue;
      std::size_t output = 0;
      for(std::size_t input = 0;
          input + 1 < phi.args.size(); input += 2) {
        if(phi.args[input].kind != Operand::OP_LABEL) continue;
        const std::size_t predecessor = graph.find(phi.args[input].block);
        bool still_predecessor = false;
        for(std::size_t edge = 0;
            edge < graph.predecessors[block].size(); ++edge)
          still_predecessor = still_predecessor ||
            graph.predecessors[block][edge] == predecessor;
        if(predecessor == static_cast<std::size_t>(-1) ||
           !reachable[predecessor] ||
           !still_predecessor)
          continue;
        phi.args[output] = phi.args[input];
        phi.args[output + 1] = phi.args[input + 1];
        output += 2;
      }
      phi.args.resize(output);
    }
  }
  std::vector<lowir_model::Block> kept;
  kept.reserve(function->blocks.size());
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    if(reachable[block])
      kept.push_back(std::move(function->blocks[block]));
  function->blocks.swap(kept);
  return true;
}

bool function_has_exception_instructions(const Function & function)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function.blocks[block].instructions.size();
        ++instruction) {
      const Instruction::Kind kind =
        function.blocks[block].instructions[instruction].kind;
      if(kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_RESUME)
        return true;
    }
  return false;
}

std::size_t rewrite_o3_group_calls(
    LowirProgram * program,
    std::size_t function_count,
    const InlineCallGraph & call_graph,
    const std::vector<std::vector<GroupSelection> > & selections,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats)
{
  std::size_t rewritten_calls = 0;
  for(std::size_t caller = 0; caller < function_count; ++caller) {
    Function & function = program->functions[caller];
    const DirectGlobalAliases aliases = direct_global_aliases(function);
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        Instruction & call = function.blocks[block].instructions[index];
        const std::size_t target = direct_target(call, call_graph);
        if(target == InlineCallGraph::no_function() ||
           selections[target].empty())
          continue;
        std::size_t selected = 0;
        while(selected < selections[target].size() &&
              (selections[target][selected].parameter >= call.args.size() ||
               !same_argument(
                 normalize_direct_global(
                   call.args[selections[target][selected].parameter], aliases),
                 selections[target][selected].value)))
          ++selected;
        if(selected == selections[target].size()) continue;
        const GroupSelection & selection = selections[target][selected];
        const std::size_t old_arguments = call.args.size();
        bool caller_needs_cleanup = false;
        for(std::size_t argument = 0; argument < call.args.size(); ++argument)
          caller_needs_cleanup = caller_needs_cleanup ||
            (selection.removed[argument] &&
             call.args[argument].kind == Operand::OP_TEMP);
        call.first.symbol = selection.replacement;
        call.first.address_binding = Operand::ADDRESS_LOCAL;
        remove_masked(
          &call.args, &selection.removed[0], selection.removed.size());
        if(call.has_call_signature)
          remove_masked(
            &call.call_params, &selection.removed[0],
            selection.removed.size());
        if(rewritten_symbols && caller_needs_cleanup)
          (*rewritten_symbols)[function.symbol] = 1;
        if(stats) {
          ++stats->ipa_calls_rewritten;
          stats->ipa_arguments_removed += old_arguments - call.args.size();
        }
        ++rewritten_calls;
      }
  }
  return rewritten_calls;
}

std::size_t o3_group_analysis_bytes(
    const std::vector<std::size_t> & direct_calls,
    const std::vector<unsigned char> & escaped,
    const std::vector<unsigned char> & invalid_shape,
    const std::vector<unsigned char> & group_limit_reached,
    const ReadonlyByteStringIndex & byte_strings,
    const std::vector<unsigned char> & groupable_strings,
    const std::vector<std::vector<ConstantGroup> > & groups,
    const std::vector<unsigned char> & used_symbol_names,
    const std::vector<std::vector<GroupSelection> > & selections,
    const std::vector<Function> & clones,
    const std::vector<std::size_t> & parameter_by_value,
    const std::vector<unsigned char> & used_parameters)
{
  std::size_t bytes =
    direct_calls.capacity() * sizeof(std::size_t) + escaped.capacity() +
    invalid_shape.capacity() + group_limit_reached.capacity() +
    byte_strings.known.capacity() +
    byte_strings.lengths.capacity() * sizeof(std::size_t) +
    groupable_strings.capacity() +
    groups.capacity() * sizeof(std::vector<ConstantGroup>) +
    used_symbol_names.capacity() +
    selections.capacity() * sizeof(std::vector<GroupSelection>) +
    clones.capacity() * sizeof(Function) +
    parameter_by_value.capacity() * sizeof(std::size_t) +
    used_parameters.capacity();
  for(std::size_t function = 0; function < groups.size(); ++function) {
    bytes += groups[function].capacity() * sizeof(ConstantGroup);
    bytes += selections[function].capacity() * sizeof(GroupSelection);
    for(std::size_t selection = 0;
        selection < selections[function].size(); ++selection)
      bytes += selections[function][selection].removed.capacity();
  }
  return bytes;
}

std::size_t minimum_group_calls(const LowirProgram & program,
                                const Operand & value)
{
  return structured_internal_integer_table(program, value) ? 4 :
    kO3MinimumGroupedCalls;
}

void collect_correlated_group_arguments(
    const LowirProgram & program, const InlineCallGraph & call_graph,
    std::size_t selected_target, std::size_t selected_parameter,
    const Operand & selected_value,
    std::vector<ArgumentAgreement> * agreements)
{
  agreements->assign(agreements->size(), ArgumentAgreement());
  for(std::size_t caller = 0; caller < program.functions.size(); ++caller) {
    const Function & function = program.functions[caller];
    const DirectGlobalAliases aliases = direct_global_aliases(function);
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & call = function.blocks[block].instructions[index];
        if(direct_target(call, call_graph) != selected_target ||
           selected_parameter >= call.args.size() ||
           !same_argument(
             normalize_direct_global(call.args[selected_parameter], aliases),
             selected_value))
          continue;
        if(call.args.size() != agreements->size()) return;
        for(std::size_t argument = 0; argument < call.args.size(); ++argument)
          update_agreement(
            &(*agreements)[argument],
            normalize_direct_global(call.args[argument], aliases));
      }
  }
  for(std::size_t parameter = 0; parameter < agreements->size(); ++parameter)
    if((*agreements)[parameter].state == AS_UNSEEN)
      (*agreements)[parameter].state = AS_DIFFERENT;
}

bool count_constant_group(std::vector<ConstantGroup> * groups,
                          std::size_t parameter,
                          const Operand & value)
{
  for(std::size_t group = 0; group < groups->size(); ++group)
    if((*groups)[group].parameter == parameter &&
       same_argument((*groups)[group].value, value)) {
      ++(*groups)[group].calls;
      return true;
    }
  if(groups->size() == kO3MaximumConstantGroupsPerTarget) return false;
  ConstantGroup group;
  group.parameter = parameter;
  group.value = value;
  group.calls = 1;
  groups->push_back(group);
  return true;
}

void record_stable_prefix_specialization(
    StablePrefixSpecializationIndex * index,
    const LowirProgram & program,
    const Function & target,
    const ConstantGroup & group,
    const GroupSelection & selection)
{
  if(!index || target.boundary.query != lowir_model::CQM_STABLE_PREFIX ||
     group.parameter + 1 != target.params.size() ||
     group.value.kind != Operand::OP_INTEGER || !group.value.has_int_value ||
     group.value.int_value < 0 || group.value.int_high != 0 ||
     selection.removed.empty() || !selection.removed.back())
    return;
  const std::size_t symbols = program.symbol_names.size();
  index->known.resize(symbols, 0);
  index->family.resize(symbols);
  index->index.resize(symbols, 0);
  index->known[selection.replacement] = 1;
  index->family[selection.replacement] = target.symbol;
  index->index[selection.replacement] =
    static_cast<std::uint64_t>(group.value.int_value);
}

void remove_internal_dead_parameters(
    LowirProgram * program,
    std::size_t function_count,
    const std::vector<std::size_t> & parameter_offsets,
    const std::vector<unsigned char> & removed_parameters,
    const std::vector<unsigned char> & eligible,
    const std::vector<unsigned char> & has_declaration)
{
  for(std::size_t function = 0; function < function_count; ++function) {
    if(program->functions[function].metadata.binding !=
         lowir_model::SBM_INTERNAL ||
       !eligible[function])
      continue;
    const std::size_t first = parameter_offsets[function];
    const std::size_t parameter_count =
      parameter_offsets[function + 1] - first;
    if(parameter_count == 0) continue;
    const unsigned char * removed = &removed_parameters[first];
    if(!has_removed_parameter(removed, parameter_count)) continue;
    const lowir_model::SymbolId symbol = program->functions[function].symbol;
    if(removed[parameter_count - 1])
      program->functions[function].boundary.query = lowir_model::CQM_DEFAULT;
    remove_masked(
      &program->functions[function].params, removed, parameter_count);
    if(has_declaration[function])
      for(std::size_t declaration = 0;
          declaration < program->function_declarations.size(); ++declaration)
        if(program->function_declarations[declaration].symbol == symbol)
        {
          if(removed[parameter_count - 1])
            program->function_declarations[declaration].boundary.query =
              lowir_model::CQM_DEFAULT;
          remove_masked(
            &program->function_declarations[declaration].params,
            removed, parameter_count);
        }
  }
}

}  // namespace

std::size_t specialize_interprocedural_arguments(
    LowirProgram & program,
    const InlineCallGraph & call_graph,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats)
{
  const std::size_t function_count = program.functions.size();
  std::vector<std::size_t> parameter_offsets(function_count + 1, 0);
  for(std::size_t function = 0; function < function_count; ++function)
    parameter_offsets[function + 1] = parameter_offsets[function] +
      program.functions[function].params.size();
  std::vector<ArgumentAgreement> agreements(parameter_offsets.back());
  std::vector<std::size_t> direct_calls(function_count, 0);
  std::vector<unsigned char> escaped(function_count, 0);
  std::vector<unsigned char> invalid_shape(function_count, 0);
  std::vector<unsigned char> has_declaration(function_count, 0);

  for(std::size_t declaration = 0;
      declaration < program.function_declarations.size(); ++declaration) {
    const std::uint32_t symbol =
      program.function_declarations[declaration].symbol;
    if(symbol >= call_graph.definition_by_symbol.size()) continue;
    const std::size_t target = call_graph.definition_by_symbol[symbol];
    if(target == InlineCallGraph::no_function()) continue;
    has_declaration[target] = 1;
    if(program.function_declarations[declaration].params.size() !=
       program.functions[target].params.size())
      invalid_shape[target] = 1;
  }

  for(std::size_t global = 0; global < program.globals.size(); ++global) {
    mark_reference(program.globals[global].init_operand, call_graph, &escaped);
    for(std::size_t item = 0;
        item < program.globals[global].data_items.size(); ++item) {
      const lowir_model::GlobalDefinition::DataItem & value =
        program.globals[global].data_items[item];
      if(value.kind == lowir_model::GlobalDefinition::DataItem::ITEM_ADDR &&
         value.symbol_id < call_graph.definition_by_symbol.size()) {
        const std::size_t target =
          call_graph.definition_by_symbol[value.symbol_id];
        if(target != InlineCallGraph::no_function()) escaped[target] = 1;
      }
    }
  }
  for(std::size_t alias = 0; alias < program.object_aliases.size(); ++alias) {
    const std::uint32_t symbol = program.object_aliases[alias].target_id;
    if(symbol < call_graph.definition_by_symbol.size()) {
      const std::size_t target = call_graph.definition_by_symbol[symbol];
      if(target != InlineCallGraph::no_function()) escaped[target] = 1;
    }
  }

  for(std::size_t caller = 0; caller < function_count; ++caller) {
    Function & function = program.functions[caller];
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        Instruction & instruction = function.blocks[block].instructions[index];
        const std::size_t target = direct_target(instruction, call_graph);
        if(target == InlineCallGraph::no_function())
          mark_reference(instruction.first, call_graph, &escaped);
        mark_reference(instruction.second, call_graph, &escaped);
        mark_reference(instruction.third, call_graph, &escaped);
        for(std::size_t arg = 0; arg < instruction.args.size(); ++arg)
          mark_reference(instruction.args[arg], call_graph, &escaped);
        if(target == InlineCallGraph::no_function()) continue;
        if(stats) ++stats->ipa_direct_call_visits;
        ++direct_calls[target];
        const Function & callee = program.functions[target];
        if(instruction.args.size() != callee.params.size() ||
           (instruction.has_call_signature &&
            instruction.call_params.size() != instruction.args.size())) {
          invalid_shape[target] = 1;
          continue;
        }
        const std::size_t first = parameter_offsets[target];
        for(std::size_t arg = 0; arg < instruction.args.size(); ++arg)
          update_agreement(&agreements[first + arg], instruction.args[arg]);
      }
  }

  std::vector<unsigned char> removed_parameters(parameter_offsets.back(), 0);
  std::vector<unsigned char> eligible(function_count, 0);
  std::vector<lowir_model::SymbolId> replacement_symbol(function_count);
  std::vector<Function> clones;
  std::vector<unsigned char> used_symbol_names(program.strings.size() + 1, 0);
  for(std::size_t symbol = 0; symbol < program.symbol_names.size(); ++symbol)
    used_symbol_names[program.symbol_names[symbol]] = 1;
  std::size_t clone_ordinal = 0;
  std::size_t cloned_instructions = 0;
  std::size_t changes = 0;
  std::vector<std::size_t> parameter_by_value;
  std::vector<unsigned char> used_parameters;
  for(std::size_t function = 0; function < function_count; ++function) {
    Function & target = program.functions[function];
    const bool internal = target.metadata.binding == lowir_model::SBM_INTERNAL;
    const std::size_t first = parameter_offsets[function];
    const std::size_t parameter_count =
      parameter_offsets[function + 1] - first;
    const ArgumentAgreement * function_agreements = parameter_count ?
      &agreements[first] : 0;
    const bool discardable_weak =
      target.metadata.binding == lowir_model::SBM_WEAK &&
      !call_graph.recursive[function] && !target.metadata.no_inline &&
      uniform_parameter_controls_cfg(
        target, function_agreements, parameter_count);
    const bool observable = escaped[function] ||
      target.metadata.object_output_root || target.metadata.keep_internal_alias ||
      target.metadata.role != lowir_model::SR_NONE ||
      target.metadata.tls_for_symbol_id.valid();
    if((!internal && !discardable_weak) || observable ||
       invalid_shape[function] ||
       direct_calls[function] == 0 ||
       target.params.empty() ||
       target.boundary.arity != lowir_model::CAM_FIXED) {
      if(stats && (internal || discardable_weak) &&
         direct_calls[function] && observable)
        ++stats->ipa_address_observable_rejects;
      continue;
    }
    eligible[function] = 1;
    if(stats) ++stats->ipa_candidate_functions;
    if(stats)
      for(std::size_t parameter = 0;
          parameter < parameter_count; ++parameter) {
        if(function_agreements[parameter].state == AS_UNIFORM)
          ++stats->ipa_uniform_parameters;
        else if(function_agreements[parameter].state == AS_DIFFERENT)
          ++stats->ipa_disagreeing_parameters;
      }
    Function specialized;
    Function * destination = &target;
    if(discardable_weak) {
      const std::size_t cost = instruction_count(target);
      if(clones.size() == kMaximumSpecializedClones ||
         cost > kMaximumClonedInstructions - cloned_instructions) {
        if(stats) ++stats->ipa_clone_budget_skips;
        continue;
      }
      specialized = target;
      destination = &specialized;
    }
    if(specialize_function(
         destination, function_agreements, parameter_count,
         &removed_parameters[first], &parameter_by_value,
         &used_parameters, stats)) {
      if(discardable_weak) {
        replacement_symbol[function] = allocate_clone_symbol(
          &program, &used_symbol_names, &clone_ordinal, "__o2spec");
        make_internal_clone(destination, replacement_symbol[function]);
        const unsigned char * removed = &removed_parameters[first];
        if(has_removed_parameter(removed, parameter_count))
          remove_masked(&destination->params, removed, parameter_count);
        cloned_instructions += instruction_count(*destination);
        clones.push_back(std::move(*destination));
        if(stats) {
          ++stats->ipa_specialized_clones;
          stats->ipa_cloned_instructions += clones.back().blocks.empty() ? 0 :
            instruction_count(clones.back());
        }
      }
      ++changes;
      if(stats) ++stats->ipa_functions_changed;
      if(rewritten_symbols && internal)
        (*rewritten_symbols)[target.symbol] = 1;
    }
  }

  for(std::size_t caller = 0; caller < function_count; ++caller) {
    Function & function = program.functions[caller];
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        Instruction & call = function.blocks[block].instructions[index];
        const std::size_t target = direct_target(call, call_graph);
        if(target == InlineCallGraph::no_function() || !eligible[target])
          continue;
        const std::size_t first = parameter_offsets[target];
        const std::size_t parameter_count =
          parameter_offsets[target + 1] - first;
        if(parameter_count == 0) continue;
        const unsigned char * removed = &removed_parameters[first];
        const bool remove_arguments = has_removed_parameter(
          removed, parameter_count);
        if(!remove_arguments && !replacement_symbol[target].valid()) continue;
        const std::size_t old_arguments = call.args.size();
        bool caller_needs_cleanup = false;
        if(remove_arguments)
          for(std::size_t argument = 0; argument < call.args.size(); ++argument)
            caller_needs_cleanup = caller_needs_cleanup ||
              (removed[argument] &&
               call.args[argument].kind == Operand::OP_TEMP);
        if(replacement_symbol[target].valid()) {
          call.first.symbol = replacement_symbol[target];
          call.first.address_binding = Operand::ADDRESS_LOCAL;
        }
        if(remove_arguments) {
          remove_masked(&call.args, removed, parameter_count);
          if(call.has_call_signature)
            remove_masked(&call.call_params, removed, parameter_count);
        }
        if(stats) {
          ++stats->ipa_calls_rewritten;
          stats->ipa_arguments_removed += old_arguments - call.args.size();
        }
        if(rewritten_symbols && caller_needs_cleanup)
          (*rewritten_symbols)[function.symbol] = 1;
      }
  }

  remove_internal_dead_parameters(
    &program, function_count, parameter_offsets, removed_parameters,
    eligible, has_declaration);
  append_specialized_clones(
    &program, function_count, &clones, rewritten_symbols);

  if(stats) {
    stats->ipa_peak_analysis_bytes =
      parameter_offsets.capacity() * sizeof(std::size_t) +
      agreements.capacity() * sizeof(ArgumentAgreement) +
      direct_calls.capacity() * sizeof(std::size_t) +
      escaped.capacity() + invalid_shape.capacity() +
      has_declaration.capacity() + removed_parameters.capacity() +
      eligible.capacity();
    stats->ipa_peak_analysis_bytes +=
      replacement_symbol.capacity() * sizeof(lowir_model::SymbolId) +
      clones.capacity() * sizeof(Function) + used_symbol_names.capacity() +
      parameter_by_value.capacity() * sizeof(std::size_t) +
      used_parameters.capacity();
  }
  return changes;
}

std::size_t specialize_o3_constant_groups(
    LowirProgram & program,
    const InlineCallGraph & call_graph,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats,
    const InlineCleanup * cleanup,
    StablePrefixSpecializationIndex * stable_prefix_specializations)
{
  const std::size_t function_count = program.functions.size();
  const ReadonlyByteStringIndex byte_strings(program);
  const std::vector<unsigned char> groupable_strings =
    internal_readonly_byte_strings(program, byte_strings);
  std::vector<std::size_t> direct_calls(function_count, 0);
  std::vector<unsigned char> escaped(function_count, 0);
  std::vector<unsigned char> invalid_shape(function_count, 0);
  std::vector<unsigned char> group_limit_reached(function_count, 0);
  std::vector<std::vector<ConstantGroup> > groups(function_count);
  for(std::size_t global = 0; global < program.globals.size(); ++global) {
    mark_reference(program.globals[global].init_operand, call_graph, &escaped);
    for(std::size_t item = 0;
        item < program.globals[global].data_items.size(); ++item) {
      const lowir_model::GlobalDefinition::DataItem & value =
        program.globals[global].data_items[item];
      if(value.kind != lowir_model::GlobalDefinition::DataItem::ITEM_ADDR ||
         value.symbol_id >= call_graph.definition_by_symbol.size())
        continue;
      const std::size_t target =
        call_graph.definition_by_symbol[value.symbol_id];
      if(target != InlineCallGraph::no_function()) escaped[target] = 1;
    }
  }
  for(std::size_t alias = 0; alias < program.object_aliases.size(); ++alias) {
    const std::uint32_t symbol = program.object_aliases[alias].target_id;
    if(symbol >= call_graph.definition_by_symbol.size()) continue;
    const std::size_t target = call_graph.definition_by_symbol[symbol];
    if(target != InlineCallGraph::no_function()) escaped[target] = 1;
  }
  for(std::size_t caller = 0; caller < function_count; ++caller) {
    const Function & function = program.functions[caller];
    const DirectGlobalAliases aliases = direct_global_aliases(function);
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function.blocks[block].instructions[index];
        const std::size_t target = direct_target(instruction, call_graph);
        if(target == InlineCallGraph::no_function())
          mark_reference(instruction.first, call_graph, &escaped);
        mark_reference(instruction.second, call_graph, &escaped);
        mark_reference(instruction.third, call_graph, &escaped);
        for(std::size_t argument = 0;
            argument < instruction.args.size(); ++argument)
          mark_reference(instruction.args[argument], call_graph, &escaped);
        if(target == InlineCallGraph::no_function()) continue;
        ++direct_calls[target];
        const Function & callee = program.functions[target];
        if(instruction.args.size() != callee.params.size() ||
           (instruction.has_call_signature &&
            instruction.call_params.size() != instruction.args.size())) {
          invalid_shape[target] = 1;
          continue;
        }
        for(std::size_t argument = 0;
            argument < instruction.args.size(); ++argument) {
          const Operand grouped =
            normalize_direct_global(instruction.args[argument], aliases);
          if(((grouped.kind == Operand::OP_INTEGER && grouped.has_int_value) ||
              structured_internal_integer_table(program, grouped) ||
              readonly_byte_string_argument(groupable_strings, grouped)) &&
             removable_parameter(callee.params[argument]))
            if(!count_constant_group(
                 &groups[target], argument, grouped) &&
               !group_limit_reached[target]) {
              group_limit_reached[target] = 1;
              if(stats) ++stats->ipa_clone_budget_skips;
            }
        }
      }
  }
  std::vector<unsigned char> used_symbol_names(program.strings.size() + 1, 0);
  for(std::size_t symbol = 0; symbol < program.symbol_names.size(); ++symbol)
    used_symbol_names[program.symbol_names[symbol]] = 1;
  std::vector<std::vector<GroupSelection> > selections(function_count);
  std::vector<Function> clones;
  std::size_t clone_ordinal = 0;
  std::size_t cloned_instructions = 0;
  std::vector<std::size_t> parameter_by_value;
  std::vector<unsigned char> used_parameters;
  for(std::size_t function = 0; function < function_count; ++function) {
    const Function & target = program.functions[function];
    const std::size_t target_instructions = instruction_count(target);
    const bool has_eh = function_has_exception_instructions(target);
    const bool observable = escaped[function] ||
      target.metadata.object_output_root || target.metadata.keep_internal_alias ||
      target.metadata.role != lowir_model::SR_NONE ||
      target.metadata.tls_for_symbol_id.valid();
    if(target.metadata.binding != lowir_model::SBM_INTERNAL || observable ||
       invalid_shape[function] || call_graph.recursive[function] ||
       target.metadata.no_inline || direct_calls[function] == 0 ||
       target.params.empty() ||
       target.boundary.arity != lowir_model::CAM_FIXED ||
       target_instructions > kO3MaximumStringGroupedTargetInstructions)
      continue;
    Function baseline = target;
    if(cleanup && cleanup->run)
      cleanup->run(&baseline, 0, cleanup->context);
    const std::size_t baseline_instructions = instruction_count(baseline);
    std::vector<unsigned char> selected(groups[function].size(), 0);
    std::size_t string_selections = 0;
    for(std::size_t round = 0;
        round <= kO3MaximumStringGroupsPerTarget; ++round) {
      std::size_t best_index = groups[function].size();
      for(std::size_t group = 0; group < groups[function].size(); ++group) {
        const ConstantGroup & candidate = groups[function][group];
        const bool candidate_is_string = readonly_byte_string_argument(
          groupable_strings, candidate.value);
        const std::size_t required_calls = candidate_is_string ? 1 :
          minimum_group_calls(program, candidate.value);
        if(selected[group] ||
           candidate.calls < required_calls ||
           candidate.calls == direct_calls[function] ||
           target_instructions > (candidate_is_string ?
             kO3MaximumStringGroupedTargetInstructions :
             kO3MaximumGroupedTargetInstructions) ||
           (has_eh && candidate_is_string) ||
           (candidate_is_string &&
            string_selections == kO3MaximumStringGroupsPerTarget) ||
           (round != 0 && !candidate_is_string))
          continue;
        if(best_index == groups[function].size() ||
           candidate.calls > groups[function][best_index].calls)
          best_index = group;
      }
      if(best_index == groups[function].size()) break;
      selected[best_index] = 1;
      const ConstantGroup & best = groups[function][best_index];
      Function clone = target;
      std::vector<ArgumentAgreement> agreements(clone.params.size());
      if(structured_internal_integer_table(program, best.value))
        collect_correlated_group_arguments(
          program, call_graph, function, best.parameter, best.value,
          &agreements);
      else {
        for(std::size_t parameter = 0;
            parameter < agreements.size(); ++parameter)
          agreements[parameter].state = AS_DIFFERENT;
        agreements[best.parameter].state = AS_UNIFORM;
        agreements[best.parameter].value = best.value;
      }
      std::vector<unsigned char> removed(clone.params.size(), 0);
      if(!specialize_function(
           &clone, &agreements[0], agreements.size(), &removed[0],
           &parameter_by_value, &used_parameters, 0))
        continue;
      const bool string_group = readonly_byte_string_argument(
        groupable_strings, best.value);
      if(string_group && !fold_readonly_byte_loads(&clone, program))
        continue;
      if(cleanup && cleanup->run) {
        for(std::size_t cleanup_round = 0;
            cleanup_round < 4; ++cleanup_round) {
          cleanup->run(&clone, 0, cleanup->context);
          if(!string_group || !fold_literal_control_with_phi_repair(&clone))
            break;
        }
        cleanup->run(&clone, 0, cleanup->context);
      }
      const lowir_model::GlobalDefinition * grouped_table =
        structured_internal_integer_table(program, best.value);
      const bool private_table = grouped_table &&
        private_call_table(
          program, call_graph, grouped_table->symbol, function,
          best.parameter);
      bool guarded_table = false;
      if(private_table) {
        std::vector<unsigned char> fixed_parameters(agreements.size(), 0);
        for(std::size_t parameter = 0;
            parameter < agreements.size(); ++parameter)
          fixed_parameters[parameter] =
            agreements[parameter].state == AS_UNIFORM;
        guarded_table = add_private_table_lower_prefilter(
          &clone, *grouped_table, fixed_parameters);
      }
      if(best.calls < kO3MinimumGroupedCalls && !guarded_table &&
         !string_group)
        continue;
      const std::size_t clone_instructions = instruction_count(clone);
      const std::size_t saved = baseline_instructions > clone_instructions ?
        baseline_instructions - clone_instructions : 0;
      const std::size_t minimum_saved =
        clone_instructions / best.calls +
        (clone_instructions % best.calls != 0);
      if(saved < minimum_saved && !guarded_table)
        continue;
      if(clones.size() == kO3MaximumGroupedClones ||
         clone_instructions >
           kO3MaximumGroupedInstructions - cloned_instructions) {
        if(stats) ++stats->ipa_clone_budget_skips;
        continue;
      }
      GroupSelection selection;
      selection.parameter = best.parameter;
      selection.value = best.value;
      selection.removed = removed;
      selection.replacement = allocate_clone_symbol(
        &program, &used_symbol_names, &clone_ordinal, "__o3groupspec");
      record_stable_prefix_specialization(
        stable_prefix_specializations, program, target, best, selection);
      make_internal_clone(&clone, selection.replacement);
      const std::size_t parameter_count = clone.params.size();
      if(selection.removed[parameter_count - 1])
        clone.boundary.query = lowir_model::CQM_DEFAULT;
      remove_masked(&clone.params, &selection.removed[0], parameter_count);
      cloned_instructions += clone_instructions;
      clones.push_back(std::move(clone));
      selections[function].push_back(std::move(selection));
      if(string_group) ++string_selections;
      if(stats) {
        ++stats->ipa_specialized_clones;
        stats->ipa_cloned_instructions += clone_instructions;
        ++stats->ipa_functions_changed;
        if(guarded_table) {
          ++stats->ipa_table_prefilter_clones;
          stats->ipa_table_prefilter_calls += best.calls;
        }
      }
    }
  }
  const std::size_t rewritten_calls = rewrite_o3_group_calls(
    &program, function_count, call_graph, selections,
    rewritten_symbols, stats);

  append_specialized_clones(
    &program, function_count, &clones, rewritten_symbols);
  if(stats)
    stats->ipa_peak_analysis_bytes = std::max(
      stats->ipa_peak_analysis_bytes,
      o3_group_analysis_bytes(
        direct_calls, escaped, invalid_shape, group_limit_reached,
        byte_strings, groupable_strings, groups, used_symbol_names,
        selections, clones,
        parameter_by_value, used_parameters));
  return rewritten_calls;
}

namespace {

const std::size_t kMaximumStableCallSignatures = 128;

bool repeat_path_instruction(const Instruction & instruction)
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
  case Instruction::IK_JUMP:
  case Instruction::IK_BRANCH:
  case Instruction::IK_SWITCH:
  case Instruction::IK_RETURN:
  case Instruction::IK_PHI:
    return true;
  default:
    return false;
  }
}

bool pure_return_region(const Function & function,
                        const lowir_analysis::Graph & graph,
                        std::size_t start, std::size_t guard,
                        std::size_t return_block)
{
  std::vector<unsigned char> state(function.blocks.size(), 0);
  std::vector<std::pair<std::size_t, std::size_t> > stack;
  stack.push_back(std::make_pair(start, 0));
  while(!stack.empty()) {
    const std::size_t block = stack.back().first;
    if(block == guard) return false;
    if(state[block] == 2) { stack.pop_back(); continue; }
    if(state[block] == 0) {
      state[block] = 1;
      const std::vector<Instruction> & instructions =
        function.blocks[block].instructions;
      if(instructions.empty()) return false;
      for(std::size_t i = 0; i < instructions.size(); ++i)
        if(!repeat_path_instruction(instructions[i])) return false;
      if(instructions.back().kind == Instruction::IK_RETURN) {
        if(block != return_block) return false;
        state[block] = 2;
        stack.pop_back();
        continue;
      }
      if(instructions.back().kind != Instruction::IK_JUMP &&
         instructions.back().kind != Instruction::IK_BRANCH &&
         instructions.back().kind != Instruction::IK_SWITCH)
        return false;
    }
    if(stack.back().second == graph.successors[block].size()) {
      state[block] = 2;
      stack.pop_back();
      continue;
    }
    const std::size_t successor =
      graph.successors[block][stack.back().second++];
    if(successor == guard || state[successor] == 1) return false;
    if(state[successor] == 0)
      stack.push_back(std::make_pair(successor, 0));
  }
  return state[return_block] == 2;
}

bool reachable_without(const lowir_analysis::Graph & graph,
                       std::size_t start, std::size_t target,
                       std::size_t excluded)
{
  if(start == excluded) return false;
  std::vector<unsigned char> seen(graph.successors.size(), 0);
  std::vector<std::size_t> work(1, start);
  seen[start] = 1;
  for(std::size_t cursor = 0; cursor < work.size(); ++cursor) {
    const std::size_t block = work[cursor];
    if(block == target) return true;
    for(std::size_t edge = 0;
        edge < graph.successors[block].size(); ++edge) {
      const std::size_t successor = graph.successors[block][edge];
      if(successor == excluded || seen[successor]) continue;
      seen[successor] = 1;
      work.push_back(successor);
    }
  }
  return false;
}

bool repeat_stable_query(
    const Function & function,
    const std::vector<unsigned char> & noreturn_symbols,
    Stats * stats)
{
  if(function.metadata.binding != lowir_model::SBM_INTERNAL ||
     function.blocks.empty() ||
     function.return_type.kind == lowir_model::LTK_VOID ||
     function.return_type.kind == lowir_model::LTK_OBJECT ||
     function.return_type.kind == lowir_model::LTK_I128 ||
     function.return_type.kind == lowir_model::LTK_F80)
    return false;
  std::size_t return_block = function.blocks.size();
  std::size_t returns = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    bool no_normal_continuation = false;
    for(std::size_t i = 0;
        i < function.blocks[block].instructions.size(); ++i) {
      const Instruction & instruction =
        function.blocks[block].instructions[i];
      if(instruction.kind == Instruction::IK_CALL &&
         (instruction.call_boundary.returns == lowir_model::CRM_NORETURN ||
          (instruction.first.kind == Operand::OP_GLOBAL &&
           instruction.first.symbol.valid() &&
           static_cast<std::uint32_t>(instruction.first.symbol) <
             noreturn_symbols.size() &&
           noreturn_symbols[instruction.first.symbol])))
        no_normal_continuation = true;
      if(instruction.kind == Instruction::IK_RETURN &&
         !no_normal_continuation) {
        return_block = block;
        ++returns;
      }
    }
  }
  if(returns != 1) return false;
  const lowir_analysis::Graph graph =
    lowir_analysis::build_graph(function, stats);
  std::vector<unsigned char> entry_path(function.blocks.size(), 0);
  std::size_t guard = 0;
  while(true) {
    if(guard >= function.blocks.size() || entry_path[guard]) return false;
    entry_path[guard] = 1;
    const std::vector<Instruction> & instructions =
      function.blocks[guard].instructions;
    if(instructions.empty()) return false;
    for(std::size_t i = 0; i < instructions.size(); ++i)
      if(!repeat_path_instruction(instructions[i])) return false;
    const Instruction & terminal = instructions.back();
    if(terminal.kind == Instruction::IK_BRANCH) break;
    if(terminal.kind != Instruction::IK_JUMP ||
       graph.successors[guard].size() != 1)
      return false;
    guard = graph.successors[guard][0];
  }
  if(graph.successors[guard].size() != 2) return false;
  for(std::size_t fast_edge = 0; fast_edge < 2; ++fast_edge) {
    const std::size_t fast = graph.successors[guard][fast_edge];
    const std::size_t slow = graph.successors[guard][1 - fast_edge];
    if(!pure_return_region(function, graph, fast, guard, return_block))
      continue;
    if(!reachable_without(graph, slow, guard, function.blocks.size()) ||
       reachable_without(graph, slow, return_block, guard))
      continue;
    return true;
  }
  return false;
}

struct StableCallSignature
{
  lowir_model::SymbolId symbol;
  std::vector<Operand> arguments;
  lowir_model::SymbolId prefix_family;
  std::vector<Operand> prefix_arguments;
  std::size_t calls = 0;
  std::uint64_t prefix_index = 0;
  bool stable_prefix = false;
};

bool same_signature(const StableCallSignature & signature,
                    const Instruction & call)
{
  if(signature.symbol != call.first.symbol ||
     signature.arguments.size() != call.args.size()) return false;
  for(std::size_t i = 0; i < call.args.size(); ++i)
    if(!same_operand(signature.arguments[i], call.args[i])) return false;
  return true;
}

bool stable_prefix_index(const Instruction & call, std::uint64_t * index)
{
  if(call.args.empty()) return false;
  const Operand & value = call.args.back();
  if(value.kind != Operand::OP_INTEGER || !value.has_int_value ||
     value.int_value < 0 || value.int_high != 0)
    return false;
  *index = static_cast<std::uint64_t>(value.int_value);
  return true;
}

bool same_stable_prefix_family(const StableCallSignature & cached,
                               const StableCallSignature & current)
{
  if(!cached.stable_prefix || !current.stable_prefix ||
     cached.prefix_family != current.prefix_family ||
     cached.prefix_arguments.size() != current.prefix_arguments.size() ||
     cached.prefix_index > current.prefix_index)
    return false;
  for(std::size_t i = 0; i < cached.prefix_arguments.size(); ++i)
    if(!same_operand(
         cached.prefix_arguments[i], current.prefix_arguments[i]))
      return false;
  return true;
}

bool classify_stable_prefix_call(
    const Instruction & call,
    const std::vector<unsigned char> & stable_prefix,
    const StablePrefixSpecializationIndex * specializations,
    lowir_model::SymbolId * family,
    std::uint64_t * index,
    std::vector<Operand> * arguments)
{
  const std::uint32_t symbol = call.first.symbol;
  if(symbol < stable_prefix.size() && stable_prefix[symbol] &&
     stable_prefix_index(call, index)) {
    *family = call.first.symbol;
    arguments->assign(call.args.begin(), call.args.end() - 1);
    return true;
  }
  if(!specializations || symbol >= specializations->known.size() ||
     !specializations->known[symbol])
    return false;
  *family = specializations->family[symbol];
  *index = specializations->index[symbol];
  *arguments = call.args;
  return true;
}

bool stable_call_barrier(const Instruction & instruction)
{
  if(instruction.volatile_access) return true;
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
  case Instruction::IK_JUMP:
  case Instruction::IK_BRANCH:
  case Instruction::IK_SWITCH:
  case Instruction::IK_RETURN:
  case Instruction::IK_PHI:
  case Instruction::IK_UNREACHABLE:
    return false;
  default:
    return true;
  }
}

struct AvailableStableCall
{
  Operand value;
  bool valid = false;
};

bool same_available(const AvailableStableCall & left,
                    const AvailableStableCall & right)
{
  return left.valid == right.valid &&
    (!left.valid || same_operand(left.value, right.value));
}

bool same_state(const std::vector<AvailableStableCall> & left,
                const std::vector<AvailableStableCall> & right)
{
  if(left.size() != right.size()) return false;
  for(std::size_t i = 0; i < left.size(); ++i)
    if(!same_available(left[i], right[i])) return false;
  return true;
}

void clear_available(std::vector<AvailableStableCall> * state)
{
  for(std::size_t i = 0; i < state->size(); ++i)
    (*state)[i].valid = false;
}

void retain_stable_prefix_results(
    const std::vector<StableCallSignature> & signatures,
    std::size_t current,
    std::vector<AvailableStableCall> * state)
{
  for(std::size_t signature = 0; signature < state->size(); ++signature)
    if((*state)[signature].valid &&
       !same_stable_prefix_family(signatures[signature], signatures[current]))
      (*state)[signature].valid = false;
}

bool advance_stable_call(
    const std::vector<StableCallSignature> & signatures,
    std::size_t signature,
    const Instruction & call,
    std::vector<AvailableStableCall> * state)
{
  if((*state)[signature].valid) return true;
  if(signatures[signature].stable_prefix)
    retain_stable_prefix_results(signatures, signature, state);
  else
    clear_available(state);
  (*state)[signature].valid = true;
  (*state)[signature].value.kind = Operand::OP_TEMP;
  (*state)[signature].value.value = call.dest;
  return false;
}

Instruction stable_call_replacement(const Instruction & call,
                                    const Operand & value)
{
  Instruction result;
  result.kind = Instruction::IK_COPY;
  result.dest = call.dest;
  result.type = call.type;
  result.first = value;
  result.debug_location = call.debug_location;
  return result;
}

}  // namespace

std::size_t eliminate_repeated_stable_calls(
    LowirProgram & program,
    std::vector<unsigned char> * rewritten_symbols,
    Stats * stats,
    const StablePrefixSpecializationIndex * stable_prefix_specializations)
{
  const std::vector<unsigned char> noreturn_symbols =
    noreturn_symbol_index(program);
  std::vector<unsigned char> stable(program.symbol_names.size(), 0);
  std::vector<unsigned char> stable_prefix(program.symbol_names.size(), 0);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    if(program.function_declarations[i].boundary.query ==
         lowir_model::CQM_STABLE_PREFIX)
      stable_prefix[program.function_declarations[i].symbol] = 1;
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    if(stats) ++stats->repeat_stable_function_visits;
    if(program.functions[i].boundary.query == lowir_model::CQM_STABLE_PREFIX)
      stable_prefix[program.functions[i].symbol] = 1;
    if(repeat_stable_query(
         program.functions[i], noreturn_symbols, stats)) {
      stable[program.functions[i].symbol] = 1;
      if(stats) ++stats->repeat_stable_functions;
    }
  }
  std::size_t rewrites = 0;
  for(std::size_t function_index = 0;
      function_index < program.functions.size(); ++function_index) {
    Function & function = program.functions[function_index];
    std::vector<StableCallSignature> signatures;
    std::vector<std::vector<std::size_t> > call_signatures(
      function.blocks.size());
    bool has_eh = false;
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      call_signatures[block].assign(
        function.blocks[block].instructions.size(),
        kMaximumStableCallSignatures);
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function.blocks[block].instructions[index];
        if(instruction.kind >= Instruction::IK_EH_TRY &&
           instruction.kind <= Instruction::IK_RESUME)
          has_eh = true;
        if(instruction.kind != Instruction::IK_CALL ||
           instruction.first.kind != Operand::OP_GLOBAL ||
           instruction.first.symbol >= stable.size() ||
           (!stable[instruction.first.symbol] &&
            !stable_prefix[instruction.first.symbol] &&
            (!stable_prefix_specializations ||
             instruction.first.symbol >=
               stable_prefix_specializations->known.size() ||
             !stable_prefix_specializations->known[
               instruction.first.symbol])) ||
           instruction.call_returns_void || !instruction.dest.valid())
          continue;
        std::uint64_t prefix_index = 0;
        lowir_model::SymbolId prefix_family;
        std::vector<Operand> prefix_arguments;
        const bool prefix = classify_stable_prefix_call(
          instruction, stable_prefix, stable_prefix_specializations,
          &prefix_family, &prefix_index, &prefix_arguments);
        if(!stable[instruction.first.symbol] && !prefix) continue;
        if(stats) ++stats->repeat_stable_call_sites;
        std::size_t signature = 0;
        while(signature < signatures.size() &&
              !same_signature(signatures[signature], instruction))
          ++signature;
        if(signature == signatures.size()) {
          if(signatures.size() == kMaximumStableCallSignatures) continue;
          StableCallSignature added;
          added.symbol = instruction.first.symbol;
          added.arguments = instruction.args;
          added.prefix_family = prefix_family;
          added.prefix_arguments.swap(prefix_arguments);
          added.prefix_index = prefix_index;
          added.stable_prefix = prefix;
          signatures.push_back(std::move(added));
        }
        ++signatures[signature].calls;
        call_signatures[block][index] = signature;
      }
    }
    if(has_eh || signatures.empty()) continue;
    for(std::size_t signature = 0;
        signature < signatures.size(); ++signature) {
      if(signatures[signature].calls < 2 &&
         !signatures[signature].stable_prefix) {
        for(std::size_t block = 0; block < call_signatures.size(); ++block)
          for(std::size_t index = 0;
              index < call_signatures[block].size(); ++index)
            if(call_signatures[block][index] == signature)
              call_signatures[block][index] = kMaximumStableCallSignatures;
      } else if(stats) {
        ++stats->repeat_stable_signatures;
      }
    }
    const lowir_analysis::Graph graph =
      lowir_analysis::build_graph(function, stats);
    typedef std::vector<AvailableStableCall> StableState;
    const StableState empty(signatures.size());
    std::vector<StableState> outgoing(function.blocks.size(), empty);
    std::deque<std::size_t> work;
    std::vector<unsigned char> queued(function.blocks.size(), 1);
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      work.push_back(block);
      if(stats) ++stats->worklist_pushes;
    }
    if(stats) {
      std::size_t analysis_bytes = stable.capacity() +
        noreturn_symbols.capacity() + queued.capacity() +
        signatures.capacity() * sizeof(StableCallSignature) +
        call_signatures.capacity() *
          sizeof(std::vector<std::size_t>) +
        outgoing.capacity() * sizeof(StableState);
      for(std::size_t signature = 0;
          signature < signatures.size(); ++signature)
        analysis_bytes += (signatures[signature].arguments.capacity() +
          signatures[signature].prefix_arguments.capacity()) * sizeof(Operand);
      for(std::size_t block = 0; block < function.blocks.size(); ++block) {
        analysis_bytes += call_signatures[block].capacity() *
          sizeof(std::size_t);
        analysis_bytes += outgoing[block].capacity() *
          sizeof(AvailableStableCall);
      }
      stats->repeat_stable_peak_analysis_bytes = std::max(
        stats->repeat_stable_peak_analysis_bytes, analysis_bytes);
    }
    std::size_t updates = 0;
    const std::size_t update_budget =
      (function.blocks.size() + 1) * (signatures.size() + 1) * 8;
    bool exhausted = false;
    while(!work.empty() && !exhausted) {
      const std::size_t block = work.front();
      work.pop_front();
      queued[block] = 0;
      StableState state = empty;
      if(block != 0 && graph.predecessors[block].size() != 0) {
        state = outgoing[graph.predecessors[block][0]];
        for(std::size_t edge = 1;
            edge < graph.predecessors[block].size(); ++edge) {
          const StableState & predecessor =
            outgoing[graph.predecessors[block][edge]];
          for(std::size_t signature = 0;
              signature < state.size(); ++signature)
            if(!same_available(state[signature], predecessor[signature]))
              state[signature].valid = false;
        }
      }
      const std::vector<Instruction> & instructions =
        function.blocks[block].instructions;
      for(std::size_t index = 0; index < instructions.size(); ++index) {
        const std::size_t signature = call_signatures[block][index];
        if(signature < signatures.size() &&
           (signatures[signature].calls >= 2 ||
            signatures[signature].stable_prefix)) {
          advance_stable_call(
            signatures, signature, instructions[index], &state);
        } else if(stable_call_barrier(instructions[index]))
          clear_available(&state);
      }
      if(same_state(state, outgoing[block])) continue;
      outgoing[block].swap(state);
      if(stats) ++stats->dataflow_updates;
      if(++updates > update_budget) { exhausted = true; break; }
      for(std::size_t edge = 0;
          edge < graph.successors[block].size(); ++edge) {
        const std::size_t successor = graph.successors[block][edge];
        if(queued[successor]) continue;
        queued[successor] = 1;
        work.push_back(successor);
        if(stats) ++stats->worklist_pushes;
      }
    }
    if(exhausted) {
      if(stats) {
        ++stats->repeat_stable_budget_skips;
        ++stats->budget_skips;
      }
      continue;
    }
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      StableState state = empty;
      if(block != 0 && graph.predecessors[block].size() != 0) {
        state = outgoing[graph.predecessors[block][0]];
        for(std::size_t edge = 1;
            edge < graph.predecessors[block].size(); ++edge) {
          const StableState & predecessor =
            outgoing[graph.predecessors[block][edge]];
          for(std::size_t signature = 0;
              signature < state.size(); ++signature)
            if(!same_available(state[signature], predecessor[signature]))
              state[signature].valid = false;
        }
      }
      std::vector<Instruction> & instructions =
        function.blocks[block].instructions;
      for(std::size_t index = 0; index < instructions.size(); ++index) {
        const std::size_t signature = call_signatures[block][index];
        if(signature < signatures.size() &&
           (signatures[signature].calls >= 2 ||
            signatures[signature].stable_prefix)) {
          if(advance_stable_call(
               signatures, signature, instructions[index], &state)) {
            instructions[index] = stable_call_replacement(
              instructions[index], state[signature].value);
            ++rewrites;
            if(stats) {
              ++stats->repeat_stable_reuses;
              ++stats->rewrites;
            }
            if(rewritten_symbols)
              (*rewritten_symbols)[function.symbol] = 1;
          }
        } else if(stable_call_barrier(instructions[index]))
          clear_available(&state);
      }
    }
  }
  return rewrites;
}

}  // namespace lowir_opt
