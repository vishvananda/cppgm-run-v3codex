#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lowir_model.h"
#include "x86_register_model.h"

namespace lowir_native {
namespace analysis {

struct FunctionFacts
{
  std::unordered_map<std::string, std::size_t> uses;
  std::unordered_map<std::string, std::size_t> first_use;
  std::unordered_map<std::string, std::size_t> last_use;
  std::unordered_map<std::string, std::size_t> definition;
  std::vector<std::size_t> calls;
  std::unordered_set<std::string> live_across_call;
  std::unordered_set<std::string> edge_live;
  std::unordered_map<std::string, unsigned> live_across_clobbers;
  bool has_va_start = false;
};

unsigned register_mask(X64Register reg);
FunctionFacts analyze_function(const lowir_model::LowirFunction & function);

}  // namespace analysis
}  // namespace lowir_native
