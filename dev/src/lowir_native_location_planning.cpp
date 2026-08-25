#include "lowir_native_location_planning.h"

#include "lowir_native.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <stdexcept>

namespace lowir_native {
namespace location_planning {

LiveLocationIndex::LiveLocationIndex(Stats * stats) : stats_(stats) {}

void LiveLocationIndex::add(
    lowir_model::ValueId value, const mir_model::MirOperand & location)
{
  if(location.kind == mir_model::MirOperand::OP_REG)
    gpr_values_[location.reg].push_back(value);
  else if(location.kind == mir_model::MirOperand::OP_XMM)
    xmm_values_[location.xmm].push_back(value);
  else return;
  if(stats_) ++stats_->live_location_updates;
}

void LiveLocationIndex::remove(
    lowir_model::ValueId value, const mir_model::MirOperand & location)
{
  std::vector<lowir_model::ValueId> * values = 0;
  if(location.kind == mir_model::MirOperand::OP_REG)
    values = &gpr_values_[location.reg];
  else if(location.kind == mir_model::MirOperand::OP_XMM)
    values = &xmm_values_[location.xmm];
  if(!values) return;
  const std::vector<lowir_model::ValueId>::iterator found =
    std::find(values->begin(), values->end(), value);
  if(found == values->end())
    throw std::logic_error("native live-location index is inconsistent");
  *found = values->back();
  values->pop_back();
  if(stats_) ++stats_->live_location_updates;
}

bool LiveLocationIndex::has_alias(
    lowir_model::ValueId, const mir_model::MirOperand & location,
    bool value_is_live) const
{
  if(stats_) ++stats_->live_location_alias_queries;
  const std::size_t self = value_is_live ? 1 : 0;
  if(location.kind == mir_model::MirOperand::OP_REG)
    return gpr_values_[location.reg].size() > self;
  if(location.kind == mir_model::MirOperand::OP_XMM)
    return xmm_values_[location.xmm].size() > self;
  return false;
}

const std::vector<lowir_model::ValueId> &
LiveLocationIndex::gpr_values(X64Register reg) const
{
  return gpr_values_[reg];
}

GeneratedFrameNames::GeneratedFrameNames(
    const lowir_model::LowirFunction & function) : function_(function)
{}

lowir_model::PresentationName GeneratedFrameNames::name(
    lowir_model::ValueId value)
{
  const lowir_model::PresentationName existing =
    lowir_model::lowir_value_presentation(function_, value);
  if(existing.valid()) return existing;
  if(!reservations_)
    reservations_.reset(new lowir_model::GeneratedNameReservations(
      function_.generated_name_reservations));
  return lowir_model::PresentationName::generated_value(
    reservations_->claim_first_available(lowir_model::GNR_GENERATED_VALUE));
}

bool xmm_destination_is_safe(
    const std::vector<ParallelXmmMove> & moves,
    std::size_t candidate)
{
  for(std::size_t i = 0; i < moves.size(); ++i) {
    if(i == candidate || !moves[i].pending ||
       moves[i].source.kind != mir_model::MirOperand::OP_XMM) continue;
    if(moves[i].source.xmm == moves[candidate].destination) return false;
  }
  return true;
}

bool managed_register(X64Register reg)
{
  return reg == XR_RDI || reg == XR_RSI || reg == XR_R8 || reg == XR_R9 ||
    allocation::is_callee_saved(reg);
}

namespace {

bool has_gpr_headroom(const allocation::RegisterPool & registers,
                      bool crosses_call)
{
  static const X64Register ordinary[] = {
    XR_R8, XR_R9, XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
  };
  static const X64Register preserved[] = {
    XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
  };
  const X64Register * choices = crosses_call ? preserved : ordinary;
  const std::size_t count = crosses_call ?
    sizeof(preserved) / sizeof(preserved[0]) :
    sizeof(ordinary) / sizeof(ordinary[0]);
  for(std::size_t i = 0; i < count; ++i)
    if(!registers.is_used(choices[i])) return true;
  return false;
}

bool has_xmm_headroom(const allocation::XmmPool & xmms)
{
  for(unsigned i = 0; i != 6; ++i)
    if(!xmms.is_used(static_cast<XmmRegister>(i))) return true;
  return false;
}

struct Candidate
{
  lowir_model::ValueId value;
  std::size_t definition;
  std::size_t end;
  bool crossing;
  bool is_phi;
  bool is_invariant;
  bool is_call_argument;
};

// A backedge span is unavoidable when no layout-forward edge jumps from
// before its header to after it — the same gate the phi pass uses.
std::vector<unsigned char> mark_unavoidable_spans(
    const std::vector<std::pair<std::size_t, std::size_t> > & spans,
    const std::vector<unsigned char> & span_is_backedge,
    const std::vector<std::pair<std::size_t, std::size_t> > & forward_edges)
{
  std::vector<unsigned char> unavoidable(spans.size(), 0);
  for(std::size_t span = 0; span < spans.size(); ++span) {
    if(!span_is_backedge[span]) continue;
    bool bypassed = false;
    for(std::size_t edge = 0; edge < forward_edges.size(); ++edge)
      if(forward_edges[edge].first < spans[span].first &&
         forward_edges[edge].second > spans[span].first) {
        bypassed = true;
        break;
      }
    unavoidable[span] = bypassed ? 0 : 1;
  }
  return unavoidable;
}

// A loop-invariant value qualifies for the invariant pass when its use
// range overlaps an unavoidable loop: it is reloaded from its frame home
// every iteration there (E6's invariant bases).  Bypassable loops keep the
// exclusion — same ceremony reasoning as the phi gate, and the P25b probe
// measured the ungated lift as negative.
bool uses_overlap_unavoidable_span(
    const std::vector<std::pair<std::size_t, std::size_t> > & spans,
    const std::vector<unsigned char> & span_unavoidable,
    std::size_t first_use, std::size_t last_use)
{
  for(std::size_t span = 0; span < spans.size(); ++span)
    if(span_unavoidable[span] && spans[span].first <= last_use &&
       first_use <= spans[span].second)
      return true;
  return false;
}

// A phi interval spans positions the per-value clobber mask never covered
// (transfers before the linearized definition, span extension past the last
// use), so phi claims query the clobber index over the exact interval.
bool clobbered_in_interval(const analysis::FunctionFacts & facts,
                           X64Register reg,
                           std::size_t start, std::size_t end)
{
  const std::size_t index = static_cast<std::size_t>(reg);
  if(index >= facts.clobber_positions.size()) return true;
  const std::vector<std::size_t> & positions = facts.clobber_positions[index];
  const std::vector<std::size_t>::const_iterator clobber =
    std::lower_bound(positions.begin(), positions.end(), start);
  return clobber != positions.end() && *clobber <= end;
}

// A candidate fits a pool register when its interval overlaps no claimed
// span.  The weighted crossing pass assigns out of definition order, so
// the callee-saved pool tracks exact claimed spans instead of a busy-until
// watermark (which is only sound for definition-ordered sweeps).
bool span_is_free(
    const std::vector<std::pair<std::size_t, std::size_t> > & spans,
    std::size_t start, std::size_t end)
{
  for(std::size_t i = 0; i < spans.size(); ++i)
    if(spans[i].first <= end && start <= spans[i].second) return false;
  return true;
}

void assign_candidate_location(const Candidate & candidate, X64Register reg,
                               FunctionLocationTimeline * timeline)
{
  const std::size_t value = static_cast<std::uint32_t>(candidate.value);
  // A phi home is reserved before the walk and receives predecessor
  // transfers before its linear definition, so its occupancy begins at
  // function entry.  Ordinary segments begin at their definition.
  const std::size_t begin = candidate.is_phi ? 0 : candidate.definition;
  (*timeline)[value].push_back(PlannedLocationSegment(
    begin, candidate.end, PLK_GPR, static_cast<unsigned>(reg)));
}

// The reactive pool prefers RBX, R12, R13 in that order, so the planner
// claims from the opposite end; the pools only meet under real pressure.
// R14 and R15 extend coverage after the original three so earlier plans
// keep their registers.  Non-crossing call-free intervals ride the
// caller-saved pair instead of competing for preserved registers.
// Phis claim first: a phi home is reserved at construction time, before
// the walk, so its register is occupied from function entry to interval
// end and cannot be shared with a second phi (the pool rejects nested
// reservations).  Phis claim callee-saved only: routing call-free phi
// intervals through R9/R8 measured WORSE (44.93B vs 44.82B honest Ir) —
// starving the reactive pool's first choices pushes every loop temporary
// into fresh callee-saved registers instead.  Ordinary candidates may
// still follow a phi on the same register after its interval releases.
// Crossing candidates assign in descending use-count order: the pool is
// smaller than the demand in call-dense bodies, and first-fit by
// definition position hands the registers to whatever is defined first
// rather than to the values whose reloads dominate the frame traffic.
void assign_candidate_registers(
    const std::vector<Candidate> & candidates,
    const analysis::FunctionFacts & facts,
    FunctionLocationTimeline * timeline,
    std::vector<std::pair<std::size_t, std::size_t> > * register_spans,
    Stats * stats)
{
  static const X64Register kPool[] =
    {XR_R13, XR_R12, XR_RBX, XR_R14, XR_R15};
  static const X64Register kCallerPool[] = {XR_R9, XR_R8};
  std::vector<std::pair<std::size_t, std::size_t> >
    claimed[sizeof(kPool) / sizeof(kPool[0])];
  std::size_t caller_busy_until[sizeof(kCallerPool) /
                                sizeof(kCallerPool[0])] = {0};
  bool phi_claimed[sizeof(kPool) / sizeof(kPool[0])] = {false};
  // Phis iterate the pool from the far end (R15 first): the ordinary pass
  // claims R13-first and the reactive pool RBX-first, so far-end claims
  // displace the fewest other residents.
  for(std::size_t i = 0; i < candidates.size(); ++i) {
    const Candidate & candidate = candidates[i];
    if(!candidate.is_phi) continue;
    for(std::size_t reg = sizeof(kPool) / sizeof(kPool[0]); reg-- > 0;) {
      if(facts.has_i128_atomic && kPool[reg] == XR_RBX) continue;
      if(phi_claimed[reg]) continue;
      if(clobbered_in_interval(facts, kPool[reg],
                               candidate.definition, candidate.end))
        continue;
      phi_claimed[reg] = true;
      // The phi's register is reserved at construction time, so it is
      // occupied from function entry, not from the transfer position.
      claimed[reg].push_back(std::make_pair(
        static_cast<std::size_t>(0), candidate.end));
      assign_candidate_location(candidate, kPool[reg], timeline);
      if(stats) {
        ++stats->planned_value_registers;
        ++stats->planned_phi_registers;
      }
      break;
    }
  }
  // Loop-invariant candidates claim next, also from the far end: their
  // grants happen at definition through the ordinary planned machinery, so
  // they share registers by interval like any candidate, but far-end
  // claiming keeps them off the ordinary pass's R13-first choices.
  for(std::size_t i = 0; i < candidates.size(); ++i) {
    const Candidate & candidate = candidates[i];
    if(!candidate.is_invariant) continue;
    for(std::size_t reg = sizeof(kPool) / sizeof(kPool[0]); reg-- > 0;) {
      if(facts.has_i128_atomic && kPool[reg] == XR_RBX) continue;
      if(!span_is_free(claimed[reg], candidate.definition, candidate.end))
        continue;
      if(clobbered_in_interval(facts, kPool[reg],
                               candidate.definition, candidate.end))
        continue;
      claimed[reg].push_back(
        std::make_pair(candidate.definition, candidate.end));
      assign_candidate_location(candidate, kPool[reg], timeline);
      if(stats) {
        ++stats->planned_value_registers;
        ++stats->planned_invariant_registers;
      }
      break;
    }
  }
  std::vector<std::size_t> crossing_order;
  for(std::size_t i = 0; i < candidates.size(); ++i) {
    const Candidate & candidate = candidates[i];
    if(candidate.is_phi || candidate.is_invariant) continue;
    if(candidate.crossing) {
      crossing_order.push_back(i);
      continue;
    }
    const unsigned crossed = facts.live_across_clobbers[
      static_cast<std::uint32_t>(candidate.value)];
    bool assigned = false;
    for(std::size_t reg = 0;
        reg < sizeof(kCallerPool) / sizeof(kCallerPool[0]); ++reg) {
      if(caller_busy_until[reg] > candidate.definition) continue;
      if(crossed & analysis::register_mask(kCallerPool[reg])) continue;
      caller_busy_until[reg] = candidate.end + 1;
      assign_candidate_location(candidate, kCallerPool[reg], timeline);
      if(stats) {
        ++stats->planned_value_registers;
        if(candidate.is_call_argument)
          ++stats->planner_assigned_call_arguments;
      }
      assigned = true;
      break;
    }
    if(!assigned && stats) {
      ++stats->planner_assign_failures;
      if(candidate.is_call_argument)
        ++stats->planner_assign_failures_call_argument;
    }
  }
  std::sort(crossing_order.begin(), crossing_order.end(),
            [&candidates, &facts](std::size_t left, std::size_t right) {
              const Candidate & a = candidates[left];
              const Candidate & b = candidates[right];
              const std::size_t a_uses =
                facts.uses[static_cast<std::uint32_t>(a.value)];
              const std::size_t b_uses =
                facts.uses[static_cast<std::uint32_t>(b.value)];
              if(a_uses != b_uses) return a_uses > b_uses;
              if(a.definition != b.definition)
                return a.definition < b.definition;
              return static_cast<std::uint32_t>(a.value) <
                static_cast<std::uint32_t>(b.value);
            });
  for(std::size_t i = 0; i < crossing_order.size(); ++i) {
    const Candidate & candidate = candidates[crossing_order[i]];
    const unsigned crossed = facts.live_across_clobbers[
      static_cast<std::uint32_t>(candidate.value)];
    bool assigned = false;
    for(std::size_t reg = 0; reg < sizeof(kPool) / sizeof(kPool[0]); ++reg) {
      if(facts.has_i128_atomic && kPool[reg] == XR_RBX) continue;
      if(!span_is_free(claimed[reg], candidate.definition, candidate.end))
        continue;
      if(crossed & analysis::register_mask(kPool[reg])) continue;
      claimed[reg].push_back(
        std::make_pair(candidate.definition, candidate.end));
      assign_candidate_location(candidate, kPool[reg], timeline);
      if(stats) {
        ++stats->planned_value_registers;
        if(candidate.is_call_argument)
          ++stats->planner_assigned_call_arguments;
      }
      assigned = true;
      break;
    }
    if(!assigned && stats) {
      ++stats->planner_assign_failures;
      if(candidate.is_call_argument)
        ++stats->planner_assign_failures_call_argument;
    }
  }
  for(std::size_t reg = 0; reg < sizeof(kPool) / sizeof(kPool[0]); ++reg)
    register_spans[static_cast<unsigned>(kPool[reg])] = claimed[reg];
}

struct LayoutScan
{
  std::vector<std::size_t> phi_transfer_start;
  std::vector<unsigned char> phi_loop_carried;
  std::vector<std::size_t> phi_header_start;
  std::vector<std::pair<std::size_t, std::size_t> > spans;
  std::vector<unsigned char> span_is_backedge;
  std::vector<std::pair<std::size_t, std::size_t> > forward_edges;
};

// One pass over the layout: phi transfer starts and loop-carried marks
// (a phi's register is written at every predecessor terminator, so its
// occupancy starts at the earliest transfer; only loop-carried phis
// qualify — merge phis measured as a net dynamic regression), backedge
// and exception-region spans (parallel span_is_backedge distinguishes
// them: interval extension uses both, the invariant gate wants loops
// only), and layout-forward edges — the bypass evidence for the
// unavoidable-header gate.
LayoutScan scan_function_layout(
    const lowir_model::LowirFunction & function,
    const std::vector<std::size_t> & block_start,
    const std::vector<std::size_t> & block_by_id,
    std::size_t function_end)
{
  using analysis::FunctionFacts;
  LayoutScan scan;
  scan.phi_transfer_start.assign(
    function.value_names.size(), FunctionFacts::missing_position());
  scan.phi_loop_carried.assign(function.value_names.size(), 0);
  scan.phi_header_start.assign(
    function.value_names.size(), FunctionFacts::missing_position());
  std::vector<std::size_t> & phi_transfer_start = scan.phi_transfer_start;
  std::vector<unsigned char> & phi_loop_carried = scan.phi_loop_carried;
  std::vector<std::size_t> & phi_header_start = scan.phi_header_start;
  std::vector<std::pair<std::size_t, std::size_t> > & spans = scan.spans;
  std::vector<unsigned char> & span_is_backedge = scan.span_is_backedge;
  std::vector<std::pair<std::size_t, std::size_t> > & forward_edges =
    scan.forward_edges;
  std::vector<std::pair<std::size_t, std::size_t> > open_regions;
  std::size_t position = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::size_t end_of_block =
      block_start[block] + function.blocks[block].instructions.size();
    for(std::size_t i = 0;
        i < function.blocks[block].instructions.size(); ++i, ++position) {
      const lowir_model::Instruction & ins =
        function.blocks[block].instructions[i];
      if(ins.kind == lowir_model::Instruction::IK_PHI) {
        if(!ins.dest.valid()) continue;
        std::size_t & start = phi_transfer_start[ins.dest];
        start = position;
        phi_header_start[ins.dest] = block_start[block];
        for(std::size_t incoming = 0;
            incoming + 1 < ins.args.size(); incoming += 2) {
          if(ins.args[incoming].kind != lowir_model::Operand::OP_LABEL)
            continue;
          const std::uint32_t id = ins.args[incoming].block;
          const std::size_t predecessor = id < block_by_id.size() ?
            block_by_id[id] : function.blocks.size();
          if(predecessor >= function.blocks.size() ||
             function.blocks[predecessor].instructions.empty()) continue;
          const std::size_t terminator = block_start[predecessor] +
            function.blocks[predecessor].instructions.size() - 1;
          start = std::min(start, terminator);
          if(terminator >= position) phi_loop_carried[ins.dest] = 1;
        }
        continue;
      }
      if(ins.kind == lowir_model::Instruction::IK_EH_TRY ||
         ins.kind == lowir_model::Instruction::IK_EH_CLEANUP) {
        std::size_t pad_start = function_end;
        if(ins.first.kind == lowir_model::Operand::OP_LABEL) {
          const std::uint32_t id = ins.first.block;
          const std::size_t target = id < block_by_id.size() ?
            block_by_id[id] : function.blocks.size();
          if(target < function.blocks.size())
            pad_start = block_start[target];
        }
        open_regions.push_back(std::make_pair(pad_start, position));
        continue;
      }
      if(ins.kind == lowir_model::Instruction::IK_EH_END) {
        if(!open_regions.empty()) {
          const std::pair<std::size_t, std::size_t> region =
            open_regions.back();
          open_regions.pop_back();
          if(region.first < position) {
            spans.push_back(std::make_pair(region.first, position));
            span_is_backedge.push_back(0);
          }
        }
        continue;
      }
      if(ins.kind != lowir_model::Instruction::IK_JUMP &&
         ins.kind != lowir_model::Instruction::IK_BRANCH &&
         ins.kind != lowir_model::Instruction::IK_SWITCH) continue;
      const lowir_model::Operand * fixed[] =
        {&ins.first, &ins.second, &ins.third};
      for(std::size_t operand = 0;
          operand < sizeof(fixed) / sizeof(fixed[0]) + ins.args.size();
          ++operand) {
        const lowir_model::Operand & label =
          operand < sizeof(fixed) / sizeof(fixed[0]) ?
          *fixed[operand] :
          ins.args[operand - sizeof(fixed) / sizeof(fixed[0])];
        if(label.kind != lowir_model::Operand::OP_LABEL) continue;
        const std::uint32_t id = label.block;
        const std::size_t target =
          id < block_by_id.size() ? block_by_id[id] : function.blocks.size();
        if(target < function.blocks.size() && target <= block) {
          spans.push_back(std::make_pair(block_start[target], end_of_block));
          span_is_backedge.push_back(1);
        } else if(target < function.blocks.size())
          forward_edges.push_back(
            std::make_pair(position, block_start[target]));
      }
    }
  }
  // An unterminated region conservatively spans to the end.
  for(std::size_t i = 0; i < open_regions.size(); ++i)
    if(open_regions[i].first < function_end) {
      spans.push_back(std::make_pair(open_regions[i].first, function_end));
      span_is_backedge.push_back(0);
    }
  return scan;
}

}  // namespace

// Layout backedge spans mirror `ControlFlowQueries` spill safety: a
// backward jump from position e to a block starting at position s means
// [s, e] may re-execute.  Exception regions add the same hazard: unwinding
// from a protected call to a landing pad laid out earlier is a backward
// edge, so a region whose pad starts before the region's end contributes
// the span [pad start, region end].  An edge-live interval extends to the
// end of every span it overlaps, to a fixed point.
FunctionLocationTimeline plan_value_locations(
    const lowir_model::LowirFunction & function,
    const analysis::FunctionFacts & facts,
    int optimization_level,
    std::vector<std::pair<std::size_t, std::size_t> > * register_spans,
    std::vector<std::pair<std::size_t, std::size_t> > * extension_spans,
    Stats * stats)
{
  using analysis::FunctionFacts;
  FunctionLocationTimeline timeline(function.value_names.size());
  for(std::size_t reg = 0; reg < 16; ++reg) register_spans[reg].clear();
  extension_spans->clear();
  if(optimization_level < 1 || facts.has_va_start || facts.has_dynamic_stack)
    return timeline;

  std::vector<std::size_t> block_start(function.blocks.size(), 0);
  std::vector<std::size_t> block_by_id;
  std::size_t position = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    block_start[block] = position;
    position += function.blocks[block].instructions.size();
    const std::uint32_t id = function.blocks[block].id;
    if(block_by_id.size() <= id)
      block_by_id.resize(static_cast<std::size_t>(id) + 1,
                         function.blocks.size());
    block_by_id[id] = block;
  }
  const std::size_t function_end = position;
  LayoutScan scan = scan_function_layout(function, block_start, block_by_id,
                                         function_end);
  *extension_spans = scan.spans;
  std::vector<std::size_t> & phi_transfer_start = scan.phi_transfer_start;
  std::vector<unsigned char> & phi_loop_carried = scan.phi_loop_carried;
  std::vector<std::size_t> & phi_header_start = scan.phi_header_start;
  std::vector<std::pair<std::size_t, std::size_t> > & spans = scan.spans;
  std::vector<std::pair<std::size_t, std::size_t> > & forward_edges =
    scan.forward_edges;
  const std::vector<unsigned char> span_unavoidable =
    mark_unavoidable_spans(spans, scan.span_is_backedge, forward_edges);

