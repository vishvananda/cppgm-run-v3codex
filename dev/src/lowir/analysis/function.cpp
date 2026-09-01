#include "lowir/analysis/function.h"

#include "lowir/optimize/errors.h"
#include "lowir/optimize/pipeline.h"
#include "lowir/optimize/support.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <vector>

namespace lowir_analysis {
namespace {

const std::size_t kNoBlock = static_cast<std::size_t>(-1);
const std::uint64_t kMissingValueDefinition =
  std::numeric_limits<std::uint64_t>::max();
const std::uint64_t kParameterValueDefinition =
  kMissingValueDefinition - 1;

void add_edge(Graph * graph, std::size_t from,
              const lowir_model::Operand & target, lowir_opt::Stats * stats)
{
  if(target.kind != lowir_model::Operand::OP_LABEL) return;
  const std::size_t found = graph->find(target.block);
  if(found == kNoBlock) return;
  graph->successors[from].insert_sorted_unique(found);
  if(stats) ++stats->cfg_edge_visits;
}

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

}  // namespace

ValueIndex::ValueIndex() {}

ValueIndex::ValueIndex(const lowir_model::Function & function,
                       lowir_opt::Stats * stats)
  : definitions_(function.value_names.size(), kMissingValueDefinition),
    uses_(function.value_names.size(), 0)
{
  for(std::size_t parameter = 0;
      parameter < function.params.size(); ++parameter) {
    const std::uint32_t value = function.params[parameter].value;
    if(value < definitions_.size())
      definitions_[value] = kParameterValueDefinition;
  }

  std::size_t instruction_visits = 0;
  std::size_t operand_visits = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const lowir_model::Instruction & instruction =
        function.blocks[block].instructions[index];
      ++instruction_visits;
      if(instruction.dest.valid() && instruction.dest < definitions_.size())
        definitions_[instruction.dest] =
          (static_cast<std::uint64_t>(block) << 32) |
          static_cast<std::uint64_t>(index);
      const std::size_t operand_count =
        lowir_opt::optimizer_support::all_operand_count(instruction);
      for(std::size_t operand_index = 0;
          operand_index < operand_count; ++operand_index) {
        // Phi args serialize as label/value pairs.  Labels are control-flow
        // roles, not value uses, and deliberately stay out of this census.
        if(instruction.kind == lowir_model::Instruction::IK_PHI &&
           operand_index >= 3 && ((operand_index - 3) & 1) == 0)
          continue;
        ++operand_visits;
        const lowir_model::Operand & operand =
          lowir_opt::optimizer_support::all_operand_at(
            instruction, operand_index);
        if(operand.kind == lowir_model::Operand::OP_TEMP &&
           operand.value < uses_.size())
          ++uses_[operand.value];
      }
    }

  if(stats) {
    ++stats->value_index_builds;
    stats->value_index_instruction_visits += instruction_visits;
    stats->value_index_operand_visits += operand_visits;
    if(!definitions_.empty()) stats->value_index_allocations += 2;
    stats->value_index_peak_bytes = std::max(
      stats->value_index_peak_bytes,
      definitions_.capacity() * sizeof(definitions_[0]) +
        uses_.capacity() * sizeof(uses_[0]));
  }
}

std::size_t ValueIndex::size() const { return definitions_.size(); }

ValueDefinition ValueIndex::definition(lowir_model::ValueId value) const
{
  ValueDefinition result;
  result.block = 0;
  result.instruction = 0;
  const std::uint32_t id = value;
  if(id >= definitions_.size() ||
     definitions_[id] == kMissingValueDefinition) {
    result.kind = ValueDefinition::MISSING;
    return result;
  }
  if(definitions_[id] == kParameterValueDefinition) {
    result.kind = ValueDefinition::PARAMETER;
    return result;
  }
  result.kind = ValueDefinition::INSTRUCTION;
  result.block = static_cast<std::uint32_t>(definitions_[id] >> 32);
  result.instruction = static_cast<std::uint32_t>(definitions_[id]);
  return result;
}

std::size_t ValueIndex::use_count(lowir_model::ValueId value) const
{
  const std::uint32_t id = value;
  return id < uses_.size() ? uses_[id] : 0;
}

EdgeList::EdgeList() : first_(0), second_(0), size_(0) {}

std::size_t EdgeList::size() const { return size_; }

std::size_t EdgeList::operator[](std::size_t index) const
{
  if(index == 0) return first_;
  if(index == 1) return second_;
  return overflow_[index - 2];
}

void EdgeList::push_back(std::size_t value)
{
  if(size_ == 0) first_ = value;
  else if(size_ == 1) second_ = value;
  else overflow_.push_back(value);
  ++size_;
}

