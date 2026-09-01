#include "lowir/optimize/terminal_query_split.h"

#include "lowir/analysis/function.h"
#include "lowir/analysis/inline.h"
#include "lowir/optimize/pipeline.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::LowirProgram;
using lowir_model::Operand;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);
const std::size_t kMinimumQueryCalls = 8;

bool direct_gpr_scalar(const LowType & type)
{
  return (type.kind >= lowir_model::LTK_I1 &&
          type.kind <= lowir_model::LTK_I64) ||
    type.kind == lowir_model::LTK_PTR;
}

bool direct_scalar_parameter(const lowir_model::Parameter & parameter)
{
  return parameter.metadata.passing == lowir_model::PPM_DIRECT &&
    parameter.type.kind != lowir_model::LTK_OBJECT &&
    parameter.type.kind != lowir_model::LTK_I128 &&
    parameter.type.kind != lowir_model::LTK_F80;
}

bool same_literal_or_identity(const Operand & left, const Operand & right)
{
  if(left.kind != right.kind ||
     left.address_binding != right.address_binding)
    return false;
  switch(left.kind) {
  case Operand::OP_TEMP: return left.value == right.value;
  case Operand::OP_SLOT: return left.slot == right.slot;
  case Operand::OP_GLOBAL: return left.symbol == right.symbol;
  case Operand::OP_LABEL: return left.block == right.block;
  case Operand::OP_INTEGER:
    return left.has_int_value == right.has_int_value &&
      left.int_value == right.int_value && left.int_high == right.int_high &&
      lowir_model::same_lowir_type(left.literal_type, right.literal_type);
  case Operand::OP_FLOAT:
    return left.has_float_bits == right.has_float_bits &&
      left.literal_low == right.literal_low &&
      left.literal_high == right.literal_high &&
      lowir_model::same_lowir_type(left.literal_type, right.literal_type);
  }
  return false;
}

struct Definition
{
  const Instruction * instruction = 0;
  std::size_t block = kNoIndex;
  std::size_t index = kNoIndex;
};

struct IntegerRange
{
  unsigned long long minimum = 0;
  unsigned long long maximum = 0;
  bool known = false;
};

struct AddressRange
{
  std::size_t parameter = kNoIndex;
  long long minimum = 0;
  long long maximum = 0;
  bool known = false;
};

