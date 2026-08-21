#include "lowir_inline_analysis.h"

#include "lowir_opt.h"

#include <algorithm>
#include <limits>
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
  graph->non_call_use.assign(count, 0);
  const auto mark_non_call_use = [graph](const Operand & operand) {
    if(operand.kind != Operand::OP_GLOBAL) return;
    const std::size_t symbol = operand.symbol;
    if(symbol >= graph->definition_by_symbol.size()) return;
    const std::size_t function = graph->definition_by_symbol[symbol];
    if(function != InlineCallGraph::no_function())
      graph->non_call_use[function] = 1;
  };
  for(std::size_t caller = 0; caller < count; ++caller) {
    const Function & function = program.functions[caller];
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0; i < function.blocks[b].instructions.size(); ++i) {
        const Instruction & instruction = function.blocks[b].instructions[i];
        const std::size_t callee = direct_callee(
          instruction, graph->definition_by_symbol);
        if(callee != InlineCallGraph::no_function()) {
          ++out_degree[caller];
          ++in_degree[callee];
        } else mark_non_call_use(instruction.first);
        mark_non_call_use(instruction.second);
        mark_non_call_use(instruction.third);
        for(std::size_t a = 0; a < instruction.args.size(); ++a)
          mark_non_call_use(instruction.args[a]);
      }
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    mark_non_call_use(program.globals[i].init_operand);
    for(std::size_t item = 0;
        item < program.globals[i].data_items.size(); ++item) {
      const lowir_model::GlobalDefinition::DataItem & data =
        program.globals[i].data_items[item];
      if(data.kind != lowir_model::GlobalDefinition::DataItem::ITEM_ADDR)
        continue;
      Operand address;
      address.kind = Operand::OP_GLOBAL;
      address.symbol = data.symbol_id;
      mark_non_call_use(address);
    }
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    Operand target;
    target.kind = Operand::OP_GLOBAL;
    target.symbol = program.object_aliases[i].target_id;
    mark_non_call_use(target);
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

std::size_t retained_use_bucket(std::size_t uses)
{
  if(uses <= 4) return uses - 1;
  if(uses <= 8) return 4;
  if(uses <= 16) return 5;
  return 6;
}

std::size_t retained_size_bucket(std::size_t instructions)
{
  const std::size_t limits[] = {1, 2, 3, 4, 6, 8, 12, 20, 40, 160};
  for(std::size_t i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i)
    if(instructions <= limits[i]) return i;
  return sizeof(limits) / sizeof(limits[0]);
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

void collect_retained_inline_census(
  const LowirProgram & program, Stats * stats)
{
  if(!stats) return;
  const InlineCallGraph graph = analyze_inline_call_graph(program, 0);
  stats->inline_retained_direct_edges = graph.edges.size();
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const std::size_t uses =
      graph.reverse_offsets[i + 1] - graph.reverse_offsets[i];
    if(uses == 0) continue;
    const Function & function = program.functions[i];
    const bool discardable = !graph.non_call_use[i] &&
      (function.metadata.binding == lowir_model::SBM_INTERNAL ||
       function.metadata.binding == lowir_model::SBM_WEAK) &&
      !function.metadata.object_output_root &&
      !function.metadata.keep_internal_alias &&
      function.metadata.role == lowir_model::SR_NONE &&
      !function.metadata.tls_for_symbol_id.valid();
    if(!discardable) continue;

    std::size_t instructions = 0;
    std::size_t returns = 0;
    bool has_eh = false;
    bool has_call = false;
    for(std::size_t b = 0; b < function.blocks.size(); ++b) {
      instructions += function.blocks[b].instructions.size();
      for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
        const Instruction::Kind kind = function.blocks[b].instructions[j].kind;
        if(kind == Instruction::IK_RETURN) ++returns;
        if(kind == Instruction::IK_CALL) has_call = true;
        if(kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_EH_END)
          has_eh = true;
      }
    }
    const bool leaf = function.blocks.size() == 1 && returns == 1 && !has_call;
    ++stats->inline_retained_discardable_definitions;
    stats->inline_retained_discardable_calls += uses;
    stats->inline_retained_discardable_instructions += instructions;
    if(leaf) ++stats->inline_retained_discardable_leaf_definitions;
    if(has_eh) ++stats->inline_retained_discardable_eh_definitions;
    if(graph.recursive[i])
      ++stats->inline_retained_discardable_recursive_definitions;
    if(function.metadata.no_inline)
      ++stats->inline_retained_discardable_no_inline_definitions;

    const std::size_t matrix_index =
      retained_use_bucket(uses) * Stats::kInlineRetainedSizeBucketCount +
      retained_size_bucket(instructions);
    ++stats->inline_retained_discardable_definition_matrix[matrix_index];
    stats->inline_retained_discardable_call_matrix[matrix_index] += uses;
    stats->inline_retained_discardable_instruction_matrix[matrix_index] +=
      instructions;

    if(uses < 2 || !leaf || has_eh || graph.recursive[i] ||
       function.metadata.no_inline ||
       function.boundary.arity == lowir_model::CAM_VARIADIC)
      continue;
    const std::size_t per_call_growth = instructions > 2 ?
      instructions - 2 : 0;
    const std::size_t extra_savings_per_call = instructions < 2 ?
      2 - instructions : 0;
    if(per_call_growth != 0 &&
       uses > std::numeric_limits<std::size_t>::max() / per_call_growth)
      continue;
    const std::size_t growth = uses * per_call_growth;
    std::size_t savings = instructions;
    if(extra_savings_per_call != 0 &&
       uses <= (std::numeric_limits<std::size_t>::max() - savings) /
         extra_savings_per_call)
      savings += uses * extra_savings_per_call;
    if(growth > savings) continue;
    ++stats->inline_retained_nonpositive_leaf_definitions;
    stats->inline_retained_nonpositive_leaf_calls += uses;
    stats->inline_retained_nonpositive_leaf_instructions += instructions;
    stats->inline_retained_nonpositive_leaf_estimated_savings +=
      savings - growth;
  }
}

}  // namespace lowir_opt
