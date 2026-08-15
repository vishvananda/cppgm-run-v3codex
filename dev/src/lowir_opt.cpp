#include "lowir_opt.h"
#include "lowir_inline_o1.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::FunctionBoundaryMetadata;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::LowTypeKind;
using lowir_model::LowirProgram;
using lowir_model::Operand;

typedef std::unordered_map<std::string, std::size_t> BlockIndex;
const std::size_t kNoBlock = static_cast<std::size_t>(-1);

bool same_operand(const Operand & a, const Operand & b)
{
  return a.kind == b.kind && a.text == b.text;
}

Operand integer_operand(long long value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_INTEGER;
  result.has_int_value = true;
  result.int_value = value;
  result.text = std::to_string(value);
  result.literal_type = type;
  return result;
}

Operand floating_operand(long double value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_FLOAT;
  result.float_value = value;
  result.literal_type = type;
  if(std::isinf(value)) result.text = value < 0 ? "-inf" : "inf";
  else if(std::isnan(value)) result.text = "nan";
  else {
    std::ostringstream text;
    text.precision(20);
    text << value;
    result.text = text.str();
    if(type.kind == lowir_model::LTK_F32) result.text += 'f';
    else if(type.kind == lowir_model::LTK_F80) result.text += 'L';
  }
  return result;
}

bool is_integer_type(const LowType & type)
{
  return type.kind == lowir_model::LTK_I1 ||
    (type.kind >= lowir_model::LTK_I8 && type.kind <= lowir_model::LTK_I64) ||
    type.kind == lowir_model::LTK_I128;
}

bool is_float_type(const LowType & type)
{
  return type.kind >= lowir_model::LTK_F32 && type.kind <= lowir_model::LTK_F80;
}

std::uint64_t width_mask(const LowType & type)
{
  return type.bit_width >= 64 ? ~UINT64_C(0) :
    (UINT64_C(1) << type.bit_width) - 1;
}

