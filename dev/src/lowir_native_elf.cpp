#include "lowir_native.h"
#include "lowir_native_address_folding.h"
#include "lowir_native_code_buffer.h"
#include "lowir_native_data_layout.h"
#include "lowir_native_float_bits.h"
#include "lowir_native_host_eh.h"
#include "lowir_native_encoding.h"
#include "lowir_native_object_elf.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
namespace lowir_native {
namespace {
using object_elf_detail::EncodedFixup;
using object_elf_detail::EncodedSection;
using object_elf_detail::HostFunctionLayout;
using object_elf_detail::declaration_object_symbols;
using object_elf_detail::host_external_global_definitions;
using object_elf_detail::host_symbol_spelling;
using object_elf_detail::make_linux_relocatable_image;
using float_bits::extended;
using float_bits::scalar;
using data_layout::global_alignment;
using data_layout::type_size;
using data_layout::type_width;
using elf_detail::CodeBuffer;
using elf_detail::CodeOffsetAdjustment;
using elf_detail::Fixup;
const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kElfHeaderSize = 64;
const std::size_t kProgramHeaderSize = 56;
const std::size_t kContentOffset = kElfHeaderSize + kProgramHeaderSize;
std::string native_object_symbol(const std::string & symbol)
{
  return symbol.empty() || symbol[0] == '@' ? symbol : "@" + symbol;
}

struct HostEhStackCleanup
{
  std::string label;
  std::string landing_pad;
  std::size_t stack_bytes = 0;
};

std::size_t function_stack_adjustment(const mir_model::MirFunction & function)
{
  const std::size_t preserved = function.callee_saved_regs.size() * 8;
  if(function.stack_size < preserved)
    throw std::logic_error("MIR stack reservation is smaller than its saves");
  return function.stack_size - preserved;
}

long long actual_frame_offset(const mir_model::MirFunction & function,
                              long long abstract_offset)
{
  if(abstract_offset >= 0) return abstract_offset;
  return abstract_offset - static_cast<long long>(function.callee_saved_regs.size() * 8);
}

void emit_function_prologue(CodeBuffer & out, const mir_model::MirFunction & function)
{
  emit_push(out, XR_RBP);
  emit_register_move(out, XR_RBP, XR_RSP);
  for(std::size_t i = 0; i < function.callee_saved_regs.size(); ++i)
    emit_push(out, function.callee_saved_regs[i]);
  emit_stack_adjust(out, true,
                    static_cast<unsigned>(function_stack_adjustment(function)));
}

void emit_function_return(CodeBuffer & out, const mir_model::MirFunction & function)
{
  if(function.has_dynamic_stack) {
    emit_register_move(out, XR_RSP, XR_RBP);
    emit_stack_adjust(out, true,
      static_cast<unsigned>(function.callee_saved_regs.size() * 8));
  } else
    emit_stack_adjust(out, false,
                      static_cast<unsigned>(function_stack_adjustment(function)));
  for(std::size_t i = function.callee_saved_regs.size(); i != 0; --i)
    emit_pop(out, function.callee_saved_regs[i - 1]);
  emit_pop(out, XR_RBP);
  out.byte(0xc3);
}

void require_operands(const mir_model::MirInstruction & instruction,
                      std::size_t count)
{
  if(instruction.operands.size() != count)
    throw std::logic_error("invalid MIR operand count for native encoding");
}

unsigned xmm_index(XmmRegister xmm)
{
  return static_cast<unsigned>(xmm);
}

void emit_scalar_prefix(CodeBuffer & out, const std::string & type)
{
  if(type == "f32") out.byte(0xf3);
  else if(type == "f64") out.byte(0xf2);
  else throw std::logic_error("SSE scalar operation requires f32 or f64");
}

void float_address(CodeBuffer & out, const mir_model::MirOperand & address,
                   const mir_model::MirFunction & function,
                   X64Register & base, long long & displacement)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    base = address.reg;
    displacement = address.offset;
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    base = XR_RBP;
    displacement = actual_frame_offset(function, address.offset);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text, address.address_binding);
    base = XR_R11;
    displacement = 0;
  } else throw std::logic_error("unsupported SSE memory operand");
}

void emit_xmm_load(CodeBuffer & out, XmmRegister destination,
                   const mir_model::MirOperand & source,
                   const std::string & type,
                   const mir_model::MirFunction & function)
{
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, source, function, base, displacement);
  emit_scalar_prefix(out, type);
  emit_rex(out, false, static_cast<X64Register>(xmm_index(destination)), base);
  out.byte(0x0f);
  out.byte(0x10);
  emit_memory_modrm(out, xmm_index(destination), base, displacement);
}

void emit_xmm_store(CodeBuffer & out, const mir_model::MirOperand & destination,
                    XmmRegister source, const std::string & type,
                    const mir_model::MirFunction & function)
{
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, destination, function, base, displacement);
  emit_scalar_prefix(out, type);
  emit_rex(out, false, static_cast<X64Register>(xmm_index(source)), base);
  out.byte(0x0f);
  out.byte(0x11);
  emit_memory_modrm(out, xmm_index(source), base, displacement);
}

void emit_xmm_register_move(CodeBuffer & out, XmmRegister destination,
                            XmmRegister source, const std::string & type)
{
  emit_scalar_prefix(out, type);
  out.byte(0x0f);
  out.byte(0x10);
  emit_modrm(out, 3, xmm_index(destination), xmm_index(source));
}

void emit_gpr_to_xmm(CodeBuffer & out, XmmRegister destination,
                     X64Register source, unsigned width)
{
  out.byte(0x66);
  emit_rex(out, width == 64, static_cast<X64Register>(xmm_index(destination)), source);
  out.byte(0x0f);
  out.byte(0x6e);
  emit_modrm(out, 3, xmm_index(destination), source);
}

void emit_xmm_to_gpr(CodeBuffer & out, X64Register destination,
                     XmmRegister source, unsigned width)
{
  out.byte(0x66);
  emit_rex(out, width == 64, static_cast<X64Register>(xmm_index(source)), destination);
  out.byte(0x0f);
  out.byte(0x7e);
  emit_modrm(out, 3, xmm_index(source), destination);
}

mir_model::MirOperand memory_operand(X64Register reg, long long offset = 0)
{
  mir_model::MirOperand operand;
  operand.kind = mir_model::MirOperand::OP_DEREF;
  operand.reg = reg;
  operand.offset = offset;
  return operand;
}

void emit_extended_immediate_store(CodeBuffer & out,
                                   const mir_model::MirOperand & destination,
                                   const std::string & text,
                                   const mir_model::MirFunction & function)
{
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, destination, function, base, displacement);
  const std::pair<std::uint64_t, std::uint64_t> words =
    extended(text);
  emit_immediate_move(out, XR_R10, words.first);
  emit_store(out, base, displacement, XR_R10, 64);
  emit_immediate_move(out, XR_R10, words.second);
  emit_store(out, base, displacement + 8, XR_R10, 64);
}

void emit_x87_memory(CodeBuffer & out, unsigned opcode, unsigned extension,
                     const mir_model::MirOperand & operand,
                     const mir_model::MirFunction & function)
{
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, operand, function, base, displacement);
  emit_rex(out, false, XR_RAX, base);
  out.byte(opcode);
  emit_memory_modrm(out, extension, base, displacement);
}

void emit_x87_load_memory(CodeBuffer & out,
                          const mir_model::MirOperand & operand,
                          const std::string & type,
                          const mir_model::MirFunction & function)
{
  if(type == "f32") emit_x87_memory(out, 0xd9, 0, operand, function);
  else if(type == "f64") emit_x87_memory(out, 0xdd, 0, operand, function);
  else if(type == "f80") emit_x87_memory(out, 0xdb, 5, operand, function);
  else throw std::logic_error("x87 load requires a floating type");
}

void emit_x87_store_pop_memory(CodeBuffer & out,
                               const mir_model::MirOperand & operand,
                               const std::string & type,
                               const mir_model::MirFunction & function)
{
  if(type == "f32") emit_x87_memory(out, 0xd9, 3, operand, function);
  else if(type == "f64") emit_x87_memory(out, 0xdd, 3, operand, function);
  else if(type == "f80") emit_x87_memory(out, 0xdb, 7, operand, function);
  else throw std::logic_error("x87 store requires a floating type");
}

void emit_x87_load(CodeBuffer & out, const mir_model::MirOperand & source,
                   const std::string & type,
                   const mir_model::MirFunction & function)
{
  if(source.kind != mir_model::MirOperand::OP_FLOAT_IMM &&
     source.kind != mir_model::MirOperand::OP_IMM &&
     source.kind != mir_model::MirOperand::OP_XMM) {
    emit_x87_load_memory(out, source, type, function);
    return;
  }
  emit_stack_adjust(out, true, 16);
  const mir_model::MirOperand scratch = memory_operand(XR_RSP);
  if(source.kind == mir_model::MirOperand::OP_XMM) {
    emit_xmm_store(out, scratch, source.xmm, type, function);
  } else if(type == "f80") {
    const std::string text = source.kind == mir_model::MirOperand::OP_FLOAT_IMM ?
      source.text : std::to_string(source.imm);
    emit_extended_immediate_store(out, scratch, text, function);
  } else {
    const std::string text = source.kind == mir_model::MirOperand::OP_FLOAT_IMM ?
      source.text : std::to_string(source.imm);
    emit_immediate_move(out, XR_R10, scalar(text, type));
    emit_store(out, XR_RSP, 0, XR_R10, type == "f32" ? 32 : 64);
  }
  emit_x87_load_memory(out, scratch, type, function);
  emit_stack_adjust(out, false, 16);
}

void emit_x87_store_pop(CodeBuffer & out,
                        const mir_model::MirOperand & destination,
                        const std::string & type,
                        const mir_model::MirFunction & function)
{
  if(destination.kind != mir_model::MirOperand::OP_XMM) {
    emit_x87_store_pop_memory(out, destination, type, function);
    return;
  }
  emit_stack_adjust(out, true, 16);
  const mir_model::MirOperand scratch = memory_operand(XR_RSP);
  emit_x87_store_pop_memory(out, scratch, type, function);
  emit_xmm_load(out, destination.xmm, scratch, type, function);
  emit_stack_adjust(out, false, 16);
}

void emit_x87_pop(CodeBuffer & out)
{
  out.byte(0xdd);
  out.byte(0xd8);
}

void emit_x87_binary(CodeBuffer & out,
                     const mir_model::MirInstruction & instruction,
                     const mir_model::MirFunction & function)
{
  require_operands(instruction, 3);
  emit_x87_load(out, instruction.operands[1], instruction.type, function);
  emit_x87_load(out, instruction.operands[2], instruction.type, function);
  out.byte(0xde);
  if(instruction.opcode == mir_model::MirInstruction::MI_FADD) out.byte(0xc1);
  else if(instruction.opcode == mir_model::MirInstruction::MI_FMUL) out.byte(0xc9);
  else if(instruction.opcode == mir_model::MirInstruction::MI_FSUB) out.byte(0xe9);
  else if(instruction.opcode == mir_model::MirInstruction::MI_FDIV) out.byte(0xf9);
  else throw std::logic_error("invalid x87 binary operation");
  emit_x87_store_pop(out, instruction.operands[0], instruction.type, function);
}

void emit_x87_compare_flags(CodeBuffer & out,
                            const mir_model::MirOperand & left,
                            const mir_model::MirOperand & right,
                            const mir_model::MirFunction & function)
{
  emit_x87_load(out, left, "f80", function);
  emit_x87_load(out, right, "f80", function);
  out.byte(0xdf);
  out.byte(0xe9); // fucomip st0, st1: compare MIR right with left and pop right.
  emit_x87_pop(out);
}

void materialize_float_operand(CodeBuffer & out, XmmRegister destination,
                               const mir_model::MirOperand & source,
                               const std::string & type,
                               const mir_model::MirFunction & function)
{
  if(source.kind == mir_model::MirOperand::OP_XMM) {
    if(source.xmm != destination)
      emit_xmm_register_move(out, destination, source.xmm, type);
  } else if(source.kind == mir_model::MirOperand::OP_FLOAT_IMM) {
    emit_immediate_move(out, XR_R11, scalar(source.text, type));
    emit_gpr_to_xmm(out, destination, XR_R11, type == "f32" ? 32 : 64);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_immediate_move(out, XR_R11,
      scalar(std::to_string(source.imm), type));
    emit_gpr_to_xmm(out, destination, XR_R11, type == "f32" ? 32 : 64);
  } else {
    emit_xmm_load(out, destination, source, type, function);
  }
}

void emit_float_move(CodeBuffer & out, const mir_model::MirInstruction & instruction,
                     const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const mir_model::MirOperand & destination = instruction.operands[0];
  const mir_model::MirOperand & source = instruction.operands[1];
  if(instruction.type == "f80") {
    if(source.kind == mir_model::MirOperand::OP_FLOAT_IMM ||
       source.kind == mir_model::MirOperand::OP_IMM) {
      emit_extended_immediate_store(out, destination,
        source.kind == mir_model::MirOperand::OP_FLOAT_IMM ?
          source.text : std::to_string(source.imm), function);
      return;
    }
    emit_x87_load(out, source, instruction.type, function);
    emit_x87_store_pop(out, destination, instruction.type, function);
    return;
  }
  if(destination.kind == mir_model::MirOperand::OP_XMM) {
    materialize_float_operand(out, destination.xmm, source, instruction.type, function);
    return;
  }
  XmmRegister value = XMM_7;
  if(source.kind == mir_model::MirOperand::OP_XMM) value = source.xmm;
  else materialize_float_operand(out, value, source, instruction.type, function);
  emit_xmm_store(out, destination, value, instruction.type, function);
}

void emit_xmm_source_instruction(CodeBuffer & out, unsigned opcode,
                                 XmmRegister destination,
                                 const mir_model::MirOperand & source,
                                 const std::string & type,
                                 const mir_model::MirFunction & function)
{
  mir_model::MirOperand actual = source;
  if(source.kind == mir_model::MirOperand::OP_FLOAT_IMM ||
     source.kind == mir_model::MirOperand::OP_IMM) {
    materialize_float_operand(out, XMM_7, source, type, function);
    actual.kind = mir_model::MirOperand::OP_XMM;
    actual.xmm = XMM_7;
  }
  if(actual.kind == mir_model::MirOperand::OP_XMM) {
    emit_scalar_prefix(out, type);
    out.byte(0x0f);
    out.byte(opcode);
    emit_modrm(out, 3, xmm_index(destination), xmm_index(actual.xmm));
    return;
  }
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, actual, function, base, displacement);
  emit_scalar_prefix(out, type);
  emit_rex(out, false, static_cast<X64Register>(xmm_index(destination)), base);
  out.byte(0x0f);
  out.byte(opcode);
  emit_memory_modrm(out, xmm_index(destination), base, displacement);
}

void emit_float_binary(CodeBuffer & out, const mir_model::MirInstruction & instruction,
                       const mir_model::MirFunction & function, unsigned opcode)
{
  if(instruction.type == "f80" || instruction.type == "f64") {
    emit_x87_binary(out, instruction, function);
    return;
  }
  require_operands(instruction, 3);
  const mir_model::MirOperand & destination = instruction.operands[0];
  const XmmRegister target = destination.kind == mir_model::MirOperand::OP_XMM ?
    destination.xmm : XMM_6;
  materialize_float_operand(out, target, instruction.operands[1],
                            instruction.type, function);
  emit_xmm_source_instruction(out, opcode, target, instruction.operands[2],
                              instruction.type, function);
  if(destination.kind != mir_model::MirOperand::OP_XMM)
    emit_xmm_store(out, destination, target, instruction.type, function);
}