class QueryValueFacts
{
public:
  explicit QueryValueFacts(const Function & function)
    : function_(function), definitions_(function.value_names.size()),
      parameter_by_value_(function.value_names.size(), kNoIndex),
      integer_state_(function.value_names.size(), 0),
      integer_ranges_(function.value_names.size()),
      address_state_(function.value_names.size(), 0),
      address_ranges_(function.value_names.size())
  {
    for(std::size_t parameter = 0; parameter < function.params.size();
        ++parameter)
      parameter_by_value_[function.params[parameter].value] = parameter;
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t index = 0;
          index < function.blocks[block].instructions.size(); ++index) {
        const Instruction & instruction =
          function.blocks[block].instructions[index];
        if(!instruction.dest.valid()) continue;
        Definition & definition = definitions_[instruction.dest];
        definition.instruction = &instruction;
        definition.block = block;
        definition.index = index;
      }
  }

  Definition definition(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP ||
       static_cast<std::size_t>(operand.value) >= definitions_.size())
      return Definition();
    return definitions_[operand.value];
  }

  bool is_parameter(lowir_model::ValueId value) const
  {
    return static_cast<std::size_t>(value) < parameter_by_value_.size() &&
      parameter_by_value_[value] != kNoIndex;
  }

  AddressRange address(const Operand & operand)
  {
    if(operand.kind != Operand::OP_TEMP) return AddressRange();
    const std::size_t value = operand.value;
    if(value >= definitions_.size()) return AddressRange();
    const std::size_t parameter = parameter_by_value_[value];
    if(parameter != kNoIndex &&
       function_.params[parameter].type.kind == lowir_model::LTK_PTR) {
      AddressRange result;
      result.parameter = parameter;
      result.known = true;
      return result;
    }
    if(address_state_[value] == 2) return address_ranges_[value];
    if(address_state_[value] == 1) return AddressRange();
    address_state_[value] = 1;
    AddressRange result;
    const Instruction * instruction = definitions_[value].instruction;
    if(instruction && instruction->kind == Instruction::IK_COPY &&
       instruction->type.kind == lowir_model::LTK_PTR) {
      result = address(instruction->first);
    } else if(instruction && instruction->kind == Instruction::IK_INDEX &&
              instruction->type.storage_size) {
      result = address(instruction->first);
      const IntegerRange offset = integer(instruction->second);
      if(result.known && offset.known) {
        const __int128 minimum = static_cast<__int128>(result.minimum) +
          static_cast<__int128>(offset.minimum) *
            instruction->type.storage_size;
        const __int128 maximum = static_cast<__int128>(result.maximum) +
          static_cast<__int128>(offset.maximum) *
            instruction->type.storage_size;
        if(minimum > std::numeric_limits<long long>::max() ||
           maximum > std::numeric_limits<long long>::max())
          result = AddressRange();
        else {
          result.minimum = static_cast<long long>(minimum);
          result.maximum = static_cast<long long>(maximum);
        }
      } else {
        result = AddressRange();
      }
    }
    address_ranges_[value] = result;
    address_state_[value] = 2;
    return result;
  }

  bool within_complete_object(const AddressRange & address,
                              std::size_t bytes) const
  {
    if(!address.known || address.parameter >= function_.params.size() ||
       address.minimum < 0 || address.maximum < address.minimum)
      return false;
    const std::size_t object_bytes =
      function_.params[address.parameter].metadata.object_bytes;
    if(!object_bytes) return false;
    const __int128 end = static_cast<__int128>(address.maximum) + bytes;
    return end <= object_bytes;
  }

  bool relative_constant_offset(const Operand & derived, const Operand & base,
                                long long * offset) const
  {
    if(derived.kind == Operand::OP_TEMP && base.kind == Operand::OP_TEMP &&
       derived.value == base.value) {
      *offset = 0;
      return true;
    }
    if(derived.kind != Operand::OP_TEMP ||
       static_cast<std::size_t>(derived.value) >= definitions_.size())
      return false;
    const Instruction * instruction = definitions_[derived.value].instruction;
    if(instruction && instruction->kind == Instruction::IK_COPY &&
       instruction->type.kind == lowir_model::LTK_PTR)
      return relative_constant_offset(instruction->first, base, offset);
    if(!instruction || instruction->kind != Instruction::IK_INDEX ||
       instruction->second.kind != Operand::OP_INTEGER ||
       !instruction->second.has_int_value || instruction->second.int_high != 0 ||
       !instruction->type.storage_size)
      return false;
    long long prefix = 0;
    if(!relative_constant_offset(instruction->first, base, &prefix))
      return false;
    const __int128 result = static_cast<__int128>(prefix) +
      static_cast<__int128>(instruction->second.int_value) *
        instruction->type.storage_size;
    if(result < std::numeric_limits<long long>::min() ||
       result > std::numeric_limits<long long>::max())
      return false;
    *offset = static_cast<long long>(result);
    return true;
  }

