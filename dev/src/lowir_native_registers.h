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
  bool is_used(X64Register reg) const;
  X64Register allocate(bool across_call);
  bool try_allocate(bool across_call, X64Register & result);
  void release(X64Register reg);
  const std::vector<X64Register> & preserves() const;

private:
  bool used_[16];
  std::vector<X64Register> preserves_;

  void remember_preserve(X64Register reg);
};

class XmmPool
{
public:
  XmmPool();

  XmmRegister allocate();
  bool try_allocate(XmmRegister & result);
  void release(XmmRegister xmm);

private:
  bool used_[8];
};

}  // namespace allocation
}  // namespace lowir_native