void emit_float_compare_flags(CodeBuffer & out,
                              const mir_model::MirOperand & left,
                              const mir_model::MirOperand & right,
                              const std::string & type,
                              const mir_model::MirFunction & function)
{
  if(type == "f80") {
    emit_x87_compare_flags(out, left, right, function);
    return;
  }
  // Compare right with left to match the MIR branch-condition convention.
  materialize_float_operand(out, XMM_6, right, type, function);
  mir_model::MirOperand actual_left = left;
  if(left.kind == mir_model::MirOperand::OP_FLOAT_IMM ||
     left.kind == mir_model::MirOperand::OP_IMM) {
    materialize_float_operand(out, XMM_7, left, type, function);
    actual_left.kind = mir_model::MirOperand::OP_XMM;
    actual_left.xmm = XMM_7;
  }
  if(actual_left.kind == mir_model::MirOperand::OP_XMM) {
    if(type == "f64") out.byte(0x66);
    out.byte(0x0f);
    out.byte(0x2e);
    emit_modrm(out, 3, xmm_index(XMM_6), xmm_index(actual_left.xmm));
    return;
  }
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, actual_left, function, base, displacement);
  if(type == "f64") out.byte(0x66);
  emit_rex(out, false, static_cast<X64Register>(xmm_index(XMM_6)), base);
  out.byte(0x0f);
  out.byte(0x2e);
  emit_memory_modrm(out, xmm_index(XMM_6), base, displacement);
}

X64Register require_register(const mir_model::MirOperand & operand);

X86Condition float_value_condition(mir_model::MirInstruction::Opcode opcode)
{
  if(opcode == mir_model::MirInstruction::MI_FEQ) return XC_E;
  if(opcode == mir_model::MirInstruction::MI_FNE) return XC_NE;
  if(opcode == mir_model::MirInstruction::MI_FLT) return XC_A;
  if(opcode == mir_model::MirInstruction::MI_FGT) return XC_B;
  if(opcode == mir_model::MirInstruction::MI_FLE) return XC_AE;
  if(opcode == mir_model::MirInstruction::MI_FGE) return XC_BE;
  throw std::logic_error("invalid scalar floating comparison opcode");
}

void emit_float_compare_value(CodeBuffer & out,
                              const mir_model::MirInstruction & instruction,
                              const mir_model::MirFunction & function)
{
  require_operands(instruction, 3);
  const X64Register destination = require_register(instruction.operands[0]);
  emit_float_compare_flags(out, instruction.operands[1], instruction.operands[2],
                           instruction.type, function);
  emit_set_condition(out, float_value_condition(instruction.opcode), destination);
  emit_move_zero_extended_byte(out, destination, destination);
  emit_set_condition(out, instruction.opcode == mir_model::MirInstruction::MI_FNE ?
                     XC_P : XC_NP, XR_R11);
  emit_move_zero_extended_byte(out, XR_R11, XR_R11);
  const bool unordered_is_true = instruction.opcode == mir_model::MirInstruction::MI_FNE;
  emit_rex(out, true, XR_R11, destination);
  out.byte(unordered_is_true ? 0x09 : 0x21);
  emit_modrm(out, 3, XR_R11, destination);
}

void emit_float_negate(CodeBuffer & out,
                       const mir_model::MirInstruction & instruction,
                       const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const mir_model::MirOperand & destination = instruction.operands[0];
  if(instruction.type == "f80") {
    emit_x87_load(out, instruction.operands[1], instruction.type, function);
    out.byte(0xd9);
    out.byte(0xe0); // fchs
    emit_x87_store_pop(out, destination, instruction.type, function);
    return;
  }
  const XmmRegister target = destination.kind == mir_model::MirOperand::OP_XMM ?
    destination.xmm : XMM_6;
  materialize_float_operand(out, target, instruction.operands[1],
                            instruction.type, function);
  emit_xmm_to_gpr(out, XR_R11, target, instruction.type == "f32" ? 32 : 64);
  emit_immediate_move(out, XR_R10, instruction.type == "f32" ?
    UINT64_C(0x80000000) : UINT64_C(0x8000000000000000));
  emit_rex(out, true, XR_R10, XR_R11);
  out.byte(0x31);
  emit_modrm(out, 3, XR_R10, XR_R11);
  emit_gpr_to_xmm(out, target, XR_R11, instruction.type == "f32" ? 32 : 64);
  if(destination.kind != mir_model::MirOperand::OP_XMM)
    emit_xmm_store(out, destination, target, instruction.type, function);
}

X64Register require_register(const mir_model::MirOperand & operand)
{
  if(operand.kind != mir_model::MirOperand::OP_REG)
    throw std::logic_error("native encoder expected a register operand");
  return operand.reg;
}

void emit_move(CodeBuffer & out, const mir_model::MirInstruction & instruction)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  const mir_model::MirOperand & source = instruction.operands[1];
  if(source.kind == mir_model::MirOperand::OP_REG)
    emit_register_move(out, destination, source.reg);
  else if(source.kind == mir_model::MirOperand::OP_IMM)
    emit_immediate_move(out, destination, static_cast<std::uint64_t>(source.imm));
  else if(source.kind == mir_model::MirOperand::OP_SYMBOL)
    emit_symbol_move(out, destination, source.text, source.address_binding);
  else if(source.kind == mir_model::MirOperand::OP_GLOBAL)
    emit_symbol_move(out, destination, source.text, source.address_binding);
  else throw std::logic_error("unsupported native move operand");
}

void emit_address_load(CodeBuffer & out, X64Register destination,
                       const mir_model::MirOperand & address, unsigned width,
                       const mir_model::MirFunction & function)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    emit_load(out, destination, address.reg, address.offset, width);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text, address.address_binding);
    emit_load(out, destination, XR_R11, 0, width);
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_load(out, destination, XR_RBP,
              actual_frame_offset(function, address.offset), width);
  } else throw std::logic_error("unsupported native load address");
}

void emit_address_store(CodeBuffer & out, const mir_model::MirOperand & address,
                        X64Register source, unsigned width,
                        const mir_model::MirFunction & function)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    emit_store(out, address.reg, address.offset, source, width);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text, address.address_binding);
    emit_store(out, XR_R11, 0, source, width);
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_store(out, XR_RBP, actual_frame_offset(function, address.offset),
               source, width);
  } else throw std::logic_error("unsupported native store address");
}

void emit_atomic_memory(CodeBuffer & out,
                        const mir_model::MirInstruction & instruction,
                        const mir_model::MirFunction & function,
                        bool locked, bool escaped,
                        unsigned byte_opcode, unsigned wide_opcode)
{
  require_operands(instruction, 2);
  X64Register base = XR_RBP;
  long long displacement = 0;
  float_address(out, instruction.operands[0], function, base, displacement);
  const X64Register source = require_register(instruction.operands[1]);
  const unsigned width = type_width(instruction.type);
  emit_size_prefix(out, width);
  if(locked) out.byte(0xf0);
  emit_rex(out, width == 64, source, base, width == 8);
  if(escaped) out.byte(0x0f);
  out.byte(width == 8 ? byte_opcode : wide_opcode);
  emit_memory_modrm(out, source, base, displacement);
}

std::pair<std::string, std::string> conversion_types(const std::string & type)
{
  const std::size_t split = type.find('.');
  if(split == std::string::npos)
    throw std::logic_error("native conversion lacks source/destination types");
  return std::make_pair(type.substr(0, split), type.substr(split + 1));
}

X64Register materialize_integer_operand(CodeBuffer & out,
                                        const mir_model::MirOperand & source,
                                        unsigned width,
                                        const mir_model::MirFunction & function)
{
  if(source.kind == mir_model::MirOperand::OP_REG) return source.reg;
  if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_immediate_move(out, XR_R11, static_cast<std::uint64_t>(source.imm));
    return XR_R11;
  }
  emit_address_load(out, XR_R11, source, width, function);
  return XR_R11;
}

void emit_near_jump(CodeBuffer & out, X86Condition condition,
                    const std::string & target)
{
  emit_condition_jump(out, condition, target);
}

void emit_unconditional_jump(CodeBuffer & out, const std::string & target)
{
  if(out.short_relative(0xeb, target)) return;
  out.byte(0xe9);
  out.relative32(target);
}

void emit_x87_load_signed_integer(CodeBuffer & out,
                                  const mir_model::MirOperand & source,
                                  unsigned width,
                                  const mir_model::MirFunction & function)
{
  const X64Register value = materialize_integer_operand(out, source, width, function);
  emit_stack_adjust(out, true, 16);
  const unsigned stored_width = width <= 16 ? 16 : (width <= 32 ? 32 : 64);
  emit_store(out, XR_RSP, 0, value, stored_width);
  const mir_model::MirOperand scratch = memory_operand(XR_RSP);
  if(stored_width == 16) emit_x87_memory(out, 0xdf, 0, scratch, function);
  else if(stored_width == 32) emit_x87_memory(out, 0xdb, 0, scratch, function);
  else emit_x87_memory(out, 0xdf, 5, scratch, function);
  emit_stack_adjust(out, false, 16);
}

void emit_x87_load_unsigned_integer(CodeBuffer & out,
                                    const mir_model::MirOperand & source,
                                    unsigned width,
                                    const mir_model::MirFunction & function)
{
  const X64Register value = materialize_integer_operand(out, source, width, function);
  if(width < 64) {
    emit_stack_adjust(out, true, 16);
    emit_store(out, XR_RSP, 0, value, 64);
    emit_x87_memory(out, 0xdf, 5, memory_operand(XR_RSP), function);
    emit_stack_adjust(out, false, 16);
    return;
  }
  emit_stack_adjust(out, true, 16);
  emit_store(out, XR_RSP, 0, value, 64);
  emit_x87_memory(out, 0xdf, 5, memory_operand(XR_RSP), function);
  emit_rex(out, true, value, value);
  out.byte(0x85);
  emit_modrm(out, 3, value, value);
  const std::string nonnegative = out.internal_label("uitofp_done");
  emit_near_jump(out, XC_NS, nonnegative);
  mir_model::MirOperand two64;
  two64.kind = mir_model::MirOperand::OP_FLOAT_IMM;
  two64.text = "18446744073709551616.0L";
  emit_x87_load(out, two64, "f80", function);
  out.byte(0xde);
  out.byte(0xc1); // faddp st1, st0
  out.label(nonnegative);
  emit_stack_adjust(out, false, 16);
}

void emit_x87_store_truncated_integer(CodeBuffer & out, X64Register destination,
                                      unsigned width,
                                      const mir_model::MirFunction & function)
{
  emit_stack_adjust(out, true, 16);
  const mir_model::MirOperand scratch = memory_operand(XR_RSP);
  if(width <= 16) emit_x87_memory(out, 0xdf, 1, scratch, function);
  else if(width <= 32) emit_x87_memory(out, 0xdb, 1, scratch, function);
  else emit_x87_memory(out, 0xdd, 1, scratch, function);
  emit_load(out, destination, XR_RSP, 0, width <= 16 ? 16 : (width <= 32 ? 32 : 64));
  emit_stack_adjust(out, false, 16);
}

void emit_x87_store_truncated_unsigned(CodeBuffer & out, X64Register destination,
                                       unsigned width,
                                       const mir_model::MirFunction & function)
{
  if(width < 64) {
    emit_x87_store_truncated_integer(out, destination, 64, function);
    if(width == 8) {
      emit_rex(out, false, destination, destination, true);
      out.byte(0x0f); out.byte(0xb6);
      emit_modrm(out, 3, destination, destination);
    } else if(width == 16) {
      emit_rex(out, true, destination, destination);
      out.byte(0x0f); out.byte(0xb7);
      emit_modrm(out, 3, destination, destination);
    } else if(width == 32) {
      emit_rex(out, false, destination, destination);
      out.byte(0x89);
      emit_modrm(out, 3, destination, destination);
    }
    return;
  }

  emit_stack_adjust(out, true, 16);
  const mir_model::MirOperand scratch = memory_operand(XR_RSP);
  mir_model::MirOperand threshold;
  threshold.kind = mir_model::MirOperand::OP_FLOAT_IMM;
  threshold.text = "9223372036854775808.0L";
  emit_x87_load(out, threshold, "f80", function);
  out.byte(0xdf);
  out.byte(0xe9); // Compare 2^63 with the retained input and pop the threshold.
  const std::string high = out.internal_label("fptoui_high");
  const std::string done = out.internal_label("fptoui_done");
  emit_near_jump(out, XC_BE, high);
  emit_x87_memory(out, 0xdd, 1, scratch, function);
  emit_load(out, destination, XR_RSP, 0, 64);
  emit_unconditional_jump(out, done);
  out.label(high);
  emit_x87_load(out, threshold, "f80", function);
  out.byte(0xde);
  out.byte(0xe9); // fsubp st1, st0
  emit_x87_memory(out, 0xdd, 1, scratch, function);
  emit_load(out, destination, XR_RSP, 0, 64);
  emit_immediate_move(out, XR_R10, UINT64_C(0x8000000000000000));
  emit_rex(out, true, XR_R10, destination);
  out.byte(0x09);
  emit_modrm(out, 3, XR_R10, destination);
  out.label(done);
  emit_stack_adjust(out, false, 16);
}

void emit_integer_to_float(CodeBuffer & out,
                           const mir_model::MirInstruction & instruction,
                           const mir_model::MirFunction & function)
{
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
  if(types.first == "i128") {
    require_operands(instruction, 3);
    const mir_model::MirOperand & low = instruction.operands[1];
    const mir_model::MirOperand & high = instruction.operands[2];
    if(instruction.opcode == mir_model::MirInstruction::MI_UITOFP)
      emit_x87_load_unsigned_integer(out, high, 64, function);
    else
      emit_x87_load_signed_integer(out, high, 64, function);
    mir_model::MirOperand two64;
    two64.kind = mir_model::MirOperand::OP_FLOAT_IMM;
    two64.text = "18446744073709551616.0L";
    emit_x87_load(out, two64, "f80", function);
    out.byte(0xde);
    out.byte(0xc9); // fmulp st1, st0
    emit_x87_load_unsigned_integer(out, low, 64, function);
    out.byte(0xde);
    out.byte(0xc1); // faddp st1, st0
    emit_x87_store_pop(out, instruction.operands[0], types.second, function);
    return;
  }
  require_operands(instruction, 2);
  const unsigned source_width = type_width(types.first);
  const mir_model::MirOperand & destination = instruction.operands[0];
  if(types.second == "f80") {
    if(instruction.opcode == mir_model::MirInstruction::MI_UITOFP)
      emit_x87_load_unsigned_integer(out, instruction.operands[1], source_width, function);
    else
      emit_x87_load_signed_integer(out, instruction.operands[1], source_width, function);
    emit_x87_store_pop(out, destination, "f80", function);
    return;
  }
  const XmmRegister target = destination.kind == mir_model::MirOperand::OP_XMM ?
    destination.xmm : XMM_6;
  const X64Register source = materialize_integer_operand(
    out, instruction.operands[1], source_width, function);
  emit_scalar_prefix(out, types.second);
  emit_rex(out, source_width == 64,
           static_cast<X64Register>(xmm_index(target)), source);
  out.byte(0x0f);
  out.byte(0x2a);
  emit_modrm(out, 3, xmm_index(target), source);
  if(destination.kind != mir_model::MirOperand::OP_XMM)
    emit_xmm_store(out, destination, target, types.second, function);
}

