#include "native/encoding/scalar_memory.h"

#include "native/analysis/data_layout.h"
#include "native/errors.h"
#include "native/encoding/instructions.h"


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
    if(address.address_binding == mir_model::MirOperand::ADDRESS_LOCAL) {
      emit_rip_load(out, destination, address.symbol,
                    address.address_binding, width);
    } else {
      emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
      emit_load(out, destination, XR_R11, 0, width);
    }
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_load(out, destination, XR_RBP,
              actual_frame_offset(function, address.offset), width);
  } else native_errors::ThrowInternal("unsupported native load address");
}

namespace {

bool integer_load_sign_extends(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_I1 ||
    type.kind == lowir_model::LTK_I8 ||
    type.kind == lowir_model::LTK_I16 ||
    type.kind == lowir_model::LTK_I32 ||
    type.kind == lowir_model::LTK_I64;
}

void emit_unaligned_vector_copy_chunk(elf_detail::CodeBuffer & out,
                                      X64Register destination,
                                      long long destination_offset,
                                      X64Register source,
                                      long long source_offset)
{
  // The allocator reserves xmm6/xmm7 for encoder scratch use.  movdqu is
  // available on every x86-64 target and imposes no alignment requirement.
  out.byte(0xf3);
  emit_rex(out, false, static_cast<X64Register>(XMM_7), source);
  out.byte(0x0f);
  out.byte(0x6f);
  emit_memory_modrm(out, static_cast<unsigned>(XMM_7), source,
                    source_offset);
  out.byte(0xf3);
  emit_rex(out, false, static_cast<X64Register>(XMM_7), destination);
  out.byte(0x0f);
  out.byte(0x7f);
  emit_memory_modrm(out, static_cast<unsigned>(XMM_7), destination,
                    destination_offset);
}

}  // namespace

void emit_address_normalized_load(
    elf_detail::CodeBuffer & out, X64Register destination,
    const mir_model::MirOperand & address, const lowir_model::LowType & type,
    const mir_model::MirFunction & function)
{
  const unsigned width = data_layout::type_width(type);
  const bool sign_extend = integer_load_sign_extends(type);
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    if(address.has_index)
      emit_indexed_normalized_load(out, destination, address.reg,
        address.index, address.scale, address.offset, width, sign_extend);
    else emit_normalized_load(out, destination, address.reg, address.offset,
                              width, sign_extend);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    if(address.address_binding == mir_model::MirOperand::ADDRESS_LOCAL) {
      emit_rip_normalized_load(out, destination, address.symbol,
                               address.address_binding, width, sign_extend);
    } else {
      emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
      emit_normalized_load(
        out, destination, XR_R11, 0, width, sign_extend);
    }
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_normalized_load(out, destination, XR_RBP,
      actual_frame_offset(function, address.offset), width, sign_extend);
  } else native_errors::ThrowInternal("unsupported normalized native load address");
}

void emit_normalized_register_move(
    elf_detail::CodeBuffer & out, X64Register destination,
    X64Register source, const lowir_model::LowType & type)
{
  lowir_native::emit_normalized_register_move(out, destination, source,
    data_layout::type_width(type), integer_load_sign_extends(type));
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
    if(address.address_binding == mir_model::MirOperand::ADDRESS_LOCAL) {
      emit_rip_store(out, address.symbol, address.address_binding,
                     source, width);
    } else {
      emit_symbol_move(out, XR_R11, address.symbol, address.address_binding);
      emit_store(out, XR_R11, 0, source, width);
    }
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_store(out, XR_RBP, actual_frame_offset(function, address.offset),
               source, width);
  } else native_errors::ThrowInternal("unsupported native store address");
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
  native_errors::ThrowInternal(
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
  } else native_errors::ThrowInternal("unsupported native immediate-store address");
}

