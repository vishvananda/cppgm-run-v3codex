#include "lowir_native.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace lowir_native {
namespace {

const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kElfHeaderSize = 64;
const std::size_t kProgramHeaderSize = 56;
const std::size_t kContentOffset = kElfHeaderSize + kProgramHeaderSize;

struct Fixup
{
  enum Kind { RELATIVE32, ABSOLUTE64 } kind = RELATIVE32;
  std::size_t offset = 0;
  std::string target;
};

class CodeBuffer
{
public:
  void byte(unsigned value) { bytes_.push_back(static_cast<unsigned char>(value)); }

  void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, 0); }

  void little(std::uint64_t value, unsigned count)
  {
    for(unsigned i = 0; i < count; ++i) byte(static_cast<unsigned>(value >> (i * 8)));
  }

  void patch(std::size_t offset, std::uint64_t value, unsigned count)
  {
    if(offset + count > bytes_.size()) throw std::logic_error("invalid ELF patch");
    for(unsigned i = 0; i < count; ++i)
      bytes_[offset + i] = static_cast<unsigned char>(value >> (i * 8));
  }

  void align(std::size_t alignment)
  {
    if(!alignment) throw std::logic_error("zero data alignment");
    while(bytes_.size() % alignment) byte(0);
  }

  void label(const std::string & name)
  {
    if(!labels_.emplace(name, bytes_.size()).second)
      throw std::logic_error("duplicate native label: " + name);
  }

  void relative32(const std::string & target)
  {
    Fixup fixup;
    fixup.kind = Fixup::RELATIVE32;
    fixup.offset = bytes_.size();
    fixup.target = target;
    fixups_.push_back(fixup);
    zeros(4);
  }

  void absolute64(const std::string & target)
  {
    Fixup fixup;
    fixup.kind = Fixup::ABSOLUTE64;
    fixup.offset = bytes_.size();
    fixup.target = target;
    fixups_.push_back(fixup);
    zeros(8);
  }

  void resolve()
  {
    for(std::size_t i = 0; i < fixups_.size(); ++i) {
      const Fixup & fixup = fixups_[i];
      const std::unordered_map<std::string, std::size_t>::const_iterator target =
        labels_.find(fixup.target);
      if(target == labels_.end()) throw std::runtime_error("undefined native symbol: " + fixup.target);
      if(fixup.kind == Fixup::RELATIVE32) {
        const std::int64_t delta = static_cast<std::int64_t>(target->second) -
                                   static_cast<std::int64_t>(fixup.offset + 4);
        if(delta < INT32_MIN || delta > INT32_MAX)
          throw std::runtime_error("native branch displacement exceeds rel32");
        patch(fixup.offset, static_cast<std::uint32_t>(delta), 4);
      } else {
        patch(fixup.offset, kLoadAddress + kContentOffset + target->second, 8);
      }
    }
  }

  const std::vector<unsigned char> & bytes() const { return bytes_; }
  std::size_t fixup_count() const { return fixups_.size(); }

private:
  std::vector<unsigned char> bytes_;
  std::unordered_map<std::string, std::size_t> labels_;
  std::vector<Fixup> fixups_;
};

void emit_rex(CodeBuffer & out, bool wide, X64Register reg, X64Register rm,
              bool force = false)
{
  const unsigned value = 0x40 | (wide ? 8 : 0) |
    ((static_cast<unsigned>(reg) >> 3) << 2) |
    (static_cast<unsigned>(rm) >> 3);
  if(value != 0x40 || force) out.byte(value);
}

