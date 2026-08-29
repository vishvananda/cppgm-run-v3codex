#include "lowir/optimize/interprocedural_specialization.h"

#include "lowir/optimize/inline_o1.h"
#include "lowir/optimize/pipeline.h"

#include <algorithm>
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
const std::size_t kO3MaximumConstantGroupsPerTarget = 64;
const std::size_t kO3MaximumGroupedClones = 24;
const std::size_t kO3MaximumGroupedInstructions = 1536;

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
  bool active = false;
  std::size_t parameter = 0;
  Operand value;
  lowir_model::SymbolId replacement;
  std::vector<unsigned char> removed;
};

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
    remove_masked(
      &program->functions[function].params, removed, parameter_count);
    if(has_declaration[function])
      for(std::size_t declaration = 0;
          declaration < program->function_declarations.size(); ++declaration)
        if(program->function_declarations[declaration].symbol == symbol)
          remove_masked(
            &program->function_declarations[declaration].params,
            removed, parameter_count);
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
    const InlineCleanup * cleanup)
{
  const std::size_t function_count = program.functions.size();
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
            argument < instruction.args.size(); ++argument)
          if(instruction.args[argument].kind == Operand::OP_INTEGER &&
             instruction.args[argument].has_int_value &&
             removable_parameter(callee.params[argument]))
            if(!count_constant_group(
                 &groups[target], argument, instruction.args[argument]) &&
               !group_limit_reached[target]) {
              group_limit_reached[target] = 1;
              if(stats) ++stats->ipa_clone_budget_skips;
            }
      }
  }

  std::vector<unsigned char> used_symbol_names(program.strings.size() + 1, 0);
  for(std::size_t symbol = 0; symbol < program.symbol_names.size(); ++symbol)
    used_symbol_names[program.symbol_names[symbol]] = 1;
  std::vector<GroupSelection> selections(function_count);
  std::vector<Function> clones;
  std::size_t clone_ordinal = 0;
  std::size_t cloned_instructions = 0;
  std::vector<std::size_t> parameter_by_value;
  std::vector<unsigned char> used_parameters;

  for(std::size_t function = 0; function < function_count; ++function) {
    const Function & target = program.functions[function];
    const std::size_t target_instructions = instruction_count(target);
    const bool observable = escaped[function] ||
      target.metadata.object_output_root || target.metadata.keep_internal_alias ||
      target.metadata.role != lowir_model::SR_NONE ||
      target.metadata.tls_for_symbol_id.valid();
    if(target.metadata.binding != lowir_model::SBM_INTERNAL || observable ||
       invalid_shape[function] || call_graph.recursive[function] ||
       target.metadata.no_inline || direct_calls[function] == 0 ||
       target.params.empty() ||
       target.boundary.arity != lowir_model::CAM_FIXED ||
       target_instructions > kO3MaximumGroupedTargetInstructions)
      continue;

    const ConstantGroup * best = 0;
    for(std::size_t group = 0; group < groups[function].size(); ++group) {
      const ConstantGroup & candidate = groups[function][group];
      if(candidate.calls < kO3MinimumGroupedCalls ||
         candidate.calls == direct_calls[function])
        continue;
      if(!best || candidate.calls > best->calls) best = &candidate;
    }
    if(!best) continue;

    Function baseline = target;
    if(cleanup && cleanup->run)
      cleanup->run(&baseline, 0, cleanup->context);
    const std::size_t baseline_instructions = instruction_count(baseline);
    Function clone = target;
    std::vector<ArgumentAgreement> agreements(clone.params.size());
    for(std::size_t parameter = 0; parameter < agreements.size(); ++parameter)
      agreements[parameter].state = AS_DIFFERENT;
    agreements[best->parameter].state = AS_UNIFORM;
    agreements[best->parameter].value = best->value;
    std::vector<unsigned char> removed(clone.params.size(), 0);
    if(!specialize_function(
         &clone, &agreements[0], agreements.size(), &removed[0],
         &parameter_by_value, &used_parameters, 0))
      continue;
    if(cleanup && cleanup->run)
      cleanup->run(&clone, 0, cleanup->context);
    const std::size_t clone_instructions = instruction_count(clone);
    const std::size_t saved = baseline_instructions > clone_instructions ?
      baseline_instructions - clone_instructions : 0;
    const std::size_t minimum_saved =
      clone_instructions / best->calls +
      (clone_instructions % best->calls != 0);
    if(saved < minimum_saved)
      continue;
    if(clones.size() == kO3MaximumGroupedClones ||
       clone_instructions >
         kO3MaximumGroupedInstructions - cloned_instructions) {
      if(stats) ++stats->ipa_clone_budget_skips;
      continue;
    }

    GroupSelection & selection = selections[function];
    selection.active = true;
    selection.parameter = best->parameter;
    selection.value = best->value;
    selection.removed = removed;
    selection.replacement = allocate_clone_symbol(
      &program, &used_symbol_names, &clone_ordinal, "__o3groupspec");
    make_internal_clone(&clone, selection.replacement);
    const std::size_t parameter_count = clone.params.size();
    remove_masked(&clone.params, &selection.removed[0], parameter_count);
    cloned_instructions += clone_instructions;
    clones.push_back(std::move(clone));
    if(stats) {
      ++stats->ipa_specialized_clones;
      stats->ipa_cloned_instructions += clone_instructions;
      ++stats->ipa_functions_changed;
    }
  }

  std::size_t rewritten_calls = 0;
  for(std::size_t caller = 0; caller < function_count; ++caller) {
    Function & function = program.functions[caller];
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        Instruction & call = function.blocks[block].instructions[index];
        const std::size_t target = direct_target(call, call_graph);
        if(target == InlineCallGraph::no_function() ||
           !selections[target].active)
          continue;
        const GroupSelection & selection = selections[target];
        if(selection.parameter >= call.args.size() ||
           !same_argument(call.args[selection.parameter], selection.value))
          continue;
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

  append_specialized_clones(
    &program, function_count, &clones, rewritten_symbols);
  if(stats) {
    std::size_t analysis_bytes =
      direct_calls.capacity() * sizeof(std::size_t) + escaped.capacity() +
      invalid_shape.capacity() + group_limit_reached.capacity() +
      groups.capacity() * sizeof(std::vector<ConstantGroup>) +
      used_symbol_names.capacity() +
      selections.capacity() * sizeof(GroupSelection) +
      clones.capacity() * sizeof(Function) +
      parameter_by_value.capacity() * sizeof(std::size_t) +
      used_parameters.capacity();
    for(std::size_t function = 0; function < function_count; ++function) {
      analysis_bytes +=
        groups[function].capacity() * sizeof(ConstantGroup);
      analysis_bytes += selections[function].removed.capacity();
    }
    stats->ipa_peak_analysis_bytes = std::max(
      stats->ipa_peak_analysis_bytes, analysis_bytes);
  }
  return rewritten_calls;
}

}  // namespace lowir_opt
