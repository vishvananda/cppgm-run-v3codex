#pragma once

#include "lowir/model/program.h"
#include "native/driver/stats.h"
#include "native/errors.h"
#include "native/analysis/function.h"
#include "native/lowering/control_flow.h"
#include "native/allocation/location_planning.h"
#include "native/mir/construction.h"
#include "native/mir/model.h"
#include "native/mir/registers.h"

#include <vector>

namespace lowir_native {
namespace spill_detail {

// Reactive spill selection: choose an evictable register resident under
// pressure, preferring the farthest next use.  The derived lowerer supplies
// values_, value_known_, cyclic_register_assumed_, facts_, control_flow_,
// live_locations_, registers_, stats_, position_, has_live_location_alias,
// current_instruction_uses, value_holds_planned_register,
// allocate_temp_frame_binding, and set_value_location.
template <class Derived>
class SpillSelection
{
protected:
  // A dead register-resident parameter's register may be reclaimed for a
  // new definition once no later use or edge can read it.
  bool reclaim_dead_parameter_register(bool needs_callee_saved)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(lowerer.control_flow_.CurrentBlockIsCyclic()) return false;
    if(lowerer.stats_) ++lowerer.stats_->reclaim_attempts;
    for(std::size_t i = 0; i < lowerer.source_.params.size(); ++i) {
      if(lowerer.stats_) ++lowerer.stats_->reclaim_parameter_visits;
      const lowir_model::ValueId value = lowerer.source_.params[i].value;
      if(!lowerer.value_known_[value]) continue;
      if(!lowerer.values_[value].parameter ||
         lowerer.values_[value].fixed_register_home ||
         lowerer.values_[value].location.kind !=
           mir_model::MirOperand::OP_REG ||
         !location_planning::managed_register(
           lowerer.values_[value].location.reg) ||
         !lowerer.registers_.is_used(lowerer.values_[value].location.reg) ||
         (needs_callee_saved &&
          !allocation::is_callee_saved(lowerer.values_[value].location.reg)))
        continue;
      if(lowerer.facts_.uses[value] != 0 ||
         lowerer.facts_.has(value,
                            analysis::FunctionFacts::VF_EDGE_LIVE) ||
         !lowerer.control_flow_.SpillIsSafe(value, lowerer.position_) ||
         lowerer.has_live_location_alias(value,
                                         lowerer.values_[value].location))
        continue;
      lowerer.registers_.release(lowerer.values_[value].location.reg);
      if(lowerer.stats_) ++lowerer.stats_->reclaims;
      return true;
    }
    return false;
  }
  bool spill_candidate(lowir_model::ValueId value,
                       bool needs_callee_saved) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    // Landing pads may read planned residents: EH never evicts them.
    if(lowerer.facts_.has_eh &&
       lowerer.value_holds_planned_register(value)) return false;
    if(!lowerer.value_known_[value] || lowerer.values_[value].parameter ||
       lowerer.values_[value].location.kind != mir_model::MirOperand::OP_REG ||
       lowerer.cyclic_register_assumed_[value] ||
       !location_planning::managed_register(
         lowerer.values_[value].location.reg) ||
       (needs_callee_saved &&
        !allocation::is_callee_saved(lowerer.values_[value].location.reg)) ||
       lowerer.has_live_location_alias(
         value, lowerer.values_[value].location) ||
       lowerer.current_instruction_uses(value)) return false;
    return ((lowerer.values_[value].has_spill_home &&
             !lowerer.control_flow_.CurrentBlockIsCyclic()) ||
            lowerer.control_flow_.SpillIsSafe(value, lowerer.position_)) &&
      lowerer.facts_.uses[value] != 0;
  }
  void record_cyclic_register_assumptions(
      const std::vector<mir_model::MirInstruction> & instructions,
      std::size_t first)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(!lowerer.control_flow_.CurrentBlockIsCyclic()) return;
    for(std::size_t instruction = first;
        instruction < instructions.size(); ++instruction)
      for(std::size_t operand = 0;
          operand < instructions[instruction].operands.size(); ++operand) {
        const mir_model::MirOperand & location =
          instructions[instruction].operands[operand];
        if(location.kind == mir_model::MirOperand::OP_REG)
          record_cyclic_register_assumption(location.reg);
        else if(location.kind == mir_model::MirOperand::OP_DEREF) {
          record_cyclic_register_assumption(location.reg);
          if(location.has_index)
            record_cyclic_register_assumption(location.index);
        }
      }
  }
  void record_cyclic_register_assumption(X64Register reg)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const std::vector<lowir_model::ValueId> & occupants =
      lowerer.live_locations_.gpr_values(reg);
    for(std::size_t value = 0; value < occupants.size(); ++value)
      lowerer.cyclic_register_assumed_[occupants[value]] = 1;
  }
  lowir_model::ValueId
  find_spill_victim_full_scan(bool needs_callee_saved)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    lowir_model::ValueId victim;
    std::size_t farthest_use = 0;
    for(std::size_t raw_value = 0;
        raw_value < lowerer.values_.size(); ++raw_value) {
      const lowir_model::ValueId value(static_cast<std::uint32_t>(raw_value));
      if(lowerer.stats_) ++lowerer.stats_->spill_value_visits;
      if(!spill_candidate(value, needs_callee_saved)) continue;
      const std::size_t last = lowerer.facts_.last_use[value] ==
        analysis::FunctionFacts::missing_position() ?
        0 : lowerer.facts_.last_use[value];
      if(!victim.valid() || last >= farthest_use) {
        victim = value;
        farthest_use = last;
      }
    }
    return victim;
  }
  lowir_model::ValueId
  find_spill_victim(bool needs_callee_saved)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    lowir_model::ValueId victim;
    std::size_t farthest_use = 0;
    bool tied = false;
    for(std::size_t reg = 0; reg < 16; ++reg) {
      const std::vector<lowir_model::ValueId> & occupants =
        lowerer.live_locations_.gpr_values(static_cast<X64Register>(reg));
      for(std::size_t i = 0; i < occupants.size(); ++i) {
        if(lowerer.stats_) ++lowerer.stats_->spill_value_visits;
        const lowir_model::ValueId value = occupants[i];
        if(!lowerer.value_known_[value])
          native_errors::ThrowInternal("native live-location value is missing");
        if(!spill_candidate(value, needs_callee_saved)) continue;
        if(lowerer.stats_) ++lowerer.stats_->spill_candidates;
        const std::size_t last = lowerer.facts_.last_use[value] ==
          analysis::FunctionFacts::missing_position() ?
          0 : lowerer.facts_.last_use[value];
        if(!victim.valid() || last > farthest_use) {
          victim = value;
          farthest_use = last;
          tied = false;
        } else if(last == farthest_use) {
          tied = true;
        }
      }
    }
    if(!tied) return victim;
    if(lowerer.stats_) ++lowerer.stats_->spill_full_scan_fallbacks;
    return find_spill_victim_full_scan(needs_callee_saved);
  }
  bool spill_one(bool needs_callee_saved,
                 std::vector<mir_model::MirInstruction> & out)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(lowerer.stats_) ++lowerer.stats_->spill_attempts;
    const lowir_model::ValueId victim = find_spill_victim(needs_callee_saved);
    if(!victim.valid()) return false;
    mir_model::MirOperand home;
    if(lowerer.values_[victim].has_spill_home)
      home = lowerer.values_[victim].spill_home;
    else {
      home = lowerer.allocate_temp_frame_binding(
        victim, lowerer.values_[victim].type, THR_REGISTER_PRESSURE);
      build::append_store(out, home, lowerer.values_[victim].location,
                          lowerer.values_[victim].type);
    }
    const X64Register released = lowerer.values_[victim].location.reg;
    lowerer.set_value_location(victim, home);
    lowerer.registers_.release(released);
    if(lowerer.stats_) ++lowerer.stats_->spills;
    return true;
  }
};

}  // namespace spill_detail
}  // namespace lowir_native