void emit_float_to_integer(CodeBuffer & out,
                           const mir_model::MirInstruction & instruction,
                           const mir_model::MirFunction & function)
{
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
  if(types.second == "i128") {
    require_operands(instruction, 3);
    const X64Register low = require_register(instruction.operands[0]);
    const X64Register high = require_register(instruction.operands[1]);
    emit_x87_load(out, instruction.operands[2], types.first, function);

    const bool signed_conversion =
      instruction.opcode == mir_model::MirInstruction::MI_FPTOSI;
    const std::string magnitude = out.internal_label("fptoi128_magnitude");
    const std::string done = out.internal_label("fptoi128_done");
    if(signed_conversion) {
      emit_immediate_move(out, XR_R11, 0);
      out.byte(0xd9);
      out.byte(0xe4); // ftst
      out.byte(0xdf);
      out.byte(0xe0); // fnstsw ax
      out.byte(0x9e); // sahf
      emit_near_jump(out, XC_AE, magnitude);
      out.byte(0xd9);
      out.byte(0xe0); // fchs
      emit_immediate_move(out, XR_R11, 1);
      out.label(magnitude);
    }

    out.byte(0xd9);
    out.byte(0xc0); // fld st0
    mir_model::MirOperand two64;
    two64.kind = mir_model::MirOperand::OP_FLOAT_IMM;
    two64.text = "18446744073709551616.0L";
    emit_x87_load(out, two64, "f80", function);
    out.byte(0xde);
    out.byte(0xf9); // fdivp st1, st0
    emit_x87_store_truncated_unsigned(out, high, 64, function);
    emit_x87_load_unsigned_integer(out,
      instruction.operands[1], 64, function);
    emit_x87_load(out, two64, "f80", function);
    out.byte(0xde);
    out.byte(0xc9); // fmulp st1, st0
    out.byte(0xde);
    out.byte(0xe9); // fsubp st1, st0
    emit_x87_store_truncated_unsigned(out, low, 64, function);

    if(signed_conversion) {
      emit_rex(out, true, XR_R11, XR_R11);
      out.byte(0x85);
      emit_modrm(out, 3, XR_R11, XR_R11);
      emit_near_jump(out, XC_E, done);
      emit_rex(out, true, static_cast<X64Register>(3), low);
      out.byte(0xf7);
      emit_modrm(out, 3, 3, low); // neg low
      emit_rex(out, true, static_cast<X64Register>(2), high);
      out.byte(0x83);
      emit_modrm(out, 3, 2, high);
      out.byte(0); // adc high, 0
      emit_rex(out, true, static_cast<X64Register>(3), high);
      out.byte(0xf7);
      emit_modrm(out, 3, 3, high); // neg high
      out.label(done);
    }
    return;
  }
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  if(types.first == "f80") {
    emit_x87_load(out, instruction.operands[1], types.first, function);
    if(instruction.opcode == mir_model::MirInstruction::MI_FPTOUI)
      emit_x87_store_truncated_unsigned(out, destination,
                                        type_width(types.second), function);
    else
      emit_x87_store_truncated_integer(out, destination,
                                       type_width(types.second), function);
    return;
  }
  materialize_float_operand(out, XMM_7, instruction.operands[1], types.first, function);
  emit_scalar_prefix(out, types.first);
  emit_rex(out, type_width(types.second) == 64, destination,
           static_cast<X64Register>(xmm_index(XMM_7)));
  out.byte(0x0f);
  out.byte(0x2c);
  emit_modrm(out, 3, destination, xmm_index(XMM_7));
}

void emit_float_width_conversion(CodeBuffer & out,
                                 const mir_model::MirInstruction & instruction,
                                 const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
  const mir_model::MirOperand & destination = instruction.operands[0];
  if(types.first == "f80" || types.second == "f80") {
    emit_x87_load(out, instruction.operands[1], types.first, function);
    emit_x87_store_pop(out, destination, types.second, function);
    return;
  }
  const XmmRegister target = destination.kind == mir_model::MirOperand::OP_XMM ?
    destination.xmm : XMM_6;
  materialize_float_operand(out, XMM_7, instruction.operands[1], types.first, function);
  emit_scalar_prefix(out, types.first);
  out.byte(0x0f);
  out.byte(0x5a);
  emit_modrm(out, 3, xmm_index(target), xmm_index(XMM_7));
  if(destination.kind != mir_model::MirOperand::OP_XMM)
    emit_xmm_store(out, destination, target, types.second, function);
}

void emit_alu(CodeBuffer & out, const mir_model::MirInstruction & instruction,
              unsigned register_opcode, unsigned immediate_extension,
              unsigned width = 64)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  const mir_model::MirOperand & source = instruction.operands[1];
  if(source.kind == mir_model::MirOperand::OP_REG) {
    emit_size_prefix(out, width);
    emit_rex(out, width == 64, source.reg, destination, width == 8);
    out.byte(width == 8 ? register_opcode - 1 : register_opcode);
    emit_modrm(out, 3, source.reg, destination);
  } else if(source.kind == mir_model::MirOperand::OP_IMM && width <= 32) {
    emit_size_prefix(out, width);
    emit_rex(out, false, XR_RAX, destination, width == 8);
    out.byte(width == 8 ? 0x80 : 0x81);
    emit_modrm(out, 3, immediate_extension, destination);
    out.little(static_cast<std::uint32_t>(source.imm), width == 8 ? 1 : width / 8);
  } else if(source.kind == mir_model::MirOperand::OP_IMM &&
            source.imm >= INT32_MIN && source.imm <= INT32_MAX) {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0x81);
    emit_modrm(out, 3, immediate_extension, destination);
    out.little(static_cast<std::uint32_t>(source.imm), 4);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_immediate_move(out, XR_R11, static_cast<std::uint64_t>(source.imm));
    mir_model::MirInstruction register_form = instruction;
    register_form.operands[1].kind = mir_model::MirOperand::OP_REG;
    register_form.operands[1].reg = XR_R11;
    emit_alu(out, register_form, register_opcode, immediate_extension, width);
  } else throw std::logic_error("unsupported native ALU operand");
}

void emit_memory_compare(CodeBuffer & out,
                         const mir_model::MirInstruction & instruction,
                         const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const mir_model::MirOperand & address = instruction.operands[0];
  X64Register base = XR_RBP;
  long long displacement = 0;
  if(address.kind == mir_model::MirOperand::OP_FRAME) {
    displacement = actual_frame_offset(function, address.offset);
  } else if(address.kind == mir_model::MirOperand::OP_DEREF) {
    base = address.reg;
    displacement = address.offset;
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text, address.address_binding);
    base = XR_R11;
  } else {
    throw std::logic_error("unsupported memory compare address");
  }
  const unsigned width = type_width(instruction.type);
  const mir_model::MirOperand & source = instruction.operands[1];
  emit_size_prefix(out, width);
  if(source.kind == mir_model::MirOperand::OP_REG) {
    emit_rex(out, width == 64, source.reg, base, width == 8);
    out.byte(width == 8 ? 0x38 : 0x39);
    emit_memory_modrm(out, source.reg, base, displacement);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_rex(out, width == 64, XR_RAX, base, width == 8);
    out.byte(width == 8 ? 0x80 : 0x81);
    emit_memory_modrm(out, 7, base, displacement);
    const unsigned immediate_bytes = width == 8 ? 1 : (width == 16 ? 2 : 4);
    out.little(static_cast<std::uint64_t>(source.imm), immediate_bytes);
  } else {
    throw std::logic_error("unsupported memory compare source");
  }
}

void emit_register_memory_compare(CodeBuffer & out,
                                  const mir_model::MirInstruction & instruction,
                                  const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  const mir_model::MirOperand & address = instruction.operands[1];
  X64Register base = XR_RBP;
  long long displacement = 0;
  if(address.kind == mir_model::MirOperand::OP_FRAME) {
    displacement = actual_frame_offset(function, address.offset);
  } else if(address.kind == mir_model::MirOperand::OP_DEREF) {
    base = address.reg;
    displacement = address.offset;
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text, address.address_binding);
    base = XR_R11;
  } else {
    throw std::logic_error("unsupported register-memory compare address");
  }
  const unsigned width = type_width(instruction.type);
  emit_size_prefix(out, width);
  emit_rex(out, width == 64, destination, base, width == 8);
  out.byte(width == 8 ? 0x3a : 0x3b);
  emit_memory_modrm(out, destination, base, displacement);
}

void emit_imultiply(CodeBuffer & out, const mir_model::MirInstruction & instruction)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  mir_model::MirOperand source = instruction.operands[1];
  if(source.kind == mir_model::MirOperand::OP_IMM &&
     (source.imm < INT32_MIN || source.imm > INT32_MAX)) {
    emit_immediate_move(out, XR_R11, static_cast<std::uint64_t>(source.imm));
    source.kind = mir_model::MirOperand::OP_REG;
    source.reg = XR_R11;
  }
  if(source.kind == mir_model::MirOperand::OP_REG) {
    emit_rex(out, true, destination, source.reg);
    out.byte(0x0f);
    out.byte(0xaf);
    emit_modrm(out, 3, destination, source.reg);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_rex(out, true, destination, destination);
    out.byte(0x69);
    emit_modrm(out, 3, destination, destination);
    out.little(static_cast<std::uint32_t>(source.imm), 4);
  } else throw std::logic_error("unsupported native multiply operand");
}

void emit_integer_extension(CodeBuffer & out,
                            const mir_model::MirInstruction & instruction,
                            bool sign_extend)
{
  require_operands(instruction, 1);
  const X64Register reg = require_register(instruction.operands[0]);
  const unsigned width = type_width(instruction.type);
  if(width == 64) return;
  if(!sign_extend && width == 32) {
    emit_rex(out, false, reg, reg);
    out.byte(0x89);
    emit_modrm(out, 3, reg, reg);
    return;
  }
  emit_rex(out, sign_extend, reg, reg,
           !sign_extend && width == 8 && reg >= XR_RSP && reg < XR_R8);
  if(sign_extend && width == 32) {
    out.byte(0x63);
  } else {
    out.byte(0x0f);
    out.byte(sign_extend ? (width == 8 ? 0xbe : 0xbf) :
                           (width == 8 ? 0xb6 : 0xb7));
  }
  emit_modrm(out, 3, reg, reg);
}

void emit_integer_unary(CodeBuffer & out,
                        const mir_model::MirInstruction & instruction,
                        unsigned extension)
{
  require_operands(instruction, 1);
  const X64Register destination = require_register(instruction.operands[0]);
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0xf7);
  emit_modrm(out, 3, extension, destination);
}

void emit_bswap(CodeBuffer & out, const mir_model::MirInstruction & instruction)
{
  require_operands(instruction, 1);
  const X64Register destination = require_register(instruction.operands[0]);
  const unsigned width = type_width(instruction.type);
  if(width != 32 && width != 64)
    throw std::logic_error("native bswap requires 32 or 64 bits");
  emit_rex(out, width == 64, XR_RAX, destination);
  out.byte(0x0f);
  out.byte(0xc8 + (static_cast<unsigned>(destination) & 7));
}

void emit_divide(CodeBuffer & out, const mir_model::MirInstruction & instruction,
                 unsigned extension)
{
  require_operands(instruction, 1);
  const X64Register divisor = require_register(instruction.operands[0]);
  emit_rex(out, true, XR_RAX, divisor);
  out.byte(0xf7);
  emit_modrm(out, 3, extension, divisor);
}

void emit_shift(CodeBuffer & out, const mir_model::MirInstruction & instruction,
                unsigned extension)
{
  require_operands(instruction, 1);
  const X64Register destination = require_register(instruction.operands[0]);
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0xd3);
  emit_modrm(out, 3, extension, destination);
}

std::string block_target(const std::string & function_name,
                         const mir_model::MirOperand & operand)
{
  if(operand.kind != mir_model::MirOperand::OP_LABEL)
    throw std::logic_error("native branch target is not a label");
  return function_name + "::" + operand.text;
}

const char * const kEhTop = ".__cppgm_eh_top";
const char * const kEhValue = ".__cppgm_eh_value";
const char * const kEhAdjusted = ".__cppgm_eh_adjusted";
const char * const kEhType = ".__cppgm_eh_type";
const char * const kEhSelector = ".__cppgm_eh_selector";
const char * const kEhCaught = ".__cppgm_eh_caught";
const char * const kEhDispatch = ".__cppgm_eh_dispatch";
const char * const kEhResume = ".__cppgm_eh_resume";

bool prepare_explicit_operands(CodeBuffer & out,
                               const mir_model::MirInstruction & instruction,
                               const mir_model::MirFunction * function)
{
  if(instruction.opcode == mir_model::MirInstruction::MI_TEST) {
    require_operands(instruction, 2);
    const X64Register left = require_register(instruction.operands[0]);
    const X64Register right = require_register(instruction.operands[1]);
    if(left != right)
      throw std::logic_error("native zero test requires one repeated register");
    emit_test_register(out, left, type_width(instruction.type));
    return true;
  }
  if(instruction.opcode == mir_model::MirInstruction::MI_COPY_BYTES) {
    require_operands(instruction, 2);
    const X64Register destination = require_register(instruction.operands[0]);
    const X64Register source = require_register(instruction.operands[1]);
    if(destination == XR_RSI && source == XR_RDI) {
      emit_register_move(out, XR_R11, XR_RDI);
      emit_register_move(out, XR_RDI, XR_RSI);
      emit_register_move(out, XR_RSI, XR_R11);
    } else if(source == XR_RDI && destination != XR_RDI) {
      emit_register_move(out, XR_RSI, XR_RDI);
      emit_register_move(out, XR_RDI, destination);
    } else {
      if(destination != XR_RDI) emit_register_move(out, XR_RDI, destination);
      if(source != XR_RSI) emit_register_move(out, XR_RSI, source);
    }
  } else if(instruction.opcode == mir_model::MirInstruction::MI_ZERO_BYTES) {
    require_operands(instruction, 1);
    const X64Register destination = require_register(instruction.operands[0]);
    if(destination != XR_RDI) emit_register_move(out, XR_RDI, destination);
  } else if(instruction.opcode == mir_model::MirInstruction::MI_RET) {
    if(instruction.operands.size() > 1)
      throw std::logic_error("native return has too many operands");
    if(!instruction.operands.empty()) {
      const mir_model::MirOperand & result = instruction.operands[0];
      if(result.kind == mir_model::MirOperand::OP_REG) {
        if(result.reg != XR_RAX) emit_register_move(out, XR_RAX, result.reg);
      } else if(result.kind == mir_model::MirOperand::OP_XMM) {
        if(!function ||
           (function->return_type != "f32" && function->return_type != "f64"))
          throw std::logic_error("native xmm return lacks a scalar-float ABI");
        if(result.xmm != XMM_0)
          emit_xmm_register_move(out, XMM_0, result.xmm,
                                 function->return_type);
      } else {
        throw std::logic_error("native return result is not a register");
      }
    }
  }
  return false;
}

void emit_compare_immediate(CodeBuffer & out, X64Register reg, unsigned value)
{
  emit_rex(out, true, XR_RAX, reg);
  out.byte(0x83);
  emit_modrm(out, 3, 7, reg);
  out.byte(value);
}

