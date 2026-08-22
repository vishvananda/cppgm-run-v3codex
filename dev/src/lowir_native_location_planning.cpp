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

}  // namespace

// Layout backedge spans mirror `ControlFlowQueries` spill safety: a
// backward jump from position e to a block starting at position s means
// [s, e] may re-execute.  Exception regions add the same hazard: unwinding
// from a protected call to a landing pad laid out earlier is a backward
// edge, so a region whose pad starts before the region's end contributes
// the span [pad start, region end].  An edge-live interval extends to the
// end of every span it overlaps, to a fixed point.
std::vector<unsigned char> plan_value_registers(
    const lowir_model::LowirFunction & function,
    const analysis::FunctionFacts & facts,
    int optimization_level,
    std::vector<std::size_t> * plan_ends,
    Stats * stats)
{
  using analysis::FunctionFacts;
  std::vector<unsigned char> plan(function.value_names.size(), 0);
  plan_ends->assign(function.value_names.size(), 0);
  if(optimization_level < 1 || facts.has_va_start || facts.has_dynamic_stack)
    return plan;

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
  std::vector<std::pair<std::size_t, std::size_t> > spans;
  std::vector<std::pair<std::size_t, std::size_t> > open_regions;
  position = 0;
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const std::size_t end_of_block =
      block_start[block] + function.blocks[block].instructions.size();
    for(std::size_t i = 0;
        i < function.blocks[block].instructions.size(); ++i, ++position) {
      const lowir_model::Instruction & ins =
        function.blocks[block].instructions[i];
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
          if(region.first < position)
            spans.push_back(std::make_pair(region.first, position));
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
        if(target < function.blocks.size() && target <= block)
          spans.push_back(std::make_pair(block_start[target], end_of_block));
      }
    }
  }
  // An unterminated region conservatively spans to the end.
  for(std::size_t i = 0; i < open_regions.size(); ++i)
    if(open_regions[i].first < function_end)
      spans.push_back(std::make_pair(open_regions[i].first, function_end));

  struct Candidate
  {
    lowir_model::ValueId value;
    std::size_t definition;
    std::size_t end;
    bool crossing;
  };
  std::vector<Candidate> candidates;
  for(std::size_t raw = 0; raw < function.value_names.size(); ++raw) {
    const lowir_model::ValueId value(static_cast<std::uint32_t>(raw));
    if(facts.definition[raw] == FunctionFacts::missing_position() ||
       facts.last_use[raw] == FunctionFacts::missing_position() ||
       facts.uses[raw] < 1 ||
       facts.has(value, FunctionFacts::VF_PARAMETER) ||
       facts.has(value, FunctionFacts::VF_LOOP_INVARIANT) ||
       facts.has(value, FunctionFacts::VF_ONLY_CALL_ARGUMENT) ||
       facts.has(value, FunctionFacts::VF_ONLY_STORAGE_ADDRESS))
      continue;
    // Non-crossing values ride caller-saved registers, so their intervals
    // must stay call-free; landing pads never preserve them, so functions
    // with exception regions keep the crossing class only.
    if(!facts.has(value, FunctionFacts::VF_LIVE_ACROSS_CALL) &&
       facts.has_eh)
      continue;
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
    candidate.definition = facts.definition[raw];
    candidate.end = facts.last_use[raw];
    candidate.crossing = facts.has(value, FunctionFacts::VF_LIVE_ACROSS_CALL);
    if(facts.has(value, FunctionFacts::VF_EDGE_LIVE)) {
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
    // never crossed; a caller-saved resident cannot survive that.
    if(!candidate.crossing) {
      const std::vector<std::size_t>::const_iterator call =
        std::lower_bound(facts.calls.begin(), facts.calls.end(),
                         candidate.definition);
      if(call != facts.calls.end() && *call <= candidate.end) continue;
    }
    candidates.push_back(candidate);
  }
  if(candidates.empty()) return plan;
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate & left, const Candidate & right) {
              return left.definition < right.definition ||
                (left.definition == right.definition &&
                 static_cast<std::uint32_t>(left.value) <
                   static_cast<std::uint32_t>(right.value));
            });

  // The reactive pool prefers RBX, R12, R13 in that order, so the planner
  // claims from the opposite end; the pools only meet under real pressure.
  // R14 and R15 extend coverage after the original three so earlier plans
  // keep their registers.  Non-crossing call-free intervals ride the
  // caller-saved pair instead of competing for preserved registers.
  static const X64Register kPool[] =
    {XR_R13, XR_R12, XR_RBX, XR_R14, XR_R15};
  static const X64Register kCallerPool[] = {XR_R9, XR_R8};
  std::size_t busy_until[sizeof(kPool) / sizeof(kPool[0])] = {0};
  std::size_t caller_busy_until[sizeof(kCallerPool) /
                                sizeof(kCallerPool[0])] = {0};
  for(std::size_t i = 0; i < candidates.size(); ++i) {
    const Candidate & candidate = candidates[i];
    const unsigned crossed = facts.live_across_clobbers[
      static_cast<std::uint32_t>(candidate.value)];
    const X64Register * pool = candidate.crossing ? kPool : kCallerPool;
    std::size_t * busy = candidate.crossing ? busy_until : caller_busy_until;
    const std::size_t pool_size = candidate.crossing ?
      sizeof(kPool) / sizeof(kPool[0]) :
      sizeof(kCallerPool) / sizeof(kCallerPool[0]);
    for(std::size_t reg = 0; reg < pool_size; ++reg) {
      if(facts.has_i128_atomic && pool[reg] == XR_RBX) continue;
      if(busy[reg] > candidate.definition) continue;
      if(crossed & analysis::register_mask(pool[reg])) continue;
      busy[reg] = candidate.end + 1;
      plan[static_cast<std::uint32_t>(candidate.value)] =
        static_cast<unsigned char>(pool[reg]) + 1;
      (*plan_ends)[static_cast<std::uint32_t>(candidate.value)] =
        candidate.end;
      if(stats) ++stats->planned_value_registers;
      break;
    }
  }
  return plan;
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