private:
  IntegerRange integer(const Operand & operand)
  {
    if(operand.kind == Operand::OP_INTEGER && operand.has_int_value &&
       operand.int_high == 0 && operand.int_value >= 0) {
      IntegerRange result;
      result.minimum = result.maximum =
        static_cast<unsigned long long>(operand.int_value);
      result.known = true;
      return result;
    }
    if(operand.kind != Operand::OP_TEMP) return IntegerRange();
    const std::size_t value = operand.value;
    if(value >= definitions_.size()) return IntegerRange();
    if(integer_state_[value] == 2) return integer_ranges_[value];
    if(integer_state_[value] == 1) return IntegerRange();
    integer_state_[value] = 1;
    IntegerRange result;
    const Instruction * instruction = definitions_[value].instruction;
    if(instruction && instruction->kind == Instruction::IK_CONST)
      result = integer(instruction->first);
    else if(instruction && instruction->kind == Instruction::IK_COPY)
      result = integer(instruction->first);
    else if(instruction && instruction->kind == Instruction::IK_BINARY &&
            instruction->op.kind == lowir_model::LowOperation::LOP_AND) {
      IntegerRange mask = integer(instruction->second);
      if(!mask.known || mask.minimum != mask.maximum)
        mask = integer(instruction->first);
      if(mask.known && mask.minimum == mask.maximum) {
        result.minimum = 0;
        result.maximum = mask.maximum;
        result.known = true;
      }
    } else if(instruction &&
              instruction->kind == Instruction::IK_BINARY &&
              instruction->op.kind == lowir_model::LowOperation::LOP_MUL) {
      const IntegerRange left = integer(instruction->first);
      const IntegerRange right = integer(instruction->second);
      if(left.known && right.known) {
        const __int128 maximum = static_cast<__int128>(left.maximum) *
          right.maximum;
        if(maximum <= std::numeric_limits<unsigned long long>::max()) {
          result.minimum = left.minimum * right.minimum;
          result.maximum = static_cast<unsigned long long>(maximum);
          result.known = true;
        }
      }
    }
    integer_ranges_[value] = result;
    integer_state_[value] = 2;
    return result;
  }

  const Function & function_;
  std::vector<Definition> definitions_;
  std::vector<std::size_t> parameter_by_value_;
  std::vector<unsigned char> integer_state_;
  std::vector<IntegerRange> integer_ranges_;
  std::vector<unsigned char> address_state_;
  std::vector<AddressRange> address_ranges_;
};

struct ComparedPair
{
  lowir_model::ValueId left;
  lowir_model::ValueId right;
  unsigned char state = 0;
};

class EquivalentQueryValues
{
public:
  EquivalentQueryValues(const QueryValueFacts & facts,
                        std::size_t slow, std::size_t terminal)
    : facts_(facts), slow_(slow), terminal_(terminal)
  {}

  bool equivalent(const Operand & left, const Operand & right)
  {
    if(left.kind != Operand::OP_TEMP || right.kind != Operand::OP_TEMP)
      return same_literal_or_identity(left, right);
    if(left.value == right.value) return true;
    ComparedPair * memo = find(left.value, right.value);
    if(memo) return memo->state == 2;
    ComparedPair entry;
    entry.left = left.value;
    entry.right = right.value;
    entry.state = 1;
    pairs_.push_back(entry);
    const std::size_t memo_index = pairs_.size() - 1;
    const bool result = equivalent_definitions(left, right);
    pairs_[memo_index].state = result ? 2 : 3;
    return result;
  }

  const std::vector<std::size_t> & slow_loads() const { return slow_loads_; }

private:
  ComparedPair * find(lowir_model::ValueId left, lowir_model::ValueId right)
  {
    for(std::size_t i = 0; i < pairs_.size(); ++i)
      if(pairs_[i].left == left && pairs_[i].right == right)
        return &pairs_[i];
    return 0;
  }