bool emit_small_copy_bytes(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction * function)
{
  const std::size_t bytes = instruction.byte_count;
  if(instruction.operands.size() != 2)
    native_errors::ThrowInternal("native copy requires two address operands");
  const mir_model::MirOperand & destination_operand = instruction.operands[0];
  const mir_model::MirOperand & source_operand = instruction.operands[1];
  const bool frame_sided =
    destination_operand.kind == mir_model::MirOperand::OP_FRAME ||
    source_operand.kind == mir_model::MirOperand::OP_FRAME;
  // Direct chunks avoid string-operation setup for small copies.  The base
  // policy also uses the wider range when naturally word-aligned.  At O2+
  // selection may request the same unaligned-safe chunks for a weakly aligned
  // medium copy; copies above 64 bytes stay on the compact fallback.
  const bool direct_chunks = bytes <= 32 ||
    (bytes <= 64 &&
     (instruction.byte_alignment >= 8 ||
      instruction.copy_encoding ==
        mir_model::MirInstruction::MBC_DIRECT_CHUNKS));
  if(bytes == 0 || !direct_chunks) {
    if(frame_sided)
      native_errors::ThrowInternal("large native copy requires address registers");
    return false;
  }
  const auto side_base = [&](const mir_model::MirOperand & operand) {
    if(operand.kind == mir_model::MirOperand::OP_REG) return operand.reg;
    if(operand.kind == mir_model::MirOperand::OP_DEREF &&
       !operand.has_index) return operand.reg;
    if(operand.kind != mir_model::MirOperand::OP_FRAME || !function)
      native_errors::ThrowInternal(
        "small native copy requires register or frame operands");
    return XR_RBP;
  };
  const auto side_offset = [&](const mir_model::MirOperand & operand) {
    return operand.kind == mir_model::MirOperand::OP_FRAME ?
      actual_frame_offset(*function, operand.offset) :
      operand.kind == mir_model::MirOperand::OP_DEREF ? operand.offset : 0ll;
  };
  const X64Register destination = side_base(destination_operand);
  const X64Register source = side_base(source_operand);
  const long long destination_base = side_offset(destination_operand);
  const long long source_base = side_offset(source_operand);
  // XMM7 is permanently reserved for encoder scratch use.  Scalar tails reuse
  // an existing MI_COPY_BYTES clobber so this target choice does not change
  // MIR liveness.  A multi-chunk copy revisits both address registers, so the
  // integer scratch must avoid them; among the three string-op clobbers one
  // always remains.
  const bool preserve_pointers = instruction.copy_preserves_pointers;
  const X64Register scratch = preserve_pointers ?
    (destination != XR_R11 && source != XR_R11 ? XR_R11 :
     destination != XR_R10 && source != XR_R10 ? XR_R10 : XR_RAX) :
    (destination != XR_RCX && source != XR_RCX ? XR_RCX :
     destination != XR_RSI && source != XR_RSI ? XR_RSI : XR_RDI);
  std::size_t offset = 0;
  while(offset < bytes) {
    const std::size_t remaining = bytes - offset;
    if(remaining >= 16) {
      emit_unaligned_vector_copy_chunk(
        out, destination, destination_base + static_cast<long long>(offset),
        source, source_base + static_cast<long long>(offset));
      offset += 16;
      continue;
    }
    const std::size_t chunk =
      remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
    emit_load(out, scratch, source,
              source_base + static_cast<long long>(offset),
              static_cast<unsigned>(chunk * 8));
    emit_store(out, destination,
               destination_base + static_cast<long long>(offset), scratch,
               static_cast<unsigned>(chunk * 8));
    offset += chunk;
  }
  return true;
}

bool emit_preserving_dynamic_copy(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction * function)
{
  if(!instruction.copy_preserves_pointers) return false;
  if(instruction.operands.size() != 3)
    native_errors::ThrowInternal("invalid explicit dynamic-copy operands");
  if(!function)
    native_errors::ThrowInternal("explicit dynamic copy outside function");
  emit_register_move(out, XR_R10, XR_RDI);
  emit_register_move(out, XR_R11, XR_RSI);
  const auto remap_saved_parameter = [](mir_model::MirOperand operand) {
    if(operand.kind == mir_model::MirOperand::OP_REG) {
      if(operand.reg == XR_RDI) operand.reg = XR_R10;
      else if(operand.reg == XR_RSI) operand.reg = XR_R11;
    } else if(operand.kind == mir_model::MirOperand::OP_DEREF) {
      if(operand.reg == XR_RDI) operand.reg = XR_R10;
      else if(operand.reg == XR_RSI) operand.reg = XR_R11;
      if(operand.has_index && operand.index == XR_RDI)
        operand.index = XR_R10;
      else if(operand.has_index && operand.index == XR_RSI)
        operand.index = XR_R11;
    }
    return operand;
  };
  const auto materialize_argument = [&](X64Register destination,
                                        std::size_t index) {
    mir_model::MirOperand operand =
      remap_saved_parameter(instruction.operands[index]);
    if(instruction.copy_address_operand_mask & (1u << index)) {
      if(operand.kind == mir_model::MirOperand::OP_FRAME)
        emit_lea(out, destination, XR_RBP,
                 actual_frame_offset(*function, operand.offset));
      else if(operand.kind == mir_model::MirOperand::OP_DEREF) {
        if(operand.has_index)
          emit_indexed_lea(out, destination, operand.reg, operand.index,
                           operand.scale, operand.offset);
        else emit_lea(out, destination, operand.reg, operand.offset);
      } else if(operand.kind == mir_model::MirOperand::OP_SYMBOL ||
                operand.kind == mir_model::MirOperand::OP_GLOBAL)
        emit_symbol_move(out, destination, operand.symbol,
                         operand.address_binding);
      else native_errors::ThrowInternal(
        "unsupported explicit dynamic-copy address");
      return;
    }
    if(operand.kind == mir_model::MirOperand::OP_REG)
      emit_register_move(out, destination, operand.reg);
    else if(operand.kind == mir_model::MirOperand::OP_IMM)
      emit_immediate_move(out, destination,
                          static_cast<std::uint64_t>(operand.imm));
    else if(operand.kind == mir_model::MirOperand::OP_SYMBOL)
      emit_symbol_move(out, destination, operand.symbol,
                       operand.address_binding);
    else if(operand.kind == mir_model::MirOperand::OP_GLOBAL &&
            operand.address_binding ==
              mir_model::MirOperand::ADDRESS_PREEMPTIBLE) {
      emit_symbol_move(out, XR_RAX, operand.symbol,
                       operand.address_binding);
      emit_load(out, destination, XR_RAX, 0, 64);
    } else emit_address_load(out, destination, operand, 64, *function);
  };
  materialize_argument(XR_RDI, 0);
  materialize_argument(XR_RSI, 1);
  materialize_argument(XR_RCX, 2);
  out.byte(0xf3); out.byte(0xa4);
  emit_register_move(out, XR_RDI, XR_R10);
  emit_register_move(out, XR_RSI, XR_R11);
  return true;
}

}  // namespace lowir_native