  std::vector<Candidate> candidates;
  for(std::size_t raw = 0; raw < function.value_names.size(); ++raw) {
    const lowir_model::ValueId value(static_cast<std::uint32_t>(raw));
    if(facts.has(value, FunctionFacts::VF_ADDRESS_REMATERIALIZE_SAFE)) {
      timeline[raw].push_back(PlannedLocationSegment(
        facts.definition[raw], facts.last_use[raw], PLK_REMATERIALIZE));
      if(stats) ++stats->planned_rematerialized_addresses;
      continue;
    }
    bool is_phi = phi_loop_carried[raw] != 0;
    // The unavoidable-header gate: claim a register only when no
    // layout-forward edge jumps from before the phi's header to after it.
    // A jumped-over loop may be dynamically cold while the function stays
    // hot, and then the claimed callee-saved register's prologue/epilogue
    // ceremony taxes every call for a loop that rarely runs (measured:
    // IsIdentifierBody's guarded Annex-E search, +19% dynamic
    // instructions).  Early returns before the header do not suppress:
    // that arm measured as forgone wins, not avoided losses.
    if(is_phi) {
      const std::size_t header = phi_header_start[raw];
      for(std::size_t edge = 0;
          edge < forward_edges.size(); ++edge)
        if(forward_edges[edge].first < header &&
           forward_edges[edge].second > header) {
          is_phi = false;
          break;
        }
    }
    // A merge phi is written before its linearized definition, so it can
    // never ride the ordinary interval model; only loop-carried phis are
    // candidates, and only through the phi pass.
    if(phi_transfer_start[raw] != FunctionFacts::missing_position() &&
       !is_phi)
      continue;
    if(facts.definition[raw] == FunctionFacts::missing_position() ||
       facts.last_use[raw] == FunctionFacts::missing_position() ||
       facts.uses[raw] < 1)
      continue;
    const bool is_invariant = !is_phi &&
      facts.has(value, FunctionFacts::VF_LOOP_INVARIANT) &&
      facts.first_use[raw] != FunctionFacts::missing_position() &&
      uses_overlap_unavoidable_span(spans, span_unavoidable,
                                    facts.first_use[raw],
                                    facts.last_use[raw]);
    // A register-resident phi or invariant base turns every
    // storage-address use into a direct [reg] operand, so the
    // storage-address exclusion does not apply to them.  A call-crossing
    // value used only as a call argument is a candidate too: without a
    // planned callee-saved home the walk demotes it to the frame at
    // definition (edge-live stabilization, EH functions retain nothing)
    // and reloads it at the consuming call's argument staging — the
    // dominant class of call-boundary frame loads on the frozen TU.
    // Non-crossing call arguments keep the exclusion: they ride the
    // reactive caller-saved pool to their call already.
    if(facts.has(value, FunctionFacts::VF_PARAMETER) ||
       (!is_invariant &&
        facts.has(value, FunctionFacts::VF_LOOP_INVARIANT)) ||
       (facts.has(value, FunctionFacts::VF_ONLY_CALL_ARGUMENT) &&
        !facts.has(value, FunctionFacts::VF_LIVE_ACROSS_CALL)) ||
       (!is_phi && !is_invariant &&
        facts.has(value, FunctionFacts::VF_ONLY_STORAGE_ADDRESS)))
      continue;
    // Non-crossing values ride caller-saved registers over call-free
    // intervals.  Unwinding only leaves a call, so a call-free interval is
    // never live into a landing pad, even in exception-bearing functions.
    const lowir_model::LowType & type =
      lowir_model::lowir_value_type(function, value);
    if(type.kind != lowir_model::LTK_PTR &&
       type.kind != lowir_model::LTK_I1 &&
       type.kind != lowir_model::LTK_I8 &&
       type.kind != lowir_model::LTK_U8 &&
       type.kind != lowir_model::LTK_I16 &&
       type.kind != lowir_model::LTK_U16 &&
       type.kind != lowir_model::LTK_I32 &&
       type.kind != lowir_model::LTK_U32 &&
       type.kind != lowir_model::LTK_I64)
      continue;
    Candidate candidate;
    candidate.value = value;
    candidate.definition = is_phi ?
      std::min(facts.definition[raw], phi_transfer_start[raw]) :
      facts.definition[raw];
    candidate.end = facts.last_use[raw];
    candidate.crossing = facts.has(value, FunctionFacts::VF_LIVE_ACROSS_CALL);
    candidate.is_phi = is_phi;
    candidate.is_invariant = is_invariant;
    candidate.is_call_argument =
      facts.has(value, FunctionFacts::VF_ONLY_CALL_ARGUMENT);
    if(stats && candidate.is_call_argument)
      ++stats->planner_candidate_call_arguments;
    // Phis are edge-live by construction (written at predecessor
    // terminators), whether or not any read crosses a block boundary.
    if(is_phi || facts.has(value, FunctionFacts::VF_EDGE_LIVE)) {
      bool grew = true;
      while(grew) {
        grew = false;
        for(std::size_t span = 0; span < spans.size(); ++span)
          if(spans[span].first <= candidate.end &&
             candidate.end < spans[span].second) {
            candidate.end = spans[span].second;
            grew = true;
          }
      }
    }
    // The extended interval may have grown over a call the counted uses
    // never crossed; a caller-saved resident cannot survive that.  Phis
    // and invariants ride the callee-saved pool, so a call-spanning
    // interval is fine for them.
    if(!is_phi && !is_invariant && !candidate.crossing) {
      const std::vector<std::size_t>::const_iterator call =
        std::lower_bound(facts.calls.begin(), facts.calls.end(),
                         candidate.definition);
      if(call != facts.calls.end() && *call <= candidate.end) continue;
    }
    candidates.push_back(candidate);
  }
  if(candidates.empty()) return timeline;
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate & left, const Candidate & right) {
              return left.definition < right.definition ||
                (left.definition == right.definition &&
                 static_cast<std::uint32_t>(left.value) <
                   static_cast<std::uint32_t>(right.value));
            });
  assign_candidate_registers(candidates, facts, &timeline,
                             register_spans, stats);
  return timeline;
}