void emit_immediate_alu(CodeBuffer & out, X64Register reg,
                        unsigned extension, unsigned value)
{
  emit_rex(out, true, static_cast<X64Register>(extension), reg);
  out.byte(0x83);
  emit_modrm(out, 3, extension, reg);
  out.byte(value);
}

void emit_test_immediate(CodeBuffer & out, X64Register reg, unsigned value)
{
  emit_rex(out, true, XR_RAX, reg);
  out.byte(0xf7);
  emit_modrm(out, 3, 0, reg);
  out.little(value, 4);
}

void emit_indirect_transfer(CodeBuffer & out, X64Register reg, bool call)
{
  emit_rex(out, true, call ? XR_RDX : XR_RSP, reg);
  out.byte(0xff);
  emit_modrm(out, 3, call ? 2 : 4, reg);
}

void emit_eh_push(CodeBuffer & out,
                  const mir_model::MirInstruction & instruction,
                  const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  long long region_kind = instruction.operands[1].imm;
  const std::map<std::string,
    std::vector<mir_model::MirHostEhClause> >::const_iterator clauses =
      function.host_eh_clauses.find(instruction.operands[0].text);
  if(clauses != function.host_eh_clauses.end())
    for(std::size_t i = 0; i < clauses->second.size(); ++i)
      if(clauses->second[i].kind ==
           mir_model::MirHostEhClause::HC_CLEANUP) {
        region_kind = 1;
        break;
      }
  emit_stack_adjust(out, true, 80);
  emit_symbol_move(out, XR_R11, kEhTop);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_store(out, XR_RSP, 0, XR_RAX, 64);
  emit_symbol_move(out, XR_RAX, block_target(function.name,
                                              instruction.operands[0]));
  emit_store(out, XR_RSP, 8, XR_RAX, 64);
  emit_store(out, XR_RSP, 16, XR_RBP, 64);
  emit_lea(out, XR_RAX, XR_RSP, 80);
  emit_store(out, XR_RSP, 24, XR_RAX, 64);
  emit_store(out, XR_RSP, 32, XR_RBX, 64);
  emit_store(out, XR_RSP, 40, XR_R12, 64);
  emit_store(out, XR_RSP, 48, XR_R13, 64);
  emit_store(out, XR_RSP, 56, XR_R14, 64);
  emit_store(out, XR_RSP, 64, XR_R15, 64);
  emit_immediate_move(out, XR_RAX, region_kind);
  emit_store(out, XR_RSP, 72, XR_RAX, 64);
  emit_register_move(out, XR_RAX, XR_RSP);
  emit_store(out, XR_R11, 0, XR_RAX, 64);
}

void emit_eh_pop(CodeBuffer & out)
{
  emit_symbol_move(out, XR_R11, kEhTop);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_load(out, XR_RCX, XR_RAX, 0, 64);
  emit_store(out, XR_R11, 0, XR_RCX, 64);
  emit_load(out, XR_RSP, XR_RAX, 24, 64);
}

void emit_eh_enter_catch(CodeBuffer & out)
{
  const std::string done = out.internal_label("eh_enter_catch_done");
  emit_symbol_move(out, XR_R11, kEhTop);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_test_register(out, XR_RAX);
  emit_condition_jump(out, XC_E, done);
  emit_load(out, XR_RCX, XR_RAX, 72, 64);
  emit_compare_immediate(out, XR_RCX, 3);
  emit_condition_jump(out, XC_NE, done);
  emit_load(out, XR_RCX, XR_RAX, 0, 64);
  emit_store(out, XR_R11, 0, XR_RCX, 64);
  emit_load(out, XR_RSP, XR_RAX, 24, 64);
  out.label(done);
}

void emit_eh_catch(CodeBuffer & out,
                   const mir_model::MirInstruction & instruction)
{
  if(instruction.operands.size() != 1 && instruction.operands.size() != 2)
    throw std::logic_error("invalid MIR EH catch operands");
  emit_eh_enter_catch(out);
  const std::string done = out.internal_label("eh_catch_done");
  emit_symbol_move(out, XR_R11, kEhSelector);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_test_register(out, XR_RAX);
  emit_condition_jump(out, XC_NE, done);
  std::string exact;
  const std::string selected = out.internal_label("eh_catch_selected");
  if(instruction.operands.size() == 2) {
    exact = out.internal_label("eh_catch_exact");
    emit_symbol_move(out, XR_RAX, instruction.operands[1].text,
                     instruction.operands[1].address_binding);
    emit_symbol_move(out, XR_R11, kEhType);
    emit_load(out, XR_RCX, XR_R11, 0, 64);
    emit_register_alu(out, 0x39, XR_RCX, XR_RAX);
    emit_condition_jump(out, XC_E, exact);
    emit_symbol_move(out, XR_R11, kEhValue);
    emit_load(out, XR_RDI, XR_R11, 0, 64);
    emit_register_move(out, XR_RSI, XR_RCX);
    emit_register_move(out, XR_RDX, XR_RAX);
    emit_stack_adjust(out, true, 8);
    out.byte(0xe8); out.relative32(".__cppgm_dynamic_cast_find");
    emit_stack_adjust(out, false, 8);
    emit_test_register(out, XR_RAX);
    emit_condition_jump(out, XC_E, done);
  } else {
    emit_symbol_move(out, XR_R11, kEhValue);
    emit_load(out, XR_RAX, XR_R11, 0, 64);
  }
  if(!exact.empty()) {
    emit_unconditional_jump(out, selected);
    out.label(exact);
    emit_symbol_move(out, XR_R11, kEhValue);
    emit_load(out, XR_RAX, XR_R11, 0, 64);
  }
  out.label(selected);
  emit_symbol_move(out, XR_R11, kEhAdjusted);
  emit_store(out, XR_R11, 0, XR_RAX, 64);
  emit_symbol_move(out, XR_R11, kEhSelector);
  emit_immediate_move(out, XR_RAX, instruction.operands[0].imm);
  emit_store(out, XR_R11, 0, XR_RAX, 64);
  out.label(done);
}

bool emit_eh_instruction(CodeBuffer & out,
                         const mir_model::MirInstruction & instruction,
                         const mir_model::MirFunction * function)
{
  switch(instruction.opcode) {
  case mir_model::MirInstruction::MI_EH_PUSH:
    if(!function) throw std::logic_error("EH push outside function");
    emit_eh_push(out, instruction, *function); return true;
  case mir_model::MirInstruction::MI_EH_POP:
    require_operands(instruction, 0); emit_eh_pop(out); return true;
  case mir_model::MirInstruction::MI_EH_CATCH: emit_eh_catch(out, instruction); return true;
  case mir_model::MirInstruction::MI_EH_FILTER: return true;
  case mir_model::MirInstruction::MI_EH_CLEANUP_CLAUSE:
    require_operands(instruction, 0); return true;
  case mir_model::MirInstruction::MI_LOAD_EXCEPTION:
  case mir_model::MirInstruction::MI_LOAD_EXCEPTION_SELECTOR:
    require_operands(instruction, 1);
    emit_eh_enter_catch(out);
    emit_symbol_move(out, XR_R11,
      instruction.opcode == mir_model::MirInstruction::MI_LOAD_EXCEPTION ?
      kEhValue : kEhSelector);
    emit_load(out, require_register(instruction.operands[0]), XR_R11, 0,
              type_width(instruction.type));
    return true;
  case mir_model::MirInstruction::MI_THROW:
    require_operands(instruction, 1);
    emit_symbol_move(out, XR_R11, kEhValue);
    emit_store(out, XR_R11, 0, require_register(instruction.operands[0]), 64);
    emit_symbol_move(out, XR_R11, kEhType);
    emit_immediate_move(out, XR_RAX, 0);
    emit_store(out, XR_R11, 0, XR_RAX, 64);
    emit_unconditional_jump(out, kEhDispatch); return true;
  case mir_model::MirInstruction::MI_RESUME:
    require_operands(instruction, 0);
    emit_unconditional_jump(out, kEhResume); return true;
  default: return false;
  }
}

bool emit_atomic_instruction(CodeBuffer & out,
                             const mir_model::MirInstruction & instruction,
                             const mir_model::MirFunction * function)
{
  switch(instruction.opcode) {
  case mir_model::MirInstruction::MI_MFENCE:
    require_operands(instruction, 0);
    out.byte(0x0f);
    out.byte(0xae);
    out.byte(0xf0);
    return true;
  case mir_model::MirInstruction::MI_XCHG:
    if(!function) throw std::logic_error("atomic exchange outside function");
    emit_atomic_memory(out, instruction, *function, false, false, 0x86, 0x87);
    return true;
  case mir_model::MirInstruction::MI_LOCK_XADD:
    if(!function) throw std::logic_error("atomic fetch-add outside function");
    emit_atomic_memory(out, instruction, *function, true, true, 0xc0, 0xc1);
    return true;
  case mir_model::MirInstruction::MI_LOCK_CMPXCHG:
    if(!function) throw std::logic_error("atomic compare-exchange outside function");
    emit_atomic_memory(out, instruction, *function, true, true, 0xb0, 0xb1);
    return true;
  case mir_model::MirInstruction::MI_LOCK_CMPXCHG16B: {
    if(!function) throw std::logic_error("atomic i128 compare-exchange outside function");
    require_operands(instruction, 1);
    X64Register base = XR_RBP;
    long long displacement = 0;
    float_address(out, instruction.operands[0], *function, base, displacement);
    out.byte(0xf0);
    emit_rex(out, true, XR_RCX, base);
    out.byte(0x0f);
    out.byte(0xc7);
    emit_memory_modrm(out, 1, base, displacement);
    return true;
  }
  default:
    return false;
  }
}

bool emit_i128_instruction(CodeBuffer & out,
                           const mir_model::MirInstruction & instruction)
{
  switch(instruction.opcode) {
  case mir_model::MirInstruction::MI_I128_SHL:
  case mir_model::MirInstruction::MI_I128_SHR:
  case mir_model::MirInstruction::MI_I128_SAR:
    require_operands(instruction, 0);
    emit_i128_shift(out, instruction.opcode);
    return true;
  case mir_model::MirInstruction::MI_I128_UDIV:
  case mir_model::MirInstruction::MI_I128_UMOD:
  case mir_model::MirInstruction::MI_I128_SDIV:
  case mir_model::MirInstruction::MI_I128_SMOD:
    require_operands(instruction, 0);
    emit_i128_division(out, instruction.opcode);
    return true;
  default:
    return false;
  }
}

void emit_tls_address_instruction(
    CodeBuffer & out, const mir_model::MirInstruction & instruction)
{
  require_operands(instruction, 2);
  if(instruction.operands[1].kind != mir_model::MirOperand::OP_SYMBOL ||
     instruction.tls_storage_symbol.empty())
    throw std::logic_error("TLS address source has invalid symbol facts");
  if(out.relocatable_addresses())
    emit_tls_address(out, require_register(instruction.operands[0]),
                     instruction.tls_storage_symbol);
  else
    emit_symbol_move(out, require_register(instruction.operands[0]),
                     instruction.operands[1].text,
                     instruction.operands[1].address_binding);
}

