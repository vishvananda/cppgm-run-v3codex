#include "lowir_function_analysis.h"

#include "lowir_opt.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace lowir_analysis {
namespace {

const std::size_t kNoBlock = static_cast<std::size_t>(-1);

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
      throw std::logic_error("invalid LowIR block identity in CFG");
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

std::vector<EdgeList> dominance_frontiers(const Graph & graph,
                                          const DominatorTree & dom)
{
  const std::size_t count = graph.successors.size();
  std::vector<EdgeList> result(count), children(count);
  for(std::size_t block = 1; block < count; ++block)
    if(dom.immediate[block] != kNoBlock && dom.immediate[block] != block)
      children[dom.immediate[block]].push_back(block);
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

FunctionAnalysis::FunctionAnalysis(const lowir_model::Function & function,
                                   lowir_opt::Stats * stats)
  : function_(function), stats_(stats), epoch_(1), graph_valid_(false),
    dominators_valid_(false), frontiers_valid_(false)
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

const std::vector<EdgeList> & FunctionAnalysis::dominance_frontier()
{
  if(frontiers_valid_) return frontiers_;
  frontiers_ = dominance_frontiers(graph(), dominator_tree());
  frontiers_valid_ = true;
  return frontiers_;
}

void FunctionAnalysis::invalidate_cfg()
{
  graph_valid_ = false;
  dominators_valid_ = false;
  frontiers_valid_ = false;
  ++epoch_;
  if(stats_) ++stats_->cfg_analysis_invalidations;
}

std::size_t FunctionAnalysis::epoch() const { return epoch_; }

}  // namespace lowir_analysis
