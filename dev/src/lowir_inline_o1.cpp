#include "lowir_inline_o1.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::LowirProgram;
using lowir_model::Operand;

const std::size_t kNoFunction = static_cast<std::size_t>(-1);
typedef std::unordered_map<std::string, Operand> ValueMap;
typedef std::unordered_map<std::string, std::string> NameMap;

bool is_eh_instruction(Instruction::Kind kind)
{
  return kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_EH_END;
}

bool direct_call(const Instruction & instruction, std::string * target)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL) return false;
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

Operand named_operand(Operand::Kind kind, const std::string & text,
                      const LowType & type = LowType())
{
  Operand result;
  result.kind = kind;
  result.text = text;
  result.literal_type = type;
  return result;
}

bool contains_eh(const Function & function)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      if(is_eh_instruction(function.blocks[i].instructions[j].kind)) return true;
  return false;
}

std::size_t instruction_count(const Function & function)
{
  std::size_t result = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    result += function.blocks[i].instructions.size();
  return result;
}

void collect_generated_id(const std::string & name,
                          std::unordered_set<std::size_t> * ids)
{
  const std::string marker = "__o1inl";
  std::size_t at = name.find(marker);
  while(at != std::string::npos) {
    std::size_t digit = at + marker.size();
    std::size_t end = digit;
    while(end < name.size() && std::isdigit(static_cast<unsigned char>(name[end])))
      ++end;
    if(end > digit && name.compare(end, 2, "__") == 0)
      ids->insert(static_cast<std::size_t>(
        std::strtoull(name.substr(digit, end - digit).c_str(), 0, 10)));
    at = name.find(marker, at + marker.size());
  }
}

struct Names
{
  std::unordered_set<std::string> values;
  std::unordered_set<std::string> slots;
  std::unordered_set<std::string> labels;
  std::unordered_set<std::size_t> site_ids;

  explicit Names(const Function & function)
  {
    for(std::size_t i = 0; i < function.params.size(); ++i) {
      values.insert(function.params[i].name);
      collect_generated_id(function.params[i].name, &site_ids);
    }
    for(std::size_t i = 0; i < function.slots.size(); ++i) {
      slots.insert(function.slots[i].first);
      collect_generated_id(function.slots[i].first, &site_ids);
    }
    for(std::size_t i = 0; i < function.blocks.size(); ++i) {
      labels.insert(function.blocks[i].label);
      collect_generated_id(function.blocks[i].label, &site_ids);
      for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
        const Instruction & ins = function.blocks[i].instructions[j];
        if(!ins.dest.empty()) values.insert(ins.dest);
        collect_generated_id(ins.dest, &site_ids);
      }
    }
  }

  std::size_t next_site()
  {
    std::size_t result = 0;
    while(site_ids.count(result)) ++result;
    site_ids.insert(result);
    return result;
  }

  std::string unique_slot(const std::string & stem, const LowType &,
                          std::size_t first_suffix = 1)
  {
    for(std::size_t suffix = first_suffix;; ++suffix) {
      const std::string candidate = stem + std::to_string(suffix);
      if(slots.insert(candidate).second) return candidate;
    }
  }
};

void rename_operand(Operand * operand, const ValueMap & values,
                    const NameMap & slots, const NameMap & labels)
{
  if(operand->kind == Operand::OP_TEMP) {
    const ValueMap::const_iterator found = values.find(operand->text);
    if(found != values.end()) *operand = found->second;
  } else if(operand->kind == Operand::OP_SLOT) {
    const NameMap::const_iterator found = slots.find(operand->text);
    if(found != slots.end()) operand->text = found->second;
  } else if(operand->kind == Operand::OP_LABEL) {
    const NameMap::const_iterator found = labels.find(operand->text);
    if(found != labels.end()) operand->text = found->second;
  }
}

Instruction clone_instruction(const Instruction & source,
                              const ValueMap & values,
                              const NameMap & slots,
                              const NameMap & labels)
{
  Instruction result = source;
  if(!result.dest.empty()) {
    const ValueMap::const_iterator found = values.find(result.dest);
    if(found == values.end() || found->second.kind != Operand::OP_TEMP)
      throw std::logic_error("inlined result has no value name");
    result.dest = found->second.text;
  }
  rename_operand(&result.first, values, slots, labels);
  rename_operand(&result.second, values, slots, labels);
  rename_operand(&result.third, values, slots, labels);
  for(std::size_t i = 0; i < result.args.size(); ++i)
    rename_operand(&result.args[i], values, slots, labels);
  return result;
}