void emit_modrm(CodeBuffer & out, unsigned mod, unsigned reg, unsigned rm)
{
  out.byte((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

void emit_register_move(CodeBuffer & out, X64Register destination,
                        X64Register source)
{
  emit_rex(out, true, source, destination);
  out.byte(0x89);
  emit_modrm(out, 3, source, destination);
}

void emit_immediate_move(CodeBuffer & out, X64Register destination,
                         std::uint64_t value)
{
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0xb8 + (static_cast<unsigned>(destination) & 7));
  out.little(value, 8);
}

void emit_symbol_move(CodeBuffer & out, X64Register destination,
                      const std::string & symbol)
{
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0xb8 + (static_cast<unsigned>(destination) & 7));
  out.absolute64(symbol);
}

void emit_memory_modrm(CodeBuffer & out, unsigned reg, X64Register base,
                       long long displacement)
{
  emit_modrm(out, 2, reg, base);
  if((static_cast<unsigned>(base) & 7) == 4)
    out.byte((0 << 6) | (4 << 3) | (static_cast<unsigned>(base) & 7));
  out.little(static_cast<std::uint32_t>(displacement), 4);
}

void emit_size_prefix(CodeBuffer & out, unsigned width)
{
  if(width == 16) out.byte(0x66);
}

void emit_load(CodeBuffer & out, X64Register destination, X64Register base,
               long long displacement, unsigned width)
{
  emit_size_prefix(out, width);
  emit_rex(out, width == 64, destination, base, width == 8);
  out.byte(width == 8 ? 0x8a : 0x8b);
  emit_memory_modrm(out, destination, base, displacement);
}

void emit_store(CodeBuffer & out, X64Register base, long long displacement,
                X64Register source, unsigned width)
{
  emit_size_prefix(out, width);
  emit_rex(out, width == 64, source, base, width == 8);
  out.byte(width == 8 ? 0x88 : 0x89);
  emit_memory_modrm(out, source, base, displacement);
}

void emit_lea(CodeBuffer & out, X64Register destination, X64Register base,
              long long displacement)
{
  emit_rex(out, true, destination, base);
  out.byte(0x8d);
  emit_memory_modrm(out, destination, base, displacement);
}

void emit_push(CodeBuffer & out, X64Register reg)
{
  if(reg >= XR_R8) out.byte(0x41);
  out.byte(0x50 + (static_cast<unsigned>(reg) & 7));
}

void emit_pop(CodeBuffer & out, X64Register reg)
{
  if(reg >= XR_R8) out.byte(0x41);
  out.byte(0x58 + (static_cast<unsigned>(reg) & 7));
}

void emit_stack_adjust(CodeBuffer & out, bool subtract, unsigned bytes)
{
  if(!bytes) return;
  out.byte(0x48);
  out.byte(bytes <= 127 ? 0x83 : 0x81);
  emit_modrm(out, 3, subtract ? 5 : 0, XR_RSP);
  if(bytes <= 127) out.byte(bytes);
  else out.little(bytes, 4);
}

std::size_t function_frame_bytes(const mir_model::MirFunction & function)
{
  std::size_t bytes = 0;
  for(std::size_t i = 0; i < function.frame_bindings.size(); ++i)
    if(function.frame_bindings[i].offset < 0)
      bytes = std::max(bytes,
        static_cast<std::size_t>(-function.frame_bindings[i].offset));
  return bytes;
}

std::size_t function_stack_adjustment(const mir_model::MirFunction & function)
{
  const std::size_t preserved = function.callee_saved_regs.size() * 8;
  const std::size_t required = preserved + function_frame_bytes(function);
  const std::size_t aligned = (required + 15) / 16 * 16;
  return aligned - preserved;
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
    emit_symbol_move(out, XR_R11, address.text);
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

std::string unsuffixed_float_text(const std::string & text)
{
  if(!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                       text.back() == 'l' || text.back() == 'L'))
    return text.substr(0, text.size() - 1);
  return text;
}

std::uint64_t scalar_float_bits(const std::string & text, const std::string & type)
{
  const std::string number = unsuffixed_float_text(text);
  errno = 0;
  char * end = 0;
  if(type == "f32") {
    const float value = std::strtof(number.c_str(), &end);
    if(errno || !end || *end) throw std::runtime_error("invalid f32 literal: " + text);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }
  if(type == "f64") {
    const double value = std::strtod(number.c_str(), &end);
    if(errno || !end || *end) throw std::runtime_error("invalid f64 literal: " + text);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }
  throw std::logic_error("floating literal requires f32 or f64");
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
    emit_immediate_move(out, XR_R11, scalar_float_bits(source.text, type));
    emit_gpr_to_xmm(out, destination, XR_R11, type == "f32" ? 32 : 64);
  } else if(source.kind == mir_model::MirOperand::OP_IMM) {
    emit_immediate_move(out, XR_R11,
      scalar_float_bits(std::to_string(source.imm), type));
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

void emit_set_condition(CodeBuffer & out, X86Condition condition, X64Register destination);
void emit_move_zero_extended_byte(CodeBuffer & out, X64Register destination,
                                  X64Register source);
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

unsigned type_width(const std::string & type)
{
  if(type == "i1" || type == "i8" || type == "u8") return 8;
  if(type == "i16" || type == "u16") return 16;
  if(type == "i32" || type == "u32" || type == "f32") return 32;
  if(type == "i64" || type == "f64" || type == "ptr") return 64;
  if(type == "f80") return 80;
  throw std::logic_error("unsupported native scalar type: " + type);
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
    emit_symbol_move(out, destination, source.text);
  else throw std::logic_error("unsupported native move operand");
}

void emit_address_load(CodeBuffer & out, X64Register destination,
                       const mir_model::MirOperand & address, unsigned width,
                       const mir_model::MirFunction & function)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    emit_load(out, destination, address.reg, address.offset, width);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text);
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
    emit_symbol_move(out, XR_R11, address.text);
    emit_store(out, XR_R11, 0, source, width);
  } else if(address.kind == mir_model::MirOperand::OP_FRAME) {
    emit_store(out, XR_RBP, actual_frame_offset(function, address.offset),
               source, width);
  } else throw std::logic_error("unsupported native store address");
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