  bool equivalent_definitions(const Operand & left, const Operand & right)
  {
    const Definition left_definition = facts_.definition(left);
    const Definition right_definition = facts_.definition(right);
    const Instruction * a = left_definition.instruction;
    const Instruction * b = right_definition.instruction;
    if(!a || !b || a->kind != b->kind ||
       !lowir_model::same_lowir_type(a->type, b->type) ||
       a->op != b->op || a->volatile_access || b->volatile_access)
      return false;
    switch(a->kind) {
    case Instruction::IK_CONST:
    case Instruction::IK_COPY:
    case Instruction::IK_ADDR:
    case Instruction::IK_UNARY:
      return equivalent(a->first, b->first);
    case Instruction::IK_INDEX:
      return a->index_projection == b->index_projection &&
        equivalent(a->first, b->first) &&
        equivalent(a->second, b->second);
    case Instruction::IK_BINARY:
    case Instruction::IK_CMP:
      return equivalent(a->first, b->first) &&
        equivalent(a->second, b->second);
    case Instruction::IK_CONVERT:
      return lowir_model::same_lowir_type(a->source_type, b->source_type) &&
        equivalent(a->first, b->first);
    case Instruction::IK_LOAD:
      if(!equivalent(a->first, b->first)) return false;
      if(left_definition.block == slow_ &&
         right_definition.block == terminal_ &&
         std::find(slow_loads_.begin(), slow_loads_.end(),
                   left_definition.index) == slow_loads_.end())
        slow_loads_.push_back(left_definition.index);
      return left_definition.block == slow_ &&
        right_definition.block == terminal_;
    default:
      return false;
    }
  }

  const QueryValueFacts & facts_;
  std::size_t slow_;
  std::size_t terminal_;
  std::vector<ComparedPair> pairs_;
  std::vector<std::size_t> slow_loads_;
};

bool ranges_overlap(const AddressRange & left, std::size_t left_bytes,
                    const AddressRange & right, std::size_t right_bytes)
{
  if(!left.known || !right.known || left.parameter != right.parameter)
    return true;
  const __int128 left_end = static_cast<__int128>(left.maximum) + left_bytes;
  const __int128 right_end =
    static_cast<__int128>(right.maximum) + right_bytes;
  return static_cast<__int128>(left.minimum) < right_end &&
    static_cast<__int128>(right.minimum) < left_end;
}

bool extracted_value_available(const Operand & operand,
                               const QueryValueFacts & facts,
                               std::size_t slow, std::size_t before)
{
  if(operand.kind == Operand::OP_SLOT) return false;
  if(operand.kind != Operand::OP_TEMP) return true;
  if(facts.is_parameter(operand.value)) return true;
  const Definition definition = facts.definition(operand);
  return definition.instruction && definition.block == slow &&
    definition.index < before;
}

bool extractable_slow_block(const Function & function,
                            const QueryValueFacts & facts, std::size_t slow,
                            std::size_t terminal)
{
  const std::vector<Instruction> & instructions =
    function.blocks[slow].instructions;
  if(instructions.empty() ||
     instructions.back().kind != Instruction::IK_JUMP ||
     instructions.back().first.kind != Operand::OP_LABEL ||
     instructions.back().first.block != function.blocks[terminal].id)
    return false;
  std::size_t calls = 0;
  for(std::size_t index = 0; index < instructions.size(); ++index) {
    const Instruction & instruction = instructions[index];
    if(instruction.volatile_access) return false;
    switch(instruction.kind) {
    case Instruction::IK_CONST:
    case Instruction::IK_COPY:
    case Instruction::IK_ADDR:
    case Instruction::IK_LOAD:
    case Instruction::IK_STORE:
    case Instruction::IK_INDEX:
    case Instruction::IK_UNARY:
    case Instruction::IK_BINARY:
    case Instruction::IK_CMP:
    case Instruction::IK_CONVERT:
    case Instruction::IK_JUMP:
      break;
    case Instruction::IK_CALL:
      if(instruction.first.kind != Operand::OP_GLOBAL) return false;
      ++calls;
      break;
    default:
      return false;
    }
    const Operand * fixed[] = {
      &instruction.first, &instruction.second, &instruction.third
    };
    for(std::size_t operand = 0; operand < 3; ++operand)
      if(!extracted_value_available(*fixed[operand], facts, slow, index))
        return false;
    for(std::size_t operand = 0; operand < instruction.args.size(); ++operand)
      if(!extracted_value_available(
           instruction.args[operand], facts, slow, index))
        return false;
  }
  return calls != 0;
}

