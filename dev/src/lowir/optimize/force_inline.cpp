#include "lowir/optimize/force_inline.h"
#include "lowir/analysis/phi_edges.h"

#include "lowir/analysis/function_reachability.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lowir_native {
namespace force_inline {
namespace {

using lowir_model::Block;
using lowir_model::BlockId;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowirProgram;
using lowir_model::Operand;

typedef std::vector<lowir_model::ValueId> RenameMap;
typedef std::vector<lowir_model::SlotId> SlotMap;

struct RenamedBlock
{
  BlockId id;
};

typedef std::vector<RenamedBlock> BlockMap;

const std::size_t kNoFunction = static_cast<std::size_t>(-1);

bool direct_call_target(const Instruction & instruction,
                        lowir_model::SymbolId * target)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL)
    return false;
  if(target) *target = instruction.first.symbol;
  return true;
}

Instruction jump_to(BlockId block)
{
  Instruction result;
  result.kind = Instruction::IK_JUMP;
  result.first.kind = Operand::OP_LABEL;
  result.first.block = block;
  return result;
}

struct InlineNames
{
  lowir_model::StringPool & strings;
  Function & function;
  bool retain;
  std::uint32_t next = 0;

  InlineNames(LowirProgram & program, Function & function_value)
    : strings(program.strings), function(function_value),
      retain(program.presentation_policy ==
        lowir_model::PRESENTATION_SERIALIZABLE) {}

  lowir_model::StringId fresh(
      lowir_model::GeneratedNameReservationKind kind, const char * prefix)
  {
    while(function.generated_name_reservations.contains(kind, next)) ++next;
    const std::uint32_t ordinal = next++;
    function.generated_name_reservations.reserve(kind, ordinal);
    return retain ? strings.intern(
      std::string(prefix) + std::to_string(ordinal)) : lowir_model::StringId();
  }

  lowir_model::StringId parameter()
    { return fresh(lowir_model::GNR_FORCE_PARAMETER,
        "__force_inline_parameter_"); }
  lowir_model::StringId temporary()
    { return fresh(lowir_model::GNR_FORCE_TEMPORARY,
        "__force_inline_temporary_"); }
  lowir_model::StringId local_slot()
    { return fresh(lowir_model::GNR_FORCE_LOCAL,
        "__force_inline_local_"); }
  lowir_model::StringId result_slot()
    { return fresh(lowir_model::GNR_FORCE_RESULT,
        "__force_inline_result_"); }
  lowir_model::StringId block()
    { return fresh(lowir_model::GNR_FORCE_BLOCK,
        "__force_inline_block_"); }
  lowir_model::StringId prologue()
    { return fresh(lowir_model::GNR_FORCE_PROLOGUE,
        "__force_inline_prologue_"); }
  lowir_model::StringId continuation()
    { return fresh(lowir_model::GNR_FORCE_CONTINUATION,
        "__force_inline_continuation_"); }
};

class Inliner {
public:
  explicit Inliner(LowirProgram * program)
    : program_(*program), candidate_by_symbol_(program->symbol_names.size(),
        kNoFunction), candidate_count_(0), tarjan_next_(0)
  {
    IndexCandidates();
  }

  bool has_candidates() const { return candidate_count_ != 0; }

  void run()
  {
    MarkRecursiveCandidates();
    expansion_state_.assign(program_.functions.size(), 0);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      ExpandFunction(i);
  }

private:
  LowirProgram & program_;
  std::vector<std::size_t> candidate_by_symbol_;
  std::size_t candidate_count_;
  std::vector<bool> recursive_;
  std::vector<int> tarjan_index_, tarjan_low_;
  std::vector<bool> tarjan_stacked_;
  std::vector<std::size_t> tarjan_stack_;
  int tarjan_next_;
  std::vector<unsigned char> expansion_state_;

