#include "lowir_native_location_planning.h"

#include "lowir_native.h"

#include <algorithm>
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
  if(optimization_level < 2 || function_has_eh) return false;

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
