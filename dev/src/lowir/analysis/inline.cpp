#include "lowir/analysis/inline.h"

#include "lowir/analysis/function.h"
#include "lowir/optimize/errors.h"
#include "lowir/optimize/pipeline.h"

#include <algorithm>
#include <deque>
#include <limits>
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

bool partial_prefix_instruction(const Instruction & instruction,
                                unsigned * stop)
{
  switch(instruction.kind) {
  case Instruction::IK_CONST:
  case Instruction::IK_COPY:
  case Instruction::IK_ADDR:
  case Instruction::IK_INDEX:
  case Instruction::IK_UNARY:
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
  case Instruction::IK_CONVERT:
  case Instruction::IK_JUMP:
  case Instruction::IK_BRANCH:
  case Instruction::IK_SWITCH:
  case Instruction::IK_RETURN:
  case Instruction::IK_PHI:
    return true;
  case Instruction::IK_LOAD:
    if(!instruction.volatile_access) return true;
    *stop |= PIPS_OTHER;
    return false;
  case Instruction::IK_CALL:
    *stop |= PIPS_CALL;
    return false;
  case Instruction::IK_STORE:
  case Instruction::IK_ATOMIC_STORE:
  case Instruction::IK_ATOMIC_EXCHANGE:
  case Instruction::IK_ATOMIC_ADD_FETCH:
  case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
  case Instruction::IK_COPYOBJ:
  case Instruction::IK_ZEROINIT:
    *stop |= PIPS_STORE;
    return false;
  case Instruction::IK_EH_TRY:
  case Instruction::IK_EH_CLEANUP:
  case Instruction::IK_EH_CLEANUP_CLAUSE:
  case Instruction::IK_EH_CATCH:
  case Instruction::IK_EH_FILTER:
  case Instruction::IK_EH_CATCH_ALL:
  case Instruction::IK_EH_END:
  case Instruction::IK_THROW:
  case Instruction::IK_EXCEPTION:
  case Instruction::IK_EXCEPTION_SELECTOR:
  case Instruction::IK_RESUME:
    *stop |= PIPS_EH;
    return false;
  case Instruction::IK_UNREACHABLE:
    *stop |= PIPS_OTHER;
    return false;
  case Instruction::IK_ATOMIC_LOAD:
  case Instruction::IK_ATOMIC_THREAD_FENCE:
  case Instruction::IK_ATOMIC_SIGNAL_FENCE:
  case Instruction::IK_VA_START:
  case Instruction::IK_VA_ARG:
  case Instruction::IK_STACK_ALLOC:
    *stop |= PIPS_OTHER;
    return false;
  }
  *stop |= PIPS_OTHER;
  return false;
}

