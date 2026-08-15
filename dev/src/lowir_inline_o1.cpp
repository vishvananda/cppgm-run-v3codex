#include "lowir_inline_o1.h"
#include "lowir_opt.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <deque>
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
  std::size_t next_site_id;

  explicit Names(const Function & function) : next_site_id(0)
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
    while(site_ids.count(next_site_id)) ++next_site_id;
    site_ids.insert(next_site_id);
    return next_site_id++;
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

Operand resolve_replacement(Operand value, const ValueMap & replacements)
{
  std::unordered_set<std::string> seen;
  while(value.kind == Operand::OP_TEMP && replacements.count(value.text) &&
        seen.insert(value.text).second)
    value = replacements.find(value.text)->second;
  return value;
}

void replace_values(Function * function, const ValueMap & replacements)
{
  if(replacements.empty()) return;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction & ins = function->blocks[i].instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        *operands[k] = resolve_replacement(*operands[k], replacements);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        ins.args[k] = resolve_replacement(ins.args[k], replacements);
    }
}

typedef std::unordered_map<std::string, std::size_t> BlockIndex;

std::vector<std::size_t> normal_successors(const Function & function,
                                            std::size_t block,
                                            const BlockIndex & index)
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
    const BlockIndex::const_iterator found = index.find(targets[i]->text);
    if(found != index.end()) result.push_back(found->second);
  }
  if(term.kind == Instruction::IK_SWITCH) {
    const BlockIndex::const_iterator fallback = index.find(term.second.text);
    if(fallback != index.end()) result.push_back(fallback->second);
    for(std::size_t i = 1; i < term.args.size(); i += 2) {
      const BlockIndex::const_iterator found = index.find(term.args[i].text);
      if(found != index.end()) result.push_back(found->second);
    }
  }
  return result;
}

struct EhContext
{
  std::vector<unsigned char> incoming;
  std::unordered_set<std::string> landing_blocks;
};

EhContext analyze_eh_context(const Function & function, Stats * stats)
{
  EhContext result;
  BlockIndex index;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    index[function.blocks[i].label] = i;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function.blocks[i].instructions[j];
      if(ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP)
        result.landing_blocks.insert(ins.first.text);
    }
  }
  if(function.blocks.empty()) return result;
  std::vector<unsigned char> incoming(function.blocks.size(), 0), queued(
    function.blocks.size(), 0);
  std::deque<std::size_t> work;
  incoming[0] = 1;
  queued[0] = 1;
  work.push_back(0);
  if(stats) ++stats->worklist_pushes;
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    queued[block] = 0;
    bool active = incoming[block] == 2;
    for(std::size_t i = 0; i < function.blocks[block].instructions.size(); ++i) {
      const Instruction::Kind kind = function.blocks[block].instructions[i].kind;
      if(kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP)
        active = true;
      else if(kind == Instruction::IK_EH_END) active = false;
    }
    const unsigned char outgoing = active ? 2 : 1;
    const std::vector<std::size_t> successors =
      normal_successors(function, block, index);
    for(std::size_t i = 0; i < successors.size(); ++i) {
      const std::size_t successor = successors[i];
      if(incoming[successor] >= outgoing) continue;
      incoming[successor] = outgoing;
      if(stats) ++stats->dataflow_updates;
      if(!queued[successor]) {
        queued[successor] = 1;
        work.push_back(successor);
        if(stats) ++stats->worklist_pushes;
      }
    }
  }
  result.incoming = incoming;
  return result;
}

