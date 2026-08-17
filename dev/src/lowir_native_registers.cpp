#include "lowir_native_registers.h"

#include <algorithm>
#include <stdexcept>

namespace lowir_native {
namespace allocation {

bool is_callee_saved(X64Register reg)
{
  return reg == XR_RBX || reg == XR_R12 || reg == XR_R13 ||
         reg == XR_R14 || reg == XR_R15;
}

RegisterPool::RegisterPool()
{
  std::fill(used_, used_ + 16, false);
}

void RegisterPool::reserve(X64Register reg)
{
  if(used_[static_cast<unsigned>(reg)])
    throw std::runtime_error("MIR register allocation conflict");
  used_[static_cast<unsigned>(reg)] = true;
  remember_preserve(reg);
}

bool RegisterPool::try_reserve(X64Register reg)
{
  if(used_[static_cast<unsigned>(reg)]) return false;
  reserve(reg);
  return true;
}

bool RegisterPool::is_used(X64Register reg) const
{
  return used_[static_cast<unsigned>(reg)];
}

X64Register RegisterPool::allocate(bool across_call)
{
  X64Register result = XR_RSP;
  if(try_allocate(across_call, result)) return result;
  throw std::runtime_error("foundation register pool exhausted");
}

bool RegisterPool::try_allocate(bool across_call, X64Register & result)
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
  for(std::size_t i = 0; i < count; ++i) {
    const unsigned index = static_cast<unsigned>(choices[i]);
    if(used_[index]) continue;
    used_[index] = true;
    remember_preserve(choices[i]);
    result = choices[i];
    return true;
  }
  return false;
}

void RegisterPool::release(X64Register reg)
{
  const unsigned index = static_cast<unsigned>(reg);
  if(reg == XR_RDI || reg == XR_RSI || reg == XR_R8 || reg == XR_R9 ||
     is_callee_saved(reg)) used_[index] = false;
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

XmmPool::XmmPool()
{
  std::fill(used_, used_ + 8, false);
}

XmmRegister XmmPool::allocate()
{
  XmmRegister result = XMM_0;
  if(try_allocate(result)) return result;
  throw std::runtime_error("scalar XMM register pool exhausted");
}

bool XmmPool::is_used(XmmRegister xmm) const
{
  return used_[static_cast<unsigned>(xmm)];
}

bool XmmPool::try_allocate(XmmRegister & result)
{
  // xmm6/xmm7 remain available to the encoder for immediate and memory forms.
  for(unsigned i = 0; i != 6; ++i) {
    if(used_[i]) continue;
    used_[i] = true;
    result = static_cast<XmmRegister>(i);
    return true;
  }
  return false;
}

void XmmPool::release(XmmRegister xmm)
{
  const unsigned index = static_cast<unsigned>(xmm);
  if(index < 6) used_[index] = false;
}

}  // namespace allocation
}  // namespace lowir_native