void replace_value(Function * function, const std::string & name,
                   const Operand & replacement)
{
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction & ins = function->blocks[i].instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP && operands[k]->text == name)
          *operands[k] = replacement;
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP && ins.args[k].text == name)
          ins.args[k] = replacement;
    }
}

std::vector<std::size_t> normal_successors(const Function & function,
                                            std::size_t block,
                                            const NameMap & index)
{
  std::vector<std::size_t> result;
  if(function.blocks[block].instructions.empty()) return result;
  const Instruction & term = function.blocks[block].instructions.back();
  const Operand * targets[2] = {0, 0};
  if(term.kind == Instruction::IK_JUMP) targets[0] = &term.first;
  else if(term.kind == Instruction::IK_BRANCH) {
    targets[0] = &term.second; targets[1] = &term.third;
  }
  for(std::size_t i = 0; i < 2; ++i) if(targets[i]) {
    const NameMap::const_iterator found = index.find(targets[i]->text);
    if(found != index.end()) result.push_back(
      static_cast<std::size_t>(std::strtoull(found->second.c_str(), 0, 10)));
  }
  if(term.kind == Instruction::IK_SWITCH) {
    const NameMap::const_iterator fallback = index.find(term.second.text);
    if(fallback != index.end()) result.push_back(
      static_cast<std::size_t>(std::strtoull(fallback->second.c_str(), 0, 10)));
    for(std::size_t i = 1; i < term.args.size(); i += 2) {
      const NameMap::const_iterator found = index.find(term.args[i].text);
      if(found != index.end()) result.push_back(
        static_cast<std::size_t>(std::strtoull(found->second.c_str(), 0, 10)));
    }
  }
  return result;
}

bool call_inside_eh(const Function & function, std::size_t call_block,
                    std::size_t call_instruction)
{
  NameMap index;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    index[function.blocks[i].label] = std::to_string(i);
  std::vector<unsigned char> active(function.blocks.size(), 0), known(
    function.blocks.size(), 0);
  if(!function.blocks.empty()) known[0] = 1;
  bool changed = true;
  while(changed) {
    changed = false;
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      if(!known[block]) continue;
      bool state = active[block] != 0;
      for(std::size_t i = 0; i < function.blocks[block].instructions.size(); ++i) {
        if(block == call_block && i == call_instruction) return state;
        const Instruction::Kind kind = function.blocks[block].instructions[i].kind;
        if(kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP)
          state = true;
        else if(kind == Instruction::IK_EH_END) state = false;
      }
      const std::vector<std::size_t> successors =
        normal_successors(function, block, index);
      for(std::size_t i = 0; i < successors.size(); ++i)
        if(!known[successors[i]] || (state && !active[successors[i]])) {
          known[successors[i]] = 1;
          active[successors[i]] = state;
          changed = true;
        }
    }
  }
  return false;
}

class Inliner
{
public:
  Inliner(LowirProgram * program,
          std::unordered_set<std::string> * rewritten_functions)
    : program_(*program), rewritten_functions_(rewritten_functions),
      tarjan_next_(0), rewrites_(0)
  {
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      definition_[program_.functions[i].name] = i;
    infer_no_unwind();
    mark_recursive();
    state_.assign(program_.functions.size(), 0);
  }

  std::size_t run()
  {
    for(std::size_t i = 0; i < program_.functions.size(); ++i) expand(i);
    return rewrites_;
  }

private:
  LowirProgram & program_;
  std::unordered_set<std::string> * rewritten_functions_;
  std::unordered_map<std::string, std::size_t> definition_;
  std::unordered_set<std::string> no_unwind_;
  std::vector<unsigned char> recursive_, state_;
  std::vector<int> tarjan_index_, tarjan_low_;
  std::vector<unsigned char> tarjan_stacked_;
  std::vector<std::size_t> tarjan_stack_;
  int tarjan_next_;
  std::size_t rewrites_;

  std::size_t callee(const Instruction & instruction) const
  {
    std::string name;
    if(!direct_call(instruction, &name)) return kNoFunction;
    const std::unordered_map<std::string, std::size_t>::const_iterator found =
      definition_.find(name);
    return found == definition_.end() ? kNoFunction : found->second;
  }