bool should_retain_edge_register(
    const mir_model::MirOperand & location,
    int optimization_level,
    bool function_has_eh,
    bool loop_carried,
    bool crosses_call,
    bool has_narrow_alias,
    bool crosses_fixed_register_clobber,
    const allocation::RegisterPool & registers,
    const allocation::XmmPool & xmms)
{
  if(optimization_level < 1 || function_has_eh) return false;

  // A retained interval keeps a definition-time frame fallback.  Cycles also
  // keep one register free because a later reactive eviction cannot rewrite
  // instructions that execute again after a backedge.
  if(location.kind == mir_model::MirOperand::OP_REG)
    return !has_narrow_alias && !crosses_fixed_register_clobber &&
      (!crosses_call || allocation::is_callee_saved(location.reg)) &&
      (!loop_carried || has_gpr_headroom(registers, crosses_call));

  if(location.kind == mir_model::MirOperand::OP_XMM)
    return !crosses_call &&
      (!loop_carried || has_xmm_headroom(xmms));

  return false;
}

void record_edge_staging(Stats * stats,
    lowir_model::Instruction::Kind instruction_kind,
    lowir_model::Operand::Kind first_kind,
    lowir_model::Operand::Kind second_kind,
    mir_model::MirOperand::Kind location_kind, bool function_has_eh,
    bool loop_invariant, bool crosses_call, bool narrow_alias,
    bool fixed_clobber, std::size_t remaining_uses)
{
  if(!stats) return;
  ++stats->edge_staging_total;
  ++stats->edge_staging_by_kind[instruction_kind];
  if(location_kind == mir_model::MirOperand::OP_REG)
    ++stats->edge_staging_gpr;
  else if(location_kind == mir_model::MirOperand::OP_XMM)
    ++stats->edge_staging_xmm;
  if(function_has_eh) ++stats->edge_staging_eh;
  if(loop_invariant) ++stats->edge_staging_loop_invariant;
  if(crosses_call) ++stats->edge_staging_crosses_call;
  if(narrow_alias) ++stats->edge_staging_narrow_alias;
  if(fixed_clobber) ++stats->edge_staging_fixed_clobber;
  if(remaining_uses == 1) ++stats->edge_staging_single_use;
  else ++stats->edge_staging_multi_use;
  if(instruction_kind == lowir_model::Instruction::IK_ADDR) {
    if(first_kind == lowir_model::Operand::OP_SLOT)
      ++stats->edge_staging_addr_slot;
    else if(first_kind == lowir_model::Operand::OP_GLOBAL)
      ++stats->edge_staging_addr_global;
    else ++stats->edge_staging_addr_other;
  }
  if(instruction_kind == lowir_model::Instruction::IK_INDEX) {
    if(second_kind == lowir_model::Operand::OP_INTEGER)
      ++stats->edge_staging_index_constant;
    else ++stats->edge_staging_index_variable;
  }
}

std::string diagnostic_value_name(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    lowir_model::ValueId value)
{
  if(lowir_model::lowir_value_presentation(function, value).valid())
    return lowir_model::lowir_value_name(program.strings, function, value);
  return "%<internal-" +
    std::to_string(static_cast<std::uint32_t>(value)) + ">";
}

}  // namespace location_planning
}  // namespace lowir_native
