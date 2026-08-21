#include "lowir_inline_analysis.h"

#include "lowir_opt.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowirProgram;
using lowir_model::Operand;

std::size_t direct_callee(
  const Instruction & instruction,
  const std::vector<std::size_t> & definition_by_symbol)
{
  if(instruction.kind != Instruction::IK_CALL ||
     instruction.first.kind != Operand::OP_GLOBAL)
    return InlineCallGraph::no_function();
  const std::size_t symbol = instruction.first.symbol;
  return symbol < definition_by_symbol.size() ?
    definition_by_symbol[symbol] : InlineCallGraph::no_function();
}

void build_edges(const LowirProgram & program, InlineCallGraph * graph)
{
  const std::size_t count = program.functions.size();
  std::vector<std::size_t> out_degree(count, 0), in_degree(count, 0);
  for(std::size_t caller = 0; caller < count; ++caller) {
    const Function & function = program.functions[caller];
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
        const std::size_t callee = direct_callee(
          function.blocks[b].instructions[i], graph->definition_by_symbol);
        if(callee == InlineCallGraph::no_function()) continue;
        ++out_degree[caller];
        ++in_degree[callee];
      }
  }

  graph->edge_offsets.assign(count + 1, 0);
  graph->reverse_offsets.assign(count + 1, 0);
  for(std::size_t i = 0; i < count; ++i) {
    graph->edge_offsets[i + 1] = graph->edge_offsets[i] + out_degree[i];
    graph->reverse_offsets[i + 1] =
      graph->reverse_offsets[i] + in_degree[i];
  }
  graph->edges.resize(graph->edge_offsets.back());
  graph->reverse_edges.resize(graph->reverse_offsets.back());
  std::vector<std::size_t> out_cursor = graph->edge_offsets;
  std::vector<std::size_t> in_cursor = graph->reverse_offsets;
  for(std::size_t caller = 0; caller < count; ++caller) {
    const Function & function = program.functions[caller];
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
        const std::size_t callee = direct_callee(
          function.blocks[b].instructions[i], graph->definition_by_symbol);
        if(callee == InlineCallGraph::no_function()) continue;
        graph->edges[out_cursor[caller]++] = callee;
        graph->reverse_edges[in_cursor[callee]++] = caller;
      }
  }
}

void finish_order_and_components(InlineCallGraph * graph)
{
  const std::size_t count = graph->edge_offsets.size() - 1;
  struct Frame { std::size_t node; std::size_t edge; };
  std::vector<unsigned char> seen(count, 0);
  graph->callee_first_order.clear();
  graph->callee_first_order.reserve(count);
  for(std::size_t root = 0; root < count; ++root) {
    if(seen[root]) continue;
    std::vector<Frame> stack;
    seen[root] = 1;
    stack.push_back(Frame{root, graph->edge_offsets[root]});
    while(!stack.empty()) {
      Frame & frame = stack.back();
      if(frame.edge < graph->edge_offsets[frame.node + 1]) {
        const std::size_t next = graph->edges[frame.edge++];
        if(!seen[next]) {
          seen[next] = 1;
          stack.push_back(Frame{next, graph->edge_offsets[next]});
        }
      } else {
        graph->callee_first_order.push_back(frame.node);
        stack.pop_back();
      }
    }
  }

  graph->component.assign(count, InlineCallGraph::no_function());
  graph->recursive.assign(count, 0);
  std::fill(seen.begin(), seen.end(), 0);
  for(std::size_t cursor = graph->callee_first_order.size(); cursor > 0;
      --cursor) {
    const std::size_t root = graph->callee_first_order[cursor - 1];
    if(seen[root]) continue;
    const std::size_t component_id = graph->component_count++;
    std::vector<std::size_t> members;
    std::vector<std::size_t> stack(1, root);
    seen[root] = 1;
    while(!stack.empty()) {
      const std::size_t node = stack.back();
      stack.pop_back();
      graph->component[node] = component_id;
      members.push_back(node);
      for(std::size_t edge = graph->reverse_offsets[node];
          edge < graph->reverse_offsets[node + 1]; ++edge) {
        const std::size_t next = graph->reverse_edges[edge];
        if(!seen[next]) {
          seen[next] = 1;
          stack.push_back(next);
        }
      }
    }
    bool recursive = members.size() > 1;
    if(!recursive) {
      const std::size_t node = members[0];
      recursive = std::find(
        graph->edges.begin() + graph->edge_offsets[node],
        graph->edges.begin() + graph->edge_offsets[node + 1], node) !=
        graph->edges.begin() + graph->edge_offsets[node + 1];
    }
    if(recursive)
      for(std::size_t i = 0; i < members.size(); ++i)
        graph->recursive[members[i]] = 1;
  }
}

}  // namespace

InlineCallGraph analyze_inline_call_graph(
  const LowirProgram & program, Stats * stats)
{
  InlineCallGraph result;
  result.definition_by_symbol.assign(
    program.symbol_names.size(), InlineCallGraph::no_function());
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const std::size_t symbol = program.functions[i].symbol;
    if(symbol >= result.definition_by_symbol.size())
      throw std::logic_error("inline function symbol is out of range");
    if(result.definition_by_symbol[symbol] != InlineCallGraph::no_function())
      throw std::logic_error("inline call graph has duplicate definitions");
    result.definition_by_symbol[symbol] = i;
  }
  build_edges(program, &result);
  finish_order_and_components(&result);
  if(stats) {
    stats->inline_direct_edges = result.edges.size();
    stats->inline_sccs = result.component_count;
    stats->inline_recursive_functions =
      std::count(result.recursive.begin(), result.recursive.end(), 1);
  }
  return result;
}

}  // namespace lowir_opt