bool pure_terminal_load_return(const Function & function,
                               const QueryValueFacts & facts,
                               std::size_t terminal,
                               const Instruction ** load)
{
  const std::vector<Instruction> & instructions =
    function.blocks[terminal].instructions;
  if(instructions.size() < 2 ||
     instructions.back().kind != Instruction::IK_RETURN ||
     instructions.back().first.kind != Operand::OP_TEMP ||
     !lowir_model::same_lowir_type(
       instructions.back().type, function.return_type))
    return false;
  for(std::size_t index = 0; index + 1 < instructions.size(); ++index) {
    const Instruction & instruction = instructions[index];
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
      break;
    default:
      return false;
    }
  }
  const Definition definition = facts.definition(instructions.back().first);
  if(!definition.instruction || definition.block != terminal ||
     definition.instruction->kind != Instruction::IK_LOAD ||
     definition.instruction->volatile_access ||
     !lowir_model::same_lowir_type(
       definition.instruction->type, function.return_type))
    return false;
  *load = definition.instruction;
  return true;
}

bool memory_stable_after(const Function & function, QueryValueFacts * facts,
                         std::size_t slow, std::size_t first,
                         const Operand & observed_operand,
                         const AddressRange & observed,
                         std::size_t observed_bytes)
{
  const std::vector<Instruction> & instructions =
    function.blocks[slow].instructions;
  for(std::size_t index = first; index < instructions.size(); ++index) {
    const Instruction & instruction = instructions[index];
    if(instruction.kind == Instruction::IK_CALL) return false;
    if(instruction.kind != Instruction::IK_STORE) continue;
    const AddressRange written = facts->address(instruction.second);
    const std::size_t bytes = instruction.type.storage_size;
    if(!bytes || !facts->within_complete_object(written, bytes))
      return false;
    long long relative = 0;
    if(facts->relative_constant_offset(
         instruction.second, observed_operand, &relative)) {
      AddressRange relative_written;
      relative_written.parameter = observed.parameter;
      relative_written.minimum = relative_written.maximum = relative;
      relative_written.known = true;
      AddressRange relative_observed;
      relative_observed.parameter = observed.parameter;
      relative_observed.known = true;
      if(ranges_overlap(
           relative_written, bytes, relative_observed, observed_bytes))
        return false;
    } else if(ranges_overlap(written, bytes, observed, observed_bytes)) {
      return false;
    }
  }
  return true;
}

struct QuerySplitCandidate
{
  std::size_t function = kNoIndex;
  std::size_t slow = kNoIndex;
  std::size_t terminal = kNoIndex;
  std::size_t store = kNoIndex;
  std::size_t calls = 0;
};

bool candidate_shape(const Function & function)
{
  if(function.metadata.binding != lowir_model::SBM_INTERNAL ||
     function.metadata.no_inline || function.blocks.size() != 3 ||
     !function.slots.empty() || !direct_gpr_scalar(function.return_type) ||
     function.boundary.arity != lowir_model::CAM_FIXED ||
     function.boundary.query != lowir_model::CQM_DEFAULT ||
     function.params.size() > 6)
    return false;
  for(std::size_t parameter = 0; parameter < function.params.size();
      ++parameter)
    if(!direct_scalar_parameter(function.params[parameter])) return false;
  return !function.blocks[0].instructions.empty() &&
    function.blocks[0].instructions.back().kind == Instruction::IK_BRANCH;
}