void EdgeList::insert_sorted_unique(std::size_t value)
{
  std::size_t position = 0;
  while(position < size_ && (*this)[position] < value) ++position;
  if(position < size_ && (*this)[position] == value) return;
  if(size_ == 0) first_ = value;
  else if(size_ == 1) {
    if(position == 0) { second_ = first_; first_ = value; }
    else second_ = value;
  } else if(position == 0) {
    overflow_.insert(overflow_.begin(), second_);
    second_ = first_;
    first_ = value;
  } else if(position == 1) {
    overflow_.insert(overflow_.begin(), second_);
    second_ = value;
  } else {
    overflow_.insert(overflow_.begin() + (position - 2), value);
  }
  ++size_;
}

std::size_t Graph::find(lowir_model::BlockId block) const
{
  const std::uint32_t id = block;
  return id < index.size() ? index[id] : kNoBlock;
}

bool DominatorTree::dominates(std::size_t parent, std::size_t child) const
{
  if(parent == child) return true;
  return parent < preorder.size() && child < preorder.size() &&
    preorder[parent] != 0 && preorder[child] != 0 &&
    preorder[parent] <= preorder[child] &&
    postorder[child] <= postorder[parent];
}

bool NaturalLoop::contains(std::size_t block) const
{
  return std::binary_search(blocks.begin(), blocks.end(), block);
}

Graph build_graph(const lowir_model::Function & function,
                  lowir_opt::Stats * stats)
{
  Graph result;
  result.successors.resize(function.blocks.size());
  result.predecessors.resize(function.blocks.size());
  result.index.assign(function.next_block_id, kNoBlock);
  result.eh_targets.assign(function.next_block_id, 0);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::uint32_t id = function.blocks[i].id;
    if(id >= result.index.size())
      lowir_opt::ThrowOptimizerInternalError(
        "invalid LowIR block identity in CFG");
    result.index[id] = i;
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const lowir_model::Block & block = function.blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const lowir_model::Instruction & ins = block.instructions[j];
      if(ins.kind == lowir_model::Instruction::IK_EH_TRY ||
         ins.kind == lowir_model::Instruction::IK_EH_CLEANUP) {
        add_edge(&result, i, ins.first, stats);
        result.eh_targets[static_cast<std::uint32_t>(ins.first.block)] = 1;
      }
    }
    if(block.instructions.empty()) continue;
    const lowir_model::Instruction & term = block.instructions.back();
    if(term.kind == lowir_model::Instruction::IK_JUMP)
      add_edge(&result, i, term.first, stats);
    else if(term.kind == lowir_model::Instruction::IK_BRANCH) {
      add_edge(&result, i, term.second, stats);
      add_edge(&result, i, term.third, stats);
    } else if(term.kind == lowir_model::Instruction::IK_SWITCH) {
      add_edge(&result, i, term.second, stats);
      for(std::size_t j = 1; j < term.args.size(); j += 2)
        add_edge(&result, i, term.args[j], stats);
    }
  }
  for(std::size_t i = 0; i < result.successors.size(); ++i)
    for(std::size_t j = 0; j < result.successors[i].size(); ++j)
      result.predecessors[result.successors[i][j]].push_back(i);
  return result;
}

DominatorTree dominators(const Graph & graph, lowir_opt::Stats * stats)
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

std::vector<EdgeList> build_dominator_children(const DominatorTree & dom)
{
  std::vector<EdgeList> children(dom.immediate.size());
  const std::size_t count = dom.immediate.size();
  for(std::size_t block = 1; block < count; ++block)
    if(dom.immediate[block] != kNoBlock && dom.immediate[block] != block)
      children[dom.immediate[block]].push_back(block);
  return children;
}

std::vector<EdgeList> dominance_frontiers(const Graph & graph,
                                          const DominatorTree & dom)
{
  const std::size_t count = graph.successors.size();
  std::vector<EdgeList> result(count);
  const std::vector<EdgeList> children = build_dominator_children(dom);
  struct Frame { std::size_t block; std::size_t child; };
  std::vector<Frame> stack;
  std::vector<std::size_t> postorder;
  if(count && dom.preorder[0]) stack.push_back(Frame{0, 0});
  while(!stack.empty()) {
    Frame & frame = stack.back();
    if(frame.child < children[frame.block].size()) {
      const std::size_t child = children[frame.block][frame.child++];
      stack.push_back(Frame{child, 0});
    } else {
      postorder.push_back(frame.block);
      stack.pop_back();
    }
  }
  for(std::size_t order = 0; order < postorder.size(); ++order) {
    const std::size_t block = postorder[order];
    for(std::size_t edge = 0; edge < graph.successors[block].size(); ++edge) {
      const std::size_t successor = graph.successors[block][edge];
      if(dom.immediate[successor] != block)
        result[block].insert_sorted_unique(successor);
    }
    for(std::size_t child = 0; child < children[block].size(); ++child) {
      const EdgeList & child_frontier = result[children[block][child]];
      for(std::size_t edge = 0; edge < child_frontier.size(); ++edge) {
        const std::size_t frontier = child_frontier[edge];
        if(dom.immediate[frontier] != block)
          result[block].insert_sorted_unique(frontier);
      }
    }
  }
  return result;
}

