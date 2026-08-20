#include "lowir_native_scalar_memory.h"

#include "lowir_native_encoding.h"

#include <stdexcept>

namespace lowir_native {

long long actual_frame_offset(const mir_model::MirFunction & function,
                              long long abstract_offset)
{
  if(abstract_offset >= 0) return abstract_offset;
  return abstract_offset -
    static_cast<long long>(function.callee_saved_regs.size() * 8);
}

void emit_address_load(elf_detail::CodeBuffer & out,
                       X64Register destination,
                       const mir_model::MirOperand & address,
                       unsigned width,
                       const mir_model::MirFunction & function)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    if(address.has_index)
      emit_indexed_load(out, destination, address.reg, address.index,
                        address.scale, address.offset, width);
    else emit_load(out, destination, address.reg, address.offset, width);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
    emit_load(out, destination, XR_R11, 0, width);
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_load(out, destination, XR_RBP,
              actual_frame_offset(function, address.offset), width);
  } else throw std::logic_error("unsupported native load address");
}

void emit_address_store(elf_detail::CodeBuffer & out,
                        const mir_model::MirOperand & address,
                        X64Register source, unsigned width,
                        const mir_model::MirFunction & function)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    if(address.has_index)
      emit_indexed_store(out, address.reg, address.index, address.scale,
                         address.offset, source, width);
    else emit_store(out, address.reg, address.offset, source, width);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
    emit_store(out, XR_R11, 0, source, width);
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_store(out, XR_RBP, actual_frame_offset(function, address.offset),
               source, width);
  } else throw std::logic_error("unsupported native store address");
}

namespace {

bool address_uses_register(const mir_model::MirOperand & address,
                           X64Register reg)
{
  return address.kind == mir_model::MirOperand::OP_DEREF &&
    (address.reg == reg || (address.has_index && address.index == reg));
}

X64Register immediate_store_scratch(const mir_model::MirOperand & address)
{
  if(address.kind == mir_model::MirOperand::OP_GLOBAL) return XR_R10;
  if(!address_uses_register(address, XR_R10)) return XR_R10;
  if(!address_uses_register(address, XR_R11)) return XR_R11;
  throw std::logic_error(
    "large immediate store address occupies both encoder scratch registers");
}

}  // namespace

void emit_address_immediate_store(
    elf_detail::CodeBuffer & out, const mir_model::MirOperand & address,
    std::uint64_t value, unsigned width,
    const mir_model::MirFunction & function)
{
  if(!immediate_store_encoding_available(width, value)) {
    const X64Register scratch = immediate_store_scratch(address);
    emit_immediate_move(out, scratch, value);
    emit_address_store(out, address, scratch, width, function);
    return;
  }
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    if(address.has_index)
      emit_indexed_immediate_store(out, address.reg, address.index,
        address.scale, address.offset, value, width);
    else emit_immediate_store(out, address.reg, address.offset, value, width);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
    emit_immediate_store(out, XR_R11, 0, value, width);
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_immediate_store(out, XR_RBP,
      actual_frame_offset(function, address.offset), value, width);
  } else throw std::logic_error("unsupported native immediate-store address");
}

}  // namespace lowir_native
