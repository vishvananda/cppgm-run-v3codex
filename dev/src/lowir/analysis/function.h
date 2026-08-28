#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <cstdint>
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

struct NaturalLoop
{
  std::size_t header;
  std::size_t preheader;
  std::size_t parent;
  std::vector<std::size_t> blocks;
  std::vector<std::size_t> latches;
  std::vector<std::size_t> exits;
  bool has_eh;

  bool contains(std::size_t block) const;
};

struct LoopForest
{
  std::vector<NaturalLoop> loops;
  std::vector<std::size_t> innermost_loop;
  std::size_t backedges;
};

struct ValueDefinition
{
  enum Kind
  {
    MISSING,
    PARAMETER,
    INSTRUCTION
  };

  Kind kind;
  std::size_t block;
  std::size_t instruction;
};

// Definition locations and use counts for one immutable function shape.
// The index needs no CFG and distinguishes parameters from missing SSA
// definitions.  Instruction-content rewrites may retain it only while value,
// block, instruction, and operand-use topology remain unchanged.
class ValueIndex
{
public:
  ValueIndex();
  ValueIndex(const lowir_model::Function & function,
             lowir_opt::Stats * stats);

  std::size_t size() const;
  ValueDefinition definition(lowir_model::ValueId value) const;
  std::size_t use_count(lowir_model::ValueId value) const;

private:
  std::vector<std::uint64_t> definitions_;
  std::vector<std::size_t> uses_;
};

Graph build_graph(const lowir_model::Function & function,
                  lowir_opt::Stats * stats);
DominatorTree dominators(const Graph & graph, lowir_opt::Stats * stats);
std::vector<EdgeList> build_dominator_children(const DominatorTree & dom);
std::vector<EdgeList> dominance_frontiers(const Graph & graph,
                                          const DominatorTree & dom);
LoopForest discover_loops(const lowir_model::Function & function,
                          const Graph & graph, const DominatorTree & dom,
                          lowir_opt::Stats * stats);

class FunctionAnalysis
{
public:
  FunctionAnalysis(const lowir_model::Function & function,
                   lowir_opt::Stats * stats);

  const Graph & graph();
  const DominatorTree & dominator_tree();
  const std::vector<EdgeList> & dominator_children();
  const std::vector<EdgeList> & dominance_frontier();
  const LoopForest & loop_forest();
  const ValueIndex & value_index();
  void invalidate_values();
  void invalidate_cfg();
  std::size_t epoch() const;

private:
  const lowir_model::Function & function_;
  lowir_opt::Stats * stats_;
  Graph graph_;
  DominatorTree dominators_;
  std::vector<EdgeList> dominator_children_;
  std::vector<EdgeList> frontiers_;
  LoopForest loops_;
  ValueIndex value_index_;
  std::size_t epoch_;
  bool graph_valid_;
  bool dominators_valid_;
  bool dominator_children_valid_;
  bool frontiers_valid_;
  bool loops_valid_;
  bool value_index_valid_;
};

}  // namespace lowir_analysis