LoopForest discover_loops(const lowir_model::Function & function,
                          const Graph & graph, const DominatorTree & dom,
                          lowir_opt::Stats * stats)
{
  LoopForest result;
  const std::size_t count = function.blocks.size();
  result.innermost_loop.assign(count, kNoBlock);
  result.backedges = 0;
  std::vector<std::size_t> header_loop(count, kNoBlock);
  for(std::size_t source = 0; source < count; ++source)
    for(std::size_t edge = 0; edge < graph.successors[source].size(); ++edge) {
      const std::size_t header = graph.successors[source][edge];
      if(!dom.dominates(header, source)) continue;
      ++result.backedges;
      if(header_loop[header] == kNoBlock) {
        header_loop[header] = result.loops.size();
        NaturalLoop loop;
        loop.header = header;
        loop.preheader = kNoBlock;
        loop.parent = kNoBlock;
        loop.has_eh = false;
        result.loops.push_back(loop);
      }
      result.loops[header_loop[header]].latches.push_back(source);
    }

  std::vector<std::uint32_t> membership(count, 0);
  std::vector<std::uint32_t> exit_membership(count, 0);
  std::uint32_t epoch = 0;
  std::vector<std::size_t> work;
  for(std::size_t loop_index = 0;
      loop_index < result.loops.size(); ++loop_index) {
    NaturalLoop & loop = result.loops[loop_index];
    ++epoch;
    if(epoch == 0) {
      std::fill(membership.begin(), membership.end(), 0);
      epoch = 1;
    }
    work.clear();
    membership[loop.header] = epoch;
    loop.blocks.push_back(loop.header);
    for(std::size_t i = 0; i < loop.latches.size(); ++i)
      if(membership[loop.latches[i]] != epoch) {
        membership[loop.latches[i]] = epoch;
        loop.blocks.push_back(loop.latches[i]);
        work.push_back(loop.latches[i]);
      }
    while(!work.empty()) {
      const std::size_t block = work.back();
      work.pop_back();
      for(std::size_t edge = 0;
          edge < graph.predecessors[block].size(); ++edge) {
        const std::size_t predecessor = graph.predecessors[block][edge];
        if(membership[predecessor] == epoch ||
           !dom.dominates(loop.header, predecessor)) continue;
        membership[predecessor] = epoch;
        loop.blocks.push_back(predecessor);
        work.push_back(predecessor);
      }
    }
    std::sort(loop.blocks.begin(), loop.blocks.end());
    for(std::size_t i = 0; i < loop.blocks.size(); ++i) {
      const std::size_t block = loop.blocks[i];
      const std::uint32_t block_id = function.blocks[block].id;
      loop.has_eh = loop.has_eh ||
        (block_id < graph.eh_targets.size() && graph.eh_targets[block_id]);
      for(std::size_t instruction = 0;
          instruction < function.blocks[block].instructions.size();
          ++instruction) {
        const lowir_model::Instruction::Kind kind =
          function.blocks[block].instructions[instruction].kind;
        loop.has_eh = loop.has_eh ||
          (kind >= lowir_model::Instruction::IK_EH_TRY &&
           kind <= lowir_model::Instruction::IK_RESUME);
      }
      for(std::size_t edge = 0;
          edge < graph.successors[block].size(); ++edge) {
        const std::size_t successor = graph.successors[block][edge];
        if(!loop.contains(successor) && exit_membership[successor] != epoch) {
          exit_membership[successor] = epoch;
          loop.exits.push_back(successor);
        }
      }
    }
    std::sort(loop.exits.begin(), loop.exits.end());
    std::size_t outside_predecessor = kNoBlock;
    bool multiple_outside = false;
    for(std::size_t edge = 0;
        edge < graph.predecessors[loop.header].size(); ++edge) {
      const std::size_t predecessor = graph.predecessors[loop.header][edge];
      if(loop.contains(predecessor)) continue;
      if(outside_predecessor != kNoBlock) multiple_outside = true;
      outside_predecessor = predecessor;
    }
    if(!multiple_outside && outside_predecessor != kNoBlock &&
       graph.successors[outside_predecessor].size() == 1)
      loop.preheader = outside_predecessor;
  }

  std::vector<std::size_t> by_size(result.loops.size());
  for(std::size_t i = 0; i < by_size.size(); ++i) by_size[i] = i;
  std::stable_sort(by_size.begin(), by_size.end(),
    [&result](std::size_t left, std::size_t right) {
      return result.loops[left].blocks.size() <
        result.loops[right].blocks.size();
    });
  // Install outer loops first.  The current owner of a loop header is then
  // its immediate containing loop, and overwriting each member records the
  // innermost owner.  This makes nesting proportional to loop membership
  // instead of comparing every pair of loops.
  for(std::size_t order = by_size.size(); order > 0; --order) {
    const std::size_t child = by_size[order - 1];
    result.loops[child].parent =
      result.innermost_loop[result.loops[child].header];
    for(std::size_t block = 0;
        block < result.loops[child].blocks.size(); ++block)
      result.innermost_loop[result.loops[child].blocks[block]] = child;
  }
  if(stats) {
    stats->loop_backedges += result.backedges;
    stats->loops_discovered += result.loops.size();
    for(std::size_t i = 0; i < result.loops.size(); ++i) {
      stats->loop_block_memberships += result.loops[i].blocks.size();
      stats->loop_exits += result.loops[i].exits.size();
    }
  }
  return result;
}

