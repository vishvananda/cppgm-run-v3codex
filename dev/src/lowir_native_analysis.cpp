#include "lowir_native_analysis.h"

#include <deque>

namespace lowir_native {
namespace analysis {
namespace {

using lowir_model::Instruction;
using lowir_model::Operand;

unsigned instruction_clobber_mask(const Instruction & instruction)
{
  const bool wide = instruction.type.kind == lowir_model::LTK_I128;
  if(wide && (instruction.kind == Instruction::IK_CONST ||
              instruction.kind == Instruction::IK_COPY))
    return register_mask(XR_RAX) | register_mask(XR_RDX) |
      register_mask(XR_R11);
  if(instruction.kind == Instruction::IK_CALL)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_RDI) | register_mask(XR_R8) |
      register_mask(XR_R9) | register_mask(XR_R10) |
      register_mask(XR_R11);
  if(instruction.kind == Instruction::IK_COPYOBJ)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RSI) | register_mask(XR_RDI);
  if(instruction.kind == Instruction::IK_ZEROINIT)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDI);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op == "div" || instruction.op == "mod" ||
      instruction.op == "udiv" || instruction.op == "umod"))
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_BINARY &&
     (instruction.op == "shl" || instruction.op == "shr" ||
      instruction.op == "ushr"))
    return register_mask(XR_RCX) | register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_INDEX &&
     instruction.second.kind != Operand::OP_INTEGER)
    return register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_CMP)
    return wide ? register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      register_mask(XR_R10) | register_mask(XR_R11) :
      register_mask(XR_RAX) | register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_STORE)
    return wide ? register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_R11) :
      register_mask(XR_RAX) | register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_LOAD)
    return wide ? register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_R11) :
      register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_ATOMIC_LOAD)
    return wide ?
      register_mask(XR_RAX) | register_mask(XR_RBX) |
      register_mask(XR_RCX) | register_mask(XR_RDX) |
      register_mask(XR_R11) : register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_ATOMIC_STORE ||
     instruction.kind == Instruction::IK_ATOMIC_EXCHANGE)
    return register_mask(XR_RAX) | register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_ATOMIC_ADD_FETCH)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  if(instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX) | register_mask(XR_RSI) |
      (wide ?
       register_mask(XR_RBX) | register_mask(XR_R10) |
       register_mask(XR_R11) : 0);
  if(instruction.kind == Instruction::IK_BRANCH)
    return register_mask(XR_RAX);
  if(instruction.kind == Instruction::IK_SWITCH)
    return register_mask(XR_RAX) | register_mask(XR_RCX);
  if(instruction.kind == Instruction::IK_VA_START)
    return register_mask(XR_RAX) | register_mask(XR_RCX) |
      register_mask(XR_RDX);
  return 0;
}

void note_operand(FunctionFacts & facts, const Operand & operand, std::size_t position)
{
  if(operand.kind != Operand::OP_TEMP) return;
  ++facts.uses[operand.text];
  if(!facts.first_use.count(operand.text)) facts.first_use[operand.text] = position;
  facts.last_use[operand.text] = position;
}

void note_instruction_operands(FunctionFacts & facts,
                               const Instruction & instruction,
                               std::size_t position)
{
  note_operand(facts, instruction.first, position);
  note_operand(facts, instruction.second, position);
  note_operand(facts, instruction.third, position);
  for(std::size_t i = 0; i < instruction.args.size(); ++i)
    note_operand(facts, instruction.args[i], position);
}

}  // namespace

unsigned register_mask(X64Register reg)
{
  return 1u << static_cast<unsigned>(reg);
}