long long normalize_integer(std::uint64_t value, const LowType & type)
{
  value &= width_mask(type);
  if(type.kind == lowir_model::LTK_U8 || type.kind == lowir_model::LTK_U16 ||
     type.kind == lowir_model::LTK_U32 || type.kind == lowir_model::LTK_PTR)
    return static_cast<long long>(value);
  if(type.bit_width && type.bit_width < 64 &&
     (value & (UINT64_C(1) << (type.bit_width - 1))))
    value |= ~width_mask(type);
  return static_cast<long long>(value);
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

bool is_eh_instruction(Instruction::Kind kind)
{
  return kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_EH_END;
}

bool is_pure(Instruction::Kind kind)
{
  return kind == Instruction::IK_CONST || kind == Instruction::IK_COPY ||
    kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

bool cse_eligible(Instruction::Kind kind)
{
  return kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

bool commutative(const std::string & op)
{
  return op == "add" || op == "mul" || op == "and" || op == "or" ||
    op == "xor";
}

std::string reverse_compare(const std::string & op)
{
  if(op == "lt") return "gt";
  if(op == "le") return "ge";
  if(op == "gt") return "lt";
  if(op == "ge") return "le";
  if(op == "ult") return "ugt";
  if(op == "ule") return "uge";
  if(op == "ugt") return "ult";
  if(op == "uge") return "ule";
  return op;
}

bool operand_less(const Operand & a, const Operand & b)
{
  if(a.kind != b.kind) return a.kind < b.kind;
  return a.text < b.text;
}

std::string expression_key(const Instruction & ins)
{
  Operand first = ins.first;
  Operand second = ins.second;
  std::string op = ins.op;
  if((ins.kind == Instruction::IK_BINARY && commutative(op)) ||
     (ins.kind == Instruction::IK_CMP && (op == "eq" || op == "ne"))) {
    if(operand_less(second, first)) std::swap(first, second);
  } else if(ins.kind == Instruction::IK_CMP && operand_less(second, first)) {
    std::swap(first, second);
    op = reverse_compare(op);
  }
  std::ostringstream key;
  key << static_cast<int>(ins.kind) << '|' << op << '|' << ins.type.text << '|'
      << ins.source_type.text << '|' << static_cast<int>(ins.index_projection)
      << '|' << static_cast<int>(first.kind) << ':' << first.text << '|'
      << static_cast<int>(second.kind) << ':' << second.text;
  return key.str();
}

bool fold_unary(const Instruction & ins, Operand * result)
{
  if(ins.op == "decay" && ins.type.kind == lowir_model::LTK_PTR) {
    *result = ins.first;
    return true;
  }
  if(ins.first.kind != Operand::OP_INTEGER || !ins.first.has_int_value ||
     !is_integer_type(ins.type)) return false;
  const std::uint64_t value = static_cast<std::uint64_t>(ins.first.int_value);
  if(ins.op == "neg")
    *result = integer_operand(normalize_integer(UINT64_C(0) - value, ins.type), ins.type);
  else if(ins.op == "bitnot")
    *result = integer_operand(normalize_integer(~value, ins.type), ins.type);
  else if(ins.op == "not")
    *result = integer_operand(value == 0, ins.type);
  else return false;
  return true;
}

bool fold_binary(const Instruction & ins, Operand * result)
{
  if(ins.first.kind != Operand::OP_INTEGER || !ins.first.has_int_value ||
     ins.second.kind != Operand::OP_INTEGER || !ins.second.has_int_value ||
     !is_integer_type(ins.type)) return false;
  const std::uint64_t a = static_cast<std::uint64_t>(ins.first.int_value);
  const std::uint64_t b = static_cast<std::uint64_t>(ins.second.int_value);
  std::uint64_t value = 0;
  if(ins.op == "add") value = a + b;
  else if(ins.op == "sub") value = a - b;
  else if(ins.op == "mul") value = a * b;
  else if(ins.op == "and") value = a & b;
  else if(ins.op == "or") value = a | b;
  else if(ins.op == "xor") value = a ^ b;
  else if(ins.op == "shl" && b < 64) value = a << b;
  else if(ins.op == "ushr" && b < 64) value = a >> b;
  else if(ins.op == "shr" && b < 64)
    value = static_cast<std::uint64_t>(ins.first.int_value >> b);
  else if((ins.op == "udiv" || ins.op == "umod") && b)
    value = ins.op == "udiv" ? a / b : a % b;
  else if((ins.op == "div" || ins.op == "mod") && ins.second.int_value &&
          !(ins.first.int_value == std::numeric_limits<long long>::min() &&
            ins.second.int_value == -1))
    value = static_cast<std::uint64_t>(ins.op == "div" ?
      ins.first.int_value / ins.second.int_value :
      ins.first.int_value % ins.second.int_value);
  else return false;
  *result = integer_operand(normalize_integer(value, ins.type), ins.type);
  return true;
}

bool fold_compare(const Instruction & ins, Operand * result)
{
  bool value = false;
  if(ins.first.kind == Operand::OP_INTEGER && ins.first.has_int_value &&
     ins.second.kind == Operand::OP_INTEGER && ins.second.has_int_value) {
    const long long a = ins.first.int_value;
    const long long b = ins.second.int_value;
    const std::uint64_t ua = static_cast<std::uint64_t>(a) & width_mask(ins.type);
    const std::uint64_t ub = static_cast<std::uint64_t>(b) & width_mask(ins.type);
    if(ins.op == "eq") value = ua == ub;
    else if(ins.op == "ne") value = ua != ub;
    else if(ins.op == "lt") value = a < b;
    else if(ins.op == "le") value = a <= b;
    else if(ins.op == "gt") value = a > b;
    else if(ins.op == "ge") value = a >= b;
    else if(ins.op == "ult") value = ua < ub;
    else if(ins.op == "ule") value = ua <= ub;
    else if(ins.op == "ugt") value = ua > ub;
    else if(ins.op == "uge") value = ua >= ub;
    else return false;
  } else if(ins.first.kind == Operand::OP_FLOAT &&
            ins.second.kind == Operand::OP_FLOAT) {
    const long double a = ins.first.float_value;
    const long double b = ins.second.float_value;
    if(ins.op == "eq") value = a == b;
    else if(ins.op == "ne") value = a != b;
    else if(ins.op == "lt") value = a < b;
    else if(ins.op == "le") value = a <= b;
    else if(ins.op == "gt") value = a > b;
    else if(ins.op == "ge") value = a >= b;
    else return false;
  } else if(same_operand(ins.first, ins.second)) {
    if(ins.op == "eq" || ins.op == "le" || ins.op == "ge" ||
       ins.op == "ule" || ins.op == "uge") value = true;
    else if(ins.op == "ne" || ins.op == "lt" || ins.op == "gt" ||
            ins.op == "ult" || ins.op == "ugt") value = false;
    else return false;
  } else return false;
  *result = integer_operand(value ? 1 : 0,
    lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
  return true;
}

bool fold_convert(const Instruction & ins, Operand * result)
{
  if(lowir_model::same_lowir_type(ins.type, ins.source_type)) {
    *result = ins.first;
    return true;
  }
  if(ins.first.kind == Operand::OP_INTEGER && ins.first.has_int_value) {
    if(is_integer_type(ins.type)) {
      std::uint64_t value = static_cast<std::uint64_t>(ins.first.int_value);
      if(ins.op == "zext") value &= width_mask(ins.source_type);
      *result = integer_operand(normalize_integer(value, ins.type), ins.type);
      return true;
    }
    if(is_float_type(ins.type) && (ins.op == "sitofp" || ins.op == "uitofp")) {
      const long double value = ins.op == "uitofp" ?
        static_cast<long double>(static_cast<std::uint64_t>(ins.first.int_value) &
                                 width_mask(ins.source_type)) :
        static_cast<long double>(ins.first.int_value);
      *result = floating_operand(value, ins.type);
      return true;
    }
  }
  if(ins.first.kind == Operand::OP_FLOAT && is_float_type(ins.type) &&
     (ins.op == "fpext" || ins.op == "fptrunc")) {
    *result = floating_operand(ins.first.float_value, ins.type);
    return true;
  }
  return false;
}

bool algebraic_identity(const Instruction & ins, Operand * result)
{
  if(ins.kind != Instruction::IK_BINARY) return false;
  if((ins.op == "add" || ins.op == "or" || ins.op == "xor") && is_zero(ins.second))
    *result = ins.first;
  else if(ins.op == "add" && is_zero(ins.first)) *result = ins.second;
  else if(ins.op == "sub" && is_zero(ins.second)) *result = ins.first;
  else if((ins.op == "mul" || ins.op == "div" || ins.op == "udiv") &&
          is_one(ins.second)) *result = ins.first;
  else if(ins.op == "mul" && is_one(ins.first)) *result = ins.second;
  else if(ins.op == "and" && is_minus_one(ins.second)) *result = ins.first;
  else if(ins.op == "and" && is_minus_one(ins.first)) *result = ins.second;
  else return false;
  return true;
}

struct Graph
{
  BlockIndex index;
  std::vector<std::vector<std::size_t> > successors;
  std::vector<std::vector<std::size_t> > predecessors;
  std::unordered_set<std::string> eh_targets;
};

void add_edge(Graph * graph, std::size_t from, const Operand & target,
              Stats * stats)
{
  if(target.kind != Operand::OP_LABEL) return;
  const BlockIndex::const_iterator found = graph->index.find(target.text);
  if(found == graph->index.end()) return;
  graph->successors[from].push_back(found->second);
  if(stats) ++stats->cfg_edge_visits;
}

Graph build_graph(const Function & function, Stats * stats)
{
  Graph result;
  result.successors.resize(function.blocks.size());
  result.predecessors.resize(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    result.index[function.blocks[i].label] = i;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const Block & block = function.blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const Instruction & ins = block.instructions[j];
      if(ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) {
        add_edge(&result, i, ins.first, stats);
        result.eh_targets.insert(ins.first.text);
      }
    }
    if(block.instructions.empty()) continue;
    const Instruction & term = block.instructions.back();
    if(term.kind == Instruction::IK_JUMP)
      add_edge(&result, i, term.first, stats);
    else if(term.kind == Instruction::IK_BRANCH) {
      add_edge(&result, i, term.second, stats);
      add_edge(&result, i, term.third, stats);
    } else if(term.kind == Instruction::IK_SWITCH) {
      add_edge(&result, i, term.second, stats);
      for(std::size_t j = 1; j < term.args.size(); j += 2)
        add_edge(&result, i, term.args[j], stats);
    }
  }
  for(std::size_t i = 0; i < result.successors.size(); ++i) {
    std::vector<std::size_t> & edges = result.successors[i];
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    for(std::size_t j = 0; j < edges.size(); ++j)
      result.predecessors[edges[j]].push_back(i);
  }
  return result;
}

struct DominatorTree
{
  std::vector<std::size_t> immediate;
  std::vector<std::size_t> preorder;
  std::vector<std::size_t> postorder;

  bool dominates(std::size_t parent, std::size_t child) const
  {
    if(parent == child) return true;
    return parent < preorder.size() && child < preorder.size() &&
      preorder[parent] != 0 && preorder[child] != 0 &&
      preorder[parent] <= preorder[child] &&
      postorder[child] <= postorder[parent];
  }
};

std::size_t evaluate_dominator(
    std::size_t node, std::vector<std::size_t> * ancestor,
    std::vector<std::size_t> * label,
    const std::vector<std::size_t> & semi)
{
  if((*ancestor)[node] == kNoBlock) return (*label)[node];
  std::vector<std::size_t> path;
  std::size_t cursor = node;
  while((*ancestor)[cursor] != kNoBlock &&
        (*ancestor)[(*ancestor)[cursor]] != kNoBlock) {
    path.push_back(cursor);
    cursor = (*ancestor)[cursor];
  }
  for(std::size_t i = path.size(); i > 0; --i) {
    const std::size_t value = path[i - 1];
    const std::size_t parent = (*ancestor)[value];
    if(semi[(*label)[parent]] < semi[(*label)[value]])
      (*label)[value] = (*label)[parent];
    (*ancestor)[value] = (*ancestor)[parent];
  }
  return (*label)[node];
}

DominatorTree dominators(const Graph & graph, Stats * stats)
{
  const std::size_t count = graph.successors.size();
  DominatorTree result;
  result.immediate.assign(count, kNoBlock);
  result.preorder.assign(count, 0);
  result.postorder.assign(count, 0);
  if(!count) return result;

  std::vector<std::size_t> semi(count, kNoBlock), parent(count, kNoBlock),
    ancestor(count, kNoBlock), label(count, kNoBlock), vertex;
  struct DfsFrame { std::size_t block; std::size_t edge; };
  std::vector<DfsFrame> dfs;
  semi[0] = 0;
  label[0] = 0;
  vertex.push_back(0);
  dfs.push_back(DfsFrame{0, 0});
  while(!dfs.empty()) {
    DfsFrame & frame = dfs.back();
    if(frame.edge == graph.successors[frame.block].size()) {
      dfs.pop_back();
      continue;
    }
    const std::size_t next = graph.successors[frame.block][frame.edge++];
    if(semi[next] != kNoBlock) continue;
    parent[next] = frame.block;
    semi[next] = vertex.size();
    label[next] = next;
    vertex.push_back(next);
    dfs.push_back(DfsFrame{next, 0});
  }

  std::vector<std::vector<std::size_t> > bucket(count);
  for(std::size_t reverse = vertex.size(); reverse > 1; --reverse) {
    const std::size_t block = vertex[reverse - 1];
    for(std::size_t i = 0; i < graph.predecessors[block].size(); ++i) {
      const std::size_t predecessor = graph.predecessors[block][i];
      if(semi[predecessor] == kNoBlock) continue;
      const std::size_t candidate = evaluate_dominator(
        predecessor, &ancestor, &label, semi);
      semi[block] = std::min(semi[block], semi[candidate]);
    }
    bucket[vertex[semi[block]]].push_back(block);
    ancestor[block] = parent[block];
    std::vector<std::size_t> & pending = bucket[parent[block]];
    for(std::size_t i = 0; i < pending.size(); ++i) {
      const std::size_t candidate = evaluate_dominator(
        pending[i], &ancestor, &label, semi);
      result.immediate[pending[i]] = semi[candidate] < semi[pending[i]] ?
        candidate : parent[block];
    }
    pending.clear();
    if(stats) ++stats->block_visits;
  }
  result.immediate[0] = 0;
  for(std::size_t i = 1; i < vertex.size(); ++i) {
    const std::size_t block = vertex[i];
    if(result.immediate[block] != vertex[semi[block]])
      result.immediate[block] = result.immediate[result.immediate[block]];
  }

  std::vector<std::vector<std::size_t> > children(count);
  for(std::size_t i = 1; i < vertex.size(); ++i)
    children[result.immediate[vertex[i]]].push_back(vertex[i]);
  std::size_t ordinal = 0;
  dfs.clear();
  result.preorder[0] = ++ordinal;
  dfs.push_back(DfsFrame{0, 0});
  while(!dfs.empty()) {
    DfsFrame & frame = dfs.back();
    if(frame.edge < children[frame.block].size()) {
      const std::size_t child = children[frame.block][frame.edge++];
      result.preorder[child] = ++ordinal;
      dfs.push_back(DfsFrame{child, 0});
    } else {
      result.postorder[frame.block] = ordinal;
      dfs.pop_back();
    }
  }
  return result;
}

struct Fact
{
  Operand value;
  std::size_t block;
};

Operand resolve_operand(Operand value,
                        const std::unordered_map<std::string, Fact> & facts,
                        std::size_t block,
                        const DominatorTree & dom)
{
  std::unordered_set<std::string> seen;
  while(value.kind == Operand::OP_TEMP && seen.insert(value.text).second) {
    const std::unordered_map<std::string, Fact>::const_iterator found =
      facts.find(value.text);
    if(found == facts.end() ||
       !dom.dominates(found->second.block, block)) break;
    value = found->second.value;
  }
  return value;
}

void resolve_instruction_operands(Instruction * ins,
                                  const std::unordered_map<std::string, Fact> & facts,
                                  std::size_t block,
                                  const DominatorTree & dom)
{
  Operand * values[] = {&ins->first, &ins->second, &ins->third};
  for(std::size_t i = 0; i < 3; ++i) {
    const bool storage_address =
      (i == 0 && (ins->kind == Instruction::IK_LOAD ||
                  ins->kind == Instruction::IK_ATOMIC_LOAD)) ||
      (i == 1 && (ins->kind == Instruction::IK_STORE ||
                  ins->kind == Instruction::IK_ATOMIC_STORE));
    if(values[i]->kind == Operand::OP_TEMP) {
      const Operand resolved = resolve_operand(*values[i], facts, block, dom);
      if(!storage_address || resolved.kind == Operand::OP_TEMP)
        *values[i] = resolved;
    }
  }
  for(std::size_t i = 0; i < ins->args.size(); ++i)
    if(ins->args[i].kind == Operand::OP_TEMP)
      ins->args[i] = resolve_operand(ins->args[i], facts, block, dom);
}

LowType result_type(const Instruction & ins)
{
  if(ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX)
    return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
  if(ins.kind == Instruction::IK_CMP ||
     ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
     ins.kind == Instruction::IK_EXCEPTION_SELECTOR)
    return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  return ins.type;
}

bool has_eh(const Function & function)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      if(is_eh_instruction(function.blocks[i].instructions[j].kind)) return true;
  return false;
}

bool reassociate(Instruction * ins,
                 const std::unordered_map<std::string, Instruction> & definitions)
{
  if(ins->kind != Instruction::IK_BINARY || !commutative(ins->op) ||
     ins->second.kind != Operand::OP_INTEGER || !ins->second.has_int_value ||
     ins->first.kind != Operand::OP_TEMP) return false;
  const std::unordered_map<std::string, Instruction>::const_iterator found =
    definitions.find(ins->first.text);
  if(found == definitions.end()) return false;
  const Instruction & parent = found->second;
  if(parent.kind != Instruction::IK_BINARY || parent.op != ins->op ||
     parent.second.kind != Operand::OP_INTEGER ||
     !parent.second.has_int_value ||
     !lowir_model::same_lowir_type(parent.type, ins->type)) return false;
  Instruction constants = *ins;
  constants.first = parent.second;
  Operand folded;
  if(!fold_binary(constants, &folded)) return false;
  ins->first = parent.first;
  ins->second = folded;
  return true;
}

bool simplify_values(Function * function, Stats * stats)
{
  if(function->blocks.empty()) return false;
  const Graph graph = build_graph(*function, stats);
  const DominatorTree dom = dominators(graph, stats);
  const bool function_has_eh = has_eh(*function);
  std::unordered_map<std::string, LowType> types;
  std::unordered_set<std::string> storage_temporaries;
  for(std::size_t i = 0; i < function->params.size(); ++i)
    types[function->params[i].name] = function->params[i].type;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(!ins.dest.empty()) types[ins.dest] = result_type(ins);
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.first.text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.second.text);
    }

  std::unordered_map<std::string, Fact> facts;
  std::unordered_map<std::string, Fact> expressions;
  std::unordered_map<std::string, Instruction> definitions;
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> kept;
    kept.reserve(function->blocks[block].instructions.size());
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction ins = function->blocks[block].instructions[index];
      if(stats) ++stats->instruction_visits;
      resolve_instruction_operands(&ins, facts, block, dom);

      Operand replacement;
      bool replace = false;
      if(ins.kind == Instruction::IK_CONST &&
         !storage_temporaries.count(ins.dest)) {
        replacement = ins.first;
        replace = true;
      } else if(ins.kind == Instruction::IK_COPY &&
                ins.dest.compare(0, 5, "%dbg_") != 0 &&
                !storage_temporaries.count(ins.dest)) {
        const std::unordered_map<std::string, LowType>::const_iterator source =
          types.find(ins.first.text);
        replace = ins.first.kind != Operand::OP_TEMP || source == types.end() ||
          lowir_model::same_lowir_type(source->second, ins.type);
        replacement = ins.first;
      } else if(ins.kind == Instruction::IK_UNARY)
        replace = fold_unary(ins, &replacement);
      else if(ins.kind == Instruction::IK_BINARY) {
        reassociate(&ins, definitions);
        replace = fold_binary(ins, &replacement) ||
          algebraic_identity(ins, &replacement);
      } else if(ins.kind == Instruction::IK_CMP) {
        replace = fold_compare(ins, &replacement);
        if(!replace && (ins.op == "eq" || ins.op == "ne") &&
           ((is_zero(ins.second) && ins.op == "ne") ||
            (is_one(ins.second) && ins.op == "eq"))) {
          const std::unordered_map<std::string, Instruction>::const_iterator boolean =
            definitions.find(ins.first.text);
          if(boolean != definitions.end() &&
             boolean->second.kind == Instruction::IK_CMP) {
            replacement = ins.first;
            replace = true;
          }
        }
      } else if(ins.kind == Instruction::IK_CONVERT)
        replace = fold_convert(ins, &replacement);

      if(replace && !ins.dest.empty()) {
        facts[ins.dest] = Fact{replacement, block};
        changed = true;
        if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
        continue;
      }

      if(cse_eligible(ins.kind) && !ins.dest.empty()) {
        const std::string key = expression_key(ins);
        const std::unordered_map<std::string, Fact>::const_iterator found =
          expressions.find(key);
        const bool cross_block_guard = function_has_eh &&
          (ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX) &&
          found != expressions.end() && found->second.block != block;
        if(found != expressions.end() && !cross_block_guard &&
           dom.dominates(found->second.block, block)) {
          facts[ins.dest] = Fact{found->second.value, block};
          changed = true;
          if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
          continue;
        }
        Operand produced;
        produced.kind = Operand::OP_TEMP;
        produced.text = ins.dest;
        produced.literal_type = result_type(ins);
        expressions[key] = Fact{produced, block};
      }
      if(!ins.dest.empty()) definitions[ins.dest] = ins;
      kept.push_back(ins);
    }
    function->blocks[block].instructions.swap(kept);
  }
  return changed;
}

