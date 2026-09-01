#pragma once

#include "lowir/analysis/function.h"
#include "lowir/model/program.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lowir_opt {

struct Stats;

struct MemoryParameterEffect
{
  bool exclusive = false;
  bool unknown_write = false;
  bool has_write = false;
  long long write_begin = 0;
  long long write_end = 0;
};

struct MemoryParameterCapture
{
  bool unknown = false;
  struct Range
  {
    long long begin;
    long long end;
  };
  std::vector<Range> ranges;
};

class MemoryGVNSession
{
public:
  MemoryGVNSession(const lowir_model::Program & program,
                   bool analyze_parameters, Stats * stats);

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
  std::vector<std::size_t> function_by_symbol_;
  std::vector<std::vector<MemoryParameterEffect> > parameter_effects_;
  std::vector<std::vector<MemoryParameterCapture> > parameter_captures_;
  std::vector<std::vector<std::size_t> > parameter_object_bytes_;
  std::uint32_t function_epoch_;
};

// Recompute cheap constant addresses rooted at pointer parameters in the
// call-free block segment which consumes them.  This is a late O3 companion
// to load reuse: preserving a value across a call must not accidentally keep
// several entry-computed addresses live across that call as well.
bool rematerialize_parameter_addresses(
  lowir_model::Function * function, Stats * stats);

}  // namespace lowir_opt