PartialInlinePrefix analyze_partial_prefix_impl(const Function & function)
{
  PartialInlinePrefix result;
  if(function.blocks.empty()) return result;
  lowir_analysis::FunctionAnalysis analysis(function, 0);
  const lowir_analysis::Graph & graph = analysis.graph();
  const lowir_analysis::DominatorTree & dominators =
    analysis.dominator_tree();
  const std::size_t count = function.blocks.size();
  const std::size_t no_block = std::numeric_limits<std::size_t>::max();
  std::vector<unsigned char> safe(count, 1), returns(count, 0), seen(count, 0),
    has_phi(count, 0);
  std::vector<unsigned> block_stops(count, 0);
  std::vector<std::size_t> predecessor(count, no_block);
  for(std::size_t block = 0; block < count; ++block) {
    const std::vector<Instruction> & instructions =
      function.blocks[block].instructions;
    for(std::size_t i = 0; i < instructions.size(); ++i) {
      safe[block] = partial_prefix_instruction(
        instructions[i], &block_stops[block]) ? safe[block] : 0;
      if(instructions[i].kind == Instruction::IK_RETURN)
        returns[block] = 1;
      if(instructions[i].kind == Instruction::IK_PHI) has_phi[block] = 1;
    }
  }
  if(!safe[0]) {
    result.stops |= block_stops[0];
    return result;
  }

  std::deque<std::size_t> work;
  work.push_back(0);
  seen[0] = 1;
  std::size_t terminal = no_block;
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    if(terminal == no_block && returns[block]) terminal = block;
    const lowir_analysis::EdgeList & successors = graph.successors[block];
    for(std::size_t edge = 0; edge < successors.size(); ++edge) {
      const std::size_t successor = successors[edge];
      if(dominators.dominates(successor, block)) {
        result.stops |= PIPS_BACKEDGE;
        continue;
      }
      // Multiple original predecessors are harmless after cloning unless the
      // block has a phi that would need an operand from an uncloned edge.
      if(graph.predecessors[successor].size() > 1 && has_phi[successor]) {
        result.stops |= PIPS_JOIN;
        continue;
      }
      if(!safe[successor]) {
        result.stops |= block_stops[successor];
        continue;
      }
      if(seen[successor]) continue;
      seen[successor] = 1;
      predecessor[successor] = block;
      work.push_back(successor);
    }
  }
  if(terminal == no_block) return result;

  std::vector<unsigned char> path(count, 0);
  for(std::size_t block = terminal; block != no_block;
      block = predecessor[block]) {
    path[block] = 1;
    result.blocks.push_back(block);
    result.instructions += function.blocks[block].instructions.size();
  }
  std::reverse(result.blocks.begin(), result.blocks.end());
  for(std::size_t block = 0; block < count; ++block) {
    if(!path[block]) continue;
    const lowir_analysis::EdgeList & successors = graph.successors[block];
    for(std::size_t edge = 0; edge < successors.size(); ++edge)
      if(!path[successors[edge]]) ++result.bailout_edges;
  }
  result.has_fast_return = true;
  return result;
}

bool constant_partial_actual_impl(const Operand & operand)
{
  return operand.kind == Operand::OP_INTEGER ||
    operand.kind == Operand::OP_FLOAT || operand.kind == Operand::OP_GLOBAL;
}

void record_partial_stops(const PartialInlinePrefix & prefix, Stats * stats)
{
  if(prefix.stops & PIPS_CALL) ++stats->partial_inline_census_call_stops;
  if(prefix.stops & PIPS_STORE) ++stats->partial_inline_census_store_stops;
  if(prefix.stops & PIPS_EH) ++stats->partial_inline_census_eh_stops;
  if(prefix.stops & PIPS_OTHER) ++stats->partial_inline_census_other_stops;
  if(prefix.stops & PIPS_BACKEDGE)
    ++stats->partial_inline_census_backedge_stops;
  if(prefix.stops & PIPS_JOIN) ++stats->partial_inline_census_join_stops;
}

}  // namespace

PartialInlinePrefix analyze_partial_inline_prefix(const Function & function)
{
  return analyze_partial_prefix_impl(function);
}

bool partial_inline_actual_is_constant(const Operand & operand)
{
  return constant_partial_actual_impl(operand);
}

