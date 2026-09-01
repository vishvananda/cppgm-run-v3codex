#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "lowir/model/program.h"
#include "native/mir/registers.h"

namespace lowir_native {
struct Stats;
namespace analysis {

struct FunctionFacts
{
  enum ValueFlag
  {
    VF_PARAMETER = 1u << 0,
    VF_LIVE_ACROSS_CALL = 1u << 1,
    VF_EDGE_LIVE = 1u << 2,
    VF_LOOP_INVARIANT = 1u << 3,
    VF_ONLY_CALL_ARGUMENT = 1u << 4,
    VF_DIRECT_BRANCH_SOURCE = 1u << 5,
    VF_DIRECT_COMPARE_STORAGE = 1u << 6,
    VF_DIRECT_COMPARE_RAX = 1u << 7,
    VF_DIRECT_BRANCH_CALL_RESULT = 1u << 8,
    VF_DIRECT_MEMORY_INDEX = 1u << 9,
    VF_SOLE_INDEX_BASE = 1u << 10,
    VF_ZERO_INDEX_PARAMETER = 1u << 11,
    VF_FORWARDED_PARAMETER_ACROSS_CALL = 1u << 12,
    VF_SWITCH_PARAMETER = 1u << 13,
    VF_DESTRUCTIVE_PARAMETER = 1u << 14,
    // A full-width scalar call result remains in rax until its first use in
    // the defining block. This permits an early GPR call argument to read the
    // ABI carrier even when a separate selected home preserves later uses.
    VF_CALL_RESULT_RAX_FIRST_USE = 1u << 15,
    // A compiler-created value may remain in its selected register across one
    // presentation-adjacent CFG edge when the source has exactly that
    // successor and the destination has exactly that predecessor.
    VF_EXACT_FORWARD_EDGE = 1u << 16,
    // Every use treats the pointer as an address for a load, store, index, or
    // bulk-memory operation. Selection may therefore retain an encodable
    // base/index/displacement instead of materializing a pointer value.
    VF_ONLY_STORAGE_ADDRESS = 1u << 17,
    // Every use is either a storage-address use or a call argument — the
    // union of shapes that consume a deferred address without needing the
    // pointer materialized as a register or frame value (argument staging
    // emits the lea itself).  Wider than VF_ONLY_STORAGE_ADDRESS, which
    // planner admission also keys on and therefore keeps its meaning.
    VF_ADDRESS_UNION_SAFE = 1u << 18,
    // The value is produced by taking the address of a fixed frame slot.
    VF_SLOT_ADDRESS = 1u << 19,
    // The value is a fixed-slot/global address and every use can consume its
    // rematerialized form: storage and call uses plus safe copy chains,
    // pointer-value stores, and returns.
    VF_ADDRESS_REMATERIALIZE_SAFE = 1u << 20,
    // The value is produced by taking the address of a global object.
    VF_GLOBAL_ADDRESS = 1u << 21
  };

  std::vector<std::size_t> uses;
  std::vector<std::size_t> first_use;
  std::vector<std::size_t> last_use;
  // A scalar copy may retain its source's immutable temporary frame home.
  // Only sources whose shared storage outlives their own final use appear.
  std::vector<std::size_t> shared_storage_last_use;
  std::vector<std::size_t> definition;
  std::vector<unsigned> value_flags;
  std::vector<std::size_t> calls;
  // The first LowIR position that destroys each physical GPR's incoming
  // value.  This is a fixed 16-entry table populated by analyze_function.
  std::vector<std::size_t> first_register_clobber;
  // Every clobbering position per physical GPR, sorted ascending; a fixed
  // 16-entry table.  Interval queries (phi planning spans positions outside
  // [definition+1, last use], which live_across_clobbers cannot answer)
  // lower_bound into these.
  std::vector<std::vector<std::size_t> > clobber_positions;
  std::vector<const lowir_model::Instruction *> deferred_branch_comparisons;
  std::vector<unsigned> live_across_clobbers;
  bool has_va_start = false;
  bool has_dynamic_stack = false;
  bool has_i128_atomic = false;
  bool has_direct_branch_parameter = false;
  // The function contains a bounded object copy directly between incoming
  // parameter addresses.  O3 may preserve those incoming carriers across
  // that copy and a later inlined bulk copy in the same composite move.
  bool has_small_direct_parameter_copy = false;
  // The function's only call is an exact scalar-parameter transfer followed
  // immediately by its void return. Native lowering may end the frame before
  // transferring control to that callee.
  bool has_direct_sibling_call = false;
  bool has_eh = false;

  static std::size_t missing_position()
  {
    return std::numeric_limits<std::size_t>::max();
  }
  bool has(lowir_model::ValueId value, ValueFlag flag) const
  {
    return value.valid() &&
      (value_flags[static_cast<std::uint32_t>(value)] & flag) != 0;
  }
  void mark(lowir_model::ValueId value, ValueFlag flag)
  {
    value_flags[static_cast<std::uint32_t>(value)] |= flag;
  }
};

struct StorageFacts
{
  enum ValueFlag
  {
    VF_PROMOTED_PARAMETER = 1u << 0,
    VF_PROMOTED_ACROSS_CALL = 1u << 1,
    VF_DEAD_SLOT_ONLY_PARAMETER = 1u << 2,
    VF_TLS_STORE_INPUT = 1u << 3
  };
  std::vector<lowir_model::ValueId> parameter_slot_aliases;
  std::vector<lowir_model::ValueId> promoted_parameter_slots;
  std::vector<lowir_model::ValueId> forwarded_parameter_slots;
  std::vector<unsigned char> value_flags;
  // Indexed by LowIR parameter ordinal; each mask uses the fixed x86 GPR IDs.
  std::vector<unsigned> promoted_parameter_clobbers;
  // Uses that survive promoted or discarded scalar-slot lowering, indexed by
  // LowIR parameter ordinal.
  std::vector<std::size_t> parameter_selected_uses;
  std::vector<unsigned char> dead_store_slots;
  bool has_promoted_parameter_slots = false;

  bool has(lowir_model::ValueId value, ValueFlag flag) const
  {
    return value.valid() &&
      (value_flags[static_cast<std::uint32_t>(value)] & flag) != 0;
  }
  void mark(lowir_model::ValueId value, ValueFlag flag)
  {
    value_flags[static_cast<std::uint32_t>(value)] |= flag;
  }
};

unsigned register_mask(X64Register reg);
bool crosses_register_clobber(const FunctionFacts & facts,
                              lowir_model::ValueId value, X64Register reg);
bool register_was_clobbered_before(const FunctionFacts & facts,
                                   X64Register reg, std::size_t position);
FunctionFacts analyze_function(const lowir_model::LowirFunction & function,
                               Stats * stats = 0,
                               int optimization_level = 0,
                               lowir_model::SymbolId memcpy_symbol =
                                 lowir_model::SymbolId());
StorageFacts analyze_storage(
    const lowir_model::LowirFunction & function,
    const FunctionFacts & function_facts,
    const std::vector<lowir_model::SymbolId> & tls_wrappers);

}  // namespace analysis
}  // namespace lowir_native
