#include "lowir_force_inline.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lowir_native {
namespace force_inline {
namespace {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowirProgram;
using lowir_model::Operand;

typedef std::unordered_map<std::string, std::string> RenameMap;

const std::size_t kNoFunction = static_cast<std::size_t>(-1);

bool direct_call_target(const Instruction & instruction, std::string * target)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL)
    return false;
  if(target) *target = instruction.first.text;
  return true;
}

Instruction jump_to(const std::string & label)
{
  Instruction result;
  result.kind = Instruction::IK_JUMP;
  result.first.kind = Operand::OP_LABEL;
  result.first.text = label;
  return result;
}

struct InlineNames
{
  std::unordered_set<std::string> values;
  std::unordered_set<std::string> slots;
  std::unordered_set<std::string> labels;
  std::size_t next = 0;

  explicit InlineNames(const Function & function)
  {
    for(std::size_t i = 0; i < function.params.size(); ++i)
      values.insert(function.params[i].name);
    for(std::size_t i = 0; i < function.slots.size(); ++i)
      slots.insert(function.slots[i].first);
    for(std::size_t i = 0; i < function.blocks.size(); ++i) {
      labels.insert(function.blocks[i].label);
      for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
        const std::string & dest = function.blocks[i].instructions[j].dest;
        if(!dest.empty()) values.insert(dest);
      }
    }
  }

  std::string fresh(std::unordered_set<std::string> & names,
                    char sigil, const std::string & role)
  {
    for(;;) {
      const std::string candidate = std::string(1, sigil) +
        "__force_inline_" + role + "_" + std::to_string(next++);
      if(names.insert(candidate).second) return candidate;
    }
  }

  std::string value(const std::string & role) { return fresh(values, '%', role); }
  std::string slot(const std::string & role) { return fresh(slots, '$', role); }
  std::string label(const std::string & role) { return fresh(labels, '^', role); }
};

class Inliner {
public:
  explicit Inliner(LowirProgram * program)
    : program_(*program), tarjan_next_(0)
  {
    IndexCandidates();
  }

  bool has_candidates() const { return !candidate_by_name_.empty(); }

  void run()
  {
    MarkRecursiveCandidates();
    expansion_state_.assign(program_.functions.size(), 0);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      ExpandFunction(i);
  }

private:
  LowirProgram & program_;
  std::unordered_map<std::string, std::size_t> candidate_by_name_;
  std::vector<bool> recursive_;
  std::vector<int> tarjan_index_, tarjan_low_;
  std::vector<bool> tarjan_stacked_;
  std::vector<std::size_t> tarjan_stack_;
  int tarjan_next_;
  std::vector<unsigned char> expansion_state_;

