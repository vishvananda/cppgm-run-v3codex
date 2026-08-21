#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {
struct Stats;
}

namespace lowir_analysis {

class EdgeList
{
public:
  EdgeList();

  std::size_t size() const;
  std::size_t operator[](std::size_t index) const;
  void push_back(std::size_t value);
  void insert_sorted_unique(std::size_t value);

private:
  std::size_t first_;
  std::size_t second_;
  std::size_t size_;
  std::vector<std::size_t> overflow_;
};

struct Graph
{
  std::vector<std::size_t> index;
  std::vector<EdgeList> successors;
  std::vector<EdgeList> predecessors;
  std::vector<unsigned char> eh_targets;

  std::size_t find(lowir_model::BlockId block) const;
};

struct DominatorTree
{
  std::vector<std::size_t> immediate;
  std::vector<std::size_t> preorder;
  std::vector<std::size_t> postorder;

  bool dominates(std::size_t parent, std::size_t child) const;
};

Graph build_graph(const lowir_model::Function & function,
                  lowir_opt::Stats * stats);
DominatorTree dominators(const Graph & graph, lowir_opt::Stats * stats);
std::vector<EdgeList> dominance_frontiers(const Graph & graph,
                                          const DominatorTree & dom);

class FunctionAnalysis
{
public:
  FunctionAnalysis(const lowir_model::Function & function,
                   lowir_opt::Stats * stats);

  const Graph & graph();
  const DominatorTree & dominator_tree();
  const std::vector<EdgeList> & dominance_frontier();
  void invalidate_cfg();
  std::size_t epoch() const;

private:
  const lowir_model::Function & function_;
  lowir_opt::Stats * stats_;
  Graph graph_;
  DominatorTree dominators_;
  std::vector<EdgeList> frontiers_;
  std::size_t epoch_;
  bool graph_valid_;
  bool dominators_valid_;
  bool frontiers_valid_;
};

}  // namespace lowir_analysis