bool find_stored_terminal_value(const Function & function,
                                std::size_t slow, std::size_t terminal,
                                QueryValueFacts * facts,
                                const Instruction & terminal_load,
                                std::size_t * store_index)
{
  const std::vector<Instruction> & instructions =
    function.blocks[slow].instructions;
  const AddressRange returned = facts->address(terminal_load.first);
  const std::size_t returned_bytes = terminal_load.type.storage_size;
  if(!returned_bytes ||
     !facts->within_complete_object(returned, returned_bytes))
    return false;
  for(std::size_t index = 0; index + 1 < instructions.size(); ++index) {
    const Instruction & store = instructions[index];
    if(store.kind != Instruction::IK_STORE || store.volatile_access ||
       !lowir_model::same_lowir_type(store.type, terminal_load.type) ||
       store.first.kind != Operand::OP_TEMP)
      continue;
    const Definition stored = facts->definition(store.first);
    if(!stored.instruction || stored.block != slow ||
       stored.instruction->kind != Instruction::IK_CALL ||
       stored.instruction->call_returns_void ||
       !lowir_model::same_lowir_type(stored.instruction->type, store.type))
      continue;
    EquivalentQueryValues equivalent(*facts, slow, terminal);
    if(!equivalent.equivalent(store.second, terminal_load.first)) continue;
    const AddressRange destination = facts->address(store.second);
    if(!facts->within_complete_object(destination, returned_bytes) ||
       !ranges_overlap(destination, returned_bytes, returned, returned_bytes))
      continue;
    bool stable = memory_stable_after(
      function, facts, slow, index + 1, store.second,
      destination, returned_bytes);
    const std::vector<std::size_t> & loads = equivalent.slow_loads();
    for(std::size_t load = 0; stable && load < loads.size(); ++load) {
      const Instruction & dependency = instructions[loads[load]];
      const AddressRange address = facts->address(dependency.first);
      const std::size_t bytes = dependency.type.storage_size;
      stable = bytes && facts->within_complete_object(address, bytes) &&
        memory_stable_after(
          function, facts, slow, loads[load] + 1, dependency.first,
          address, bytes);
    }
    if(!stable) continue;
    *store_index = index;
    return true;
  }
  return false;
}

bool analyze_candidate(const LowirProgram & program,
                       const InlineCallGraph & graph,
                       std::size_t function_index,
                       QuerySplitCandidate * candidate)
{
  const Function & function = program.functions[function_index];
  if(!candidate_shape(function) || graph.recursive[function_index] ||
     graph.non_call_use[function_index])
    return false;
  const std::size_t calls = graph.reverse_offsets[function_index + 1] -
    graph.reverse_offsets[function_index];
  if(calls < kMinimumQueryCalls) return false;
  const lowir_analysis::Graph cfg = lowir_analysis::build_graph(function, 0);
  if(cfg.successors[0].size() != 2) return false;
  for(std::size_t terminal_edge = 0; terminal_edge < 2; ++terminal_edge) {
    const std::size_t terminal = cfg.successors[0][terminal_edge];
    const std::size_t slow = cfg.successors[0][1 - terminal_edge];
    if(terminal == 0 || slow == 0 || terminal == slow ||
       cfg.predecessors[terminal].size() != 2 ||
       cfg.predecessors[slow].size() != 1 ||
       cfg.successors[slow].size() != 1 ||
       cfg.successors[slow][0] != terminal)
      continue;
    QueryValueFacts facts(function);
    const Instruction * terminal_load = 0;
    if(!extractable_slow_block(function, facts, slow, terminal) ||
       !pure_terminal_load_return(
         function, facts, terminal, &terminal_load))
      continue;
    std::size_t store = kNoIndex;
    if(!find_stored_terminal_value(
         function, slow, terminal, &facts, *terminal_load, &store))
      continue;
    candidate->function = function_index;
    candidate->slow = slow;
    candidate->terminal = terminal;
    candidate->store = store;
    candidate->calls = calls;
    return true;
  }
  return false;
}

lowir_model::SymbolId allocate_helper_symbol(LowirProgram * program)
{
  std::vector<unsigned char> used_names(program->strings.size(), 0);
  for(std::size_t symbol = 0; symbol < program->symbol_names.size(); ++symbol)
    used_names[program->symbol_names[symbol]] = 1;
  for(std::size_t ordinal = 0;; ++ordinal) {
    const lowir_model::StringId name = program->strings.intern(
      std::string("__o3queryslow") + std::to_string(ordinal));
    if(static_cast<std::size_t>(name) >= used_names.size())
      used_names.resize(static_cast<std::size_t>(name) + 1, 0);
    if(used_names[name]) continue;
    return lowir_model::append_lowir_symbol(*program, name);
  }
}