void emit_integer_to_float(CodeBuffer & out,
                           const mir_model::MirInstruction & instruction,
                           const mir_model::MirFunction & function)
{
  require_operands(instruction, 2);
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
  const unsigned source_width = type_width(types.first);
  const mir_model::MirOperand & destination = instruction.operands[0];
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
  require_operands(instruction, 2);
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
  const X64Register destination = require_register(instruction.operands[0]);
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

void emit_set_condition(CodeBuffer & out, X86Condition condition, X64Register destination)
{
  emit_rex(out, false, XR_RAX, destination, destination >= XR_RSP);
  out.byte(0x0f);
  out.byte(0x90 + static_cast<unsigned>(condition));
  emit_modrm(out, 3, 0, destination);
}

void emit_move_zero_extended_byte(CodeBuffer & out, X64Register destination,
                                  X64Register source)
{
  emit_rex(out, true, destination, source, true);
  out.byte(0x0f);
  out.byte(0xb6);
  emit_modrm(out, 3, destination, source);
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
  emit_rex(out, true, reg, reg, width == 8);
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

void emit_instruction(CodeBuffer & out,
                      const mir_model::MirInstruction & instruction,
                      const mir_model::MirFunction * function)
{
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
  case mir_model::MirInstruction::MI_CMP:
    if(instruction.operands.size() == 2 &&
       (instruction.operands[0].kind == mir_model::MirOperand::OP_FRAME ||
        instruction.operands[0].kind == mir_model::MirOperand::OP_DEREF)) {
      if(!function) throw std::logic_error("memory compare outside function");
      emit_memory_compare(out, instruction, *function);
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
  case mir_model::MirInstruction::MI_JCC:
    if(!function) throw std::logic_error("conditional branch outside function");
    require_operands(instruction, 1);
    out.byte(0x0f);
    out.byte(0x80 + static_cast<unsigned>(instruction.condition));
    out.relative32(block_target(function->name, instruction.operands[0]));
    return;
  case mir_model::MirInstruction::MI_JMP:
    if(!function) throw std::logic_error("jump outside function");
    require_operands(instruction, 1);
    out.byte(0xe9);
    out.relative32(block_target(function->name, instruction.operands[0]));
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
  case mir_model::MirInstruction::MI_EXIT:
    emit_immediate_move(out, XR_RAX, 60);
    out.byte(0x0f);
    out.byte(0x05);
    return;
  default:
    throw std::logic_error("MIR opcode is not implemented by foundation encoder");
  }
}

void emit_function(CodeBuffer & out, const mir_model::MirFunction & function)
{
  out.label(function.name);
  emit_function_prologue(out, function);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    out.label(function.name + "::" + function.blocks[i].label);
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      emit_instruction(out, function.blocks[i].instructions[j], &function);
  }
}

std::size_t type_size(const std::string & type)
{
  if(type == "i1" || type == "i8" || type == "u8") return 1;
  if(type == "i16" || type == "u16") return 2;
  if(type == "i32" || type == "u32" || type == "f32") return 4;
  if(type == "i64" || type == "f64" || type == "ptr") return 8;
  if(type == "f80") return 16;
  throw std::logic_error("unsupported native data type: " + type);
}

void emit_integer_data(CodeBuffer & out, long long value, std::size_t size)
{
  out.little(static_cast<std::uint64_t>(value), static_cast<unsigned>(size));
}

void emit_global(CodeBuffer & out, const mir_model::MirGlobalDefinition & global)
{
  std::size_t global_alignment = 1;
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR)
    global_alignment = type_size(global.type);
  else {
    for(std::size_t i = 0; i < global.data_items.size(); ++i)
      if(global.data_items[i].kind != mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO)
        global_alignment = std::max(global_alignment, type_size(global.data_items[i].type));
  }
  out.align(global_alignment);
  out.label(global.name);
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR) {
    const std::size_t size = type_size(global.type);
    if(global.init_kind == mir_model::MirGlobalDefinition::GI_ADDR) {
      out.absolute64(global.symbol);
    } else if(global.init_kind == mir_model::MirGlobalDefinition::GI_FLOAT) {
      out.little(scalar_float_bits(global.literal_text, global.type),
                 static_cast<unsigned>(size));
    } else {
      emit_integer_data(out, global.int_value, size);
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
      out.absolute64(item.symbol);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_INTEGER)
      emit_integer_data(out, item.int_value, size);
    else if(item.kind == mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT)
      out.little(scalar_float_bits(item.literal_text, item.type),
                 static_cast<unsigned>(size));
    else throw std::logic_error("unsupported native global data item");
  }
}

