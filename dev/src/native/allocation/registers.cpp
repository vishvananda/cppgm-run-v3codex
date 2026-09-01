#include "native/allocation/registers.h"
#include "native/errors.h"

#include <algorithm>
#include <limits>

namespace lowir_native {
namespace allocation {

bool is_callee_saved(X64Register reg)
{
  return reg == XR_RBX || reg == XR_R12 || reg == XR_R13 ||
         reg == XR_R14 || reg == XR_R15;
}

AllocationDecisionLog::AllocationDecisionLog()
  : cursor_(0), position_(0),
    value_(std::numeric_limits<std::uint32_t>::max()), replaying_(false) {}

void AllocationDecisionLog::set_context(std::size_t position,
                                        std::uint32_t value)
{
  position_ = position;
  value_ = value;
}

AllocationDecision AllocationDecisionLog::resolve(
    AllocationDecisionOperation operation, unsigned requested_register,
    unsigned selected_register, bool across_call, bool success)
{
  if(!replaying_) {
    AllocationDecision decision;
    decision.operation = operation;
    decision.position = position_;
    decision.value = value_;
    decision.requested_register = requested_register;
    decision.selected_register = selected_register;
    decision.across_call = across_call;
    decision.success = success;
    decisions_.push_back(decision);
    return decision;
  }
  if(cursor_ == decisions_.size())
    native_errors::ThrowInternal("allocation decision replay exhausted");
  const AllocationDecision decision = decisions_[cursor_++];
  if(decision.operation != operation || decision.position != position_ ||
     decision.value != value_ ||
     decision.requested_register != requested_register ||
     decision.across_call != across_call || decision.success != success ||
     (success && decision.selected_register != selected_register))
    native_errors::ThrowInternal("allocation decision replay diverged");
  return decision;
}

void AllocationDecisionLog::begin_replay()
{
  cursor_ = 0;
  position_ = 0;
  value_ = std::numeric_limits<std::uint32_t>::max();
  replaying_ = true;
}

void AllocationDecisionLog::finish_replay() const
{
  if(!replaying_ || cursor_ != decisions_.size())
    native_errors::ThrowInternal("allocation decision replay incomplete");
}

RegisterPool::RegisterPool(AllocationDecisionLog * decisions)
  : decisions_(decisions)
{
  std::fill(used_, used_ + 16, false);
  std::fill(plan_held_, plan_held_ + 16, false);
  std::fill(reservation_count_, reservation_count_ + 16, 0U);
}

void RegisterPool::reserve_raw(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  used_[index] = true;
  plan_held_[index] = false;
  ++reservation_count_[index];
  remember_preserve(reg);
}

void RegisterPool::reserve(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  if(used_[index]) native_errors::ThrowInternal("MIR register allocation conflict");
  if(decisions_) decisions_->resolve(
    ADO_GPR_RESERVE, index, index, false, true);
  reserve_raw(reg);
}

void RegisterPool::hold_for_plan(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  if(decisions_) decisions_->resolve(
    ADO_GPR_HOLD_FOR_PLAN, index, index, false, true);
  plan_held_[index] = true;
}

bool RegisterPool::plan_held(X64Register reg) const
{
  return plan_held_[static_cast<unsigned>(reg)];
}

bool RegisterPool::try_reserve(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  const bool success = !used_[index];
  const AllocationDecision decision = decisions_ ? decisions_->resolve(
    ADO_GPR_TRY_RESERVE, index, index, false, success) :
    AllocationDecision();
  const bool resolved = decisions_ ? decision.success : success;
  if(resolved) reserve_raw(reg);
  return resolved;
}

bool RegisterPool::is_used(X64Register reg) const
{
  return used_[static_cast<unsigned>(reg)];
}

X64Register RegisterPool::allocate(bool across_call)
{
  X64Register result = XR_RSP;
  const bool success = choose(across_call, &result);
  const AllocationDecision decision = decisions_ ? decisions_->resolve(
    ADO_GPR_ALLOCATE, 16, static_cast<unsigned>(result), across_call,
    success) : AllocationDecision();
  if(decisions_ && decision.success)
    result = static_cast<X64Register>(decision.selected_register);
  if(!success) native_errors::ThrowResourceLimit(
    "foundation register pool exhausted");
  reserve_raw(result);
  return result;
}

bool RegisterPool::choose(bool across_call, X64Register * result) const
{
  static const X64Register ordinary[] = {
    XR_R8, XR_R9, XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
  };
  static const X64Register preserved[] = {
    XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
  };
  const X64Register * choices = across_call ? preserved : ordinary;
  const std::size_t count = across_call ?
    sizeof(preserved) / sizeof(preserved[0]) :
    sizeof(ordinary) / sizeof(ordinary[0]);
  for(std::size_t pass = 0; pass < 2; ++pass)
    for(std::size_t i = 0; i < count; ++i) {
      const unsigned index = static_cast<unsigned>(choices[i]);
      if(used_[index] || (pass == 0 && plan_held_[index])) continue;
      *result = choices[i];
      return true;
    }
  return false;
}

bool RegisterPool::try_allocate(bool across_call, X64Register & result)
{
  const bool success = choose(across_call, &result);
  const AllocationDecision decision = decisions_ ? decisions_->resolve(
    ADO_GPR_TRY_ALLOCATE, 16, static_cast<unsigned>(result), across_call,
    success) : AllocationDecision();
  const bool resolved = decisions_ ? decision.success : success;
  if(!resolved) return false;
  if(decisions_) result = static_cast<X64Register>(decision.selected_register);
  reserve_raw(result);
  return true;
}

void RegisterPool::release(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  if(decisions_) decisions_->resolve(
    ADO_GPR_RELEASE, index, index, false, true);
  if(reg == XR_RDI || reg == XR_RSI || reg == XR_R8 || reg == XR_R9 ||
     is_callee_saved(reg)) used_[index] = false;
}

void RegisterPool::discard_unused_reservation(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  if(decisions_) decisions_->resolve(
    ADO_GPR_DISCARD, index, index, false, true);
  if(reg == XR_RDI || reg == XR_RSI || reg == XR_R8 || reg == XR_R9 ||
     is_callee_saved(reg)) used_[index] = false;
  if(!is_callee_saved(reg) ||
     reservation_count_[index] != 1) return;
  preserves_.erase(std::remove(preserves_.begin(), preserves_.end(), reg),
                   preserves_.end());
}

const std::vector<X64Register> & RegisterPool::preserves() const
{
  return preserves_;
}

void RegisterPool::remember_preserve(X64Register reg)
{
  if(is_callee_saved(reg) &&
     std::find(preserves_.begin(), preserves_.end(), reg) == preserves_.end())
    preserves_.push_back(reg);
}

XmmPool::XmmPool(AllocationDecisionLog * decisions)
  : decisions_(decisions)
{
  std::fill(used_, used_ + 8, false);
}

XmmRegister XmmPool::allocate()
{
  XmmRegister result = XMM_0;
  const bool success = choose(&result);
  const AllocationDecision decision = decisions_ ? decisions_->resolve(
    ADO_XMM_ALLOCATE, 8, static_cast<unsigned>(result), false, success) :
    AllocationDecision();
  if(decisions_ && decision.success)
    result = static_cast<XmmRegister>(decision.selected_register);
  if(!success) native_errors::ThrowResourceLimit(
    "scalar XMM register pool exhausted");
  used_[static_cast<unsigned>(result)] = true;
  return result;
}

bool XmmPool::is_used(XmmRegister xmm) const
{
  return used_[static_cast<unsigned>(xmm)];
}

bool XmmPool::choose(XmmRegister * result) const
{
  // xmm6/xmm7 remain available to the encoder for immediate and memory forms.
  for(unsigned i = 0; i != 6; ++i) {
    if(used_[i]) continue;
    *result = static_cast<XmmRegister>(i);
    return true;
  }
  return false;
}

bool XmmPool::try_allocate(XmmRegister & result)
{
  const bool success = choose(&result);
  const AllocationDecision decision = decisions_ ? decisions_->resolve(
    ADO_XMM_TRY_ALLOCATE, 8, static_cast<unsigned>(result), false, success) :
    AllocationDecision();
  const bool resolved = decisions_ ? decision.success : success;
  if(!resolved) return false;
  if(decisions_) result = static_cast<XmmRegister>(decision.selected_register);
  used_[static_cast<unsigned>(result)] = true;
  return true;
}

void XmmPool::release(XmmRegister xmm)
{
  const unsigned index = static_cast<unsigned>(xmm);
  if(decisions_) decisions_->resolve(
    ADO_XMM_RELEASE, index, index, false, true);
  if(index < 6) used_[index] = false;
}

}  // namespace allocation
}  // namespace lowir_native