void emit_instruction(CodeBuffer & out, const mir_model::MirInstruction & instruction,
                      const mir_model::MirFunction * function) {
  if(emit_eh_instruction(out, instruction, function) || emit_atomic_instruction(out, instruction, function) ||
     emit_i128_instruction(out, instruction)) return;
  if(prepare_explicit_operands(out, instruction, function)) return;
  switch(instruction.opcode) {
  case mir_model::MirInstruction::MI_MOV:
    emit_move(out, instruction);
    return;
  case mir_model::MirInstruction::MI_LOAD:
    if(!function) throw std::logic_error("load outside function");
    require_operands(instruction, 2);
    emit_address_load(out, require_register(instruction.operands[0]), instruction.operands[1],
                      type_width(instruction.type), *function);
    return;
  case mir_model::MirInstruction::MI_STORE:
    if(!function) throw std::logic_error("store outside function");
    require_operands(instruction, 2);
    emit_address_store(out, instruction.operands[0], require_register(instruction.operands[1]),
                       type_width(instruction.type), *function);
    return;
  case mir_model::MirInstruction::MI_LEA:
    if(!function) throw std::logic_error("lea outside function");
    require_operands(instruction, 2);
    if(instruction.operands[1].kind == mir_model::MirOperand::OP_FRAME) {
      emit_lea(out, require_register(instruction.operands[0]), XR_RBP,
               actual_frame_offset(*function, instruction.operands[1].offset));
      return;
    }
    if(instruction.operands[1].kind != mir_model::MirOperand::OP_DEREF)
      throw std::logic_error("native lea source is not memory-shaped");
    emit_lea(out, require_register(instruction.operands[0]),
             instruction.operands[1].reg, instruction.operands[1].offset);
    return;
  case mir_model::MirInstruction::MI_FMOV:
    if(!function) throw std::logic_error("floating move outside function");
    emit_float_move(out, instruction, *function);
    return;
  case mir_model::MirInstruction::MI_FNEG:
    if(!function) throw std::logic_error("floating negate outside function");
    emit_float_negate(out, instruction, *function);
    return;
  case mir_model::MirInstruction::MI_FADD:
    if(!function) throw std::logic_error("floating add outside function");
    emit_float_binary(out, instruction, *function, 0x58);
    return;
  case mir_model::MirInstruction::MI_FSUB:
    if(!function) throw std::logic_error("floating subtract outside function");
    emit_float_binary(out, instruction, *function, 0x5c);
    return;
  case mir_model::MirInstruction::MI_FMUL:
    if(!function) throw std::logic_error("floating multiply outside function");
    emit_float_binary(out, instruction, *function, 0x59);
    return;
  case mir_model::MirInstruction::MI_FDIV:
    if(!function) throw std::logic_error("floating divide outside function");
    emit_float_binary(out, instruction, *function, 0x5e);
    return;
  case mir_model::MirInstruction::MI_FCMP:
    if(!function) throw std::logic_error("floating compare outside function");
    require_operands(instruction, 2);
    emit_float_compare_flags(out, instruction.operands[0], instruction.operands[1],
                             instruction.type, *function);
    return;
  case mir_model::MirInstruction::MI_FEQ:
  case mir_model::MirInstruction::MI_FNE:
  case mir_model::MirInstruction::MI_FLT:
  case mir_model::MirInstruction::MI_FGT:
  case mir_model::MirInstruction::MI_FLE:
  case mir_model::MirInstruction::MI_FGE:
    if(!function) throw std::logic_error("floating comparison outside function");
    emit_float_compare_value(out, instruction, *function);
    return;
  case mir_model::MirInstruction::MI_SITOFP:
  case mir_model::MirInstruction::MI_UITOFP:
    if(!function) throw std::logic_error("integer-to-float conversion outside function");
    emit_integer_to_float(out, instruction, *function);
    return;
  case mir_model::MirInstruction::MI_FPTOSI:
  case mir_model::MirInstruction::MI_FPTOUI:
    if(!function) throw std::logic_error("float-to-integer conversion outside function");
    emit_float_to_integer(out, instruction, *function);
    return;
  case mir_model::MirInstruction::MI_FPEXT:
  case mir_model::MirInstruction::MI_FPTRUNC:
    if(!function) throw std::logic_error("floating width conversion outside function");
    emit_float_width_conversion(out, instruction, *function);
    return;
  case mir_model::MirInstruction::MI_FSTP:
    if(!function) throw std::logic_error("x87 store outside function");
    require_operands(instruction, 1);
    emit_x87_store_pop(out, instruction.operands[0], instruction.type, *function);
    return;
  case mir_model::MirInstruction::MI_FPOP:
    require_operands(instruction, 0);
    emit_x87_pop(out);
    return;
  case mir_model::MirInstruction::MI_ADD:
    emit_alu(out, instruction, 0x01, 0);
    return;
  case mir_model::MirInstruction::MI_SUB:
    emit_alu(out, instruction, 0x29, 5);
    return;
  case mir_model::MirInstruction::MI_AND:
    emit_alu(out, instruction, 0x21, 4);
    return;
  case mir_model::MirInstruction::MI_OR:
    emit_alu(out, instruction, 0x09, 1);
    return;
  case mir_model::MirInstruction::MI_XOR:
    emit_alu(out, instruction, 0x31, 6);
    return;
  case mir_model::MirInstruction::MI_IMUL:
    emit_imultiply(out, instruction);
    return;
  case mir_model::MirInstruction::MI_MUL:
    emit_divide(out, instruction, 4);
    return;
  case mir_model::MirInstruction::MI_CMP:
    if(instruction.operands.size() == 2 &&
       instruction.operands[0].kind == mir_model::MirOperand::OP_REG &&
       instruction.operands[1].kind == mir_model::MirOperand::OP_IMM &&
       instruction.operands[1].imm == 0) {
      emit_test_register(out, instruction.operands[0].reg,
                         type_width(instruction.type));
    } else if(instruction.operands.size() == 2 &&
       (instruction.operands[0].kind == mir_model::MirOperand::OP_FRAME ||
        instruction.operands[0].kind == mir_model::MirOperand::OP_DEREF ||
        instruction.operands[0].kind == mir_model::MirOperand::OP_GLOBAL)) {
      if(!function) throw std::logic_error("memory compare outside function");
      emit_memory_compare(out, instruction, *function);
    } else if(instruction.operands.size() == 2 &&
              (instruction.operands[1].kind == mir_model::MirOperand::OP_FRAME ||
               instruction.operands[1].kind == mir_model::MirOperand::OP_DEREF ||
               instruction.operands[1].kind == mir_model::MirOperand::OP_GLOBAL)) {
      if(!function) throw std::logic_error("memory compare outside function");
      emit_register_memory_compare(out, instruction, *function);
    } else {
      emit_alu(out, instruction, 0x39, 7, type_width(instruction.type));
    }
    return;
  case mir_model::MirInstruction::MI_SETCC:
    require_operands(instruction, 1);
    emit_set_condition(out, instruction.condition, require_register(instruction.operands[0]));
    return;
  case mir_model::MirInstruction::MI_MOVZX:
    require_operands(instruction, 2);
    emit_move_zero_extended_byte(out, require_register(instruction.operands[0]),
                                 require_register(instruction.operands[1]));
    return;
  case mir_model::MirInstruction::MI_SEXT:
    emit_integer_extension(out, instruction, true);
    return;
  case mir_model::MirInstruction::MI_ZEXT:
    emit_integer_extension(out, instruction, false);
    return;
  case mir_model::MirInstruction::MI_NEG:
    emit_integer_unary(out, instruction, 3);
    return;
  case mir_model::MirInstruction::MI_NOT:
    emit_integer_unary(out, instruction, 2);
    return;
  case mir_model::MirInstruction::MI_BSWAP:
    emit_bswap(out, instruction);
    return;
  case mir_model::MirInstruction::MI_CQO:
    require_operands(instruction, 0);
    out.byte(0x48);
    out.byte(0x99);
    return;
  case mir_model::MirInstruction::MI_IDIV:
    emit_divide(out, instruction, 7);
    return;
  case mir_model::MirInstruction::MI_DIV:
    emit_divide(out, instruction, 6);
    return;
  case mir_model::MirInstruction::MI_SHL_CL:
    emit_shift(out, instruction, 4);
    return;
  case mir_model::MirInstruction::MI_SHR_CL:
    emit_shift(out, instruction, 5);
    return;
  case mir_model::MirInstruction::MI_SAR_CL:
    emit_shift(out, instruction, 7);
    return;
  case mir_model::MirInstruction::MI_TLS_ADDR:
    emit_tls_address_instruction(out, instruction);
    return;
  case mir_model::MirInstruction::MI_JCC:
    if(!function) throw std::logic_error("conditional branch outside function");
    require_operands(instruction, 1);
    emit_condition_jump(out, instruction.condition,
      block_target(function->name, instruction.operands[0]));
    return;
  case mir_model::MirInstruction::MI_JMP:
    if(!function) throw std::logic_error("jump outside function");
    require_operands(instruction, 1);
    emit_unconditional_jump(out,
      block_target(function->name, instruction.operands[0]));
    return;
  case mir_model::MirInstruction::MI_CALL:
    require_operands(instruction, 1);
    if(instruction.operands[0].kind != mir_model::MirOperand::OP_SYMBOL)
      throw std::logic_error("direct call target is not a symbol");
    out.byte(0xe8);
    out.relative32(instruction.operands[0].text);
    return;
  case mir_model::MirInstruction::MI_CALL_INDIRECT:
    require_operands(instruction, 1);
    emit_rex(out, true, XR_RDX, require_register(instruction.operands[0]));
    out.byte(0xff);
    emit_modrm(out, 3, 2, require_register(instruction.operands[0]));
    return;
  case mir_model::MirInstruction::MI_COPY_BYTES:
    emit_immediate_move(out, XR_RCX, instruction.byte_count);
    out.byte(0xf3);
    out.byte(0xa4);
    return;
  case mir_model::MirInstruction::MI_ZERO_BYTES:
    emit_immediate_move(out, XR_RCX, instruction.byte_count);
    out.byte(0x31);
    out.byte(0xc0);
    out.byte(0xf3);
    out.byte(0xaa);
    return;
  case mir_model::MirInstruction::MI_RET:
    if(!function) throw std::logic_error("return outside function");
    emit_function_return(out, *function);
    return;
  case mir_model::MirInstruction::MI_FRET:
    if(!function) throw std::logic_error("x87 return outside function");
    require_operands(instruction, 1);
    emit_x87_load(out, instruction.operands[0], instruction.type, *function);
    emit_function_return(out, *function);
    return;
  case mir_model::MirInstruction::MI_EXIT:
    emit_immediate_move(out, XR_RAX, 60); out.byte(0x0f); out.byte(0x05); return;
  default:
    throw std::logic_error("MIR opcode is not implemented by foundation encoder");
  }
}

struct ConstantByteStore
{
  X64Register base = XR_RAX;
  X64Register address = XR_RAX;
  X64Register value = XR_RAX;
  long long offset = 0;
  unsigned char byte = 0;
};

bool parse_constant_byte_store(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, ConstantByteStore * result)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(start > instructions.size() || instructions.size() - start < 4)
    return false;
  const MirInstruction & copy = instructions[start];
  const MirInstruction & address = instructions[start + 1];
  const MirInstruction & value = instructions[start + 2];
  const MirInstruction & store = instructions[start + 3];
  if(copy.opcode != MirInstruction::MI_MOV || copy.operands.size() != 2 ||
     copy.operands[0].kind != MirOperand::OP_REG ||
     copy.operands[1].kind != MirOperand::OP_REG ||
     address.opcode != MirInstruction::MI_LEA ||
     address.operands.size() != 2 ||
     address.operands[0].kind != MirOperand::OP_REG ||
     address.operands[1].kind != MirOperand::OP_DEREF ||
     value.opcode != MirInstruction::MI_MOV || value.operands.size() != 2 ||
     value.operands[0].kind != MirOperand::OP_REG ||
     value.operands[1].kind != MirOperand::OP_IMM ||
     store.opcode != MirInstruction::MI_STORE || store.type != "i8" ||
     store.operands.size() != 2 ||
     store.operands[0].kind != MirOperand::OP_DEREF ||
     store.operands[1].kind != MirOperand::OP_REG)
    return false;
  result->address = copy.operands[0].reg;
  result->base = copy.operands[1].reg;
  result->value = value.operands[0].reg;
  if(result->address == result->base || result->value == result->base ||
     result->value == result->address ||
     address.operands[0].reg != result->address ||
     address.operands[1].reg != result->address ||
     store.operands[0].reg != result->address ||
     store.operands[1].reg != result->value)
    return false;
  const long long address_offset = address.operands[1].offset;
  const long long store_offset = store.operands[0].offset;
  if((store_offset > 0 && address_offset >
        std::numeric_limits<long long>::max() - store_offset) ||
     (store_offset < 0 && address_offset <
        std::numeric_limits<long long>::min() - store_offset))
    return false;
  result->offset = address_offset + store_offset;
  result->byte = static_cast<unsigned char>(value.operands[1].imm);
  return true;
}

std::size_t emit_coalesced_constant_byte_stores(
    CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function)
{
  std::vector<ConstantByteStore> stores;
  std::size_t cursor = start;
  for(;; cursor += 4) {
    ConstantByteStore next;
    if(!parse_constant_byte_store(instructions, cursor, &next)) break;
    if(!stores.empty()) {
      const ConstantByteStore & previous = stores.back();
      if(next.base != previous.base || next.address != previous.address ||
         next.value != previous.value ||
         previous.offset == std::numeric_limits<long long>::max() ||
         next.offset != previous.offset + 1)
        break;
    }
    stores.push_back(next);
  }
  if(stores.size() < 4) return 0;
  std::size_t position = 0;
  while(position < stores.size()) {
    const std::size_t remaining = stores.size() - position;
    const std::size_t width = remaining >= 8 ? 8 :
      remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
    std::uint64_t packed = 0;
    for(std::size_t i = 0; i < width; ++i)
      packed |= static_cast<std::uint64_t>(stores[position + i].byte) <<
        (i * 8);
    emit_immediate_move(out, stores[position].value, packed);
    emit_store(out, stores[position].base, stores[position].offset,
      stores[position].value, static_cast<unsigned>(width * 8));
    position += width;
  }
  // The removed address and immediate setup instructions define physical
  // registers in MIR.  Re-emit the final definitions so any later use sees
  // exactly the state produced by the scalar sequence.
  const std::size_t final_setup = start + (stores.size() - 1) * 4;
  for(std::size_t i = 0; i < 3; ++i)
    emit_instruction(out, instructions[final_setup + i], &function);
  return stores.size() * 4;
}

bool parse_forwarded_frame_reload(
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, long long * frame_offset,
    X64Register * source, X64Register * destination)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(start > instructions.size() || instructions.size() - start < 2)
    return false;
  const MirInstruction & store = instructions[start];
  const MirInstruction & load = instructions[start + 1];
  if(store.opcode != MirInstruction::MI_STORE ||
     load.opcode != MirInstruction::MI_LOAD ||
     store.type != load.type || type_width(store.type) > 64 ||
     store.operands.size() != 2 || load.operands.size() != 2 ||
     store.operands[0].kind != MirOperand::OP_FRAME ||
     store.operands[1].kind != MirOperand::OP_REG ||
     load.operands[0].kind != MirOperand::OP_REG ||
     load.operands[1].kind != MirOperand::OP_FRAME ||
     store.operands[0].offset != load.operands[1].offset)
    return false;
  *frame_offset = store.operands[0].offset;
  *source = store.operands[1].reg;
  *destination = load.operands[0].reg;
  return true;
}

struct FrameUseFacts
{
  std::size_t count = 0;
  const mir_model::MirInstruction * store = 0;
  const mir_model::MirInstruction * load = 0;
  std::size_t store_block = 0;
  std::size_t store_index = 0;
  std::size_t load_block = 0;
  std::size_t load_index = 0;
};

struct FrameReloadPlan
{
  std::unordered_set<long long> adjacent;
  std::unordered_map<long long, X64Register> delayed;
};

bool preserves_forwarded_register(
    const mir_model::MirInstruction & instruction, X64Register source)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(instruction.opcode != MirInstruction::MI_LOAD &&
     instruction.opcode != MirInstruction::MI_LEA &&
     instruction.opcode != MirInstruction::MI_MOV)
    return false;
  return !instruction.operands.empty() &&
    instruction.operands[0].kind == MirOperand::OP_REG &&
    instruction.operands[0].reg != source;
}

FrameReloadPlan find_single_use_frame_reloads(
    const mir_model::MirFunction & function)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  std::unordered_map<long long, FrameUseFacts> uses;
  uses.reserve(function.frame_bindings.size());
  for(std::size_t i = 0; i < function.frame_bindings.size(); ++i) {
    const mir_model::MirFrameBinding & binding = function.frame_bindings[i];
    if(binding.kind == mir_model::MirFrameBinding::FB_TEMP &&
       (binding.type == "ptr" || binding.type == "i64" ||
        binding.type == "i32" || binding.type == "u32" ||
        binding.type == "i16" || binding.type == "u16" ||
        binding.type == "i8" || binding.type == "u8" ||
        binding.type == "i1"))
      uses.emplace(binding.offset, FrameUseFacts());
  }
  if(function.host_eh_enabled) {
    uses.erase(function.host_eh_exception_offset);
    uses.erase(function.host_eh_selector_offset);
  }
  FrameReloadPlan result;
  if(uses.empty()) return result;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::vector<mir_model::MirInstruction> & instructions =
      function.blocks[i].instructions;
    for(std::size_t j = 0; j < instructions.size(); ++j) {
      for(std::size_t k = 0;
          k < instructions[j].operands.size(); ++k) {
        const mir_model::MirOperand & operand = instructions[j].operands[k];
        if(operand.kind != mir_model::MirOperand::OP_FRAME) continue;
        const std::unordered_map<long long, FrameUseFacts>::iterator found =
          uses.find(operand.offset);
        if(found != uses.end()) ++found->second.count;
      }
      const MirInstruction & instruction = instructions[j];
      if(instruction.operands.size() != 2) continue;
      if(instruction.opcode == MirInstruction::MI_STORE &&
         instruction.operands[0].kind == MirOperand::OP_FRAME &&
         instruction.operands[1].kind == MirOperand::OP_REG) {
        const std::unordered_map<long long, FrameUseFacts>::iterator found =
          uses.find(instruction.operands[0].offset);
        if(found != uses.end()) {
          found->second.store = &instruction;
          found->second.store_block = i;
          found->second.store_index = j;
        }
      } else if(instruction.opcode == MirInstruction::MI_LOAD &&
                instruction.operands[0].kind == MirOperand::OP_REG &&
                instruction.operands[1].kind == MirOperand::OP_FRAME) {
        const std::unordered_map<long long, FrameUseFacts>::iterator found =
          uses.find(instruction.operands[1].offset);
        if(found != uses.end()) {
          found->second.load = &instruction;
          found->second.load_block = i;
          found->second.load_index = j;
        }
      }
    }
  }
  result.adjacent.reserve(uses.size());
  result.delayed.reserve(uses.size() / 4);
  for(std::unordered_map<long long, FrameUseFacts>::const_iterator use =
        uses.begin(); use != uses.end(); ++use) {
    const FrameUseFacts & facts = use->second;
    if(facts.count != 2 || !facts.store || !facts.load ||
       facts.store_block != facts.load_block ||
       facts.store->type != facts.load->type ||
       facts.store_index >= facts.load_index)
      continue;
    const std::size_t gap = facts.load_index - facts.store_index - 1;
    if(gap == 0) {
      result.adjacent.insert(use->first);
      continue;
    }
    const X64Register source = facts.store->operands[1].reg;
    bool preserved = gap != 0 && gap <= 5;
    for(std::size_t i = facts.store_index + 1;
        preserved && i < facts.load_index; ++i)
      preserved = preserves_forwarded_register(
        function.blocks[facts.store_block].instructions[i], source);
    if(preserved)
      result.delayed.emplace(use->first, source);
  }
  return result;
}