void count_operand_use(const Operand & operand,
                       std::unordered_map<std::string, std::size_t> * uses)
{
  if(operand.kind == Operand::OP_TEMP) ++(*uses)[operand.text];
}

bool call_is_removable(const Instruction & ins,
                       const std::unordered_map<std::string,
                         FunctionBoundaryMetadata> & boundaries)
{
  if(ins.kind != Instruction::IK_CALL || ins.dest.empty()) return false;
  FunctionBoundaryMetadata boundary = ins.call_boundary;
  if(ins.first.kind == Operand::OP_GLOBAL) {
    const std::unordered_map<std::string, FunctionBoundaryMetadata>::const_iterator
      found = boundaries.find(ins.first.text);
    if(found != boundaries.end()) boundary = found->second;
  }
  return boundary.effects == lowir_model::CFXM_READNONE &&
    boundary.unwind == lowir_model::CUM_NO &&
    boundary.returns != lowir_model::CRM_NORETURN;
}

bool eliminate_dead_code(Function * function,
                         const std::unordered_map<std::string,
                           FunctionBoundaryMetadata> & boundaries,
                         Stats * stats)
{
  typedef std::pair<std::size_t, std::size_t> Location;
  std::unordered_map<std::string, Location> definitions;
  std::unordered_map<std::string, std::size_t> uses;
  std::vector<std::vector<unsigned char> > dead(function->blocks.size());
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    dead[i].assign(function->blocks[i].instructions.size(), 0);
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(!ins.dest.empty()) definitions[ins.dest] = Location(i, j);
      count_operand_use(ins.first, &uses);
      count_operand_use(ins.second, &uses);
      count_operand_use(ins.third, &uses);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        count_operand_use(ins.args[k], &uses);
      if(stats) ++stats->instruction_visits;
    }
  }

  std::deque<Location> work;
  for(std::unordered_map<std::string, Location>::const_iterator it =
        definitions.begin(); it != definitions.end(); ++it) {
    const Instruction & ins =
      function->blocks[it->second.first].instructions[it->second.second];
    const std::unordered_map<std::string, std::size_t>::const_iterator used =
      uses.find(it->first);
    if((used == uses.end() || used->second == 0) &&
       (is_pure(ins.kind) || ins.kind == Instruction::IK_LOAD ||
        call_is_removable(ins, boundaries))) {
      work.push_back(it->second);
      if(stats) ++stats->worklist_pushes;
    }
  }

  const auto release_operand = [&](const Operand & operand) {
    if(operand.kind != Operand::OP_TEMP) return;
    std::unordered_map<std::string, std::size_t>::iterator used =
      uses.find(operand.text);
    if(used == uses.end() || used->second == 0) return;
    --used->second;
    if(used->second != 0) return;
    const std::unordered_map<std::string, Location>::const_iterator found =
      definitions.find(operand.text);
    if(found == definitions.end()) return;
    const Instruction & producer =
      function->blocks[found->second.first].instructions[found->second.second];
    if(is_pure(producer.kind) || producer.kind == Instruction::IK_LOAD ||
       call_is_removable(producer, boundaries)) {
      work.push_back(found->second);
      if(stats) ++stats->worklist_pushes;
    }
  };

  std::size_t removed = 0;
  while(!work.empty()) {
    const Location location = work.front();
    work.pop_front();
    if(dead[location.first][location.second]) continue;
    dead[location.first][location.second] = 1;
    ++removed;
    const Instruction & ins =
      function->blocks[location.first].instructions[location.second];
    release_operand(ins.first);
    release_operand(ins.second);
    release_operand(ins.third);
    for(std::size_t i = 0; i < ins.args.size(); ++i)
      release_operand(ins.args[i]);
  }
  if(!removed) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> kept;
    kept.reserve(function->blocks[i].instructions.size());
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j)
      if(!dead[i][j]) kept.push_back(function->blocks[i].instructions[j]);
    function->blocks[i].instructions.swap(kept);
  }
  if(stats) stats->rewrites += removed;
  return true;
}

