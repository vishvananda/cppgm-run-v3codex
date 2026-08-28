#pragma once

#include "lowir/analysis/lowir_function_analysis.h"
#include "lowir/model/lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lowir_opt {

struct Stats;

class MemoryGVNSession
{
public:
  explicit MemoryGVNSession(const lowir_model::Program & program);

  bool eliminate_redundant_loads(
    lowir_model::Function * function,
    lowir_analysis::FunctionAnalysis * analysis, Stats * stats,
    bool preserve_value_lifetimes = false);

private:
  lowir_model::FunctionBoundaryMetadata call_boundary(
    const lowir_model::Instruction & instruction) const;
  void begin_function();
  void touch_symbol(lowir_model::SymbolId symbol);

  std::vector<lowir_model::FunctionBoundaryMetadata> boundaries_;
  std::vector<unsigned char> known_boundaries_;
  std::vector<lowir_model::GlobalStorageMode> global_storage_;
  std::vector<std::uint32_t> symbol_epochs_;
  std::vector<std::size_t> symbol_classes_;
  std::uint32_t function_epoch_;
};

}  // namespace lowir_opt