bool emit_delayed_frame_forwarding(
    CodeBuffer & out, const mir_model::MirInstruction & instruction,
    const FrameReloadPlan & plan)
{
  using mir_model::MirInstruction;
  using mir_model::MirOperand;
  if(instruction.opcode == MirInstruction::MI_STORE &&
     instruction.operands.size() == 2 &&
     instruction.operands[0].kind == MirOperand::OP_FRAME &&
     plan.delayed.count(instruction.operands[0].offset))
    return true;
  if(instruction.opcode != MirInstruction::MI_LOAD ||
     instruction.operands.size() != 2 ||
     instruction.operands[0].kind != MirOperand::OP_REG ||
     instruction.operands[1].kind != MirOperand::OP_FRAME)
    return false;
  const std::unordered_map<long long, X64Register>::const_iterator source =
    plan.delayed.find(instruction.operands[1].offset);
  if(source == plan.delayed.end()) return false;
  const X64Register destination = instruction.operands[0].reg;
  if(source->second != destination)
    emit_sized_register_move(out, destination, source->second,
      type_width(instruction.type));
  return true;
}

std::size_t emit_forwarded_frame_reload(
    CodeBuffer & out,
    const std::vector<mir_model::MirInstruction> & instructions,
    std::size_t start, const mir_model::MirFunction & function,
    const FrameReloadPlan & frame_reload_plan)
{
  long long frame_offset = 0;
  X64Register source = XR_RAX;
  X64Register destination = XR_RAX;
  if(!parse_forwarded_frame_reload(instructions, start, &frame_offset,
       &source, &destination)) return 0;
  if(!frame_reload_plan.adjacent.count(frame_offset))
    emit_instruction(out, instructions[start], &function);
  if(source != destination)
    emit_sized_register_move(out, destination, source,
      type_width(instructions[start].type));
  return 2;
}

void emit_function(CodeBuffer & out, const mir_model::MirFunction & function)
{
  // The x86-64 member-function-pointer representation reserves bit zero of
  // the target word as the virtual-slot tag.  Keep every native function
  // entry at least two-byte aligned so a direct target cannot carry that tag.
  out.align(2);
  const std::size_t function_start = out.size();
  out.label(function.name);
  const std::string object_symbol = native_object_symbol(function.object_symbol);
  if(!object_symbol.empty() && object_symbol != function.name)
    out.label(object_symbol);
  emit_function_prologue(out, function);
  const FrameReloadPlan frame_reload_plan =
    find_single_use_frame_reloads(function);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const mir_model::MirBlock & block = function.blocks[i];
    out.label(function.name + "::" + block.label);
    const std::vector<bool> flags_live =
      condition_flags_live_before(block.instructions);
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      std::size_t folded = 0;
      if(address_folding::is_setup_load_sequence(block.instructions, j))
        folded = address_folding::emit_dead_setup_load(
          out, block.instructions, j, function);
      if(folded) {
        j += folded - 1;
        continue;
      }
      const std::size_t divided = emit_power_of_two_division(
        out, block.instructions, j);
      if(divided) {
        j += divided - 1;
        continue;
      }
      if(emit_delayed_frame_forwarding(out,
           block.instructions[j], frame_reload_plan))
        continue;
      const std::size_t forwarded = emit_forwarded_frame_reload(
        out, block.instructions, j, function, frame_reload_plan);
      if(forwarded) {
        j += forwarded - 1;
        continue;
      }
      const std::size_t coalesced = emit_coalesced_constant_byte_stores(
        out, block.instructions, j, function);
      if(coalesced) {
        j += coalesced - 1;
        continue;
      }
      if(emit_flag_safe_zero_move(
           out, block.instructions[j], flags_live[j]))
        continue;
      emit_instruction(out, block.instructions[j], &function);
    }
  }
  out.relax_forward_branches(function_start);
}

void emit_runtime_labels(CodeBuffer & out,
                         const mir_model::MirRuntimeFunction & runtime)
{
  out.label(runtime.name);
  const std::string object_symbol = native_object_symbol(runtime.object_symbol);
  if(!object_symbol.empty() && object_symbol != runtime.name)
    out.label(object_symbol);
}

void emit_eh_restore(CodeBuffer & out)
{
  emit_load(out, XR_RBX, XR_RAX, 32, 64);
  emit_load(out, XR_R12, XR_RAX, 40, 64);
  emit_load(out, XR_R13, XR_RAX, 48, 64);
  emit_load(out, XR_R14, XR_RAX, 56, 64);
  emit_load(out, XR_R15, XR_RAX, 64, 64);
  emit_load(out, XR_RBP, XR_RAX, 16, 64);
}

void emit_eh_dispatch(CodeBuffer & out)
{
  const std::string cleanup = ".__cppgm_eh_dispatch_cleanup";
  const std::string skip = ".__cppgm_eh_dispatch_skip";
  const std::string unhandled = ".__cppgm_eh_unhandled";
  out.label(kEhDispatch);
  emit_symbol_move(out, XR_R11, kEhTop);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_test_register(out, XR_RAX);
  emit_condition_jump(out, XC_E, unhandled);
  emit_load(out, XR_RCX, XR_RAX, 72, 64);
  emit_compare_immediate(out, XR_RCX, 2);
  emit_condition_jump(out, XC_E, skip);
  emit_compare_immediate(out, XR_RCX, 3);
  emit_condition_jump(out, XC_E, skip);
  emit_compare_immediate(out, XR_RCX, 1);
  emit_condition_jump(out, XC_E, cleanup);
  emit_immediate_move(out, XR_RCX, 3);
  emit_store(out, XR_RAX, 72, XR_RCX, 64);
  emit_load(out, XR_R11, XR_RAX, 8, 64);
  emit_eh_restore(out);
  emit_register_move(out, XR_RSP, XR_RAX);
  emit_indirect_transfer(out, XR_R11, false);
  out.label(cleanup);
  emit_immediate_move(out, XR_RCX, 2);
  emit_store(out, XR_RAX, 72, XR_RCX, 64);
  emit_load(out, XR_R11, XR_RAX, 8, 64);
  emit_eh_restore(out);
  emit_register_move(out, XR_RSP, XR_RAX);
  emit_indirect_transfer(out, XR_R11, false);
  out.label(skip);
  emit_load(out, XR_RCX, XR_RAX, 0, 64);
  emit_store(out, XR_R11, 0, XR_RCX, 64);
  emit_unconditional_jump(out, kEhDispatch);
  out.label(unhandled);
  emit_immediate_move(out, XR_RDI, 134);
  emit_immediate_move(out, XR_RAX, 60);
  out.byte(0x0f); out.byte(0x05);
}

void emit_eh_resume(CodeBuffer & out,
                    const std::vector<mir_model::MirRuntimeFunction> & runtimes)
{
  for(std::size_t i = 0; i < runtimes.size(); ++i)
    if(runtimes[i].kind == mir_model::RuntimeFunction::RF_EH_RESUME)
      emit_runtime_labels(out, runtimes[i]);
  out.label(kEhResume);
  emit_symbol_move(out, XR_R11, kEhTop);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_test_register(out, XR_RAX);
  emit_condition_jump(out, XC_E, kEhDispatch);
  emit_load(out, XR_RCX, XR_RAX, 72, 64);
  emit_compare_immediate(out, XR_RCX, 2);
  emit_condition_jump(out, XC_NE, kEhDispatch);
  emit_load(out, XR_RCX, XR_RAX, 0, 64);
  emit_store(out, XR_R11, 0, XR_RCX, 64);
  emit_unconditional_jump(out, kEhDispatch);
}

void emit_eh_allocate(CodeBuffer & out)
{
  emit_lea(out, XR_RSI, XR_RDI, 32);
  emit_immediate_move(out, XR_RAX, 9);
  emit_immediate_move(out, XR_RDI, 0);
  emit_immediate_move(out, XR_RDX, 3);
  emit_immediate_move(out, XR_R10, 0x22);
  emit_immediate_move(out, XR_R8, UINT64_MAX);
  emit_immediate_move(out, XR_R9, 0);
  out.byte(0x0f); out.byte(0x05);
  emit_lea(out, XR_RAX, XR_RAX, 32);
  out.byte(0xc3);
}

void emit_malloc_runtime(CodeBuffer & out)
{
  emit_register_move(out, XR_RSI, XR_RDI);
  emit_immediate_move(out, XR_RAX, 9);
  emit_immediate_move(out, XR_RDI, 0);
  emit_immediate_move(out, XR_RDX, 3);
  emit_immediate_move(out, XR_R10, 0x22);
  emit_immediate_move(out, XR_R8, UINT64_MAX);
  emit_immediate_move(out, XR_R9, 0);
  out.byte(0x0f); out.byte(0x05); out.byte(0xc3);
}

std::string runtime_data_name(
    const std::vector<mir_model::MirRuntimeData> & data,
    mir_model::RuntimeData::Kind kind)
{
  for(std::size_t i = 0; i < data.size(); ++i)
    if(data[i].kind == kind) return data[i].name;
  return std::string();
}

void emit_dynamic_cast_find(
    CodeBuffer & out, const std::vector<mir_model::MirRuntimeData> & data)
{
  const std::string helper = ".__cppgm_dynamic_cast_find";
  const std::string si = out.internal_label("dynamic_cast_si");
  const std::string vmi = out.internal_label("dynamic_cast_vmi");
  const std::string loop = out.internal_label("dynamic_cast_loop");
  const std::string skip = out.internal_label("dynamic_cast_skip");
  const std::string record = out.internal_label("dynamic_cast_record");
  const std::string done = out.internal_label("dynamic_cast_done");
  const std::string ambiguous = out.internal_label("dynamic_cast_ambiguous");
  const std::string si_type = runtime_data_name(
    data, mir_model::RuntimeData::RD_RTTI_SI);
  const std::string vmi_type = runtime_data_name(
    data, mir_model::RuntimeData::RD_RTTI_VMI);
  out.label(helper);
  emit_push(out, XR_RBP); emit_register_move(out, XR_RBP, XR_RSP);
  emit_push(out, XR_RBX); emit_push(out, XR_R12); emit_push(out, XR_R13);
  emit_push(out, XR_R14); emit_push(out, XR_R15); emit_stack_adjust(out, true, 8);
  emit_register_move(out, XR_RBX, XR_RDI);
  emit_register_move(out, XR_R12, XR_RSI);
  emit_register_move(out, XR_R13, XR_RDX);
  emit_immediate_move(out, XR_RAX, 0); emit_store(out, XR_RBP, -48, XR_RAX, 64);
  emit_register_alu(out, 0x39, XR_R12, XR_R13);
  emit_condition_jump(out, XC_E, record);
  emit_load(out, XR_RCX, XR_R12, 0, 64);
  if(!si_type.empty()) {
    emit_symbol_move(out, XR_RAX, si_type,
                     mir_model::MirOperand::ADDRESS_PREEMPTIBLE);
    emit_lea(out, XR_RAX, XR_RAX, 16);
    emit_register_alu(out, 0x39, XR_RCX, XR_RAX);
    emit_condition_jump(out, XC_E, si);
  }
  if(!vmi_type.empty()) {
    emit_symbol_move(out, XR_RAX, vmi_type,
                     mir_model::MirOperand::ADDRESS_PREEMPTIBLE);
    emit_lea(out, XR_RAX, XR_RAX, 16);
    emit_register_alu(out, 0x39, XR_RCX, XR_RAX);
    emit_condition_jump(out, XC_E, vmi);
  }
  emit_unconditional_jump(out, done);
  out.label(si);
  emit_register_move(out, XR_RDI, XR_RBX);
  emit_load(out, XR_RSI, XR_R12, 16, 64);
  emit_register_move(out, XR_RDX, XR_R13);
  out.byte(0xe8); out.relative32(helper);
  emit_store(out, XR_RBP, -48, XR_RAX, 64);
  emit_unconditional_jump(out, done);
  out.label(vmi);
  emit_load(out, XR_R15, XR_R12, 20, 32);
  emit_lea(out, XR_R14, XR_R12, 24);
  out.label(loop);
  emit_test_register(out, XR_R15); emit_condition_jump(out, XC_E, done);
  emit_load(out, XR_RSI, XR_R14, 0, 64);
  emit_load(out, XR_RCX, XR_R14, 8, 64);
  emit_test_immediate(out, XR_RCX, 2); emit_condition_jump(out, XC_E, skip);
  emit_register_move(out, XR_RAX, XR_RCX);
  emit_rex(out, true, XR_RAX, XR_RAX); out.byte(0xc1);
  emit_modrm(out, 3, 7, XR_RAX); out.byte(8);
  emit_test_immediate(out, XR_RCX, 1);
  const std::string direct = out.internal_label("dynamic_cast_direct");
  emit_condition_jump(out, XC_E, direct);
  emit_load(out, XR_R11, XR_RBX, 0, 64);
  emit_register_alu(out, 0x01, XR_R11, XR_RAX);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  out.label(direct);
  emit_register_move(out, XR_RDI, XR_RBX);
  emit_register_alu(out, 0x01, XR_RDI, XR_RAX);
  emit_register_move(out, XR_RDX, XR_R13);
  out.byte(0xe8); out.relative32(helper);
  emit_immediate_move(out, XR_RCX, UINT64_MAX);
  emit_register_alu(out, 0x39, XR_RAX, XR_RCX);
  emit_condition_jump(out, XC_E, ambiguous);
  emit_test_register(out, XR_RAX); emit_condition_jump(out, XC_E, skip);
  emit_load(out, XR_RCX, XR_RBP, -48, 64);
  emit_test_register(out, XR_RCX); emit_condition_jump(out, XC_E, record);
  emit_register_alu(out, 0x39, XR_RCX, XR_RAX);
  emit_condition_jump(out, XC_NE, ambiguous);
  out.label(skip);
  emit_immediate_alu(out, XR_R14, 0, 16);
  emit_immediate_alu(out, XR_R15, 5, 1);
  emit_unconditional_jump(out, loop);
  out.label(record);
  emit_store(out, XR_RBP, -48, XR_RBX, 64);
  emit_unconditional_jump(out, done);
  out.label(ambiguous);
  emit_immediate_move(out, XR_RAX, UINT64_MAX);
  emit_store(out, XR_RBP, -48, XR_RAX, 64);
  out.label(done);
  emit_load(out, XR_RAX, XR_RBP, -48, 64);
  emit_stack_adjust(out, false, 8); emit_pop(out, XR_R15); emit_pop(out, XR_R14);
  emit_pop(out, XR_R13); emit_pop(out, XR_R12); emit_pop(out, XR_RBX);
  emit_pop(out, XR_RBP); out.byte(0xc3);
}

void emit_dynamic_cast_runtime(
    CodeBuffer & out, const std::vector<mir_model::MirRuntimeData> & data,
    bool emit_find)
{
  const std::string null_result = out.internal_label("dynamic_cast_null");
  const std::string done = out.internal_label("dynamic_cast_runtime_done");
  emit_test_register(out, XR_RDI); emit_condition_jump(out, XC_E, null_result);
  emit_load(out, XR_RAX, XR_RDI, 0, 64);
  emit_load(out, XR_R11, XR_RAX, -16, 64);
  emit_register_alu(out, 0x01, XR_RDI, XR_R11);
  emit_load(out, XR_RSI, XR_RAX, -8, 64);
  emit_stack_adjust(out, true, 8);
  out.byte(0xe8); out.relative32(".__cppgm_dynamic_cast_find");
  emit_stack_adjust(out, false, 8);
  emit_immediate_move(out, XR_RCX, UINT64_MAX);
  emit_register_alu(out, 0x39, XR_RAX, XR_RCX);
  emit_condition_jump(out, XC_NE, done);
  out.label(null_result); emit_immediate_move(out, XR_RAX, 0);
  out.label(done); out.byte(0xc3);
  if(emit_find) emit_dynamic_cast_find(out, data);
}