std::vector<std::string> bypass_targets(const Function & function,
                                        const Graph & graph)
{
  const std::size_t count = function.blocks.size();
  std::vector<std::size_t> next(count, kNoBlock);
  for(std::size_t i = 0; i < count; ++i) {
    const Block & block = function.blocks[i];
    if(graph.eh_targets.count(block.label) || block.instructions.size() != 1 ||
       block.instructions[0].kind != Instruction::IK_JUMP) continue;
    const BlockIndex::const_iterator found =
      graph.index.find(block.instructions[0].first.text);
    if(found != graph.index.end()) next[i] = found->second;
  }
  std::vector<std::string> result(count);
  std::vector<unsigned char> state(count, 0);
  for(std::size_t start = 0; start < count; ++start) {
    if(state[start] == 2) continue;
    std::vector<std::size_t> path;
    std::size_t cursor = start;
    while(state[cursor] == 0 && next[cursor] != kNoBlock) {
      state[cursor] = 1;
      path.push_back(cursor);
      cursor = next[cursor];
    }
    if(state[cursor] == 0) {
      state[cursor] = 2;
      result[cursor] = function.blocks[cursor].label;
    }
    if(state[cursor] == 1) {
      std::size_t cycle = 0;
      while(cycle < path.size() && path[cycle] != cursor) ++cycle;
      for(std::size_t i = cycle; i < path.size(); ++i) {
        result[path[i]] = function.blocks[path[i]].label;
        state[path[i]] = 2;
      }
      for(std::size_t i = cycle; i > 0; --i) {
        result[path[i - 1]] = function.blocks[cursor].label;
        state[path[i - 1]] = 2;
      }
      continue;
    }
    std::string target = result[cursor];
    for(std::size_t i = path.size(); i > 0; --i) {
      result[path[i - 1]] = target;
      state[path[i - 1]] = 2;
    }
  }
  for(std::size_t i = 0; i < count; ++i)
    if(result[i].empty()) result[i] = function.blocks[i].label;
  return result;
}