  void IndexCandidates()
  {
    std::vector<unsigned char> forced(program_.symbol_names.size(), 0);
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i)
      if(program_.function_declarations[i].metadata.force_inline)
        forced[program_.function_declarations[i].symbol] = 1;
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(program_.functions[i].metadata.force_inline)
        forced[program_.functions[i].symbol] = 1;
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & function = program_.functions[i];
      if(!forced[function.symbol] ||
         function.boundary.arity == lowir_model::CAM_VARIADIC)
        continue;
      if(candidate_by_symbol_[function.symbol] != kNoFunction)
        throw std::runtime_error("multiple force-inline function definitions");
      candidate_by_symbol_[function.symbol] = i;
      ++candidate_count_;
    }
  }

  std::size_t Candidate(const Instruction & instruction) const
  {
    lowir_model::SymbolId target;
    if(!direct_call_target(instruction, &target)) return kNoFunction;
    return candidate_by_symbol_[target];
  }

  void VisitCandidate(std::size_t function_index)
  {
    tarjan_index_[function_index] = tarjan_next_;
    tarjan_low_[function_index] = tarjan_next_++;
    tarjan_stack_.push_back(function_index);
    tarjan_stacked_[function_index] = true;
    const Function & function = program_.functions[function_index];
    bool self_edge = false;
    for(std::size_t i = 0; i < function.blocks.size(); ++i) {
      for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
        const std::size_t callee = Candidate(function.blocks[i].instructions[j]);
        if(callee == kNoFunction) continue;
        self_edge = self_edge || callee == function_index;
        if(tarjan_index_[callee] < 0) {
          VisitCandidate(callee);
          tarjan_low_[function_index] = std::min(
            tarjan_low_[function_index], tarjan_low_[callee]);
        } else if(tarjan_stacked_[callee]) {
          tarjan_low_[function_index] = std::min(
            tarjan_low_[function_index], tarjan_index_[callee]);
        }
      }
    }
    if(tarjan_low_[function_index] != tarjan_index_[function_index]) return;
    std::vector<std::size_t> component;
    for(;;) {
      const std::size_t member = tarjan_stack_.back();
      tarjan_stack_.pop_back();
      tarjan_stacked_[member] = false;
      component.push_back(member);
      if(member == function_index) break;
    }
    if(component.size() > 1 || self_edge)
      for(std::size_t i = 0; i < component.size(); ++i)
        recursive_[component[i]] = true;
  }

  void MarkRecursiveCandidates()
  {
    const std::size_t count = program_.functions.size();
    recursive_.assign(count, false);
    tarjan_index_.assign(count, -1);
    tarjan_low_.assign(count, -1);
    tarjan_stacked_.assign(count, false);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(candidate_by_symbol_[program_.functions[i].symbol] == i &&
         tarjan_index_[i] < 0) VisitCandidate(i);
  }

  void ExpandFunction(std::size_t function_index)
  {
    if(expansion_state_[function_index] == 2) return;
    if(expansion_state_[function_index] == 1) return;
    expansion_state_[function_index] = 1;
    const Function & source = program_.functions[function_index];
    std::vector<std::size_t> dependencies;
    for(std::size_t i = 0; i < source.blocks.size(); ++i)
      for(std::size_t j = 0; j < source.blocks[i].instructions.size(); ++j) {
        const std::size_t callee = Candidate(source.blocks[i].instructions[j]);
        if(callee != kNoFunction && !recursive_[callee])
          dependencies.push_back(callee);
      }
    for(std::size_t i = 0; i < dependencies.size(); ++i)
      ExpandFunction(dependencies[i]);
    InlineCalls(function_index);
    expansion_state_[function_index] = 2;
  }

  static void RenameOperand(Operand * operand, const RenameMap & values,
                            const SlotMap & slots, const BlockMap & blocks)
  {
    if(operand->kind == Operand::OP_LABEL) {
      const std::uint32_t id = operand->block;
      if(id >= blocks.size() || !blocks[id].id.valid())
        throw std::logic_error("force-inline block has no renamed identity");
      operand->block = blocks[id].id;
      return;
    }
    if(operand->kind == Operand::OP_SLOT) {
      const std::uint32_t id = operand->slot;
      if(id >= slots.size() || !slots[id].valid())
        throw std::logic_error("force-inline slot has no renamed identity");
      operand->slot = slots[id];
      return;
    }
    if(operand->kind != Operand::OP_TEMP) return;
    const std::uint32_t id = operand->value;
    if(id >= values.size() || !values[id].valid())
      throw std::logic_error("force-inline operand has no renamed identity");
    operand->value = values[id];
  }

  static Instruction CloneInstruction(const Instruction & source,
                                      const RenameMap & values,
                                      const SlotMap & slots,
                                      const BlockMap & blocks)
  {
    Instruction result = source;
    if(result.dest.valid()) {
      const std::uint32_t id = result.dest;
      if(id >= values.size() || !values[id].valid())
        throw std::logic_error("force-inline result has no renamed identity");
      result.dest = values[id];
    }
    RenameOperand(&result.first, values, slots, blocks);
    RenameOperand(&result.second, values, slots, blocks);
    RenameOperand(&result.third, values, slots, blocks);
    for(std::size_t i = 0; i < result.args.size(); ++i)
      RenameOperand(&result.args[i], values, slots, blocks);
    return result;
  }

  void BuildRenameMaps(Function * caller, const Function & callee,
                       InlineNames * names,
                       RenameMap * values, SlotMap * slots,
                       BlockMap * blocks)
  {
    values->resize(callee.value_names.size());
    for(std::size_t i = 0; i < callee.params.size(); ++i) {
      const lowir_model::StringId name = names->parameter();
      (*values)[callee.params[i].value] = name.valid() ?
        lowir_model::append_lowir_value(
          *caller, name, callee.params[i].type) :
        lowir_model::append_lowir_unnamed_value(
          *caller, callee.params[i].type);
    }
    slots->resize(callee.slot_names.size());
    for(std::size_t i = 0; i < callee.slots.size(); ++i) {
      const lowir_model::SlotId source = callee.slots[i];
      (*slots)[source] = lowir_model::append_lowir_slot(
        *caller, names->local_slot(),
        lowir_model::lowir_slot_type(callee, source));
    }
    blocks->resize(callee.next_block_id);
    for(std::size_t i = 0; i < callee.blocks.size(); ++i) {
      RenamedBlock block;
      block.id = lowir_model::allocate_lowir_block_id(
        *caller, names->block());
      (*blocks)[callee.blocks[i].id] = block;
      for(std::size_t j = 0; j < callee.blocks[i].instructions.size(); ++j) {
        const lowir_model::ValueId dest =
          callee.blocks[i].instructions[j].dest;
        if(dest.valid()) {
          const lowir_model::StringId name = names->temporary();
          (*values)[dest] = name.valid() ?
            lowir_model::append_lowir_value(
              *caller, name, lowir_model::lowir_value_type(callee, dest)) :
            lowir_model::append_lowir_unnamed_value(
              *caller, lowir_model::lowir_value_type(callee, dest));
        }
      }
    }
  }

  Block BuildPrologue(const Function & callee, const Instruction & call,
                      const RenameMap & values, BlockId id, BlockId entry)
  {
    if(call.args.size() != callee.params.size())
      throw std::runtime_error("force-inline call argument count mismatch");
    Block result;
    result.id = id;
    for(std::size_t i = 0; i < callee.params.size(); ++i) {
      Instruction copy;
      copy.kind = Instruction::IK_COPY;
      copy.dest = values[callee.params[i].value];
      copy.type = callee.params[i].type;
      copy.first = call.args[i];
      copy.debug_location = call.debug_location;
      result.instructions.push_back(copy);
    }
    result.instructions.push_back(jump_to(entry));
    return result;
  }

  Block CloneBlock(const Block & source, const Function & callee,
                   const Instruction & call, const RenameMap & values,
                   const SlotMap & slots, const BlockMap & blocks,
                   BlockId continuation,
                   lowir_model::SlotId result_slot)
  {
    Block result;
    result.id = blocks[source.id].id;
    for(std::size_t i = 0; i < source.instructions.size(); ++i) {
      const Instruction & instruction = source.instructions[i];
      if(instruction.kind != Instruction::IK_RETURN) {
        result.instructions.push_back(CloneInstruction(
          instruction, values, slots, blocks));
        continue;
      }
      if(callee.return_type.kind != lowir_model::LTK_VOID) {
        Instruction store;
        store.kind = Instruction::IK_STORE;
        store.type = callee.return_type;
        store.first = instruction.first;
        RenameOperand(&store.first, values, slots, blocks);
        store.second.kind = Operand::OP_SLOT;
        store.second.slot = result_slot;
        store.second.literal_type = callee.return_type;
        store.debug_location = instruction.debug_location;
        result.instructions.push_back(store);
      }
      result.instructions.push_back(jump_to(continuation));
    }
    (void)call;
    return result;
  }

  Block BuildContinuation(const Instruction & call,
                          const std::vector<Instruction> & tail,
                          BlockId id,
                          lowir_model::SlotId result_slot)
  {
    Block result;
    result.id = id;
    if(!call.call_returns_void) {
      if(!call.dest.valid() || !result_slot.valid())
        throw std::logic_error("force-inline value call has no result identity");
      Instruction load;
      load.kind = Instruction::IK_LOAD;
      load.dest = call.dest;
      load.type = call.type;
      load.first.kind = Operand::OP_SLOT;
      load.first.slot = result_slot;
      load.first.literal_type = call.type;
      load.debug_location = call.debug_location;
      result.instructions.push_back(load);
    }
    result.instructions.insert(result.instructions.end(), tail.begin(), tail.end());
    if(result.instructions.empty())
      throw std::logic_error("force-inline continuation is empty");
    return result;
  }

  void InlineCall(Function * caller, std::size_t block_index,
                  std::size_t instruction_index, const Function & callee,
                  InlineNames * names)
  {
    const Instruction call =
      caller->blocks[block_index].instructions[instruction_index];
    RenameMap values;
    SlotMap slots;
    BlockMap blocks;
    BuildRenameMaps(caller, callee, names, &values, &slots, &blocks);
    const lowir_model::StringId prologue_label = names->prologue();
    const lowir_model::StringId continuation_label = names->continuation();
    const BlockId prologue_id =
      lowir_model::allocate_lowir_block_id(
        *caller, prologue_label);
    const BlockId continuation_id =
      lowir_model::allocate_lowir_block_id(
        *caller, continuation_label);
    lowir_model::SlotId result_slot;
    if(!call.call_returns_void)
      result_slot = lowir_model::append_lowir_slot(
        *caller, names->result_slot(), call.type);
    std::vector<Instruction> tail(
      caller->blocks[block_index].instructions.begin() + instruction_index + 1,
      caller->blocks[block_index].instructions.end());
    if(tail.empty())
      throw std::logic_error("force-inline call has no continuation");
    const BlockId old_predecessor = caller->blocks[block_index].id;
    lowir_phi_edges::rewrite_moved_phi_edges(
      caller, tail.back(), old_predecessor, continuation_id);
    caller->blocks[block_index].instructions.resize(instruction_index);
    caller->blocks[block_index].instructions.push_back(jump_to(prologue_id));
    std::vector<Block> inserted;
    inserted.reserve(callee.blocks.size() + 2);
    inserted.push_back(BuildPrologue(callee, call, values, prologue_id,
      blocks[callee.blocks[0].id].id));
    for(std::size_t i = 0; i < callee.blocks.size(); ++i)
      inserted.push_back(CloneBlock(callee.blocks[i], callee, call,
        values, slots, blocks, continuation_id, result_slot));
    inserted.push_back(BuildContinuation(
      call, tail, continuation_id, result_slot));
    caller->blocks.insert(caller->blocks.begin() + block_index + 1,
      inserted.begin(), inserted.end());
  }

  void InlineCalls(std::size_t function_index)
  {
    Function & caller = program_.functions[function_index];
    bool has_candidate = false;
    for(std::size_t block = 0; !has_candidate && block < caller.blocks.size();
        ++block)
      for(std::size_t instruction = 0;
          instruction < caller.blocks[block].instructions.size(); ++instruction) {
        const std::size_t callee =
          Candidate(caller.blocks[block].instructions[instruction]);
        if(callee != kNoFunction && !recursive_[callee]) {
          has_candidate = true;
          break;
        }
      }
    if(!has_candidate) return;

    InlineNames names(program_, caller);
    for(std::size_t block = 0; block < caller.blocks.size(); ++block) {
      for(std::size_t instruction = 0;
          instruction < caller.blocks[block].instructions.size(); ++instruction) {
        const std::size_t callee_index =
          Candidate(caller.blocks[block].instructions[instruction]);
        if(callee_index == kNoFunction || recursive_[callee_index]) continue;
        const Function callee = program_.functions[callee_index];
        InlineCall(&caller, block, instruction, callee, &names);
        break;
      }
    }
  }
};

}  // namespace