void put_little(std::vector<unsigned char> & out, std::size_t offset,
                std::uint64_t value, unsigned count)
{
  if(offset + count > out.size()) throw std::logic_error("invalid ELF header field");
  for(unsigned i = 0; i < count; ++i)
    out[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}

std::vector<unsigned char> make_elf_image(const CodeBuffer & content)
{
  const std::size_t file_size = kContentOffset + content.bytes().size();
  std::vector<unsigned char> image(file_size, 0);
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
  std::copy(content.bytes().begin(), content.bytes().end(), image.begin() + kContentOffset);
  return image;
}

}  // namespace

void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            Stats * stats)
{
  if(program.target != "linux") throw std::runtime_error("ELF writer requires linux target");
  if(program.startup.empty()) throw std::runtime_error("native executable has no startup entry");
  CodeBuffer content;
  content.label("__startup");
  for(std::size_t i = 0; i < program.startup.size(); ++i)
    emit_instruction(content, program.startup[i], 0);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    emit_function(content, program.functions[i]);
  for(std::size_t i = 0; i < program.globals.size(); ++i)
    emit_global(content, program.globals[i]);
  content.resolve();
  const std::vector<unsigned char> image = make_elf_image(content);

  std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
  if(!out) throw std::runtime_error("unable to open native output: " + path);
  out.write(reinterpret_cast<const char *>(&image[0]),
            static_cast<std::streamsize>(image.size()));
  if(!out) throw std::runtime_error("unable to write native output: " + path);
  out.close();
  if(::chmod(path.c_str(), 0755) != 0)
    throw std::runtime_error("unable to mark native output executable: " + path +
                             ": " + std::strerror(errno));
  if(stats) {
    stats->fixups = content.fixup_count();
    stats->output_bytes = image.size();
  }
}

}  // namespace lowir_native