bool cleanup_cfg(Function * function, Stats * stats)
{
  if(function->blocks.empty()) return false;
  bool changed = false;
  Graph graph = build_graph(*function, stats);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    Block & block = function->blocks[i];
    if(block.instructions.empty()) continue;
    Instruction & term = block.instructions.back();
    if(term.kind == Instruction::IK_BRANCH) {
      if(term.first.kind == Operand::OP_INTEGER && term.first.has_int_value) {
        const Operand selected = term.first.int_value ? term.second : term.third;
        const lowir_model::InstructionDebugLocation debug = term.debug_location;
        term = Instruction();
        term.kind = Instruction::IK_JUMP;
        term.first = selected;
        term.debug_location = debug;
        changed = true;
      } else if(term.second.text == term.third.text) {
        term.kind = Instruction::IK_JUMP;
        term.first = term.second;
        term.second = Operand();
        term.third = Operand();
        changed = true;
      }
    } else if(term.kind == Instruction::IK_SWITCH &&
              term.first.kind == Operand::OP_INTEGER && term.first.has_int_value) {
      Operand selected = term.second;
      for(std::size_t j = 0; j + 1 < term.args.size(); j += 2)
        if(term.args[j].kind == Operand::OP_INTEGER &&
           term.args[j].has_int_value &&
           term.args[j].int_value == term.first.int_value) {
          selected = term.args[j + 1];
          break;
        }
      const lowir_model::InstructionDebugLocation debug = term.debug_location;
      term = Instruction();
      term.kind = Instruction::IK_JUMP;
      term.first = selected;
      term.debug_location = debug;
      changed = true;
    }
  }

  if(changed) graph = build_graph(*function, stats);
  const std::vector<std::string> bypass = bypass_targets(*function, graph);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction & ins = function->blocks[i].instructions[j];
      Operand * targets[3] = {0, 0, 0};
      std::size_t count = 0;
      if(ins.kind == Instruction::IK_JUMP || ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) targets[count++] = &ins.first;
      else if(ins.kind == Instruction::IK_BRANCH) {
        targets[count++] = &ins.second; targets[count++] = &ins.third;
      } else if(ins.kind == Instruction::IK_SWITCH) targets[count++] = &ins.second;
      for(std::size_t k = 0; k < count; ++k) {
        const BlockIndex::const_iterator found = graph.index.find(targets[k]->text);
        const std::string target = found == graph.index.end() ?
          targets[k]->text : bypass[found->second];
        if(target != targets[k]->text &&
           ins.kind != Instruction::IK_EH_TRY &&
           ins.kind != Instruction::IK_EH_CLEANUP) {
          targets[k]->text = target;
          changed = true;
        }
      }
      if(ins.kind == Instruction::IK_SWITCH)
        for(std::size_t k = 1; k < ins.args.size(); k += 2) {
          const BlockIndex::const_iterator found = graph.index.find(ins.args[k].text);
          const std::string target = found == graph.index.end() ?
            ins.args[k].text : bypass[found->second];
          if(target != ins.args[k].text) { ins.args[k].text = target; changed = true; }
        }
      if(ins.kind == Instruction::IK_BRANCH &&
         ins.second.text == ins.third.text) {
        const Operand selected = ins.second;
        const lowir_model::InstructionDebugLocation debug = ins.debug_location;
        ins = Instruction();
        ins.kind = Instruction::IK_JUMP;
        ins.first = selected;
        ins.debug_location = debug;
        changed = true;
      }
    }
  }

  graph = build_graph(*function, stats);
  std::vector<unsigned char> reachable(function->blocks.size(), 0);
  std::deque<std::size_t> work;
  reachable[0] = 1;
  work.push_back(0);
  while(!work.empty()) {
    const std::size_t block = work.front(); work.pop_front();
    for(std::size_t i = 0; i < graph.successors[block].size(); ++i) {
      const std::size_t next = graph.successors[block][i];
      if(!reachable[next]) { reachable[next] = 1; work.push_back(next); }
    }
  }

  // EH cleanup code can intentionally use an address computed on a source
  // edge which constant folding proves untaken.  The address is still part of
  // the cleanup contract, so rematerialize simple dead-edge definitions at
  // the entry before pruning that edge.
  struct Definition { std::size_t block; Instruction instruction; };
  std::unordered_map<std::string, Definition> definitions;
  std::unordered_map<std::string, std::vector<std::string> > dependencies;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.dest.empty()) continue;
      definitions[ins.dest] = Definition{i, ins};
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(operands[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(ins.args[k].text);
    }
  std::unordered_set<std::string> available;
  for(std::size_t i = 0; i < function->params.size(); ++i)
    available.insert(function->params[i].name);
  const std::size_t entry_end = function->blocks[0].instructions.empty() ? 0 :
    function->blocks[0].instructions.size() - 1;
  for(std::size_t i = 0; i < entry_end; ++i)
    if(!function->blocks[0].instructions[i].dest.empty())
      available.insert(function->blocks[0].instructions[i].dest);
  std::vector<Instruction> rematerialized;
  const auto eligible_definition = [&](const std::string & name) {
    const std::unordered_map<std::string, Definition>::const_iterator found =
      definitions.find(name);
    return found != definitions.end() && !reachable[found->second.block] &&
      is_pure(found->second.instruction.kind);
  };
  const auto rematerialize = [&](const std::string & name) {
    if(available.count(name)) return true;
    if(!eligible_definition(name)) return false;
    struct Frame { std::string name; std::size_t dependency; };
    std::vector<Frame> stack(1, Frame{name, 0});
    std::unordered_set<std::string> active;
    active.insert(name);
    while(!stack.empty()) {
      Frame & frame = stack.back();
      const std::vector<std::string> & required = dependencies[frame.name];
      while(frame.dependency < required.size() &&
            available.count(required[frame.dependency]))
        ++frame.dependency;
      if(frame.dependency < required.size()) {
        const std::string dependency = required[frame.dependency++];
        if(active.count(dependency) || !eligible_definition(dependency))
          return false;
        active.insert(dependency);
        stack.push_back(Frame{dependency, 0});
        continue;
      }
      rematerialized.push_back(definitions.find(frame.name)->second.instruction);
      available.insert(frame.name);
      active.erase(frame.name);
      stack.pop_back();
    }
    return true;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) if(reachable[i])
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP &&
           definitions.count(operands[k]->text) &&
           !reachable[definitions.find(operands[k]->text)->second.block])
          rematerialize(operands[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP &&
           definitions.count(ins.args[k].text) &&
           !reachable[definitions.find(ins.args[k].text)->second.block])
          rematerialize(ins.args[k].text);
    }
  if(!rematerialized.empty()) {
    function->blocks[0].instructions.insert(
      function->blocks[0].instructions.begin() + entry_end,
      rematerialized.begin(), rematerialized.end());
    changed = true;
    if(stats) stats->rewrites += rematerialized.size();
  }

  std::vector<Block> live;
  live.reserve(function->blocks.size());
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    if(reachable[i]) live.push_back(function->blocks[i]);
    else changed = true;
  }
  function->blocks.swap(live);

  graph = build_graph(*function, stats);
  std::vector<unsigned char> block_has_eh(function->blocks.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j)
      block_has_eh[i] = block_has_eh[i] ||
        is_eh_instruction(block.instructions[j].kind);
  }
  std::vector<std::size_t> merge_next(function->blocks.size(), kNoBlock),
    merge_parent(function->blocks.size(), kNoBlock);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    if(block.instructions.empty() ||
       block.instructions.back().kind != Instruction::IK_JUMP) continue;
    const BlockIndex::const_iterator target =
      graph.index.find(block.instructions.back().first.text);
    if(target == graph.index.end() || target->second == i ||
       block_has_eh[i] || block_has_eh[target->second] ||
       graph.eh_targets.count(block.label) ||
       graph.eh_targets.count(block.instructions.back().first.text) ||
       graph.predecessors[target->second].size() != 1) continue;
    merge_next[i] = target->second;
    merge_parent[target->second] = i;
  }
  std::vector<unsigned char> consumed(function->blocks.size(), 0);
  std::vector<Block> merged(function->blocks.size());
  std::size_t merged_edges = 0;
  for(std::size_t head = 0; head < function->blocks.size(); ++head) {
    if(merge_next[head] == kNoBlock || merge_parent[head] != kNoBlock) continue;
    merged[head] = function->blocks[head];
    std::size_t cursor = head;
    while(merge_next[cursor] != kNoBlock) {
      const std::size_t target = merge_next[cursor];
      consumed[target] = 1;
      merged[head].instructions.pop_back();
      merged[head].instructions.insert(merged[head].instructions.end(),
        function->blocks[target].instructions.begin(),
        function->blocks[target].instructions.end());
      cursor = target;
      ++merged_edges;
    }
  }
  if(merged_edges) {
    std::vector<Block> compact;
    compact.reserve(function->blocks.size() - merged_edges);
    for(std::size_t i = 0; i < function->blocks.size(); ++i) {
      if(consumed[i]) continue;
      compact.push_back(merged[i].label.empty() ? function->blocks[i] : merged[i]);
    }
    function->blocks.swap(compact);
    changed = true;
    if(stats) stats->rewrites += merged_edges;
  }
  return changed;
}

void normal_successors(const Function & function, const Graph & graph,
                       std::size_t block, std::vector<std::size_t> * out)
{
  if(function.blocks[block].instructions.empty()) return;
  const Instruction & term = function.blocks[block].instructions.back();
  const Operand * targets[2] = {0, 0};
  if(term.kind == Instruction::IK_JUMP) targets[0] = &term.first;
  else if(term.kind == Instruction::IK_BRANCH) {
    targets[0] = &term.second; targets[1] = &term.third;
  }
  for(std::size_t i = 0; i < 2; ++i)
    if(targets[i] && graph.index.count(targets[i]->text))
      out->push_back(graph.index.find(targets[i]->text)->second);
  if(term.kind == Instruction::IK_SWITCH) {
    if(graph.index.count(term.second.text))
      out->push_back(graph.index.find(term.second.text)->second);
    for(std::size_t i = 1; i < term.args.size(); i += 2)
      if(graph.index.count(term.args[i].text))
        out->push_back(graph.index.find(term.args[i].text)->second);
  }
}