class Inliner
{
public:
  Inliner(LowirProgram * program,
          std::unordered_set<std::string> * rewritten_functions,
          Stats * stats)
    : program_(*program), rewritten_functions_(rewritten_functions),
      stats_(stats), rewrites_(0)
  {
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      definition_[program_.functions[i].name] = i;
    contains_eh_.resize(program_.functions.size(), 0);
    instruction_counts_.resize(program_.functions.size(), 0);
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      contains_eh_[i] = contains_eh(program_.functions[i]);
      instruction_counts_[i] = instruction_count(program_.functions[i]);
    }
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
  Stats * stats_;
  std::unordered_map<std::string, std::size_t> definition_;
  std::unordered_set<std::string> no_unwind_;
  std::vector<unsigned char> recursive_, state_, contains_eh_;
  std::vector<std::size_t> instruction_counts_;
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
    std::vector<std::size_t> unresolved(program_.functions.size(), 0);
    std::vector<unsigned char> unsafe(program_.functions.size(), 0);
    std::unordered_map<std::string, std::vector<std::size_t> > dependents;
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & function = program_.functions[i];
      if(no_unwind_.count(function.name)) continue;
      unsafe[i] = contains_eh_[i];
      for(std::size_t b = 0; b < function.blocks.size(); ++b)
        for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
          const Instruction & ins = function.blocks[b].instructions[j];
          if(ins.kind == Instruction::IK_THROW || ins.kind == Instruction::IK_RESUME)
            unsafe[i] = 1;
          else if(ins.kind == Instruction::IK_CALL) {
            std::string target;
            if(!direct_call(ins, &target)) {
              unsafe[i] = 1;
              continue;
            }
            if(no_unwind_.count(target)) continue;
            ++unresolved[i];
            dependents[target].push_back(i);
          }
        }
    }
    std::deque<std::size_t> work;
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      if(!unsafe[i] && unresolved[i] == 0 &&
         no_unwind_.insert(program_.functions[i].name).second) {
        work.push_back(i);
        if(stats_) ++stats_->worklist_pushes;
      }
    while(!work.empty()) {
      const std::size_t resolved = work.front();
      work.pop_front();
      const std::unordered_map<std::string,
        std::vector<std::size_t> >::const_iterator found =
          dependents.find(program_.functions[resolved].name);
      if(found == dependents.end()) continue;
      for(std::size_t i = 0; i < found->second.size(); ++i) {
        const std::size_t caller = found->second[i];
        if(unsafe[caller] || unresolved[caller] == 0) continue;
        --unresolved[caller];
        if(stats_) ++stats_->dataflow_updates;
        if(unresolved[caller] == 0 &&
           no_unwind_.insert(program_.functions[caller].name).second) {
          work.push_back(caller);
          if(stats_) ++stats_->worklist_pushes;
        }
      }
    }
  }

  void mark_recursive()
  {
    const std::size_t count = program_.functions.size();
    recursive_.assign(count, 0);
    std::vector<std::vector<std::size_t> > edges(count), reverse(count);
    std::vector<unsigned char> self(count, 0);
    for(std::size_t i = 0; i < count; ++i) {
      const Function & function = program_.functions[i];
      for(std::size_t b = 0; b < function.blocks.size(); ++b)
        for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
          const std::size_t target = callee(function.blocks[b].instructions[j]);
          if(target == kNoFunction) continue;
          edges[i].push_back(target);
          reverse[target].push_back(i);
          self[i] = self[i] || target == i;
        }
    }
    struct Frame { std::size_t node; std::size_t edge; };
    std::vector<unsigned char> seen(count, 0);
    std::vector<std::size_t> order;
    for(std::size_t root = 0; root < count; ++root) {
      if(seen[root]) continue;
      std::vector<Frame> stack;
      seen[root] = 1;
      stack.push_back(Frame{root, 0});
      while(!stack.empty()) {
        Frame & frame = stack.back();
        if(frame.edge < edges[frame.node].size()) {
          const std::size_t next = edges[frame.node][frame.edge++];
          if(!seen[next]) {
            seen[next] = 1;
            stack.push_back(Frame{next, 0});
          }
        } else {
          order.push_back(frame.node);
          stack.pop_back();
        }
      }
    }
    std::fill(seen.begin(), seen.end(), 0);
    for(std::size_t cursor = order.size(); cursor > 0; --cursor) {
      const std::size_t root = order[cursor - 1];
      if(seen[root]) continue;
      std::vector<std::size_t> component, stack(1, root);
      seen[root] = 1;
      while(!stack.empty()) {
        const std::size_t node = stack.back();
        stack.pop_back();
        component.push_back(node);
        for(std::size_t i = 0; i < reverse[node].size(); ++i)
          if(!seen[reverse[node][i]]) {
            seen[reverse[node][i]] = 1;
            stack.push_back(reverse[node][i]);
          }
      }
      if(component.size() > 1 || self[root])
        for(std::size_t i = 0; i < component.size(); ++i)
          recursive_[component[i]] = 1;
    }
  }

  bool candidate(std::size_t caller, std::size_t target,
                 bool landing, bool inside_eh) const
  {
    if(target == kNoFunction || recursive_[target] || target == caller) return false;
    const Function & callee_function = program_.functions[target];
    if(callee_function.boundary.arity == lowir_model::CAM_VARIADIC ||
       contains_eh_[target]) return false;
    if(instruction_counts_[target] > 40 &&
       !callee_function.metadata.prefer_local_object_binding) return false;
    if(landing) return false;
    return !inside_eh ||
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
    contains_eh_[function_index] = contains_eh(program_.functions[function_index]);
    instruction_counts_[function_index] =
      instruction_count(program_.functions[function_index]);
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

  bool leaf_inline_shape(const Function & function) const
  {
    if(function.blocks.size() != 1) return false;
    std::size_t returns = 0;
    for(std::size_t i = 0; i < function.blocks[0].instructions.size(); ++i) {
      if(function.blocks[0].instructions[i].kind == Instruction::IK_CALL)
        return false;
      if(function.blocks[0].instructions[i].kind == Instruction::IK_RETURN)
        ++returns;
    }
    return returns == 1;
  }

  void inline_leaf_call(std::size_t caller_index, const Instruction & call,
                        const Function & callee_function, Names * names,
                        ValueMap * replacements,
                        std::vector<Instruction> * output)
  {
    const Block & source = callee_function.blocks[0];
    const std::string prefix = "__o1inl" +
      std::to_string(names->next_site()) + "__";
    ValueMap values;
    NameMap slots, labels;
    build_maps(callee_function, call, prefix, names, &values, &slots, &labels);
    Function & caller = program_.functions[caller_index];
    for(std::size_t i = 0; i < callee_function.slots.size(); ++i)
      caller.slots.push_back(std::make_pair(
        slots.find(callee_function.slots[i].first)->second,
        callee_function.slots[i].second));

    for(std::size_t i = 0; i < source.instructions.size(); ++i) {
      const Instruction & instruction = source.instructions[i];
      if(instruction.kind != Instruction::IK_RETURN) {
        output->push_back(clone_instruction(instruction, values, slots, labels));
        continue;
      }
      if(call.call_returns_void) continue;
      Operand returned = instruction.first;
      rename_operand(&returned, values, slots, labels);
      (*replacements)[call.dest] = returned;
    }
  }

  void inline_call(std::size_t caller_index, std::size_t block_index,
                   std::size_t instruction_index, const Function & callee_function,
                   Names * names, ValueMap * replacements,
                   std::unordered_map<std::string, unsigned char> * block_eh,
                   bool inside_eh)
  {
    Function & caller = program_.functions[caller_index];
    const Instruction call = caller.blocks[block_index].instructions[instruction_index];
    const std::string prefix = "__o1inl" +
      std::to_string(names->next_site()) + "__";
    ValueMap values;
    NameMap slots, labels;
    build_maps(callee_function, call, prefix, names, &values, &slots, &labels);
    for(NameMap::const_iterator it = labels.begin(); it != labels.end(); ++it)
      (*block_eh)[it->second] = inside_eh ? 1 : 0;
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
        (*block_eh)[wrapper_continuation] = inside_eh ? 1 : 0;
      } else caller.blocks[block_index].instructions.insert(
          caller.blocks[block_index].instructions.end(), tail.begin(), tail.end());
      if(have_single_object_return)
        (*replacements)[call.dest] = single_object_return;
      if(have_single_scalar_return)
        (*replacements)[call.dest] = single_scalar_return;
      return;
    }

    const std::string continuation = "^" + prefix + "cont";
    names->labels.insert(continuation);
    (*block_eh)[continuation] = inside_eh ? 1 : 0;
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
      (*replacements)[call.dest] = replacement;
    }
    if(has_result && !object_result && returns == 1 && have_single_scalar_return)
      (*replacements)[call.dest] = single_scalar_return;
  }

  bool batch_inline_leaf_calls(std::size_t function_index,
                               std::size_t block_index,
                               const EhContext & eh,
                               const std::unordered_map<std::string,
                                 unsigned char> & block_eh,
                               Names * names, ValueMap * replacements)
  {
    Function & function = program_.functions[function_index];
    std::vector<Instruction> source;
    source.swap(function.blocks[block_index].instructions);
    const std::string label = function.blocks[block_index].label;
    const bool landing = eh.landing_blocks.count(label) != 0;
    bool active = block_eh.find(label)->second != 0;
    bool batch_safe = true;
    for(std::size_t i = 0; batch_safe && i < source.size(); ++i) {
      const std::size_t target = callee(source[i]);
      if(target != kNoFunction &&
         candidate(function_index, target, landing, active) &&
         !leaf_inline_shape(program_.functions[target]))
        batch_safe = false;
      if(source[i].kind == Instruction::IK_EH_TRY ||
         source[i].kind == Instruction::IK_EH_CLEANUP) active = true;
      else if(source[i].kind == Instruction::IK_EH_END) active = false;
    }
    if(!batch_safe) {
      function.blocks[block_index].instructions.swap(source);
      return false;
    }

    std::vector<Instruction> rebuilt;
    rebuilt.reserve(source.size());
    active = block_eh.find(label)->second != 0;
    bool changed = false;
    for(std::size_t i = 0; i < source.size(); ++i) {
      const Instruction & ins = source[i];
      const std::size_t target = callee(ins);
      if(target != kNoFunction && candidate(function_index, target, landing, active) &&
         leaf_inline_shape(program_.functions[target])) {
        inline_leaf_call(function_index, ins, program_.functions[target],
          names, replacements, &rebuilt);
        ++rewrites_;
        changed = true;
        if(stats_) {
          ++stats_->inline_call_visits;
          ++stats_->inline_calls;
        }
        continue;
      }
      if(ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) active = true;
      else if(ins.kind == Instruction::IK_EH_END) active = false;
      rebuilt.push_back(ins);
    }
    function.blocks[block_index].instructions.swap(rebuilt);
    return changed;
  }

  void inline_calls(std::size_t function_index)
  {
    Names names(program_.functions[function_index]);
    const EhContext eh = analyze_eh_context(
      program_.functions[function_index], stats_);
    std::unordered_map<std::string, unsigned char> block_eh;
    for(std::size_t b = 0; b < program_.functions[function_index].blocks.size(); ++b)
      block_eh[program_.functions[function_index].blocks[b].label] =
        b < eh.incoming.size() && eh.incoming[b] == 2 ? 1 : 0;
    ValueMap replacements;
    bool changed = false;
    for(std::size_t b = 0;
        b < program_.functions[function_index].blocks.size(); ++b) {
      changed |= batch_inline_leaf_calls(function_index, b, eh, block_eh,
        &names, &replacements);
      bool active = block_eh[
        program_.functions[function_index].blocks[b].label] != 0;
      std::size_t j = 0;
      while(j < program_.functions[function_index].blocks[b].instructions.size()) {
        const Instruction & ins =
          program_.functions[function_index].blocks[b].instructions[j];
        const std::size_t target = callee(ins);
        if(target != kNoFunction) {
          if(stats_) ++stats_->inline_call_visits;
          if(candidate(function_index, target,
               eh.landing_blocks.count(
                 program_.functions[function_index].blocks[b].label) != 0,
               active)) {
            inline_call(function_index, b, j, program_.functions[target],
              &names, &replacements, &block_eh, active);
            ++rewrites_;
            changed = true;
            if(stats_) ++stats_->inline_calls;
            continue;
          }
        }
        const Instruction::Kind kind = ins.kind;
        if(kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP)
          active = true;
        else if(kind == Instruction::IK_EH_END) active = false;
        ++j;
      }
    }
    replace_values(&program_.functions[function_index], replacements);
    if(changed && rewritten_functions_)
      rewritten_functions_->insert(program_.functions[function_index].name);
  }
};

}  // namespace

std::size_t inline_o1_calls(
  LowirProgram & program,
  std::unordered_set<std::string> * rewritten_functions,
  Stats * stats)
{
  Inliner inliner(&program, rewritten_functions, stats);
  return inliner.run();
}

}  // namespace lowir_opt
