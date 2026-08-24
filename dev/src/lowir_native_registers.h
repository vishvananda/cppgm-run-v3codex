#pragma once

#include <vector>

#include "x86_register_model.h"

namespace lowir_native {
namespace allocation {

bool is_callee_saved(X64Register reg);

class RegisterPool
{
public:
  RegisterPool();

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

  void remember_preserve(X64Register reg);
};

class XmmPool
{
public:
  XmmPool();

  XmmRegister allocate();
  bool is_used(XmmRegister xmm) const;
  bool try_allocate(XmmRegister & result);
  void release(XmmRegister xmm);

private:
  bool used_[8];
};

}  // namespace allocation
}  // namespace lowir_native