bool exceeds_state_budget(std::size_t blocks, std::size_t facts,
                          std::size_t instructions)
{
  const std::size_t scale = blocks + facts + instructions + 1;
  const std::size_t budget = scale >
      std::numeric_limits<std::size_t>::max() / 16 ?
    std::numeric_limits<std::size_t>::max() : scale * 16;
  return facts != 0 && blocks > budget / facts;
}

bool eliminate_dead_slot_stores(Function * function, Stats * stats)
{
  if(function->slots.empty() || function->blocks.empty()) return false;
  std::unordered_set<std::string> escaped;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          escaped.insert(operands[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT)
          escaped.insert(ins.args[k].text);
    }
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  if(exceeds_state_budget(function->blocks.size(), function->slots.size(),
                          instruction_total)) {
    if(stats) ++stats->budget_skips;
    return false;
  }
  const Graph graph = build_graph(*function, stats);
  typedef std::unordered_set<std::string> LiveSlots;
  std::vector<LiveSlots> live_in(function->blocks.size());
  const auto transfer = [&](std::size_t block) {
    LiveSlots live;
    std::vector<std::size_t> successors;
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      live.insert(live_in[successors[i]].begin(), live_in[successors[i]].end());
    const std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      const Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.index.count(ins.first.text)) {
        const LiveSlots & handler = live_in[graph.index.find(ins.first.text)->second];
        live.insert(handler.begin(), handler.end());
      }
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live.insert(ins.first.text);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped.count(ins.second.text))
        live.erase(ins.second.text);
      else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT) live.insert(operands[i]->text);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT) live.insert(ins.args[i].text);
      }
      if(stats) ++stats->instruction_visits;
    }
    return live;
  };
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 1);
  for(std::size_t reverse = function->blocks.size(); reverse > 0; --reverse)
    work.push_back(reverse - 1);
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    queued[block] = 0;
    LiveSlots live = transfer(block);
    if(live == live_in[block]) continue;
    live_in[block].swap(live);
    if(stats) ++stats->dataflow_updates;
    for(std::size_t i = 0; i < graph.predecessors[block].size(); ++i) {
      const std::size_t predecessor = graph.predecessors[block][i];
      if(queued[predecessor]) continue;
      queued[predecessor] = 1;
      work.push_back(predecessor);
      if(stats) ++stats->worklist_pushes;
    }
  }

  bool changed = false;
  for(std::size_t reverse = function->blocks.size(); reverse > 0; --reverse) {
    const std::size_t block = reverse - 1;
    LiveSlots live;
    std::vector<std::size_t> successors;
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      live.insert(live_in[successors[i]].begin(), live_in[successors[i]].end());
    std::vector<Instruction> kept_reverse;
    const std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      const Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.index.count(ins.first.text)) {
        const LiveSlots & handler = live_in[graph.index.find(ins.first.text)->second];
        live.insert(handler.begin(), handler.end());
      }
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live.insert(ins.first.text);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped.count(ins.second.text)) {
        if(!live.count(ins.second.text)) {
          changed = true;
          if(stats) ++stats->rewrites;
          continue;
        }
        live.erase(ins.second.text);
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT) live.insert(operands[i]->text);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT) live.insert(ins.args[i].text);
      }
      kept_reverse.push_back(ins);
    }
    std::reverse(kept_reverse.begin(), kept_reverse.end());
    function->blocks[block].instructions.swap(kept_reverse);
  }
  return changed;
}

bool remove_dead_slots(Function * function, Stats * stats)
{
  std::unordered_map<std::string, std::size_t> loads;
  std::unordered_set<std::string> escaped;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        ++loads[ins.first.text];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          escaped.insert(values[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT) escaped.insert(ins.args[k].text);
    }
  std::unordered_set<std::string> dead;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(!loads[function->slots[i].first] && !escaped.count(function->slots[i].first))
      dead.insert(function->slots[i].first);
  if(dead.empty()) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> kept;
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if((ins.kind == Instruction::IK_LOAD &&
          dead.count(ins.first.text)) ||
         (ins.kind == Instruction::IK_STORE &&
          dead.count(ins.second.text))) {
        if(stats) ++stats->rewrites;
        continue;
      }
      kept.push_back(ins);
    }
    function->blocks[i].instructions.swap(kept);
  }
  std::vector<std::pair<std::string, LowType> > slots;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(!dead.count(function->slots[i].first)) slots.push_back(function->slots[i]);
  function->slots.swap(slots);
  return true;
}

bool local_slot_forward(Function * function, Stats * stats)
{
  std::unordered_map<std::string, std::unordered_set<std::size_t> > use_blocks;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          use_blocks[operands[k]->text].insert(i);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          use_blocks[ins.args[k].text].insert(i);
    }
  bool changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::unordered_map<std::string, Operand> values;
    std::unordered_map<std::string, Operand> aliases;
    std::vector<Instruction> kept;
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction ins = function->blocks[i].instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP && aliases.count(operands[k]->text))
          *operands[k] = aliases[operands[k]->text];
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP && aliases.count(ins.args[k].text))
          ins.args[k] = aliases[ins.args[k].text];
      if(ins.kind == Instruction::IK_STORE && ins.second.kind == Operand::OP_SLOT) {
        values[ins.second.text] = ins.first;
        kept.push_back(ins);
      } else if(ins.kind == Instruction::IK_LOAD &&
                ins.first.kind == Operand::OP_SLOT && values.count(ins.first.text) &&
                use_blocks[ins.dest].size() <= 1 &&
                (use_blocks[ins.dest].empty() ||
                 use_blocks[ins.dest].count(i))) {
        aliases[ins.dest] = values[ins.first.text];
        changed = true;
        if(stats) ++stats->rewrites;
      } else {
        if(ins.kind == Instruction::IK_CALL || ins.kind == Instruction::IK_COPYOBJ ||
           ins.kind == Instruction::IK_ZEROINIT || is_eh_instruction(ins.kind))
          values.clear();
        kept.push_back(ins);
      }
    }
    function->blocks[i].instructions.swap(kept);
  }
  return changed;
}

