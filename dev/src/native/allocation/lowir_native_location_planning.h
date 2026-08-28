#pragma once

#include "lowir/model/program.h"
#include "native/analysis/lowir_native_analysis.h"
#include "native/allocation/lowir_native_registers.h"
#include "native/mir/mir_model.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>

#include <memory>
#include <string>
#include <vector>

namespace lowir_native {

struct Stats;
namespace analysis { class ControlFlowQueries; }

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

enum PlannedLocationKind
{
  PLK_GPR,
  PLK_CYCLIC_REGION_GPR,
  PLK_LOCAL_PHI_GPR,
  PLK_XMM,
  PLK_FRAME,
  PLK_REMATERIALIZE
};

inline bool planned_register_kind(PlannedLocationKind kind)
{
  return kind == PLK_GPR || kind == PLK_CYCLIC_REGION_GPR ||
    kind == PLK_LOCAL_PHI_GPR;
}

struct PlannedLocationSegment
{
  PlannedLocationSegment(std::size_t segment_begin,
                         std::size_t segment_end,
                         PlannedLocationKind location_kind,
                         unsigned location_index = 0)
    : begin(segment_begin), end(segment_end), kind(location_kind),
      index(location_index) {}

  std::size_t begin;
  std::size_t end;
  PlannedLocationKind kind;
  unsigned index;
};

typedef std::vector<PlannedLocationSegment> ValueLocationTimeline;
typedef std::vector<ValueLocationTimeline> FunctionLocationTimeline;

// Assign eligible scalar temporaries proven conflict-free by a linear scan
// over `[definition, extended last use]` intervals.  The result is an
// explicit per-value location timeline; gate (i) initially emits the same
// single GPR segment represented by the legacy register/plan_end vectors.
// Later P30 phases can add frame/rematerialized gaps and multiple register
// segments without changing the walk's interface.  Segment ends cover
// layout-backward jump spans AND exception regions whose landing pad lies
// before the region end, since unwinding is a backward edge too.
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
FunctionLocationTimeline plan_value_locations(
    const lowir_model::LowirFunction & function,
    const analysis::FunctionFacts & facts,
    int optimization_level,
    std::vector<std::pair<std::size_t, std::size_t> > * register_spans,
    std::vector<std::pair<std::size_t, std::size_t> > *
      cyclic_region_register_spans,
    std::vector<std::pair<std::size_t, std::size_t> > * extension_spans,
    Stats * stats);

// CRTP state and queries for planned register residency.  The derived
// lowerer supplies values_, value_known_, registers_, stats_, position_,
// facts_, and crosses_register_clobber.
template <class Derived>
class PlannedResidency
{
protected:
  void compute_location_timeline(
      const lowir_model::LowirFunction & function,
      const analysis::FunctionFacts & facts,
      int optimization_level, Stats * stats)
  {
    location_timeline_ = plan_value_locations(
      function, facts, optimization_level, planned_register_spans_,
      cyclic_region_register_spans_, &extension_spans_, stats);
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
  const PlannedLocationSegment * planned_register_segment_at(
      lowir_model::ValueId value, std::size_t position) const
  {
    if(!value.valid() ||
       static_cast<std::size_t>(value) >= location_timeline_.size()) return 0;
    const ValueLocationTimeline & timeline = location_timeline_[value];
    for(std::size_t i = 0; i < timeline.size(); ++i)
      if(planned_register_kind(timeline[i].kind) &&
         timeline[i].begin <= position &&
         position <= timeline[i].end) return &timeline[i];
    return 0;
  }
  unsigned char planned_register_entry(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    const PlannedLocationSegment * segment =
      planned_register_segment_at(value, lowerer.position_);
    return segment ? segment->index + 1 : 0;
  }
  const PlannedLocationSegment * planned_register_segment(
      lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return planned_register_segment_at(value, lowerer.position_);
  }
  bool planned_rematerialization(lowir_model::ValueId value) const
  {
    if(!value.valid() ||
       static_cast<std::size_t>(value) >= location_timeline_.size())
      return false;
    const ValueLocationTimeline & timeline = location_timeline_[value];
    for(std::size_t i = 0; i < timeline.size(); ++i)
      if(timeline[i].kind == PLK_REMATERIALIZE) return true;
    return false;
  }
  bool value_holds_planned_register(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    if(!value.valid() || !lowerer.value_known_[value] ||
       lowerer.values_[value].location.kind != mir_model::MirOperand::OP_REG ||
       static_cast<std::size_t>(value) >= location_timeline_.size())
      return false;
    const ValueLocationTimeline & timeline = location_timeline_[value];
    for(std::size_t i = 0; i < timeline.size(); ++i)
      if(planned_register_kind(timeline[i].kind) &&
         timeline[i].begin <= lowerer.position_ &&
         lowerer.position_ <= timeline[i].end &&
         timeline[i].index == static_cast<unsigned>(
           lowerer.values_[value].location.reg))
        return true;
    return false;
  }
  bool value_has_cyclic_region_plan(lowir_model::ValueId value) const
  {
    const PlannedLocationSegment * segment = planned_register_segment(value);
    return segment && segment->kind == PLK_CYCLIC_REGION_GPR;
  }
  bool try_planned_grant(lowir_model::ValueId value, X64Register * result)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const PlannedLocationSegment * segment =
      planned_register_segment(value);
    const unsigned char entry = segment ? segment->index + 1 : 0;
    const X64Register planned = static_cast<X64Register>(entry - 1);
    if(entry == 0) return false;
    if(lowerer.crosses_register_clobber(value, planned)) {
      if(lowerer.stats_) ++lowerer.stats_->planned_grant_clobber_fails;
      return false;
    }
    if(!lowerer.registers_.try_reserve(planned)) {
      if(lowerer.stats_) {
        ++lowerer.stats_->planned_grant_busy_fails;
        if(segment->kind == PLK_CYCLIC_REGION_GPR)
          ++lowerer.stats_->planned_cyclic_region_busy_fails;
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
    if(lowerer.stats_) {
      ++lowerer.stats_->planned_register_grants;
      if(segment->kind == PLK_CYCLIC_REGION_GPR)
        ++lowerer.stats_->planned_cyclic_region_grants;
    }
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
    if(!value_holds_planned_register(value) ||
       deferred_carrier_registers_[static_cast<unsigned>(
         lowerer.values_[value].location.reg)])
      return false;
    const ValueLocationTimeline & timeline = location_timeline_[value];
    for(std::size_t i = 0; i < timeline.size(); ++i)
      if(planned_register_kind(timeline[i].kind) &&
         timeline[i].index == static_cast<unsigned>(
           lowerer.values_[value].location.reg) &&
         timeline[i].begin <= lowerer.position_ &&
         lowerer.position_ >= timeline[i].end)
        return true;
    return false;
  }
  void mark_deferred_carrier(X64Register reg)
  {
    deferred_carrier_registers_[static_cast<unsigned>(reg)] = 1;
  }
  // A planned resident whose final use lies inside a span is only
  // releasable once the walk passes its extended plan end — but the
  // consume-time check runs at the final use, so without a schedule the
  // register stays held to the end of the function.  Block entry flushes
  // every schedule entry the walk has passed.
  void build_planned_release_schedule()
  {
    planned_release_schedule_.clear();
    planned_release_cursor_ = 0;
    planned_promotion_schedule_.clear();
    planned_promotion_cursor_ = 0;
    while(!deferred_span_end_releases_.empty())
      deferred_span_end_releases_.pop();
    planned_release_done_.assign(location_timeline_.size(), 0);
    for(std::size_t v = 0; v < location_timeline_.size(); ++v) {
      for(std::size_t i = 0; i < location_timeline_[v].size(); ++i) {
        const PlannedLocationSegment & segment = location_timeline_[v][i];
        if(!planned_register_kind(segment.kind)) continue;
        planned_release_schedule_.push_back(
          std::make_pair(segment.end,
                         lowir_model::ValueId(
                           static_cast<std::uint32_t>(v))));
        if(segment.kind == PLK_LOCAL_PHI_GPR ||
           segment.begin >
             static_cast<const Derived &>(*this).facts_.definition[v])
          planned_promotion_schedule_.push_back(
            std::make_pair(segment.begin,
                           lowir_model::ValueId(
                             static_cast<std::uint32_t>(v))));
      }
    }
    std::sort(planned_release_schedule_.begin(),
              planned_release_schedule_.end());
    std::sort(planned_promotion_schedule_.begin(),
              planned_promotion_schedule_.end());
  }
  void promote_planned_segments(
      std::vector<mir_model::MirInstruction> & out)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    while(planned_promotion_cursor_ < planned_promotion_schedule_.size() &&
          planned_promotion_schedule_[planned_promotion_cursor_].first <
            lowerer.position_)
      ++planned_promotion_cursor_;
    while(planned_promotion_cursor_ < planned_promotion_schedule_.size() &&
          planned_promotion_schedule_[planned_promotion_cursor_].first ==
            lowerer.position_) {
      const lowir_model::ValueId value =
        planned_promotion_schedule_[planned_promotion_cursor_++].second;
      const PlannedLocationSegment * segment = 0;
      const ValueLocationTimeline & timeline = location_timeline_[value];
      for(std::size_t i = 0; i < timeline.size(); ++i)
        if(planned_register_kind(timeline[i].kind) &&
           timeline[i].begin == lowerer.position_ &&
           (timeline[i].kind == PLK_LOCAL_PHI_GPR ||
            timeline[i].begin > lowerer.facts_.definition[value])) {
          segment = &timeline[i];
          break;
        }
      if(!segment || !lowerer.value_known_[value] ||
         lowerer.values_[value].location.kind !=
           mir_model::MirOperand::OP_FRAME)
        continue;
      const X64Register reg = static_cast<X64Register>(segment->index);
      if(!lowerer.registers_.try_reserve(reg)) {
        if(lowerer.stats_) {
          if(segment->kind == PLK_LOCAL_PHI_GPR)
            ++lowerer.stats_->planned_local_phi_busy_fails;
          else
            ++lowerer.stats_->planned_use_tail_busy_fails;
        }
        continue;
      }
      const mir_model::MirOperand source = lowerer.values_[value].location;
      if(segment->kind != PLK_LOCAL_PHI_GPR)
        lowerer.move_value_to_register(
          out, reg, source, lowerer.values_[value].type);
      mir_model::MirOperand replacement;
      replacement.kind = mir_model::MirOperand::OP_REG;
      replacement.reg = reg;
      lowerer.set_value_location(value, replacement);
      if(lowerer.stats_) {
        ++lowerer.stats_->planned_register_grants;
        if(segment->kind == PLK_LOCAL_PHI_GPR)
          ++lowerer.stats_->planned_local_phi_promotions;
        else
          ++lowerer.stats_->planned_use_tail_promotions;
      }
    }
  }
  void plan_staged_use_tail(lowir_model::ValueId value)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    using analysis::FunctionFacts;
    if(lowerer.optimization_level_ < 1 ||
       lowerer.facts_.uses[value] < 2 ||
       !lowerer.facts_.has(value, FunctionFacts::VF_EDGE_LIVE) ||
       !lowerer.facts_.has(value, FunctionFacts::VF_LIVE_ACROSS_CALL) ||
       lowerer.facts_.has(value, FunctionFacts::VF_PARAMETER) ||
       lowerer.facts_.has(value, FunctionFacts::VF_SLOT_ADDRESS) ||
       lowerer.facts_.has(value, FunctionFacts::VF_GLOBAL_ADDRESS) ||
       lowerer.facts_.has(value, FunctionFacts::VF_ONLY_STORAGE_ADDRESS) ||
       lowerer.facts_.definition[value] ==
         FunctionFacts::missing_position() ||
       lowerer.facts_.last_use[value] ==
         FunctionFacts::missing_position())
      return;
    std::vector<std::size_t>::const_iterator call =
      std::upper_bound(lowerer.facts_.calls.begin(),
                       lowerer.facts_.calls.end(),
                       lowerer.facts_.last_use[value]);
    if(call == lowerer.facts_.calls.begin()) return;
    --call;
    if(*call <= lowerer.facts_.definition[value]) return;
    std::size_t begin = 0, end = 0;
    if(!lowerer.control_flow_.FindDominatedUseTail(
         value, *call, &begin, &end)) return;
    const lowir_model::LowType & type = lowerer.values_[value].type;
    if(type.kind != lowir_model::LTK_PTR &&
       type.kind != lowir_model::LTK_I1 &&
       type.kind != lowir_model::LTK_I8 &&
       type.kind != lowir_model::LTK_U8 &&
       type.kind != lowir_model::LTK_I16 &&
       type.kind != lowir_model::LTK_U16 &&
       type.kind != lowir_model::LTK_I32 &&
       type.kind != lowir_model::LTK_U32 &&
       type.kind != lowir_model::LTK_I64)
      return;
    if(lowerer.stats_) ++lowerer.stats_->planner_use_tail_candidates;
    static const X64Register pool[] = {XR_R9, XR_R8, XR_RDI, XR_RSI};
    build_use_tail_conflict_index();
    for(std::size_t choice = 0;
        choice < sizeof(pool) / sizeof(pool[0]); ++choice) {
      const X64Register reg = pool[choice];
      if(use_tail_span_conflicts(choice, begin, end)) continue;
      const std::vector<std::size_t> & clobbers =
        lowerer.facts_.clobber_positions[static_cast<std::size_t>(reg)];
      const std::vector<std::size_t>::const_iterator clobber =
        std::lower_bound(clobbers.begin(), clobbers.end(), begin);
      if(clobber != clobbers.end() && *clobber <= end) continue;
      location_timeline_[value].push_back(PlannedLocationSegment(
        begin, end, PLK_GPR, static_cast<unsigned>(reg)));
      use_tail_conflict_spans_[choice].push_back(
        std::make_pair(begin, end));
      planned_promotion_schedule_.push_back(std::make_pair(begin, value));
      std::sort(planned_promotion_schedule_.begin(),
                planned_promotion_schedule_.end());
      if(lowerer.stats_) {
        ++lowerer.stats_->planned_value_registers;
        ++lowerer.stats_->planner_use_tail_assignments;
      }
      return;
    }
  }
  void build_use_tail_conflict_index()
  {
    if(use_tail_conflict_index_ready_) return;
    use_tail_conflict_index_ready_ = true;
    for(std::size_t value = 0; value < location_timeline_.size(); ++value)
      for(std::size_t i = 0; i < location_timeline_[value].size(); ++i) {
        const PlannedLocationSegment & segment =
          location_timeline_[value][i];
        if(!planned_register_kind(segment.kind)) continue;
        static const X64Register pool[] = {XR_R9, XR_R8, XR_RDI, XR_RSI};
        for(std::size_t reg = 0; reg < sizeof(pool) / sizeof(pool[0]); ++reg)
          if(segment.index == static_cast<unsigned>(pool[reg])) {
            use_tail_conflict_spans_[reg].push_back(
              std::make_pair(segment.begin, segment.end));
            break;
          }
      }
  }
  bool use_tail_span_conflicts(std::size_t reg, std::size_t begin,
                               std::size_t end) const
  {
    const std::vector<std::pair<std::size_t, std::size_t> > & spans =
      use_tail_conflict_spans_[reg];
    for(std::size_t i = 0; i < spans.size(); ++i)
      if(spans[i].first <= end && begin <= spans[i].second) return true;
    return false;
  }
  void note_planned_release(lowir_model::ValueId value)
  {
    planned_release_done_[value] = 1;
  }
  void flush_planned_releases()
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    while(planned_release_cursor_ < planned_release_schedule_.size() &&
          planned_release_schedule_[planned_release_cursor_].first <
            lowerer.position_)
      release_at_span_end(
        planned_release_schedule_[planned_release_cursor_++].second);
    while(!deferred_span_end_releases_.empty() &&
          deferred_span_end_releases_.top().first < lowerer.position_) {
      const lowir_model::ValueId value(deferred_span_end_releases_.top().second);
      deferred_span_end_releases_.pop();
      release_at_span_end(value);
    }
  }
  // An edge-live register is dead once the walk passes the extended end of
  // its final counted use: every span that could re-execute the use is
  // over — the same envelope interval_over trusts for planned values.
  void release_at_span_end(lowir_model::ValueId value)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(planned_release_done_[value] ||
       !lowerer.value_known_[value] ||
       lowerer.facts_.uses[value] != 0 ||
       !lowerer.value_outlives_counted_uses(value) ||
       lowerer.values_[value].parameter ||
       lowerer.values_[value].fixed_register_home ||
       lowerer.phi_planned_home_[value] != 0 ||
       lowerer.values_[value].location.kind != mir_model::MirOperand::OP_REG ||
       lowerer.values_[value].location.reg == XR_RAX ||
       deferred_carrier_registers_[static_cast<unsigned>(
         lowerer.values_[value].location.reg)] ||
       lowerer.has_live_location_alias(
         value, lowerer.values_[value].location))
      return;
    planned_release_done_[value] = 1;
    lowerer.live_locations_.remove(value, lowerer.values_[value].location);
    lowerer.registers_.release(lowerer.values_[value].location.reg);
    hold_released_for_plan(lowerer.values_[value].location.reg);
    if(lowerer.stats_) ++lowerer.stats_->planned_interval_releases;
  }
  // An unplanned edge-live value whose final counted use sits inside a
  // backedge or exception span cannot release at the use, but its register
  // is reclaimable at the extended span end.
  void maybe_schedule_span_end_release(lowir_model::ValueId value)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(planned_register_entry(value) != 0 ||
       lowerer.phi_planned_home_[value] != 0 ||
       lowerer.values_[value].parameter ||
       lowerer.values_[value].fixed_register_home ||
       lowerer.values_[value].location.kind != mir_model::MirOperand::OP_REG)
      return;
    std::size_t end = lowerer.position_;
    bool grew = true;
    while(grew) {
      grew = false;
      for(std::size_t i = 0; i < extension_spans_.size(); ++i)
        if(extension_spans_[i].first <= end &&
           end < extension_spans_[i].second) {
          end = extension_spans_[i].second;
          grew = true;
        }
    }
    deferred_span_end_releases_.push(
      std::make_pair(end, static_cast<std::uint32_t>(value)));
  }
  bool value_outlives_counted_uses(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return lowerer.facts_.has(
        value, analysis::FunctionFacts::VF_EDGE_LIVE) ||
      lowerer.phi_planned_home_[value] != 0;
  }
  bool value_is_live(lowir_model::ValueId value) const
  {
    const Derived & lowerer = static_cast<const Derived &>(*this);
    return lowerer.facts_.uses[value] != 0 ||
      value_outlives_counted_uses(value);
  }
  // A future claim on a just-released register keeps it out of ordinary
  // reactive rotation so the claim's grant still finds it free.
  void hold_released_for_plan(X64Register reg)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const std::vector<std::pair<std::size_t, std::size_t> > & spans =
      planned_register_spans_[static_cast<unsigned>(reg)];
    for(std::size_t i = 0; i < spans.size(); ++i)
      if(spans[i].first > lowerer.position_) {
        lowerer.registers_.hold_for_plan(reg);
        return;
      }
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
  bool cyclic_region_span_conflicts(X64Register reg, std::size_t start,
                                    std::size_t end) const
  {
    const std::vector<std::pair<std::size_t, std::size_t> > & spans =
      cyclic_region_register_spans_[static_cast<unsigned>(reg)];
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
    for(std::size_t pass = 0; pass < 3; ++pass)
      for(std::size_t i = 0;
          i < sizeof(preserved) / sizeof(preserved[0]); ++i) {
        if(cyclic_region_span_conflicts(preserved[i], start, end))
          continue;
        if(pass == 0 && planned_span_conflicts(preserved[i], start, end))
          continue;
        if(pass < 2 && lowerer.registers_.plan_held(preserved[i]))
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
    std::size_t end = lowerer.facts_.uses[value] != 0 &&
      lowerer.facts_.last_use[value] !=
        analysis::FunctionFacts::missing_position() ?
      lowerer.facts_.last_use[value] : lowerer.position_;
    // An edge-live value whose final textual use is inside a backedge or
    // backward exception span may be read again after later layout
    // positions execute.  Advertise that same envelope to reactive
    // allocation; otherwise a short-looking value can occupy a register
    // reserved for a later definition in the cycle and cannot safely be
    // evicted when the reservation begins.
    if(lowerer.facts_.has(value, analysis::FunctionFacts::VF_EDGE_LIVE)) {
      bool grew = true;
      while(grew) {
        grew = false;
        for(std::size_t i = 0; i < extension_spans_.size(); ++i)
          if(extension_spans_[i].first <= end &&
             end < extension_spans_[i].second) {
            end = extension_spans_[i].second;
            grew = true;
          }
      }
    }
    return end;
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

  FunctionLocationTimeline location_timeline_;
  std::vector<std::pair<std::size_t, std::size_t> >
    planned_register_spans_[16];
  std::vector<std::pair<std::size_t, std::size_t> >
    cyclic_region_register_spans_[16];
  std::vector<std::pair<std::size_t, std::size_t> > extension_spans_;
  std::vector<std::pair<std::size_t, lowir_model::ValueId> >
    planned_release_schedule_;
  std::size_t planned_release_cursor_ = 0;
  std::vector<std::pair<std::size_t, lowir_model::ValueId> >
    planned_promotion_schedule_;
  std::size_t planned_promotion_cursor_ = 0;
  std::vector<std::pair<std::size_t, std::size_t> >
    use_tail_conflict_spans_[4];
  bool use_tail_conflict_index_ready_ = false;
  std::vector<unsigned char> planned_release_done_;
  std::priority_queue<
    std::pair<std::size_t, std::uint32_t>,
    std::vector<std::pair<std::size_t, std::uint32_t> >,
    std::greater<std::pair<std::size_t, std::uint32_t> > >
    deferred_span_end_releases_;
  unsigned char deferred_carrier_registers_[16] = {};
};

bool should_retain_edge_register(
    const mir_model::MirOperand & location,
    int optimization_level,
    bool requires_eh_fallback,
    bool loop_carried,
    bool crosses_call,
    bool has_narrow_alias,
    bool crosses_fixed_register_clobber,
    const allocation::RegisterPool & registers,
    const allocation::XmmPool & xmms);

void record_edge_staging(Stats * stats,
    lowir_model::Instruction::Kind instruction_kind,
    lowir_model::Operand::Kind first_kind,
    lowir_model::Operand::Kind second_kind,
    mir_model::MirOperand::Kind location_kind, bool function_has_eh,
    bool loop_invariant, bool crosses_call, bool narrow_alias,
    bool fixed_clobber, std::size_t remaining_uses);

std::string diagnostic_value_name(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    lowir_model::ValueId value);

}  // namespace location_planning
}  // namespace lowir_native