InlineCallGraph analyze_inline_call_graph(
  const LowirProgram & program, Stats * stats)
{
  InlineCallGraph result;
  result.definition_by_symbol.assign(
    program.symbol_names.size(), InlineCallGraph::no_function());
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const std::size_t symbol = program.functions[i].symbol;
    if(symbol >= result.definition_by_symbol.size())
      ThrowOptimizerInternalError("inline function symbol is out of range");
    if(result.definition_by_symbol[symbol] != InlineCallGraph::no_function())
      ThrowOptimizerInternalError(
        "inline call graph has duplicate definitions");
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

void collect_partial_inline_census(
  const LowirProgram & program, const InlineCallGraph & graph, Stats * stats)
{
  if(!stats) return;
  const std::size_t count = program.functions.size();
  std::vector<PartialInlinePrefix> prefixes(count);
  std::vector<unsigned char> prefix_known(count, 0), eligible_callee(count, 0);
  std::vector<std::size_t> caller_counts(count, 0), touched;
  touched.reserve(32);
  const std::size_t no_function = InlineCallGraph::no_function();

  for(std::size_t caller = 0; caller < count; ++caller) {
    if(graph.edge_offsets[caller] == graph.edge_offsets[caller + 1]) continue;
    for(std::size_t edge = graph.edge_offsets[caller];
        edge < graph.edge_offsets[caller + 1]; ++edge) {
      const std::size_t target = graph.edges[edge];
      if(caller_counts[target]++ == 0) touched.push_back(target);
    }
    lowir_analysis::FunctionAnalysis analysis(program.functions[caller], 0);
    const lowir_analysis::LoopForest & loops = analysis.loop_forest();
    const Function & caller_function = program.functions[caller];
    for(std::size_t block = 0; block < caller_function.blocks.size(); ++block)
      for(std::size_t i = 0;
          i < caller_function.blocks[block].instructions.size(); ++i) {
        const Instruction & call =
          caller_function.blocks[block].instructions[i];
        const std::size_t target = direct_callee(
          call, graph.definition_by_symbol);
        if(target == no_function) continue;
        ++stats->partial_inline_census_direct_calls;
        const Function & callee = program.functions[target];
        if(graph.recursive[target] || target == caller) {
          ++stats->partial_inline_census_reject_recursive;
          continue;
        }
        if(callee.metadata.no_inline) {
          ++stats->partial_inline_census_reject_no_inline;
          continue;
        }
        if(callee.params.size() != call.args.size()) {
          ++stats->partial_inline_census_reject_argument_shape;
          continue;
        }
        if(callee.boundary.arity == lowir_model::CAM_VARIADIC) {
          ++stats->partial_inline_census_reject_variadic;
          continue;
        }
        if((!call.call_returns_void &&
            (callee.return_type.kind == lowir_model::LTK_OBJECT ||
             call.type.kind == lowir_model::LTK_OBJECT))) {
          ++stats->partial_inline_census_reject_object_result;
          continue;
        }
        if(!prefix_known[target]) {
          prefixes[target] = analyze_partial_inline_prefix(callee);
          prefix_known[target] = 1;
        }
        const PartialInlinePrefix & prefix = prefixes[target];
        record_partial_stops(prefix, stats);
        if(!prefix.has_fast_return) {
          ++stats->partial_inline_census_reject_no_fast_return;
          continue;
        }
        if(prefix.bailout_edges == 0) {
          ++stats->partial_inline_census_reject_no_bailout;
          continue;
        }
        ++stats->partial_inline_census_eligible_calls;
        if(!eligible_callee[target]) {
          eligible_callee[target] = 1;
          ++stats->partial_inline_census_eligible_callees;
        }
        if(callee.metadata.inline_hint)
          ++stats->partial_inline_census_hint_calls;
        std::size_t constants = 0;
        for(std::size_t arg = 0; arg < call.args.size(); ++arg)
          if(partial_inline_actual_is_constant(call.args[arg])) ++constants;
        if(constants) ++stats->partial_inline_census_constant_calls;
        stats->partial_inline_census_constant_actuals += constants;
        if(block < loops.innermost_loop.size() &&
           loops.innermost_loop[block] != no_function)
          ++stats->partial_inline_census_loop_calls;
        if(caller_counts[target] > 1)
          ++stats->partial_inline_census_repeated_callee_calls;
        stats->partial_inline_census_prefix_blocks += prefix.blocks.size();
        stats->partial_inline_census_prefix_instructions +=
          prefix.instructions;
        stats->partial_inline_census_bailout_edges += prefix.bailout_edges;
        if(prefix.instructions <= 8)
          ++stats->partial_inline_census_prefix_0_8;
        else if(prefix.instructions <= 12)
          ++stats->partial_inline_census_prefix_9_12;
        else if(prefix.instructions <= 16)
          ++stats->partial_inline_census_prefix_13_16;
        else if(prefix.instructions <= 24)
          ++stats->partial_inline_census_prefix_17_24;
        else ++stats->partial_inline_census_prefix_over_24;
      }
    for(std::size_t i = 0; i < touched.size(); ++i)
      caller_counts[touched[i]] = 0;
    touched.clear();
  }
}

}  // namespace lowir_opt