void make_private_helper(Function * function, lowir_model::SymbolId symbol)
{
  function->symbol = symbol;
  function->boundary.query = lowir_model::CQM_DEFAULT;
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
  function->metadata.no_inline = true;
}

void apply_candidate(LowirProgram * program,
                     const QuerySplitCandidate & candidate,
                     lowir_model::SymbolId helper_symbol)
{
  Function helper = program->functions[candidate.function];
  make_private_helper(&helper, helper_symbol);
  lowir_model::Block slow = helper.blocks[candidate.slow];
  const Operand result = slow.instructions[candidate.store].first;
  Instruction helper_return;
  helper_return.kind = Instruction::IK_RETURN;
  helper_return.type = helper.return_type;
  helper_return.first = result;
  helper_return.debug_location = slow.instructions.back().debug_location;
  slow.instructions.back() = helper_return;
  helper.blocks.clear();
  helper.blocks.push_back(std::move(slow));

  Function & function = program->functions[candidate.function];
  lowir_model::Block & original_slow = function.blocks[candidate.slow];
  original_slow.instructions.clear();
  Instruction call;
  call.kind = Instruction::IK_CALL;
  call.dest = lowir_model::append_lowir_fresh_generated_value(
    function, function.return_type);
  call.type = function.return_type;
  call.first.kind = Operand::OP_GLOBAL;
  call.first.symbol = helper_symbol;
  call.call_returns_void = false;
  call.call_boundary = helper.boundary;
  call.debug_location = function.debug_location;
  for(std::size_t parameter = 0; parameter < function.params.size();
      ++parameter) {
    Operand argument;
    argument.kind = Operand::OP_TEMP;
    argument.value = function.params[parameter].value;
    call.args.push_back(argument);
  }
  original_slow.instructions.push_back(call);
  Instruction return_instruction;
  return_instruction.kind = Instruction::IK_RETURN;
  return_instruction.type = function.return_type;
  return_instruction.first.kind = Operand::OP_TEMP;
  return_instruction.first.value = call.dest;
  return_instruction.debug_location = function.debug_location;
  original_slow.instructions.push_back(return_instruction);
  program->functions.push_back(std::move(helper));
}

}  // namespace

std::size_t split_o3_terminal_query_slow_suffix(
    LowirProgram & program, std::vector<unsigned char> * rewritten_symbols,
    Stats * stats)
{
  const std::size_t function_count = program.functions.size();
  const InlineCallGraph graph = analyze_inline_call_graph(program, 0);
  QuerySplitCandidate best;
  for(std::size_t function = 0; function < function_count; ++function) {
    QuerySplitCandidate candidate;
    if(!analyze_candidate(program, graph, function, &candidate)) continue;
    if(best.function == kNoIndex || candidate.calls > best.calls)
      best = candidate;
  }
  if(best.function == kNoIndex) return 0;
  const lowir_model::SymbolId rewritten = program.functions[best.function].symbol;
  const lowir_model::SymbolId helper = allocate_helper_symbol(&program);
  const std::size_t extracted_instructions =
    program.functions[best.function].blocks[best.slow].instructions.size();
  apply_candidate(&program, best, helper);
  if(stats) {
    ++stats->o3_terminal_query_splits;
    stats->o3_terminal_query_call_sites += best.calls;
    stats->o3_terminal_query_extracted_instructions +=
      extracted_instructions;
    ++stats->rewrites;
  }
  if(rewritten_symbols) {
    rewritten_symbols->resize(program.symbol_names.size(), 0);
    (*rewritten_symbols)[rewritten] = 1;
  }
  return 1;
}

}  // namespace lowir_opt
