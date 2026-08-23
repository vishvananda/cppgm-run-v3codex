#pragma once

#include "lowir_model.h"
#include "lowir_native_analysis.h"
#include "lowir_native_registers.h"
#include "mir_model.h"

#include <memory>
#include <string>
#include <vector>

namespace lowir_native {

struct Stats;

namespace location_planning {

class LiveLocationIndex
{
public:
  explicit LiveLocationIndex(Stats * stats);

  void add(lowir_model::ValueId value,
           const mir_model::MirOperand & location);
  void remove(lowir_model::ValueId value,
              const mir_model::MirOperand & location);
  bool has_alias(lowir_model::ValueId value,
                 const mir_model::MirOperand & location,
                 bool value_is_live) const;
  const std::vector<lowir_model::ValueId> &
    gpr_values(X64Register reg) const;

private:
  Stats * stats_;
  std::vector<lowir_model::ValueId> gpr_values_[16];
  std::vector<lowir_model::ValueId> xmm_values_[8];
};

class GeneratedFrameNames
{
public:
  explicit GeneratedFrameNames(const lowir_model::LowirFunction & function);

  lowir_model::PresentationName name(lowir_model::ValueId value);

private:
  const lowir_model::LowirFunction & function_;
  std::unique_ptr<lowir_model::GeneratedNameReservations> reservations_;
};

struct ParallelXmmMove
{
  XmmRegister destination = XMM_0;
  mir_model::MirOperand source;
  lowir_model::LowType type;
  bool pending = true;
};

bool xmm_destination_is_safe(
    const std::vector<ParallelXmmMove> & moves,
    std::size_t candidate);

bool managed_register(X64Register reg);

// Assign call-crossing scalar temporaries proven conflict-free by a linear
// scan over `[definition, extended last use]` intervals to callee-saved
// registers, claimed from the opposite end of the reactive preference
// order.  Each entry is the planned register plus one; zero means no plan.
// plan_ends carries the extended interval end used for runtime release; it
// covers layout-backward jump spans AND exception regions whose landing
// pad lies before the region end, since unwinding is a backward edge too.
// Phi destinations participate with an interval starting at the earliest
// predecessor terminator (where the first transfer writes the home) and
// always span-extended; they claim callee-saved registers first, one phi
// per register, because DefinePhi reserves the home at construction time.
std::vector<unsigned char> plan_value_registers(
    const lowir_model::LowirFunction & function,
    const analysis::FunctionFacts & facts,
    int optimization_level,
    std::vector<std::size_t> * plan_ends,
    Stats * stats);

// CRTP state and queries for planned register residency.  The derived
// lowerer supplies values_, value_known_, registers_, stats_, position_,
// facts_, and crosses_register_clobber.
template <class Derived>
class PlannedResidency
{
protected:
  void compute_value_register_plan(
      const lowir_model::LowirFunction & function,
      const analysis::FunctionFacts & facts,
      int optimization_level, Stats * stats)
  {
    value_register_plan_ = plan_value_registers(
      function, facts, optimization_level, &value_plan_end_, stats);
  }
  unsigned char planned_register_entry(lowir_model::ValueId value) const
  {
    return value.valid() &&
      static_cast<std::size_t>(value) < value_register_plan_.size() ?
      value_register_plan_[value] : 0;
  }
  bool value_holds_planned_register(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    const unsigned char entry = planned_register_entry(value);
    return entry != 0 && lowerer.value_known_[value] &&
      lowerer.values_[value].location.kind == mir_model::MirOperand::OP_REG &&
      static_cast<unsigned char>(
        lowerer.values_[value].location.reg) + 1 == entry;
  }
  bool try_planned_grant(lowir_model::ValueId value, X64Register * result)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const unsigned char entry = planned_register_entry(value);
    const X64Register planned = static_cast<X64Register>(entry - 1);
    if(entry == 0 || lowerer.crosses_register_clobber(value, planned) ||
       !lowerer.registers_.try_reserve(planned)) return false;
    *result = planned;
    if(lowerer.stats_) ++lowerer.stats_->planned_register_grants;
    return true;
  }
  // The extended interval end lies past every layout backedge span and
  // every backward exception region that could re-read the register, so
  // once the walk passes it the final counted use may release it.  A
  // cached dereference operand replays its carrier register at every later
  // consumer of the owning address, beyond the carrier's counted uses, so
  // a carrier register is never released by interval end.
  bool planned_interval_over(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return value_holds_planned_register(value) &&
      static_cast<std::size_t>(value) < value_plan_end_.size() &&
      lowerer.position_ >= value_plan_end_[value] &&
      !deferred_carrier_registers_[static_cast<unsigned>(
        lowerer.values_[value].location.reg)];
  }
  void mark_deferred_carrier(X64Register reg)
  {
    deferred_carrier_registers_[static_cast<unsigned>(reg)] = 1;
  }

  std::vector<unsigned char> value_register_plan_;
  std::vector<std::size_t> value_plan_end_;
  unsigned char deferred_carrier_registers_[16] = {};
};

bool should_retain_edge_register(
    const mir_model::MirOperand & location,
    int optimization_level,
    bool function_has_eh,
    bool loop_carried,
    bool crosses_call,
    bool has_narrow_alias,
    bool crosses_fixed_register_clobber,
    const allocation::RegisterPool & registers,
    const allocation::XmmPool & xmms);

std::string diagnostic_value_name(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    lowir_model::ValueId value);

}  // namespace location_planning
}  // namespace lowir_native