bool forward_single_store_slots(Function * function, Stats * stats)
{
  struct SlotFact
  {
    std::size_t stores = 0;
    std::size_t store_block = 0;
    std::size_t store_instruction = 0;
    Operand value;
    bool escaped = false;
    bool dominates_loads = true;
  };
  std::unordered_map<std::string, SlotFact> facts;
  std::unordered_set<std::string> storage_temporaries;
  std::size_t first_exception_edge = kNoBlock;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(function->slots[i].second.kind != lowir_model::LTK_OBJECT)
      facts[function->slots[i].first] = SlotFact();
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[b].instructions[j];
      if(ins.kind == Instruction::IK_STORE &&
         ins.second.kind == Operand::OP_SLOT && facts.count(ins.second.text)) {
        SlotFact & fact = facts[ins.second.text];
        ++fact.stores;
        fact.store_block = b;
        fact.store_instruction = j;
        fact.value = ins.first;
      }
      if(b == 0 && (ins.kind == Instruction::IK_EH_TRY ||
                    ins.kind == Instruction::IK_EH_CLEANUP))
        first_exception_edge = std::min(first_exception_edge, j);
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.first.text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.second.text);
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_SLOT && facts.count(operands[k]->text) &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          facts[operands[k]->text].escaped = true;
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT && facts.count(ins.args[k].text))
          facts[ins.args[k].text].escaped = true;
    }
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[b].instructions[j];
      if(ins.kind != Instruction::IK_LOAD || ins.first.kind != Operand::OP_SLOT)
        continue;
      std::unordered_map<std::string, SlotFact>::iterator fact =
        facts.find(ins.first.text);
      if(fact == facts.end() || fact->second.stores != 1 ||
         fact->second.store_block != 0 || fact->second.escaped)
        continue;
      if((b == 0 && j < fact->second.store_instruction) ||
         (b != 0 && first_exception_edge < fact->second.store_instruction))
        fact->second.dominates_loads = false;
    }
  std::unordered_set<std::string> forwarded;
  for(std::unordered_map<std::string, SlotFact>::const_iterator it = facts.begin();
      it != facts.end(); ++it)
    if(it->second.stores == 1 && it->second.store_block == 0 &&
       !it->second.escaped && it->second.dominates_loads)
      forwarded.insert(it->first);
  if(forwarded.empty()) return false;

  std::unordered_map<std::string, Operand> aliases;
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[b].instructions[j];
      if(ins.kind == Instruction::IK_LOAD && forwarded.count(ins.first.text) &&
         !storage_temporaries.count(ins.dest))
        aliases[ins.dest] = facts[ins.first.text].value;
    }
  const auto resolve_alias = [&](Operand value) {
    std::unordered_set<std::string> seen;
    while(value.kind == Operand::OP_TEMP && aliases.count(value.text) &&
          seen.insert(value.text).second)
      value = aliases.find(value.text)->second;
    return value;
  };
  for(std::size_t b = 0; b < function->blocks.size(); ++b) {
    std::vector<Instruction> kept;
    for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
      Instruction ins = function->blocks[b].instructions[j];
      if(ins.kind == Instruction::IK_LOAD && forwarded.count(ins.first.text) &&
         storage_temporaries.count(ins.dest)) {
        ins.first = resolve_alias(facts[ins.first.text].value);
        ins.kind = ins.first.kind == Operand::OP_INTEGER ||
          ins.first.kind == Operand::OP_FLOAT ?
            Instruction::IK_CONST : Instruction::IK_COPY;
        ins.second = Operand();
        ins.third = Operand();
        kept.push_back(ins);
        if(stats) ++stats->rewrites;
        continue;
      }
      if((ins.kind == Instruction::IK_LOAD && forwarded.count(ins.first.text)) ||
         (ins.kind == Instruction::IK_STORE && forwarded.count(ins.second.text))) {
        if(stats) ++stats->rewrites;
        continue;
      }
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k) *operands[k] = resolve_alias(*operands[k]);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        ins.args[k] = resolve_alias(ins.args[k]);
      kept.push_back(ins);
    }
    function->blocks[b].instructions.swap(kept);
  }
  std::vector<std::pair<std::string, LowType> > slots;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(!forwarded.count(function->slots[i].first))
      slots.push_back(function->slots[i]);
  function->slots.swap(slots);
  return true;
}

struct AbstractState
{
  bool executable = false;
  std::unordered_map<std::string, Operand> values;
};

bool meet_state(AbstractState * target, const AbstractState & incoming)
{
  if(!incoming.executable) return false;
  if(!target->executable) { *target = incoming; return true; }
  bool changed = false;
  for(std::unordered_map<std::string, Operand>::iterator it = target->values.begin();
      it != target->values.end();) {
    const std::unordered_map<std::string, Operand>::const_iterator found =
      incoming.values.find(it->first);
    if(found == incoming.values.end() || !same_operand(it->second, found->second)) {
      it = target->values.erase(it);
      changed = true;
    } else ++it;
  }
  return changed;
}

Operand abstract_resolve(Operand value, const AbstractState & state)
{
  std::unordered_set<std::string> seen;
  while((value.kind == Operand::OP_TEMP || value.kind == Operand::OP_SLOT) &&
        seen.insert(value.text).second) {
    const std::unordered_map<std::string, Operand>::const_iterator found =
      state.values.find(value.text);
    if(found == state.values.end()) break;
    value = found->second;
  }
  return value;
}

void strip_local_facts(AbstractState * state,
                       const std::vector<std::string> & local_temporaries)
{
  if(local_temporaries.empty()) return;
  const std::unordered_set<std::string> locals(
    local_temporaries.begin(), local_temporaries.end());
  for(std::unordered_map<std::string, Operand>::iterator it =
        state->values.begin(); it != state->values.end();) {
    if(locals.count(it->first) ||
       (it->second.kind == Operand::OP_TEMP && locals.count(it->second.text)))
      it = state->values.erase(it);
    else ++it;
  }
}

