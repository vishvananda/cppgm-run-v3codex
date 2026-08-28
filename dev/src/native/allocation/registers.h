#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "native/mir/registers.h"

namespace lowir_native {
namespace allocation {

bool is_callee_saved(X64Register reg);

enum AllocationDecisionOperation
{
  ADO_GPR_RESERVE,
  ADO_GPR_TRY_RESERVE,
  ADO_GPR_ALLOCATE,
  ADO_GPR_TRY_ALLOCATE,
  ADO_GPR_RELEASE,
  ADO_GPR_DISCARD,
  ADO_GPR_HOLD_FOR_PLAN,
  ADO_XMM_ALLOCATE,
  ADO_XMM_TRY_ALLOCATE,
  ADO_XMM_RELEASE
};

struct AllocationDecision
{
  AllocationDecisionOperation operation;
  std::size_t position;
  std::uint32_t value;
  unsigned requested_register;
  unsigned selected_register;
  bool across_call;
  bool success;
};

// Gate (i)'s per-function seam: a first walk records every physical-pool
// mutation and its result; the emitting walk consumes that exact sequence.
class AllocationDecisionLog
{
public:
  AllocationDecisionLog();

  void set_context(std::size_t position, std::uint32_t value);
  AllocationDecision resolve(AllocationDecisionOperation operation,
    unsigned requested_register, unsigned selected_register,
    bool across_call, bool success);
  void begin_replay();
  void finish_replay() const;

private:
  std::vector<AllocationDecision> decisions_;
  std::size_t cursor_;
  std::size_t position_;
  std::uint32_t value_;
  bool replaying_;
};

class RegisterPool
{
public:
  explicit RegisterPool(AllocationDecisionLog * decisions = 0);

  void reserve(X64Register reg);
  bool try_reserve(X64Register reg);
  bool is_used(X64Register reg) const;
  X64Register allocate(bool across_call);
  bool try_allocate(bool across_call, X64Register & result);
  void release(X64Register reg);
  void discard_unused_reservation(X64Register reg);
  const std::vector<X64Register> & preserves() const;
  // A released register with a future planned claim stays available only
  // as a last resort, so reactive values do not sit on it into the claim.
  void hold_for_plan(X64Register reg);
  bool plan_held(X64Register reg) const;

private:
  bool used_[16];
  bool plan_held_[16];
  unsigned reservation_count_[16];
  std::vector<X64Register> preserves_;
  AllocationDecisionLog * decisions_;

  void remember_preserve(X64Register reg);
  void reserve_raw(X64Register reg);
  bool choose(bool across_call, X64Register * result) const;
};

class XmmPool
{
public:
  explicit XmmPool(AllocationDecisionLog * decisions = 0);

  XmmRegister allocate();
  bool is_used(XmmRegister xmm) const;
  bool try_allocate(XmmRegister & result);
  void release(XmmRegister xmm);

private:
  bool used_[8];
  AllocationDecisionLog * decisions_;
  bool choose(XmmRegister * result) const;
};

}  // namespace allocation
}  // namespace lowir_native