FunctionFacts analyze_function(const lowir_model::LowirFunction & function)
{
  FunctionFacts facts;
  std::size_t position = 0;
  for(std::size_t i = 0; i < function.params.size(); ++i)
    facts.definition[function.params[i].name] = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j, ++position) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      note_instruction_operands(facts, instruction, position);
      if(!instruction.dest.empty()) facts.definition[instruction.dest] = position;
      if(instruction.kind == Instruction::IK_CALL) facts.calls.push_back(position);
      if(instruction.kind == Instruction::IK_VA_START) facts.has_va_start = true;
      if((instruction.kind == Instruction::IK_ATOMIC_LOAD ||
          instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) &&
         instruction.type.kind == lowir_model::LTK_I128)
        facts.has_i128_atomic = true;
    }
  }

  typedef std::unordered_set<std::string> ValueSet;
  const std::size_t block_count = function.blocks.size();
  std::vector<ValueSet> block_use(block_count);
  std::vector<ValueSet> block_def(block_count);
  std::vector<ValueSet> live_in(block_count);
  std::vector<ValueSet> live_out(block_count);
  std::vector<std::vector<std::size_t> > successors(block_count);
  std::vector<std::vector<std::size_t> > predecessors(block_count);
  std::unordered_map<std::string, std::size_t> block_index;
  for(std::size_t i = 0; i < block_count; ++i)
    block_index[function.blocks[i].label] = i;

  for(std::size_t i = 0; i < block_count; ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & instruction = function.blocks[i].instructions[j];
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k)
        if(fixed[k]->kind == Operand::OP_TEMP &&
           !block_def[i].count(fixed[k]->text))
          block_use[i].insert(fixed[k]->text);
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_TEMP &&
           !block_def[i].count(instruction.args[k].text))
          block_use[i].insert(instruction.args[k].text);
      if(!instruction.dest.empty()) block_def[i].insert(instruction.dest);
    }
    if(function.blocks[i].instructions.empty()) continue;
    const Instruction & terminal = function.blocks[i].instructions.back();
    std::vector<std::string> targets;
    if(terminal.kind == Instruction::IK_JUMP) targets.push_back(terminal.first.text);
    else if(terminal.kind == Instruction::IK_BRANCH) {
      targets.push_back(terminal.second.text);
      targets.push_back(terminal.third.text);
    } else if(terminal.kind == Instruction::IK_SWITCH) {
      targets.push_back(terminal.second.text);
      for(std::size_t k = 1; k < terminal.args.size(); k += 2)
        targets.push_back(terminal.args[k].text);
    }
    for(std::size_t j = 0; j < targets.size(); ++j) {
      const std::unordered_map<std::string, std::size_t>::const_iterator found =
        block_index.find(targets[j]);
      if(found == block_index.end()) continue;
      successors[i].push_back(found->second);
      predecessors[found->second].push_back(i);
    }
  }

  std::deque<std::size_t> worklist;
  std::vector<bool> queued(block_count, true);
  for(std::size_t i = 0; i < block_count; ++i) worklist.push_back(i);
  while(!worklist.empty()) {
    const std::size_t block = worklist.front();
    worklist.pop_front();
    queued[block] = false;
    ValueSet new_out;
    for(std::size_t i = 0; i < successors[block].size(); ++i)
      new_out.insert(live_in[successors[block][i]].begin(),
                     live_in[successors[block][i]].end());
    ValueSet new_in = block_use[block];
    for(ValueSet::const_iterator value = new_out.begin(); value != new_out.end(); ++value)
      if(!block_def[block].count(*value)) new_in.insert(*value);
    if(new_out == live_out[block] && new_in == live_in[block]) continue;
    live_out[block].swap(new_out);
    live_in[block].swap(new_in);
    for(std::size_t i = 0; i < predecessors[block].size(); ++i) {
      const std::size_t predecessor = predecessors[block][i];
      if(queued[predecessor]) continue;
      queued[predecessor] = true;
      worklist.push_back(predecessor);
    }
  }

  for(std::size_t i = 0; i < block_count; ++i) {
    facts.edge_live.insert(live_in[i].begin(), live_in[i].end());
    facts.edge_live.insert(live_out[i].begin(), live_out[i].end());
    ValueSet live = live_out[i];
    for(std::size_t j = function.blocks[i].instructions.size(); j != 0; --j) {
      const Instruction & instruction = function.blocks[i].instructions[j - 1];
      if(!instruction.dest.empty()) live.erase(instruction.dest);
      const unsigned clobbers = instruction_clobber_mask(instruction);
      if(clobbers)
        for(ValueSet::const_iterator value = live.begin(); value != live.end(); ++value)
          facts.live_across_clobbers[*value] |= clobbers;
      if(instruction.kind == Instruction::IK_CALL)
        facts.live_across_call.insert(live.begin(), live.end());
      const Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < sizeof(fixed) / sizeof(fixed[0]); ++k)
        if(fixed[k]->kind == Operand::OP_TEMP) live.insert(fixed[k]->text);
      for(std::size_t k = 0; k < instruction.args.size(); ++k)
        if(instruction.args[k].kind == Operand::OP_TEMP)
          live.insert(instruction.args[k].text);
    }
  }
  return facts;
}

}  // namespace analysis
}  // namespace lowir_native