void emit_abort_runtime(CodeBuffer & out)
{
  emit_immediate_move(out, XR_RDI, 134);
  emit_immediate_move(out, XR_RAX, 60);
  out.byte(0x0f); out.byte(0x05);
}

void emit_eh_begin_catch(CodeBuffer & out)
{
  emit_symbol_move(out, XR_R11, kEhCaught);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_store(out, XR_RDI, -16, XR_RAX, 64);
  emit_store(out, XR_R11, 0, XR_RDI, 64);
  emit_symbol_move(out, XR_R11, kEhAdjusted);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  out.byte(0xc3);
}

void emit_eh_end_catch(CodeBuffer & out)
{
  const std::string done = ".__cppgm_eh_end_catch_done";
  emit_symbol_move(out, XR_R11, kEhCaught);
  emit_load(out, XR_RAX, XR_R11, 0, 64);
  emit_test_register(out, XR_RAX);
  emit_condition_jump(out, XC_E, done);
  emit_load(out, XR_RCX, XR_RAX, -16, 64);
  emit_store(out, XR_R11, 0, XR_RCX, 64);
  emit_load(out, XR_RCX, XR_RAX, -32, 64);
  emit_test_register(out, XR_RCX);
  emit_condition_jump(out, XC_E, done);
  emit_register_move(out, XR_RDI, XR_RAX);
  emit_stack_adjust(out, true, 8);
  emit_indirect_transfer(out, XR_RCX, true);
  emit_stack_adjust(out, false, 8);
  out.label(done);
  out.byte(0xc3);
}

void emit_eh_throw_runtime(CodeBuffer & out)
{
  emit_store(out, XR_RDI, -32, XR_RDX, 64);
  emit_store(out, XR_RDI, -24, XR_RSI, 64);
  emit_symbol_move(out, XR_R11, kEhValue);
  emit_store(out, XR_R11, 0, XR_RDI, 64);
  emit_symbol_move(out, XR_R11, kEhAdjusted);
  emit_store(out, XR_R11, 0, XR_RDI, 64);
  emit_symbol_move(out, XR_R11, kEhType);
  emit_store(out, XR_R11, 0, XR_RSI, 64);
  emit_symbol_move(out, XR_R11, kEhSelector);
  emit_immediate_move(out, XR_RAX, 0);
  emit_store(out, XR_R11, 0, XR_RAX, 64);
  emit_unconditional_jump(out, kEhDispatch);
}

void emit_eh_rethrow(CodeBuffer & out)
{
  emit_symbol_move(out, XR_R11, kEhCaught);
  emit_load(out, XR_RDI, XR_R11, 0, 64);
  emit_symbol_move(out, XR_R11, kEhValue);
  emit_store(out, XR_R11, 0, XR_RDI, 64);
  emit_symbol_move(out, XR_R11, kEhAdjusted);
  emit_store(out, XR_R11, 0, XR_RDI, 64);
  emit_load(out, XR_RSI, XR_RDI, -24, 64);
  emit_symbol_move(out, XR_R11, kEhType);
  emit_store(out, XR_R11, 0, XR_RSI, 64);
  emit_unconditional_jump(out, kEhDispatch);
}

void emit_eh_runtime(CodeBuffer & out, const mir_model::MirProgram & program)
{
  if(program.uses_eh) {
    emit_eh_dispatch(out);
    emit_eh_resume(out, program.runtime_functions);
    emit_dynamic_cast_find(out, program.runtime_data);
  }
  for(std::size_t i = 0; i < program.runtime_functions.size(); ++i) {
    const mir_model::MirRuntimeFunction & runtime = program.runtime_functions[i];
    if(runtime.kind == mir_model::RuntimeFunction::RF_EH_RESUME) continue;
    emit_runtime_labels(out, runtime);
    switch(runtime.kind) {
    case mir_model::RuntimeFunction::RF_EH_ALLOCATE: emit_eh_allocate(out); break;
    case mir_model::RuntimeFunction::RF_EH_BEGIN_CATCH: emit_eh_begin_catch(out); break;
    case mir_model::RuntimeFunction::RF_EH_END_CATCH: emit_eh_end_catch(out); break;
    case mir_model::RuntimeFunction::RF_EH_RETHROW: emit_eh_rethrow(out); break;
    case mir_model::RuntimeFunction::RF_EH_THROW: emit_eh_throw_runtime(out); break;
    case mir_model::RuntimeFunction::RF_EH_PERSONALITY: out.byte(0xc3); break;
    case mir_model::RuntimeFunction::RF_EH_RESUME: break;
    case mir_model::RuntimeFunction::RF_ALLOCATE_MEMORY: emit_malloc_runtime(out); break;
    case mir_model::RuntimeFunction::RF_FREE_MEMORY: out.byte(0xc3); break;
    case mir_model::RuntimeFunction::RF_PURE_VIRTUAL: emit_abort_runtime(out); break;
    case mir_model::RuntimeFunction::RF_DYNAMIC_CAST:
      emit_dynamic_cast_runtime(
        out, program.runtime_data, !program.uses_eh); break;
    case mir_model::RuntimeFunction::RF_BAD_CAST:
    case mir_model::RuntimeFunction::RF_BAD_TYPEID: emit_abort_runtime(out); break;
    }
  }
}

void emit_eh_data(CodeBuffer & out, const mir_model::MirProgram & program)
{
  if(!program.uses_eh && program.runtime_data.empty()) return;
  out.align(8);
  if(program.uses_eh) {
    out.label(kEhTop); out.zeros(8);
    out.label(kEhValue); out.zeros(8);
    out.label(kEhAdjusted); out.zeros(8);
    out.label(kEhType); out.zeros(8);
    out.label(kEhSelector); out.zeros(8);
    out.label(kEhCaught); out.zeros(8);
  }
  for(std::size_t i = 0; i < program.runtime_data.size(); ++i) {
    out.align(16);
    out.label(program.runtime_data[i].name);
    const std::string object_symbol =
      native_object_symbol(program.runtime_data[i].object_symbol);
    if(!object_symbol.empty() && object_symbol != program.runtime_data[i].name)
      out.label(object_symbol);
    out.zeros(32);
  }
}

void emit_integer_data(CodeBuffer & out, long long value, std::size_t size, const std::string& literal_text)
{
  if(size <= 8) {
    out.little(static_cast<std::uint64_t>(value), static_cast<unsigned>(size));
    return;
  }
  if(size != 16) throw std::logic_error("unsupported wide integer data size");
	std::uint64_t low, high; parse_wide_literal_words(literal_text.empty() ? std::to_string(value) : literal_text, &low, &high);
	out.little(low, 8); out.little(high, 8);
}

void emit_float_data(CodeBuffer & out, const std::string & text,
                     const std::string & type)
{
  if(type == "f80") {
    const std::pair<std::uint64_t, std::uint64_t> words =
      extended(text);
    out.little(words.first, 8);
    out.little(words.second, 8);
    return;
  }
  out.little(scalar(text, type),
             static_cast<unsigned>(type_size(type)));
}

void emit_global(CodeBuffer & out, const mir_model::MirGlobalDefinition & global)
{
  out.align(global_alignment(global));
  out.label(global.name);
  const std::string object_symbol = native_object_symbol(global.object_symbol);
  if(!object_symbol.empty() && object_symbol != global.name)
    out.label(object_symbol);
  if(global.thread_local_storage && !global.thread_local_wrapper_symbol.empty())
    out.label(global.thread_local_wrapper_symbol);
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR) {
    const std::size_t size = type_size(global.type);
    if(global.init_kind == mir_model::MirGlobalDefinition::GI_ADDR) {
      out.absolute64(global.symbol, global.addr_addend);
    } else if(global.init_kind == mir_model::MirGlobalDefinition::GI_FLOAT) {
      emit_float_data(out, global.literal_text, global.type);
    } else {
      emit_integer_data(out, global.int_value, size, global.literal_text);
    }
    return;
  }
  for(std::size_t i = 0; i < global.data_items.size(); ++i) {
    const mir_model::MirGlobalDefinition::DataItem & item = global.data_items[i];
    if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO) {
      out.zeros(item.zero_bytes);
      continue;
    }
    const std::size_t size = type_size(item.type);
    out.align(size);
    if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_ADDR)
      out.absolute64(item.symbol, item.addr_addend);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_INTEGER)
      emit_integer_data(out, item.int_value, size, item.literal_text);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT)
      emit_float_data(out, item.literal_text, item.type);
    else throw std::logic_error("unsupported native global data item");
  }
}

HostFunctionLayout emit_host_tls_wrapper(
    CodeBuffer & out, const std::string & internal_symbol,
    const lowir_model::SymbolMetadata & metadata)
{
  if(metadata.tls_for_symbol.empty())
    throw std::logic_error("TLS wrapper has no storage target");
  out.align(2);
  HostFunctionLayout layout;
  layout.internal_symbol = internal_symbol;
  layout.object_symbol = metadata.object_symbol;
  layout.offset = out.size();
  out.label(internal_symbol);
  const std::string object_symbol = native_object_symbol(metadata.object_symbol);
  if(!object_symbol.empty() && object_symbol != internal_symbol)
    out.label(object_symbol);
  emit_tls_address(out, XR_RAX, metadata.tls_for_symbol);
  out.byte(0xc3);
  layout.size = out.size() - layout.offset;
  return layout;
}

void emit_relocatable_objects(
    CodeBuffer & out, const std::vector<RelocatableObject> & objects)
{
  for(std::size_t i = 0; i < objects.size(); ++i) {
    for(std::size_t j = 0; j < objects[i].sections.size(); ++j) {
      const RelocatableSection & section = objects[i].sections[j];
      out.align(section.alignment);
      const std::size_t base = out.size();
      out.append(section.bytes);
      for(std::size_t k = 0; k < section.labels.size(); ++k)
        out.label_at(section.labels[k].name, base + section.labels[k].offset);
      for(std::size_t k = 0; k < section.relocations.size(); ++k) {
        const RelocatableRelocation & relocation = section.relocations[k];
        if(relocation.kind == RelocatableRelocation::RELATIVE32)
          out.relative32_at(base + relocation.offset, relocation.target,
                            relocation.addend);
        else
          out.absolute64_at(base + relocation.offset, relocation.target,
                            relocation.addend);
      }
    }
  }
}

void put_little(std::vector<unsigned char> & out, std::size_t offset,
                std::uint64_t value, unsigned count)
{
  if(offset + count > out.size()) throw std::logic_error("invalid ELF header field");
  for(unsigned i = 0; i < count; ++i)
    out[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}

std::vector<unsigned char> make_elf_header(std::size_t content_size)
{
  const std::size_t file_size = kContentOffset + content_size;
  std::vector<unsigned char> image(kContentOffset, 0);
  image[0] = 0x7f;
  image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
  image[4] = 2;
  image[5] = 1;
  image[6] = 1;
  put_little(image, 16, 2, 2);
  put_little(image, 18, 62, 2);
  put_little(image, 20, 1, 4);
  put_little(image, 24, kLoadAddress + kContentOffset, 8);
  put_little(image, 32, kElfHeaderSize, 8);
  put_little(image, 40, 0, 8);
  put_little(image, 48, 0, 4);
  put_little(image, 52, kElfHeaderSize, 2);
  put_little(image, 54, kProgramHeaderSize, 2);
  put_little(image, 56, 1, 2);

  const std::size_t ph = kElfHeaderSize;
  put_little(image, ph + 0, 1, 4);
  put_little(image, ph + 4, 7, 4);
  put_little(image, ph + 8, 0, 8);
  put_little(image, ph + 16, kLoadAddress, 8);
  put_little(image, ph + 24, kLoadAddress, 8);
  put_little(image, ph + 32, file_size, 8);
  put_little(image, ph + 40, file_size, 8);
  put_little(image, ph + 48, 0x1000, 8);
  return image;
}

void emit_program_tail(CodeBuffer & content,
                       const mir_model::MirProgram & program,
                       const std::vector<RelocatableObject> & objects)
{
  emit_eh_runtime(content, program);
  for(std::size_t i = 0; i < program.globals.size(); ++i)
    emit_global(content, program.globals[i]);
  emit_eh_data(content, program);
  emit_relocatable_objects(content, objects);
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i)
    content.alias(native_object_symbol(program.object_aliases[i].object_symbol),
                  program.object_aliases[i].target);
}

void finish_native_executable(
    const std::string & path, CodeBuffer & content, Stats * stats,
    std::uint64_t encode_nanoseconds,
    const std::chrono::steady_clock::time_point & encode_started)
{
  content.resolve();
  const std::vector<unsigned char> header = make_elf_header(content.size());
  if(stats) encode_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - encode_started).count());

  std::chrono::steady_clock::time_point write_started;
  if(stats) write_started = std::chrono::steady_clock::now();
  std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
  if(!out) throw std::runtime_error("unable to open native output: " + path);
  out.write(reinterpret_cast<const char *>(&header[0]),
            static_cast<std::streamsize>(header.size()));
  if(!content.bytes().empty())
    out.write(reinterpret_cast<const char *>(&content.bytes()[0]),
              static_cast<std::streamsize>(content.bytes().size()));
  if(!out) throw std::runtime_error("unable to write native output: " + path);
  out.close();
  if(::chmod(path.c_str(), 0755) != 0)
    throw std::runtime_error("unable to mark native output executable: " + path +
                             ": " + std::strerror(errno));
  if(stats) {
    stats->fixups = content.fixup_count();
    stats->output_bytes = header.size() + content.size();
    stats->encode_nanoseconds = encode_nanoseconds;
    stats->write_nanoseconds = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - write_started).count());
  }
}