  void infer_no_unwind()
  {
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i)
      if(program_.function_declarations[i].boundary.unwind == lowir_model::CUM_NO)
        no_unwind_.insert(program_.function_declarations[i].name);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(program_.functions[i].boundary.unwind == lowir_model::CUM_NO)
        no_unwind_.insert(program_.functions[i].name);
    bool changed = true;
    while(changed) {
      changed = false;
      for(std::size_t i = 0; i < program_.functions.size(); ++i) {
        const Function & function = program_.functions[i];
        if(no_unwind_.count(function.name)) continue;
        bool safe = !contains_eh(function);
        for(std::size_t b = 0; safe && b < function.blocks.size(); ++b)
          for(std::size_t j = 0; safe && j < function.blocks[b].instructions.size(); ++j) {
            const Instruction & ins = function.blocks[b].instructions[j];
            if(ins.kind == Instruction::IK_THROW || ins.kind == Instruction::IK_RESUME)
              safe = false;
            else if(ins.kind == Instruction::IK_CALL) {
              std::string target;
              safe = direct_call(ins, &target) && no_unwind_.count(target);
            }
          }
        if(safe) { no_unwind_.insert(function.name); changed = true; }
      }
    }
  }

  void visit(std::size_t function_index)
  {
    tarjan_index_[function_index] = tarjan_next_;
    tarjan_low_[function_index] = tarjan_next_++;
    tarjan_stack_.push_back(function_index);
    tarjan_stacked_[function_index] = 1;
    bool self = false;
    const Function & function = program_.functions[function_index];
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
        const std::size_t next = callee(function.blocks[b].instructions[j]);
        if(next == kNoFunction) continue;
        self = self || next == function_index;
        if(tarjan_index_[next] < 0) {
          visit(next);
          tarjan_low_[function_index] = std::min(
            tarjan_low_[function_index], tarjan_low_[next]);
        } else if(tarjan_stacked_[next])
          tarjan_low_[function_index] = std::min(
            tarjan_low_[function_index], tarjan_index_[next]);
      }
    if(tarjan_low_[function_index] != tarjan_index_[function_index]) return;
    std::vector<std::size_t> component;
    for(;;) {
      const std::size_t member = tarjan_stack_.back();
      tarjan_stack_.pop_back();
      tarjan_stacked_[member] = 0;
      component.push_back(member);
      if(member == function_index) break;
    }
    if(self || component.size() > 1)
      for(std::size_t i = 0; i < component.size(); ++i)
        recursive_[component[i]] = 1;
  }

  void mark_recursive()
  {
    const std::size_t count = program_.functions.size();
    recursive_.assign(count, 0);
    tarjan_index_.assign(count, -1);
    tarjan_low_.assign(count, -1);
    tarjan_stacked_.assign(count, 0);
    for(std::size_t i = 0; i < count; ++i)
      if(tarjan_index_[i] < 0) visit(i);
  }

  bool landing_block(const Function & function, const std::string & label) const
  {
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
        const Instruction & ins = function.blocks[b].instructions[j];
        if((ins.kind == Instruction::IK_EH_TRY ||
            ins.kind == Instruction::IK_EH_CLEANUP) && ins.first.text == label)
          return true;
      }
    return false;
  }

  bool candidate(std::size_t caller, std::size_t block,
                 std::size_t instruction, std::size_t target) const
  {
    if(target == kNoFunction || recursive_[target] || target == caller) return false;
    const Function & callee_function = program_.functions[target];
    if(callee_function.boundary.arity == lowir_model::CAM_VARIADIC ||
       contains_eh(callee_function)) return false;
    if(instruction_count(callee_function) > 40 &&
       !callee_function.metadata.prefer_local_object_binding) return false;
    const Function & caller_function = program_.functions[caller];
    if(landing_block(caller_function, caller_function.blocks[block].label))
      return false;
    return !call_inside_eh(caller_function, block, instruction) ||
      no_unwind_.count(callee_function.name);
  }

  bool strip_explicit_no_unwind_eh(Function * function)
  {
    if(function->boundary.unwind != lowir_model::CUM_NO) return false;
    for(std::size_t b = 0; b < function->blocks.size(); ++b)
      for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
        const Instruction & ins = function->blocks[b].instructions[j];
        if(ins.kind == Instruction::IK_CALL) {
          std::string target;
          if(!direct_call(ins, &target) || !no_unwind_.count(target)) return false;
        } else if(ins.kind == Instruction::IK_THROW) return false;
      }
    bool changed = false;
    for(std::size_t b = 0; b < function->blocks.size(); ++b) {
      std::vector<Instruction> kept;
      for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
        const Instruction & ins = function->blocks[b].instructions[j];
        if(ins.kind == Instruction::IK_EH_TRY ||
           ins.kind == Instruction::IK_EH_CLEANUP ||
           ins.kind == Instruction::IK_EH_END) {
          changed = true;
          continue;
        }
        kept.push_back(ins);
      }
      function->blocks[b].instructions.swap(kept);
    }
    return changed;
  }

  void expand(std::size_t function_index)
  {
    if(state_[function_index] == 2 || state_[function_index] == 1) return;
    state_[function_index] = 1;
    inline_calls(function_index);
    if(strip_explicit_no_unwind_eh(&program_.functions[function_index])) {
      ++rewrites_;
      if(rewritten_functions_)
        rewritten_functions_->insert(program_.functions[function_index].name);
    }
    state_[function_index] = 2;
  }

  void build_maps(const Function & callee_function, const Instruction & call,
                  const std::string & prefix, Names * names, ValueMap * values,
                  NameMap * slots, NameMap * labels)
  {
    if(callee_function.params.size() != call.args.size())
      throw std::runtime_error("inline call argument count mismatch");
    for(std::size_t i = 0; i < callee_function.params.size(); ++i)
      (*values)[callee_function.params[i].name] = call.args[i];
    for(std::size_t i = 0; i < callee_function.slots.size(); ++i) {
      const std::string renamed = "$" + prefix +
        callee_function.slots[i].first.substr(1);
      (*slots)[callee_function.slots[i].first] = renamed;
      names->slots.insert(renamed);
    }
    for(std::size_t i = 0; i < callee_function.blocks.size(); ++i) {
      const std::string renamed = "^" + prefix +
        callee_function.blocks[i].label.substr(1);
      (*labels)[callee_function.blocks[i].label] = renamed;
      names->labels.insert(renamed);
      for(std::size_t j = 0;
          j < callee_function.blocks[i].instructions.size(); ++j) {
        const Instruction & ins = callee_function.blocks[i].instructions[j];
        if(ins.dest.empty()) continue;
        const std::string value = "%" + prefix + ins.dest.substr(1);
        (*values)[ins.dest] = named_operand(Operand::OP_TEMP, value);
        names->values.insert(value);
      }
    }
  }

  void inline_call(std::size_t caller_index, std::size_t block_index,
                   std::size_t instruction_index, const Function & callee_function,
                   Names * names)
  {
    Function & caller = program_.functions[caller_index];
    const Instruction call = caller.blocks[block_index].instructions[instruction_index];
    const std::string prefix = "__o1inl" +
      std::to_string(names->next_site()) + "__";
    ValueMap values;
    NameMap slots, labels;
    build_maps(callee_function, call, prefix, names, &values, &slots, &labels);
    for(std::size_t i = 0; i < callee_function.slots.size(); ++i)
      caller.slots.push_back(std::make_pair(
        slots.find(callee_function.slots[i].first)->second,
        callee_function.slots[i].second));

    std::size_t returns = 0;
    for(std::size_t b = 0; b < callee_function.blocks.size(); ++b)
      for(std::size_t j = 0; j < callee_function.blocks[b].instructions.size(); ++j)
        if(callee_function.blocks[b].instructions[j].kind == Instruction::IK_RETURN)
          ++returns;
    const bool object_result =
      callee_function.return_type.kind == lowir_model::LTK_OBJECT;
    const bool has_result = !call.call_returns_void;
    std::string merge_slot;
    if(has_result && returns > 1) {
      merge_slot = names->unique_slot("$" + prefix +
        (object_result ? "retmergeobj__" : "retmerge__"), call.type);
      caller.slots.push_back(std::make_pair(merge_slot, call.type));
    }

    std::vector<Instruction> tail(
      caller.blocks[block_index].instructions.begin() + instruction_index + 1,
      caller.blocks[block_index].instructions.end());
    caller.blocks[block_index].instructions.resize(instruction_index);

    Operand single_object_return;
    bool have_single_object_return = false;
    Operand single_scalar_return;
    bool have_single_scalar_return = false;
    if(callee_function.blocks.size() == 1 && returns == 1) {
      const Block & source = callee_function.blocks[0];
      bool void_call_wrapper = call.call_returns_void;
      bool wrapper_has_call = false;
      for(std::size_t j = 0; j < source.instructions.size(); ++j)
        wrapper_has_call = wrapper_has_call ||
          source.instructions[j].kind == Instruction::IK_CALL;
      void_call_wrapper = void_call_wrapper && wrapper_has_call;
      const std::string wrapper_continuation = "^" + prefix + "cont";
      for(std::size_t j = 0; j < source.instructions.size(); ++j) {
        const Instruction & ins = source.instructions[j];
        if(ins.kind != Instruction::IK_RETURN) {
          caller.blocks[block_index].instructions.push_back(
            clone_instruction(ins, values, slots, labels));
        } else if(void_call_wrapper) {
          caller.blocks[block_index].instructions.push_back(
            jump_to(wrapper_continuation));
        } else if(has_result && object_result) {
          single_object_return = ins.first;
          rename_operand(&single_object_return, values, slots, labels);
          have_single_object_return = true;
        } else if(has_result) {
          single_scalar_return = ins.first;
          rename_operand(&single_scalar_return, values, slots, labels);
          have_single_scalar_return = true;
        }
      }
      if(void_call_wrapper) {
        Block continuation_block;
        continuation_block.label = wrapper_continuation;
        continuation_block.instructions = tail;
        caller.blocks.insert(caller.blocks.begin() + block_index + 1,
          continuation_block);
        names->labels.insert(wrapper_continuation);
      } else caller.blocks[block_index].instructions.insert(
          caller.blocks[block_index].instructions.end(), tail.begin(), tail.end());
      if(have_single_object_return) replace_value(
        &caller, call.dest, single_object_return);
      if(have_single_scalar_return) replace_value(
        &caller, call.dest, single_scalar_return);
      return;
    }

    const std::string continuation = "^" + prefix + "cont";
    names->labels.insert(continuation);
    caller.blocks[block_index].instructions.push_back(jump_to(
      labels.find(callee_function.blocks[0].label)->second));
    std::vector<Block> inserted;
    for(std::size_t b = 0; b < callee_function.blocks.size(); ++b) {
      Block block;
      block.label = labels.find(callee_function.blocks[b].label)->second;
      for(std::size_t j = 0;
          j < callee_function.blocks[b].instructions.size(); ++j) {
        const Instruction & ins = callee_function.blocks[b].instructions[j];
        if(ins.kind != Instruction::IK_RETURN) {
          block.instructions.push_back(clone_instruction(ins, values, slots, labels));
          continue;
        }
        Operand returned = ins.first;
        rename_operand(&returned, values, slots, labels);
        if(has_result && returns > 1) {
          Instruction merge;
          if(object_result) {
            merge.kind = Instruction::IK_COPYOBJ;
            merge.byte_count = call.type.storage_size;
            merge.byte_alignment = call.type.alignment;
            merge.first = returned;
            merge.second = named_operand(Operand::OP_SLOT, merge_slot, call.type);
          } else {
            merge.kind = Instruction::IK_STORE;
            merge.type = call.type;
            merge.first = returned;
            merge.second = named_operand(Operand::OP_SLOT, merge_slot, call.type);
          }
          block.instructions.push_back(merge);
        } else if(has_result && object_result) {
          single_object_return = returned;
          have_single_object_return = true;
        } else if(has_result) {
          single_scalar_return = returned;
          have_single_scalar_return = true;
        }
        block.instructions.push_back(jump_to(continuation));
      }
      inserted.push_back(block);
    }
    Block continuation_block;
    continuation_block.label = continuation;
    if(has_result && returns > 1 && !object_result) {
      Instruction load;
      load.kind = Instruction::IK_LOAD;
      load.dest = call.dest;
      load.type = call.type;
      load.first = named_operand(Operand::OP_SLOT, merge_slot, call.type);
      continuation_block.instructions.push_back(load);
    }
    continuation_block.instructions.insert(continuation_block.instructions.end(),
      tail.begin(), tail.end());
    inserted.push_back(continuation_block);
    caller.blocks.insert(caller.blocks.begin() + block_index + 1,
      inserted.begin(), inserted.end());
    if(has_result && object_result) {
      const Operand replacement = returns > 1 ?
        named_operand(Operand::OP_SLOT, merge_slot, call.type) : single_object_return;
      replace_value(&caller, call.dest, replacement);
    }
    if(has_result && !object_result && returns == 1 && have_single_scalar_return)
      replace_value(&caller, call.dest, single_scalar_return);
  }

  void inline_calls(std::size_t function_index)
  {
    Names names(program_.functions[function_index]);
    bool changed = true;
    while(changed) {
      changed = false;
      Function & caller = program_.functions[function_index];
      for(std::size_t b = 0; b < caller.blocks.size() && !changed; ++b)
        for(std::size_t j = 0; j < caller.blocks[b].instructions.size(); ++j) {
          const std::size_t target = callee(caller.blocks[b].instructions[j]);
          if(!candidate(function_index, b, j, target)) continue;
          const Function callee_copy = program_.functions[target];
          inline_call(function_index, b, j, callee_copy, &names);
          ++rewrites_;
          if(rewritten_functions_)
            rewritten_functions_->insert(program_.functions[function_index].name);
          changed = true;
          break;
        }
    }
  }
};

}  // namespace

std::size_t inline_o1_calls(
  LowirProgram & program,
  std::unordered_set<std::string> * rewritten_functions)
{
  Inliner inliner(&program, rewritten_functions);
  return inliner.run();
}

}  // namespace lowir_opt