bool promote_slots(Function * function, Stats * stats)
{
  if(function->blocks.empty() || function->slots.empty()) return false;
  std::unordered_set<std::string> eligible;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(function->slots[i].second.kind != lowir_model::LTK_OBJECT)
      eligible.insert(function->slots[i].first);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          eligible.erase(values[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT) eligible.erase(ins.args[k].text);
  }
  if(eligible.empty()) return false;
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  if(exceeds_state_budget(function->blocks.size(), eligible.size(),
                          instruction_total)) {
    if(stats) ++stats->budget_skips;
    return false;
  }

  std::unordered_set<std::string> storage_temporaries;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.first.text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.second.text);
    }

  const Graph graph = build_graph(*function, stats);
  std::vector<AbstractState> incoming(function->blocks.size());
  incoming[0].executable = true;
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 0);
  work.push_back(0); queued[0] = 1;
  if(stats) ++stats->worklist_pushes;
  std::vector<std::unordered_map<std::string, Operand> > replacements(
    function->blocks.size());
  while(!work.empty()) {
    const std::size_t block_index = work.front(); work.pop_front();
    queued[block_index] = 0;
    AbstractState state = incoming[block_index];
    replacements[block_index].clear();
    const Block & block = function->blocks[block_index];
    std::vector<std::pair<std::size_t, AbstractState> > exceptional;
    std::vector<std::string> local_temporaries;
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      Instruction ins = block.instructions[i];
      const Operand original_first = ins.first;
      const Operand original_second = ins.second;
      ins.first = abstract_resolve(ins.first, state);
      ins.second = abstract_resolve(ins.second, state);
      ins.third = abstract_resolve(ins.third, state);
      for(std::size_t j = 0; j < ins.args.size(); ++j)
        ins.args[j] = abstract_resolve(ins.args[j], state);
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.index.count(ins.first.text)) {
        AbstractState handler = state;
        strip_local_facts(&handler, local_temporaries);
        exceptional.push_back(std::make_pair(
          graph.index.find(ins.first.text)->second, handler));
      }
      if(ins.kind == Instruction::IK_STORE &&
         original_second.kind == Operand::OP_SLOT &&
         eligible.count(original_second.text))
        state.values[original_second.text] = ins.first;
      else if(ins.kind == Instruction::IK_LOAD &&
              original_first.kind == Operand::OP_SLOT &&
              eligible.count(original_first.text)) {
        const std::unordered_map<std::string, Operand>::const_iterator value =
          state.values.find(original_first.text);
        if(value != state.values.end()) {
          state.values[ins.dest] = value->second;
          local_temporaries.push_back(ins.dest);
          replacements[block_index][ins.dest] = value->second;
        }
      } else if(!ins.dest.empty()) {
        Operand folded;
        bool known = ins.kind == Instruction::IK_CONST ? (folded = ins.first, true) :
          ins.kind == Instruction::IK_COPY &&
            ins.dest.compare(0, 5, "%dbg_") != 0 ? (folded = ins.first, true) :
          ins.kind == Instruction::IK_UNARY ? fold_unary(ins, &folded) :
          ins.kind == Instruction::IK_BINARY ? fold_binary(ins, &folded) :
          ins.kind == Instruction::IK_CMP ? fold_compare(ins, &folded) :
          ins.kind == Instruction::IK_CONVERT ? fold_convert(ins, &folded) : false;
        if(known) {
          state.values[ins.dest] = folded;
          local_temporaries.push_back(ins.dest);
        }
        else state.values.erase(ins.dest);
      }
      if(stats) ++stats->instruction_visits;
    }
    std::vector<std::size_t> normal;
    if(!block.instructions.empty()) {
      Instruction term = block.instructions.back();
      term.first = abstract_resolve(term.first, state);
      if(term.kind == Instruction::IK_JUMP && graph.index.count(term.first.text))
        normal.push_back(graph.index.find(term.first.text)->second);
      else if(term.kind == Instruction::IK_BRANCH) {
        term.first = abstract_resolve(term.first, state);
        const Operand & selected = term.first.kind == Operand::OP_INTEGER &&
          term.first.has_int_value ? (term.first.int_value ? term.second : term.third) :
          term.second;
        if(graph.index.count(selected.text))
          normal.push_back(graph.index.find(selected.text)->second);
        if(!(term.first.kind == Operand::OP_INTEGER && term.first.has_int_value) &&
           graph.index.count(term.third.text))
          normal.push_back(graph.index.find(term.third.text)->second);
      } else if(term.kind == Instruction::IK_SWITCH) {
        Operand selected = term.second;
        term.first = abstract_resolve(term.first, state);
        if(term.first.kind == Operand::OP_INTEGER && term.first.has_int_value)
          for(std::size_t i = 0; i + 1 < term.args.size(); i += 2) {
            Operand case_value = abstract_resolve(term.args[i], state);
            if(case_value.kind == Operand::OP_INTEGER && case_value.has_int_value &&
               case_value.int_value == term.first.int_value) selected = term.args[i + 1];
          }
        if(graph.index.count(selected.text))
          normal.push_back(graph.index.find(selected.text)->second);
        if(!(term.first.kind == Operand::OP_INTEGER && term.first.has_int_value))
          for(std::size_t i = 1; i < term.args.size(); i += 2)
            if(graph.index.count(term.args[i].text))
              normal.push_back(graph.index.find(term.args[i].text)->second);
      }
    }
    strip_local_facts(&state, local_temporaries);
    for(std::size_t i = 0; i < exceptional.size(); ++i) {
      if(meet_state(&incoming[exceptional[i].first], exceptional[i].second) &&
         !queued[exceptional[i].first]) {
        work.push_back(exceptional[i].first); queued[exceptional[i].first] = 1;
        if(stats) { ++stats->worklist_pushes; ++stats->dataflow_updates; }
      }
    }
    for(std::size_t i = 0; i < normal.size(); ++i)
      if(meet_state(&incoming[normal[i]], state) && !queued[normal[i]]) {
        work.push_back(normal[i]); queued[normal[i]] = 1;
        if(stats) { ++stats->worklist_pushes; ++stats->dataflow_updates; }
      }
  }

  std::unordered_set<std::string> slots_with_loads;
  std::unordered_set<std::string> slots_with_unresolved_loads;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind != Instruction::IK_LOAD || ins.first.kind != Operand::OP_SLOT ||
         !eligible.count(ins.first.text))
        continue;
      slots_with_loads.insert(ins.first.text);
      if(!replacements[i].count(ins.dest))
        slots_with_unresolved_loads.insert(ins.first.text);
    }
  std::unordered_set<std::string> promoted;
  for(std::unordered_set<std::string>::const_iterator slot = eligible.begin();
      slot != eligible.end(); ++slot)
    if(slots_with_loads.count(*slot) && !slots_with_unresolved_loads.count(*slot))
      promoted.insert(*slot);
  if(promoted.empty()) return false;
  std::unordered_map<std::string, Operand> load_aliases;
  for(std::size_t i = 0; i < replacements.size(); ++i)
    for(std::unordered_map<std::string, Operand>::const_iterator it =
          replacements[i].begin(); it != replacements[i].end(); ++it)
      load_aliases[it->first] = it->second;
  const auto resolve_load_alias = [&load_aliases](Operand value) {
    std::unordered_set<std::string> seen;
    while(value.kind == Operand::OP_TEMP && load_aliases.count(value.text) &&
          seen.insert(value.text).second)
      value = load_aliases.find(value.text)->second;
    return value;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::unordered_map<std::string, Operand> aliases = load_aliases;
    for(std::unordered_set<std::string>::const_iterator it =
          storage_temporaries.begin(); it != storage_temporaries.end(); ++it)
      aliases.erase(*it);
    const auto resolve_alias = [&aliases](Operand value) {
      std::unordered_set<std::string> seen;
      while(value.kind == Operand::OP_TEMP && aliases.count(value.text) &&
            seen.insert(value.text).second)
        value = aliases.find(value.text)->second;
      return value;
    };
    std::vector<Instruction> kept;
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction ins = function->blocks[i].instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        *operands[k] = resolve_alias(*operands[k]);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        ins.args[k] = resolve_alias(ins.args[k]);
      if(ins.kind == Instruction::IK_LOAD && promoted.count(ins.first.text) &&
         storage_temporaries.count(ins.dest)) {
        ins.first = resolve_load_alias(load_aliases.find(ins.dest)->second);
        ins.kind = ins.first.kind == Operand::OP_INTEGER ||
          ins.first.kind == Operand::OP_FLOAT ?
            Instruction::IK_CONST : Instruction::IK_COPY;
        ins.second = Operand();
        ins.third = Operand();
        kept.push_back(ins);
        if(stats) ++stats->rewrites;
        continue;
      }
      if((ins.kind == Instruction::IK_LOAD && promoted.count(ins.first.text)) ||
         (ins.kind == Instruction::IK_STORE && promoted.count(ins.second.text))) {
        if(stats) ++stats->rewrites;
        continue;
      }
      kept.push_back(ins);
    }
    function->blocks[i].instructions.swap(kept);
  }
  std::vector<std::pair<std::string, LowType> > slots;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(!promoted.count(function->slots[i].first)) slots.push_back(function->slots[i]);
  function->slots.swap(slots);
  return true;
}

std::unordered_map<std::string, FunctionBoundaryMetadata>
function_boundaries(const LowirProgram & program)
{
  std::unordered_map<std::string, FunctionBoundaryMetadata> result;
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    result[program.function_declarations[i].name] =
      program.function_declarations[i].boundary;
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    result[program.functions[i].name] = program.functions[i].boundary;
  return result;
}

std::size_t instruction_count(const LowirProgram & program)
{
  std::size_t result = 0;
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    for(std::size_t j = 0; j < program.functions[i].blocks.size(); ++j)
      result += program.functions[i].blocks[j].instructions.size();
  return result;
}

}  // namespace

void optimize(LowirProgram & program, int level, Stats * stats)
{
  if(level < 0 || level > 2) throw std::logic_error("invalid LowIR optimization level");
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  Stats local;
  Stats * observed = stats ? stats : &local;
  *observed = Stats();
  observed->functions = program.functions.size();
  observed->input_instructions = instruction_count(program);
  if(level == 0) {
    observed->output_instructions = observed->input_instructions;
    observed->elapsed_nanoseconds = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
    return;
  }
  const std::unordered_map<std::string, FunctionBoundaryMetadata> boundaries =
    function_boundaries(program);
  std::unordered_set<std::string> inlined_functions;
  observed->rewrites += inline_o1_calls(program, &inlined_functions, observed);
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    Function & function = program.functions[i];
    // This is an explicit bounded pass schedule, not a whole-function
    // fixed-point retry.  The individual propagation and liveness analyses
    // use their own dirty worklists.
    simplify_values(&function, observed);
    eliminate_dead_code(&function, boundaries, observed);
    cleanup_cfg(&function, observed);
    simplify_values(&function, observed);
    eliminate_dead_code(&function, boundaries, observed);
    if(inlined_functions.count(function.name) || level >= 2)
      forward_single_store_slots(&function, observed);
    if(inlined_functions.count(function.name))
      local_slot_forward(&function, observed);
    simplify_values(&function, observed);
    eliminate_dead_code(&function, boundaries, observed);
    cleanup_cfg(&function, observed);
    remove_dead_slots(&function, observed);
    cleanup_cfg(&function, observed);
    eliminate_dead_code(&function, boundaries, observed);
    cleanup_cfg(&function, observed);
    if(level >= 2 && promote_slots(&function, observed)) {
      simplify_values(&function, observed);
      eliminate_dead_code(&function, boundaries, observed);
      cleanup_cfg(&function, observed);
      simplify_values(&function, observed);
      eliminate_dead_code(&function, boundaries, observed);
      remove_dead_slots(&function, observed);
    }
    if(level >= 2 && eliminate_dead_slot_stores(&function, observed)) {
      eliminate_dead_code(&function, boundaries, observed);
      remove_dead_slots(&function, observed);
    }
  }
  observed->output_instructions = instruction_count(program);
  observed->elapsed_nanoseconds = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
}

}  // namespace lowir_opt