void emit_host_instruction(
    CodeBuffer & out,
    const mir_model::MirInstruction & instruction,
    const mir_model::MirFunction & function,
    const std::string & landing_pad,
    HostFunctionLayout & layout,
    std::vector<HostEhStackCleanup> & stack_cleanups)
{
  if(instruction.opcode == mir_model::MirInstruction::MI_EH_PUSH) {
    require_operands(instruction, 2);
    return;
  }
  if(instruction.opcode == mir_model::MirInstruction::MI_EH_POP) {
    require_operands(instruction, 0);
    return;
  }
  if(instruction.opcode == mir_model::MirInstruction::MI_EH_CATCH || instruction.opcode == mir_model::MirInstruction::MI_EH_FILTER) return;
  if(instruction.opcode ==
       mir_model::MirInstruction::MI_EH_CLEANUP_CLAUSE) return;
  if(instruction.opcode == mir_model::MirInstruction::MI_LOAD_EXCEPTION ||
     instruction.opcode ==
       mir_model::MirInstruction::MI_LOAD_EXCEPTION_SELECTOR) {
    require_operands(instruction, 1);
    emit_load(out, require_register(instruction.operands[0]), XR_RBP,
      actual_frame_offset(function,
        instruction.opcode == mir_model::MirInstruction::MI_LOAD_EXCEPTION ?
        function.host_eh_exception_offset : function.host_eh_selector_offset),
      type_width(instruction.type));
    return;
  }
  if(instruction.opcode == mir_model::MirInstruction::MI_RESUME) {
    require_operands(instruction, 0);
    emit_load(out, XR_RDI, XR_RBP,
      actual_frame_offset(function, function.host_eh_exception_offset), 64);
    out.byte(0xe8);
    out.relative32("_Unwind_Resume");
    return;
  }
  const bool call = instruction.opcode == mir_model::MirInstruction::MI_CALL ||
    instruction.opcode == mir_model::MirInstruction::MI_CALL_INDIRECT;
  const std::size_t start = out.size();
  emit_instruction(out, instruction, &function);
  if(call && !instruction.call_unwind_no && !landing_pad.empty()) {
    HostFunctionLayout::CallSite site;
    site.start = start - layout.offset;
    site.length = out.size() - start;
    site.action_pad = landing_pad;
    if(instruction.call_stack_bytes) {
      HostEhStackCleanup cleanup;
      cleanup.label = ".__host_eh_stack_cleanup_" +
        std::to_string(stack_cleanups.size());
      cleanup.landing_pad = landing_pad;
      cleanup.stack_bytes = instruction.call_stack_bytes;
      site.landing_pad = cleanup.label;
      stack_cleanups.push_back(cleanup);
    } else {
      site.landing_pad = landing_pad;
    }
    layout.call_sites.push_back(site);
  }
}

HostFunctionLayout emit_host_function(
    CodeBuffer & out, const mir_model::MirFunction & function, Stats * stats)
{
  host_eh_detail::HostEhRegionPlan region_plan;
  if(function.host_eh_enabled)
    region_plan = host_eh_detail::analyze_host_eh_regions(function);
  if(stats && function.host_eh_enabled) {
    stats->eh_region_states += region_plan.state_count;
    stats->eh_region_edges += region_plan.edge_count;
    stats->eh_call_sites += region_plan.protected_call_count;
  }
  out.align(2);
  HostFunctionLayout layout;
  layout.internal_symbol = function.name;
  layout.object_symbol = function.object_symbol;
  layout.offset = out.size();
  layout.callee_saved_regs = function.callee_saved_regs;
  layout.clauses = function.host_eh_clauses;
  out.label(function.name);
  const std::string object_symbol = native_object_symbol(function.object_symbol);
  if(!object_symbol.empty() && object_symbol != function.name)
    out.label(object_symbol);
  emit_function_prologue(out, function);
  const FrameReloadPlan frame_reload_plan =
    find_single_use_frame_reloads(function);
  const std::string no_landing_pad;
  std::vector<HostEhStackCleanup> stack_cleanups;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const mir_model::MirBlock & block = function.blocks[i];
    out.label(function.name + "::" + block.label);
    const std::vector<bool> flags_live =
      condition_flags_live_before(block.instructions);
    if(function.host_eh_clauses.count(block.label)) {
      emit_store(out, XR_RBP,
        actual_frame_offset(function, function.host_eh_exception_offset),
        XR_RAX, 64);
      emit_store(out, XR_RBP,
        actual_frame_offset(function, function.host_eh_selector_offset),
        XR_RDX, 64);
    }
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      std::size_t folded = 0;
      if(address_folding::is_setup_load_sequence(block.instructions, j))
        folded = address_folding::emit_dead_setup_load(
          out, block.instructions, j, function);
      if(folded) {
        j += folded - 1;
        continue;
      }
      const std::size_t divided = emit_power_of_two_division(
        out, block.instructions, j);
      if(divided) {
        j += divided - 1;
        continue;
      }
      if(emit_delayed_frame_forwarding(
           out, block.instructions[j], frame_reload_plan))
        continue;
      const std::size_t forwarded = emit_forwarded_frame_reload(
        out, block.instructions, j, function, frame_reload_plan);
      if(forwarded) {
        j += forwarded - 1;
        continue;
      }
      const std::size_t coalesced = emit_coalesced_constant_byte_stores(
        out, block.instructions, j, function);
      if(coalesced) {
        j += coalesced - 1;
        continue;
      }
      if(emit_flag_safe_zero_move(
           out, block.instructions[j], flags_live[j]))
        continue;
      const std::size_t landing_block = function.host_eh_enabled ?
        region_plan.call_landing_blocks[i][j] : 0;
      emit_host_instruction(out, block.instructions[j], function,
        landing_block ? function.blocks[landing_block - 1].label :
                        no_landing_pad,
        layout, stack_cleanups);
    }
  }
  for(std::size_t i = 0; i < stack_cleanups.size(); ++i) {
    out.label(function.name + "::" + stack_cleanups[i].label);
    emit_stack_adjust(out, false,
      static_cast<unsigned>(stack_cleanups[i].stack_bytes));
    emit_unconditional_jump(
      out, function.name + "::" + stack_cleanups[i].landing_pad);
  }
  const CodeOffsetAdjustment adjustment =
    out.relax_forward_branches(layout.offset);
  for(std::size_t i = 0; i < layout.call_sites.size(); ++i) {
    const std::size_t old_start = layout.offset + layout.call_sites[i].start;
    const std::size_t old_end = old_start + layout.call_sites[i].length;
    const std::size_t new_start = adjustment.translate(old_start);
    const std::size_t new_end = adjustment.translate(old_end);
    layout.call_sites[i].start = new_start - layout.offset;
    layout.call_sites[i].length = new_end - new_start;
  }
  layout.size = out.size() - layout.offset;
  return layout;
}

EncodedSection encoded_section(CodeBuffer && source,
                               const std::string & name,
                               std::uint64_t flags,
                               std::size_t alignment)
{
  EncodedSection result;
  result.name = name;
  result.flags = flags;
  result.alignment = alignment;
  result.bytes = source.take_bytes();
  result.labels = source.labels();
  result.fixups.reserve(source.fixups().size());
  for(std::size_t i = 0; i < source.fixups().size(); ++i) {
    EncodedFixup fixup;
    fixup.kind = source.fixups()[i].kind == Fixup::ABSOLUTE64 ?
      EncodedFixup::EF_ABSOLUTE64 :
      source.fixups()[i].kind == Fixup::ADDRESS32 ?
      EncodedFixup::EF_ADDRESS32 :
      source.fixups()[i].kind == Fixup::TLS_OFFSET32 ?
      EncodedFixup::EF_TLS_OFFSET32 : EncodedFixup::EF_RELATIVE32;
    fixup.address_binding = source.fixups()[i].address_binding;
    fixup.offset = source.fixups()[i].offset;
    fixup.target = source.fixups()[i].target;
    fixup.addend = source.fixups()[i].addend;
    result.fixups.push_back(fixup);
  }
  return result;
}

struct DataSectionBuffer
{
  std::string name;
  std::uint64_t flags;
  std::size_t alignment;
  CodeBuffer content;

  DataSectionBuffer(const std::string & section_name, std::uint64_t section_flags)
    : name(section_name), flags(section_flags), alignment(1), content(0) {}
};

std::size_t intern_data_section(
    const std::string & name, std::uint64_t flags,
    std::vector<DataSectionBuffer> & sections,
    std::unordered_map<std::string, std::size_t> & indexes)
{
  const std::unordered_map<std::string, std::size_t>::const_iterator found =
    indexes.find(name);
  if(found != indexes.end()) {
    sections[found->second].flags |= flags;
    return found->second;
  }
  const std::size_t index = sections.size();
  indexes[name] = index;
  sections.push_back(DataSectionBuffer(name, flags));
  return index;
}

}  // namespace

void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            Stats * stats)
{
  write_linux_executable(path, program, std::vector<RelocatableObject>(), stats);
}

void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            const std::vector<RelocatableObject> & objects,
                            Stats * stats)
{
  if(program.target != "linux") throw std::runtime_error("ELF writer requires linux target");
  if(program.startup.empty()) throw std::runtime_error("native executable has no startup entry");
  std::chrono::steady_clock::time_point encode_start;
  if(stats) encode_start = std::chrono::steady_clock::now();
  CodeBuffer content;
  content.label("__startup");
  for(std::size_t i = 0; i < program.startup.size(); ++i)
    emit_instruction(content, program.startup[i], 0);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    emit_function(content, program.functions[i]);
  emit_program_tail(content, program, objects);
  finish_native_executable(path, content, stats, 0, encode_start);
}

void write_linux_executable(const std::string & path,
                            const lowir_model::LowirProgram & source,
                            const std::string & target,
                            const std::vector<RelocatableObject> & objects,
                            int optimization_level,
                            Stats * stats)
{
  ProgramLoweringSession lowering(source, target, optimization_level, stats);
  mir_model::MirProgram program = lowering.take_program_shell();
  if(program.startup.empty())
    throw std::runtime_error("native executable has no startup entry");
  CodeBuffer content;
  std::uint64_t encode_nanoseconds = 0;
  std::chrono::steady_clock::time_point encode_started;
  if(stats) encode_started = std::chrono::steady_clock::now();
  content.label("__startup");
  for(std::size_t i = 0; i < program.startup.size(); ++i)
    emit_instruction(content, program.startup[i], 0);
  if(stats) encode_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - encode_started).count());
  for(std::size_t i = 0; i < lowering.function_count(); ++i) {
    const mir_model::MirFunction function = lowering.lower_function(i);
    if(stats) encode_started = std::chrono::steady_clock::now();
    emit_function(content, function);
    if(stats) encode_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - encode_started).count());
  }
  if(stats) encode_started = std::chrono::steady_clock::now();
  emit_program_tail(content, program, objects);
  finish_native_executable(path, content, stats, encode_nanoseconds,
                           encode_started);
}

void write_linux_relocatable(
    const std::string & path,
    const lowir_model::LowirProgram & source,
    const std::string & target,
    int optimization_level,
    Stats * stats)
{
  if(target != "linux")
    throw std::runtime_error("ELF object writer requires linux target");
  ProgramLoweringSession lowering(source, target, optimization_level, stats);
  mir_model::MirProgram program = lowering.take_program_shell();
  CodeBuffer text(0, true);
  std::vector<HostFunctionLayout> functions;
  functions.reserve(lowering.function_count() + source.function_declarations.size());
  std::uint64_t encode_nanoseconds = 0;
  std::unordered_set<std::string> emitted_tls_wrappers;
  for(std::size_t i = 0; i < source.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & wrapper =
      source.function_declarations[i];
    if(wrapper.metadata.tls_for_symbol.empty() ||
       !emitted_tls_wrappers.insert(wrapper.name).second) continue;
    functions.push_back(emit_host_tls_wrapper(
      text, wrapper.name, wrapper.metadata));
  }
  for(std::size_t i = 0; i < lowering.function_count(); ++i) {
    const mir_model::MirFunction function = lowering.lower_function(i);
    const std::chrono::steady_clock::time_point started =
      stats ? std::chrono::steady_clock::now() :
              std::chrono::steady_clock::time_point();
    functions.push_back(emit_host_function(text, function, stats));
    if(stats) encode_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }
  std::vector<DataSectionBuffer> data_sections;
  std::unordered_map<std::string, std::size_t> data_section_indexes;
  intern_data_section(".data", 3, data_sections, data_section_indexes);
  const std::unordered_set<std::string> suppressed_globals =
    host_external_global_definitions(source, program);
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const mir_model::MirGlobalDefinition & global = program.globals[i];
    if(suppressed_globals.count(global.name)) continue;
    const std::string section_name = !global.section_name.empty() ?
      global.section_name : global.thread_local_storage ? ".tdata" : ".data";
    const std::uint64_t flags = 2 | (global.readonly ? 0 : 1) |
      (global.thread_local_storage ? 0x400 : 0);
    const std::size_t section_index = intern_data_section(
      section_name, flags, data_sections, data_section_indexes);
    DataSectionBuffer & section = data_sections[section_index];
    section.alignment = std::max(section.alignment, global_alignment(global));
    emit_global(section.content, global);
  }
  bool needs_personality = false;
  const std::unordered_map<std::string, std::string> host_declarations =
    declaration_object_symbols(source);
  std::unordered_set<std::string> catch_types;
  const auto record_eh_type = [&](const std::string & symbol) {
    const auto named = host_declarations.find(symbol); catch_types.insert(named == host_declarations.end() ? host_symbol_spelling(symbol) : named->second);
  };
  for(std::size_t i = 0; i < functions.size(); ++i)
  {
    needs_personality = needs_personality || !functions[i].call_sites.empty();
    for(std::map<std::string,
          std::vector<mir_model::MirHostEhClause> >::const_iterator clauses =
          functions[i].clauses.begin(); clauses != functions[i].clauses.end();
        ++clauses)
      for(std::size_t clause = 0; clause < clauses->second.size(); ++clause)
        if(clauses->second[clause].kind ==
             mir_model::MirHostEhClause::HC_CATCH &&
           !clauses->second[clause].catch_all)
        {
          record_eh_type(clauses->second[clause].type_symbol);
        }
        else if(clauses->second[clause].kind == mir_model::MirHostEhClause::HC_FILTER)
          for(std::size_t type = 0; type < clauses->second[clause].filter_type_symbols.size(); ++type) record_eh_type(clauses->second[clause].filter_type_symbols[type]);
  }
  std::vector<std::string> ordered_catch_types(
    catch_types.begin(), catch_types.end());
  std::sort(ordered_catch_types.begin(), ordered_catch_types.end());
  CodeBuffer & ordinary_data = data_sections[0].content;
  for(std::size_t i = 0; i < ordered_catch_types.size(); ++i) {
    const std::string & type = ordered_catch_types[i];
    ordinary_data.align(8);
    ordinary_data.label("DW.ref." + type);
    ordinary_data.absolute64(type);
    data_sections[0].alignment = std::max<std::size_t>(
      data_sections[0].alignment, 8);
  }
  if(needs_personality) {
    ordinary_data.align(8);
    ordinary_data.label("DW.ref.__gxx_personality_v0");
    ordinary_data.absolute64("__gxx_personality_v0");
    data_sections[0].alignment = std::max<std::size_t>(
      data_sections[0].alignment, 8);
  }
  const std::chrono::steady_clock::time_point image_started =
    stats ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point();
  std::size_t relocations = 0;
  std::vector<EncodedSection> encoded_data_sections;
  encoded_data_sections.reserve(data_sections.size());
  for(std::size_t i = 0; i < data_sections.size(); ++i)
    encoded_data_sections.push_back(encoded_section(
      std::move(data_sections[i].content), data_sections[i].name,
      data_sections[i].flags, data_sections[i].alignment));
  const std::vector<unsigned char> image = make_linux_relocatable_image(
    source, encoded_section(std::move(text), ".text", 6, 16),
    std::move(encoded_data_sections),
    functions,
    relocations);
  if(stats) encode_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - image_started).count());
  const std::chrono::steady_clock::time_point write_started =
    stats ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point();
  std::ofstream output(path.c_str(),
    std::ios::out | std::ios::binary | std::ios::trunc);
  if(!output) throw std::runtime_error("unable to open object output: " + path);
  if(!image.empty()) output.write(
    reinterpret_cast<const char *>(&image[0]),
    static_cast<std::streamsize>(image.size()));
  if(!output) throw std::runtime_error("unable to write object output: " + path);
  if(stats) {
    stats->fixups = relocations;
    stats->output_bytes = image.size();
    stats->encode_nanoseconds = encode_nanoseconds;
    stats->write_nanoseconds = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - write_started).count());
  }
}

}  // namespace lowir_native