FunctionAnalysis::FunctionAnalysis(const lowir_model::Function & function,
                                   lowir_opt::Stats * stats)
  : function_(function), stats_(stats), epoch_(1), graph_valid_(false),
    dominators_valid_(false), dominator_children_valid_(false),
    frontiers_valid_(false), loops_valid_(false), value_index_valid_(false)
{}

const Graph & FunctionAnalysis::graph()
{
  if(graph_valid_) {
    if(stats_) ++stats_->cfg_analysis_reuses;
    return graph_;
  }
  graph_ = build_graph(function_, stats_);
  graph_valid_ = true;
  if(stats_) ++stats_->cfg_analysis_builds;
  return graph_;
}

const DominatorTree & FunctionAnalysis::dominator_tree()
{
  if(dominators_valid_) {
    if(stats_) ++stats_->dominator_analysis_reuses;
    return dominators_;
  }
  dominators_ = dominators(graph(), stats_);
  dominators_valid_ = true;
  if(stats_) ++stats_->dominator_analysis_builds;
  return dominators_;
}

const std::vector<EdgeList> & FunctionAnalysis::dominator_children()
{
  if(dominator_children_valid_) return dominator_children_;
  dominator_children_ = build_dominator_children(dominator_tree());
  dominator_children_valid_ = true;
  return dominator_children_;
}

const std::vector<EdgeList> & FunctionAnalysis::dominance_frontier()
{
  if(frontiers_valid_) return frontiers_;
  frontiers_ = dominance_frontiers(graph(), dominator_tree());
  frontiers_valid_ = true;
  return frontiers_;
}

const LoopForest & FunctionAnalysis::loop_forest()
{
  if(loops_valid_) {
    if(stats_) ++stats_->loop_analysis_reuses;
    return loops_;
  }
  const std::chrono::steady_clock::time_point started =
    stats_ ? std::chrono::steady_clock::now() :
             std::chrono::steady_clock::time_point();
  loops_ = discover_loops(function_, graph(), dominator_tree(), stats_);
  loops_valid_ = true;
  if(stats_) {
    ++stats_->loop_analysis_builds;
    stats_->loop_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }
  return loops_;
}

const ValueIndex & FunctionAnalysis::value_index()
{
  if(value_index_valid_) {
    if(stats_) ++stats_->value_index_reuses;
    return value_index_;
  }
  value_index_ = ValueIndex(function_, stats_);
  value_index_valid_ = true;
  return value_index_;
}

void FunctionAnalysis::invalidate_values()
{
  if(!value_index_valid_) return;
  value_index_ = ValueIndex();
  value_index_valid_ = false;
  if(stats_) ++stats_->value_index_invalidations;
}

void FunctionAnalysis::invalidate_cfg()
{
  graph_valid_ = false;
  dominators_valid_ = false;
  dominator_children_valid_ = false;
  frontiers_valid_ = false;
  loops_valid_ = false;
  ++epoch_;
  if(stats_) ++stats_->cfg_analysis_invalidations;
}

std::size_t FunctionAnalysis::epoch() const { return epoch_; }

}  // namespace lowir_analysis
