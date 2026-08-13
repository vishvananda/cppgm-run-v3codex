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
  std::unordered_set<std::string> parameters;
  std::vector<std::size_t> calls;
  std::unordered_set<std::string> live_across_call;
  std::unordered_set<std::string> edge_live;
  std::unordered_set<std::string> only_call_arguments;
  std::unordered_set<std::string> direct_branch_sources;
  std::unordered_set<std::string> direct_compare_storage_values;
  std::unordered_set<std::string> direct_compare_rax_values;
  std::unordered_set<std::string> direct_branch_call_results;
  std::unordered_set<std::string> zero_index_parameters;
  std::unordered_set<std::string> forwarded_parameters_across_call;
  std::unordered_set<std::string> switch_parameters;
  std::unordered_set<std::string> destructive_parameters;
  std::unordered_map<std::string, const lowir_model::Instruction *>
    deferred_branch_comparisons;
  std::unordered_map<std::string, unsigned> live_across_clobbers;
  bool has_va_start = false;
  bool has_dynamic_stack = false;
  bool has_i128_atomic = false;
  bool has_direct_branch_parameter = false;
};

struct StorageFacts
{
  std::unordered_map<std::string, std::string> parameter_slot_aliases;
  std::unordered_map<std::string, std::string> promoted_parameter_slots;
  std::unordered_map<std::string, std::string> forwarded_parameter_slots;
  std::unordered_set<std::string> promoted_parameters;
  std::unordered_set<std::string> promoted_parameters_across_call;
  std::unordered_set<std::string> dead_slot_only_parameters;
  std::unordered_set<std::string> dead_store_slots;
  std::unordered_set<std::string> tls_store_inputs;
};

unsigned register_mask(X64Register reg);
FunctionFacts analyze_function(const lowir_model::LowirFunction & function);
StorageFacts analyze_storage(
    const lowir_model::LowirFunction & function,
    const FunctionFacts & function_facts,
    const std::unordered_map<std::string, std::string> & tls_wrappers);

}  // namespace analysis
}  // namespace lowir_native