  void IndexCandidates()
  {
    std::unordered_set<std::string> forced;
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i)
      if(program_.function_declarations[i].metadata.force_inline)
        forced.insert(program_.function_declarations[i].name);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(program_.functions[i].metadata.force_inline)
        forced.insert(program_.functions[i].name);
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & function = program_.functions[i];
      if(!forced.count(function.name) ||
         function.boundary.arity == lowir_model::CAM_VARIADIC)
        continue;
      if(!candidate_by_name_.emplace(function.name, i).second)
        throw std::runtime_error("multiple force-inline function definitions");
    }
  }

  std::size_t Candidate(const Instruction & instruction) const
  {
    std::string target;
    if(!direct_call_target(instruction, &target)) return kNoFunction;
    const std::unordered_map<std::string, std::size_t>::const_iterator found =
      candidate_by_name_.find(target);
    return found == candidate_by_name_.end() ? kNoFunction : found->second;
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
    for(std::unordered_map<std::string, std::size_t>::const_iterator it =
          candidate_by_name_.begin(); it != candidate_by_name_.end(); ++it)
      if(tarjan_index_[it->second] < 0) VisitCandidate(it->second);
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
                            const RenameMap & slots, const RenameMap & labels)
  {
    const RenameMap * map = operand->kind == Operand::OP_TEMP ? &values :
      operand->kind == Operand::OP_SLOT ? &slots :
      operand->kind == Operand::OP_LABEL ? &labels : 0;
    if(!map) return;
    const RenameMap::const_iterator found = map->find(operand->text);
    if(found == map->end())
      throw std::logic_error("force-inline operand has no renamed identity");
    operand->text = found->second;
  }

  static Instruction CloneInstruction(const Instruction & source,
                                      const RenameMap & values,
                                      const RenameMap & slots,
                                      const RenameMap & labels)
  {
    Instruction result = source;
    if(!result.dest.empty()) {
      const RenameMap::const_iterator found = values.find(result.dest);
      if(found == values.end())
        throw std::logic_error("force-inline result has no renamed identity");
      result.dest = found->second;
    }
    RenameOperand(&result.first, values, slots, labels);
    RenameOperand(&result.second, values, slots, labels);
    RenameOperand(&result.third, values, slots, labels);
    for(std::size_t i = 0; i < result.args.size(); ++i)
      RenameOperand(&result.args[i], values, slots, labels);
    return result;
  }

  void BuildRenameMaps(const Function & callee, InlineNames * names,
                       RenameMap * values, RenameMap * slots,
                       RenameMap * labels)
  {
    for(std::size_t i = 0; i < callee.params.size(); ++i)
      (*values)[callee.params[i].name] = names->value("parameter");
    for(std::size_t i = 0; i < callee.slots.size(); ++i)
      (*slots)[callee.slots[i].first] = names->slot("local");
    for(std::size_t i = 0; i < callee.blocks.size(); ++i) {
      (*labels)[callee.blocks[i].label] = names->label("block");
      for(std::size_t j = 0; j < callee.blocks[i].instructions.size(); ++j) {
        const std::string & dest = callee.blocks[i].instructions[j].dest;
        if(!dest.empty()) (*values)[dest] = names->value("temporary");
      }
    }
  }

  Block BuildPrologue(const Function & callee, const Instruction & call,
                      const RenameMap & values, const std::string & label,
                      const std::string & entry)
  {
    if(call.args.size() != callee.params.size())
      throw std::runtime_error("force-inline call argument count mismatch");
    Block result;
    result.label = label;
    for(std::size_t i = 0; i < callee.params.size(); ++i) {
      Instruction copy;
      copy.kind = Instruction::IK_COPY;
      copy.dest = values.find(callee.params[i].name)->second;
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
                   const RenameMap & slots, const RenameMap & labels,
                   const std::string & continuation,
                   const std::string & result_slot)
  {
    Block result;
    result.label = labels.find(source.label)->second;
    for(std::size_t i = 0; i < source.instructions.size(); ++i) {
      const Instruction & instruction = source.instructions[i];
      if(instruction.kind != Instruction::IK_RETURN) {
        result.instructions.push_back(CloneInstruction(
          instruction, values, slots, labels));
        continue;
      }
      if(callee.return_type.kind != lowir_model::LTK_VOID) {
        Instruction store;
        store.kind = Instruction::IK_STORE;
        store.type = callee.return_type;
        store.first = instruction.first;
        RenameOperand(&store.first, values, slots, labels);
        store.second.kind = Operand::OP_SLOT;
        store.second.text = result_slot;
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
                          const std::string & label,
                          const std::string & result_slot)
  {
    Block result;
    result.label = label;
    if(!call.call_returns_void) {
      if(call.dest.empty() || result_slot.empty())
        throw std::logic_error("force-inline value call has no result identity");
      Instruction load;
      load.kind = Instruction::IK_LOAD;
      load.dest = call.dest;
      load.type = call.type;
      load.first.kind = Operand::OP_SLOT;
      load.first.text = result_slot;
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
    RenameMap values, slots, labels;
    BuildRenameMaps(callee, names, &values, &slots, &labels);
    const std::string prologue_label = names->label("prologue");
    const std::string continuation_label = names->label("continuation");
    std::string result_slot;
    if(!call.call_returns_void) {
      result_slot = names->slot("result");
      caller->slots.push_back(std::make_pair(result_slot, call.type));
    }
    for(std::size_t i = 0; i < callee.slots.size(); ++i)
      caller->slots.push_back(std::make_pair(
        slots.find(callee.slots[i].first)->second, callee.slots[i].second));
    std::vector<Instruction> tail(
      caller->blocks[block_index].instructions.begin() + instruction_index + 1,
      caller->blocks[block_index].instructions.end());
    caller->blocks[block_index].instructions.resize(instruction_index);
    caller->blocks[block_index].instructions.push_back(jump_to(prologue_label));
    std::vector<Block> inserted;
    inserted.reserve(callee.blocks.size() + 2);
    inserted.push_back(BuildPrologue(callee, call, values, prologue_label,
      labels.find(callee.blocks[0].label)->second));
    for(std::size_t i = 0; i < callee.blocks.size(); ++i)
      inserted.push_back(CloneBlock(callee.blocks[i], callee, call,
        values, slots, labels, continuation_label, result_slot));
    inserted.push_back(BuildContinuation(
      call, tail, continuation_label, result_slot));
    caller->blocks.insert(caller->blocks.begin() + block_index + 1,
      inserted.begin(), inserted.end());
  }

  void InlineCalls(std::size_t function_index)
  {
    Function & caller = program_.functions[function_index];
    InlineNames names(caller);
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
  std::unique_ptr<LowirProgram> result(new LowirProgram(source));
  Inliner inliner(result.get());
  if(!inliner.has_candidates()) return std::unique_ptr<LowirProgram>();
  inliner.run();
  for(std::size_t i = 0; i < result->function_declarations.size(); ++i)
    result->function_declarations[i].metadata.force_inline = false;
  for(std::size_t i = 0; i < result->functions.size(); ++i)
    result->functions[i].metadata.force_inline = false;
  return result;
}

}  // namespace force_inline
}  // namespace lowir_native