std::unique_ptr<LowirProgram> rewrite_program(const LowirProgram & source)
{
	bool has_forced_definition = false;
	for(std::size_t i = 0; i < source.functions.size(); ++i)
		if(source.functions[i].metadata.force_inline &&
		   source.functions[i].boundary.arity != lowir_model::CAM_VARIADIC) {
			has_forced_definition = true;
			break;
		}
	if(!has_forced_definition) {
		std::vector<unsigned char> forced_declarations(
			source.symbol_names.size(), 0);
		for(std::size_t i = 0; i < source.function_declarations.size(); ++i)
			if(source.function_declarations[i].metadata.force_inline)
				forced_declarations[source.function_declarations[i].symbol] = 1;
		for(std::size_t i = 0;
			!has_forced_definition && i < source.functions.size(); ++i)
			has_forced_definition =
				forced_declarations[source.functions[i].symbol] != 0 &&
				source.functions[i].boundary.arity != lowir_model::CAM_VARIADIC;
	}
	if(!has_forced_definition) return std::unique_ptr<LowirProgram>();
  std::unique_ptr<LowirProgram> result(new LowirProgram(source));
  Inliner inliner(result.get());
  if(!inliner.has_candidates()) return std::unique_ptr<LowirProgram>();
  inliner.run();
	lowir_model::prune_unreachable_weak_functions(*result);
  for(std::size_t i = 0; i < result->function_declarations.size(); ++i)
    result->function_declarations[i].metadata.force_inline = false;
  for(std::size_t i = 0; i < result->functions.size(); ++i)
    result->functions[i].metadata.force_inline = false;
  return result;
}

}  // namespace force_inline
}  // namespace lowir_native
