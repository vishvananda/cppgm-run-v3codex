#pragma once

#include "lowir_model.h"
#include "lowir_native_analysis.h"
#include "lowir_native_registers.h"
#include "mir_model.h"

#include <algorithm>

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
// register_spans (indexed by X64Register) receives the planner's claimed
// [start, end] intervals for callee-saved pool registers, so the walk's
// reactive allocator can steer scratch away from registers a planned
// value will need.  The spans are advisory: ignoring them only makes a
// later planned grant fail busy, never wrong code.
// extension_spans receives every layout backedge and backward exception
// span (the interval-extension material), filled even for functions the
// planner otherwise skips, so the walk can test span membership.
std::vector<unsigned char> plan_value_registers(
    const lowir_model::LowirFunction & function,
    const analysis::FunctionFacts & facts,
    int optimization_level,
    std::vector<std::size_t> * plan_ends,
    std::vector<std::pair<std::size_t, std::size_t> > * register_spans,
    std::vector<std::pair<std::size_t, std::size_t> > * extension_spans,
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
      function, facts, optimization_level, &value_plan_end_,
      planned_register_spans_, &extension_spans_, stats);
  }
  // True when no layout backedge or backward exception span contains the
  // position: a final counted use here can never be re-executed, so an
  // otherwise edge-live register is genuinely dead past it.
  bool position_outside_extension_spans(std::size_t position) const
  {
    for(std::size_t i = 0; i < extension_spans_.size(); ++i)
      if(extension_spans_[i].first <= position &&
         position < extension_spans_[i].second) return false;
    return true;
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
    if(entry == 0) return false;
    if(lowerer.crosses_register_clobber(value, planned)) {
      if(lowerer.stats_) ++lowerer.stats_->planned_grant_clobber_fails;
      return false;
    }
    if(!lowerer.registers_.try_reserve(planned)) {
      if(lowerer.stats_) {
        ++lowerer.stats_->planned_grant_busy_fails;
        if(allocation::is_callee_saved(planned))
          ++lowerer.stats_->planned_grant_busy_fails_callee;
        const std::vector<lowir_model::ValueId> & holders =
          lowerer.live_locations_.gpr_values(planned);
        for(std::size_t i = 0; i < holders.size(); ++i)
          if(planned_register_entry(holders[i]) == entry) {
            ++lowerer.stats_->planned_grant_busy_planned_holder;
            break;
          }
        if(holders.empty())
          ++lowerer.stats_->planned_grant_busy_no_holder;
        else {
          bool parameter_holder = false;
          for(std::size_t i = 0; i < holders.size(); ++i)
            if(lowerer.values_[holders[i]].parameter) parameter_holder = true;
          if(parameter_holder)
            ++lowerer.stats_->planned_grant_busy_parameter_holder;
          else
            ++lowerer.stats_->planned_grant_busy_value_holder;
        }
      }
      return false;
    }
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
  // True when a planned value's claimed interval on reg overlaps
  // [start, end] — the reactive allocator prefers a different register.
  bool planned_span_conflicts(X64Register reg, std::size_t start,
                              std::size_t end) const
  {
    const std::vector<std::pair<std::size_t, std::size_t> > & spans =
      planned_register_spans_[static_cast<unsigned>(reg)];
    for(std::size_t i = 0; i < spans.size(); ++i)
      if(spans[i].first <= end && start <= spans[i].second) return true;
    return false;
  }
  // Reactive callee-saved allocation prefers registers with no planned
  // interval overlapping the requester's remaining lifetime, so scratch
  // stops occupying the registers planned call-crossing values need.  The
  // second pass keeps the original behavior: a conflict never turns an
  // allocation success into a failure.
  bool try_allocate_preserved_avoiding_plans(std::size_t start,
                                             std::size_t end,
                                             X64Register * result)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    static const X64Register preserved[] = {
      XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
    };
    for(std::size_t pass = 0; pass < 2; ++pass)
      for(std::size_t i = 0;
          i < sizeof(preserved) / sizeof(preserved[0]); ++i) {
        if(pass == 0 && planned_span_conflicts(preserved[i], start, end))
          continue;
        if(lowerer.registers_.try_reserve(preserved[i])) {
          *result = preserved[i];
          return true;
        }
      }
    return false;
  }
  std::size_t reactive_lifetime_end(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return lowerer.facts_.uses[value] != 0 &&
      lowerer.facts_.last_use[value] !=
        analysis::FunctionFacts::missing_position() ?
      lowerer.facts_.last_use[value] : lowerer.position_;
  }
  // Eager parameter homes occupy their register from function entry.
  X64Register allocate_preserved_for_parameter(lowir_model::ValueId value)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    X64Register result = XR_RSP;
    if(try_allocate_preserved_avoiding_plans(
         0, reactive_lifetime_end(value), &result)) return result;
    return lowerer.registers_.allocate(true);
  }
  // --stats classification of where planned values actually land.
  void record_planned_definition(lowir_model::ValueId id,
                                 const mir_model::MirOperand & location)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(!lowerer.stats_) return;
    const unsigned char entry = planned_register_entry(id);
    if(entry == 0) return;
    const bool callee = allocation::is_callee_saved(
      static_cast<X64Register>(entry - 1));
    if(location.kind == mir_model::MirOperand::OP_REG &&
       static_cast<unsigned char>(location.reg) + 1 == entry) {
      ++lowerer.stats_->planned_defined_in_plan;
      if(callee) ++lowerer.stats_->planned_defined_in_plan_callee;
    } else if(location.kind == mir_model::MirOperand::OP_FRAME) {
      ++lowerer.stats_->planned_defined_frame;
      if(callee) ++lowerer.stats_->planned_defined_frame_callee;
    } else if(location.kind == mir_model::MirOperand::OP_REG) {
      ++lowerer.stats_->planned_defined_other_register;
      if(callee) ++lowerer.stats_->planned_defined_other_register_callee;
    } else
      ++lowerer.stats_->planned_defined_elsewhere;
  }
  // --stats classification of frame-sourced GPR call-argument loads by
  // value class — the census that sized the call-crossing residual.
  void record_call_argument_frame_load(const lowir_model::Operand & argument)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    using analysis::FunctionFacts;
    // Deduced so the forward-declared Stats completes at instantiation.
    auto * stats = lowerer.stats_;
    ++stats->call_arg_frame_loads;
    if(argument.kind != lowir_model::Operand::OP_TEMP) {
      ++stats->call_arg_frame_loads_slot;
      return;
    }
    const analysis::FunctionFacts & facts = lowerer.facts_;
    if(facts.has(argument.value, FunctionFacts::VF_PARAMETER))
      ++stats->call_arg_frame_loads_parameter;
    else if(facts.has(argument.value, FunctionFacts::VF_ONLY_CALL_ARGUMENT)) {
      ++stats->call_arg_frame_loads_only_call_argument;
      if(std::binary_search(facts.calls.begin(), facts.calls.end(),
                            facts.definition[argument.value]))
        ++stats->call_arg_frame_loads_oca_call_result;
      if(facts.has(argument.value, FunctionFacts::VF_LIVE_ACROSS_CALL))
        ++stats->call_arg_frame_loads_oca_crossing;
      if(facts.has(argument.value, FunctionFacts::VF_EDGE_LIVE))
        ++stats->call_arg_frame_loads_oca_edge_live;
      if(facts.uses[argument.value] > 1)
        ++stats->call_arg_frame_loads_oca_multi_use;
    }
    else if(facts.has(argument.value, FunctionFacts::VF_ONLY_STORAGE_ADDRESS))
      ++stats->call_arg_frame_loads_storage_address;
    else if(planned_register_entry(argument.value) != 0)
      ++stats->call_arg_frame_loads_planned;
    else if(facts.has(argument.value, FunctionFacts::VF_LIVE_ACROSS_CALL))
      ++stats->call_arg_frame_loads_crossing_unplanned;
    else
      ++stats->call_arg_frame_loads_other;
  }

  std::vector<unsigned char> value_register_plan_;
  std::vector<std::size_t> value_plan_end_;
  std::vector<std::pair<std::size_t, std::size_t> >
    planned_register_spans_[16];
  std::vector<std::pair<std::size_t, std::size_t> > extension_spans_;
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
