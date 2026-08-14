#include "lowir_native.h"
#include "lowir_native_host_eh.h"
#include "lowir_native_object_elf.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kElfHeaderSize = 64;
const std::size_t kProgramHeaderSize = 56;
const std::size_t kContentOffset = kElfHeaderSize + kProgramHeaderSize;

std::string native_object_symbol(const std::string & symbol)
{
  return symbol.empty() || symbol[0] == '@' ? symbol : "@" + symbol;
}

struct Fixup
{
  enum Kind { RELATIVE32, ABSOLUTE64, ADDRESS32, TLS_OFFSET32 }
    kind = RELATIVE32;
  mir_model::MirOperand::AddressBinding address_binding =
    mir_model::MirOperand::ADDRESS_LOCAL;
  std::size_t offset = 0;
  std::string target;
  long long addend = 0;
};

class CodeBuffer
{
public:
  explicit CodeBuffer(std::size_t base_offset = kContentOffset,
                      bool relocatable_addresses = false)
    : base_offset_(base_offset),
      relocatable_addresses_(relocatable_addresses) {}

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
    while((base_offset_ + bytes_.size()) % alignment) byte(0);
  }

  void label(const std::string & name)
  {
    if(!labels_.emplace(name, bytes_.size()).second)
      throw std::logic_error("duplicate native label: " + name);
  }

  void label_at(const std::string & name, std::size_t offset)
  {
    if(offset > bytes_.size()) throw std::logic_error("native label is out of bounds");
    if(!labels_.emplace(name, offset).second)
      throw std::runtime_error("duplicate native symbol: " + name);
  }

  void alias(const std::string & name, const std::string & target)
  {
    const std::unordered_map<std::string, std::size_t>::const_iterator found =
      labels_.find(target);
    if(found == labels_.end())
      throw std::runtime_error("native alias has undefined target: " + target);
    label_at(name, found->second);
  }

  std::size_t size() const { return bytes_.size(); }
  bool relocatable_addresses() const { return relocatable_addresses_; }

  void append(const std::vector<unsigned char> & bytes)
  {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
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

  void absolute64(const std::string & target, long long addend = 0)
  {
    Fixup fixup;
    fixup.kind = Fixup::ABSOLUTE64;
    fixup.offset = bytes_.size();
    fixup.target = target;
    fixup.addend = addend;
    fixups_.push_back(fixup);
    zeros(8);
  }

  void address32(const std::string & target,
                 mir_model::MirOperand::AddressBinding address_binding)
  {
    Fixup fixup;
    fixup.kind = Fixup::ADDRESS32;
    fixup.address_binding = address_binding;
    fixup.offset = bytes_.size();
    fixup.target = target;
    fixups_.push_back(fixup);
    zeros(4);
  }

  void tls_offset32(const std::string & target)
  {
    Fixup fixup;
    fixup.kind = Fixup::TLS_OFFSET32;
    fixup.offset = bytes_.size();
    fixup.target = target;
    fixups_.push_back(fixup);
    zeros(4);
  }

  void relative32_at(std::size_t offset, const std::string & target,
                     long long elf_addend)
  {
    if(offset > bytes_.size() || 4 > bytes_.size() - offset)
      throw std::logic_error("native relative relocation is out of bounds");
    Fixup fixup;
    fixup.kind = Fixup::RELATIVE32;
    fixup.offset = offset;
    fixup.target = target;
    fixup.addend = elf_addend + 4;
    fixups_.push_back(fixup);
  }

  void absolute64_at(std::size_t offset, const std::string & target,
                     long long addend)
  {
    if(offset > bytes_.size() || 8 > bytes_.size() - offset)
      throw std::logic_error("native absolute relocation is out of bounds");
    Fixup fixup;
    fixup.kind = Fixup::ABSOLUTE64;
    fixup.offset = offset;
    fixup.target = target;
    fixup.addend = addend;
    fixups_.push_back(fixup);
  }

  void resolve()
  {
    for(std::size_t i = 0; i < fixups_.size(); ++i) {
      const Fixup & fixup = fixups_[i];
      const std::unordered_map<std::string, std::size_t>::const_iterator target =
        labels_.find(fixup.target);
      if(target == labels_.end()) throw std::runtime_error("undefined native symbol: " + fixup.target);
      if(fixup.kind == Fixup::RELATIVE32 ||
         fixup.kind == Fixup::ADDRESS32 || fixup.kind == Fixup::TLS_OFFSET32) {
        const std::int64_t delta = static_cast<std::int64_t>(target->second) -
                                   static_cast<std::int64_t>(fixup.offset + 4) +
                                   fixup.addend;
        if(delta < INT32_MIN || delta > INT32_MAX)
          throw std::runtime_error("native branch displacement exceeds rel32");
        patch(fixup.offset, static_cast<std::uint32_t>(delta), 4);
      } else {
        std::uint64_t address = kLoadAddress + kContentOffset + target->second;
        if(fixup.addend >= 0) {
          const std::uint64_t addend = static_cast<std::uint64_t>(fixup.addend);
          if(UINT64_MAX - address < addend)
            throw std::runtime_error("native address fixup overflows");
          address += addend;
        } else {
          const std::uint64_t magnitude =
            static_cast<std::uint64_t>(-(fixup.addend + 1)) + 1;
          if(address < magnitude)
            throw std::runtime_error("native address fixup underflows");
          address -= magnitude;
        }
        patch(fixup.offset, address, 8);
      }
    }
  }

  const std::vector<unsigned char> & bytes() const { return bytes_; }
  const std::unordered_map<std::string, std::size_t> & labels() const
  {
    return labels_;
  }
  const std::vector<Fixup> & fixups() const { return fixups_; }
  std::size_t fixup_count() const { return fixups_.size(); }

  std::string internal_label(const char * purpose)
  {
    return std::string(".__cppgm_x87_") + purpose + "_" +
      std::to_string(next_internal_label_++);
  }

private:
  std::size_t base_offset_;
  bool relocatable_addresses_;
  std::vector<unsigned char> bytes_;
  std::unordered_map<std::string, std::size_t> labels_;
  std::vector<Fixup> fixups_;
  std::size_t next_internal_label_ = 0;
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
                      const std::string & symbol,
                      mir_model::MirOperand::AddressBinding address_binding =
                        mir_model::MirOperand::ADDRESS_LOCAL)
{
  // Keep symbol-address intent distinct from calls and stored pointers.  The
  // relocatable writer leaves this RIP-relative LEA for a definition in this
  // object and changes it to a GOT load for an import.
  if(out.relocatable_addresses()) {
    emit_rex(out, true, destination, XR_RBP);
    out.byte(0x8d);
    emit_modrm(out, 0, destination, 5);
    out.address32(symbol, address_binding);
  } else {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0xb8 + (static_cast<unsigned>(destination) & 7));
    out.absolute64(symbol);
  }
}

void emit_tls_address(CodeBuffer & out, X64Register destination,
                      const std::string & symbol)
{
  // PA32's non-PIE host link permits one local-exec TPOFF32 displacement.
  out.byte(0x64);
  emit_rex(out, true, destination, XR_RSP);
  out.byte(0x8b);
  emit_modrm(out, 0, destination, 4);
  out.byte(0x25);
  out.zeros(4);
  emit_rex(out, true, XR_RAX, destination);
  out.byte(0x81);
  emit_modrm(out, 3, 0, destination);
  out.tls_offset32(symbol);
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

void emit_i128_shift(CodeBuffer & out, mir_model::MirInstruction::Opcode opcode)
{
  const std::string high = out.internal_label("i128_shift_high");
  const std::string done = out.internal_label("i128_shift_done");
  // cmp rcx, 64; jae high
  out.byte(0x48); out.byte(0x83); out.byte(0xf9); out.byte(0x40);
  out.byte(0x0f); out.byte(0x83); out.relative32(high);
  if(opcode == mir_model::MirInstruction::MI_I128_SHL) {
    // shld rdx, rax, cl; shl rax, cl
    out.byte(0x48); out.byte(0x0f); out.byte(0xa5); out.byte(0xc2);
    out.byte(0x48); out.byte(0xd3); out.byte(0xe0);
  } else {
    // shrd rax, rdx, cl; shr/sar rdx, cl
    out.byte(0x48); out.byte(0x0f); out.byte(0xad); out.byte(0xd0);
    out.byte(0x48); out.byte(0xd3);
    out.byte(opcode == mir_model::MirInstruction::MI_I128_SAR ? 0xfa : 0xea);
  }
  out.byte(0xe9); out.relative32(done);
  out.label(high);
  if(opcode == mir_model::MirInstruction::MI_I128_SHL) {
    emit_register_move(out, XR_RDX, XR_RAX);
    out.byte(0x48); out.byte(0xd3); out.byte(0xe2); // shl rdx, cl
    out.byte(0x48); out.byte(0x31); out.byte(0xc0); // xor rax, rax
  } else {
    emit_register_move(out, XR_RAX, XR_RDX);
    out.byte(0x48); out.byte(0xd3);
    out.byte(opcode == mir_model::MirInstruction::MI_I128_SAR ? 0xf8 : 0xe8);
    if(opcode == mir_model::MirInstruction::MI_I128_SAR) {
      out.byte(0x48); out.byte(0xc1); out.byte(0xfa); out.byte(0x3f);
    } else {
      out.byte(0x48); out.byte(0x31); out.byte(0xd2); // xor rdx, rdx
    }
  }
  out.label(done);
}

void emit_register_alu(CodeBuffer & out, unsigned opcode,
                       X64Register destination, X64Register source)
{
  emit_rex(out, true, source, destination);
  out.byte(opcode);
  emit_modrm(out, 3, source, destination);
}

void emit_condition_jump(CodeBuffer & out, X86Condition condition,
                         const std::string & target)
{
  out.byte(0x0f);
  out.byte(0x80 + static_cast<unsigned>(condition));
  out.relative32(target);
}

void emit_i128_abs(CodeBuffer & out, X64Register low, X64Register high,
                   X64Register mask)
{
  emit_register_alu(out, 0x31, low, mask);   // xor low, mask
  emit_register_alu(out, 0x31, high, mask);  // xor high, mask
  emit_register_alu(out, 0x29, low, mask);   // sub low, mask
  emit_register_alu(out, 0x19, high, mask);  // sbb high, mask
}

void emit_i128_division(CodeBuffer & out,
                        mir_model::MirInstruction::Opcode opcode)
{
  const bool signed_division = opcode == mir_model::MirInstruction::MI_I128_SDIV ||
    opcode == mir_model::MirInstruction::MI_I128_SMOD;
  const bool remainder = opcode == mir_model::MirInstruction::MI_I128_UMOD ||
    opcode == mir_model::MirInstruction::MI_I128_SMOD;
  const unsigned scratch_bytes = signed_division ? 32 : 16;
  emit_stack_adjust(out, true, scratch_bytes);

  if(signed_division) {
    emit_register_move(out, XR_RDI, XR_RDX);
    out.byte(0x48); out.byte(0xc1); out.byte(0xff); out.byte(0x3f); // sar rdi,63
    emit_store(out, XR_RSP, 16, XR_RDI, 64);
    emit_register_move(out, XR_R10, XR_RSI);
    out.byte(0x49); out.byte(0xc1); out.byte(0xfa); out.byte(0x3f); // sar r10,63
    emit_register_move(out, XR_R11, XR_RDI);
    emit_register_alu(out, 0x31, XR_R11, XR_R10);
    emit_store(out, XR_RSP, 24, XR_R11, 64);
    emit_i128_abs(out, XR_RAX, XR_RDX, XR_RDI);
    emit_i128_abs(out, XR_RCX, XR_RSI, XR_R10);
  }

  emit_store(out, XR_RSP, 0, XR_RCX, 64);
  emit_store(out, XR_RSP, 8, XR_RSI, 64);
  emit_register_alu(out, 0x31, XR_R10, XR_R10); // xor remainder low
  emit_register_alu(out, 0x31, XR_R11, XR_R11); // xor remainder high
  emit_immediate_move(out, XR_RCX, 128);

  const std::string loop = out.internal_label("i128_div_loop");
  const std::string subtract = out.internal_label("i128_div_subtract");
  const std::string skip = out.internal_label("i128_div_skip");
  out.label(loop);
  out.byte(0x48); out.byte(0xd1); out.byte(0xe0); // shl rax,1
  out.byte(0x48); out.byte(0xd1); out.byte(0xd2); // rcl rdx,1
  out.byte(0x49); out.byte(0xd1); out.byte(0xd2); // rcl r10,1
  out.byte(0x49); out.byte(0xd1); out.byte(0xd3); // rcl r11,1

  emit_load(out, XR_RDI, XR_RSP, 8, 64);
  emit_register_alu(out, 0x39, XR_R11, XR_RDI);
  emit_condition_jump(out, XC_B, skip);
  emit_condition_jump(out, XC_A, subtract);
  emit_load(out, XR_RDI, XR_RSP, 0, 64);
  emit_register_alu(out, 0x39, XR_R10, XR_RDI);
  emit_condition_jump(out, XC_B, skip);

  out.label(subtract);
  emit_load(out, XR_RDI, XR_RSP, 0, 64);
  emit_register_alu(out, 0x29, XR_R10, XR_RDI);
  emit_load(out, XR_RDI, XR_RSP, 8, 64);
  emit_register_alu(out, 0x19, XR_R11, XR_RDI);
  out.byte(0x48); out.byte(0x83); out.byte(0xc8); out.byte(0x01); // or rax,1

  out.label(skip);
  out.byte(0x48); out.byte(0xff); out.byte(0xc9); // dec rcx
  emit_condition_jump(out, XC_NE, loop);

  if(signed_division) {
    emit_load(out, XR_RDI, XR_RSP, 24, 64);
    emit_i128_abs(out, XR_RAX, XR_RDX, XR_RDI);
    emit_load(out, XR_RDI, XR_RSP, 16, 64);
    emit_i128_abs(out, XR_R10, XR_R11, XR_RDI);
  }
  if(remainder) {
    emit_register_move(out, XR_RAX, XR_R10);
    emit_register_move(out, XR_RDX, XR_R11);
  }
  emit_stack_adjust(out, false, scratch_bytes);
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
  if(number == "snan" && type == "f32") return UINT64_C(0x7fa00000);
  if(number == "snan" && type == "f64") return UINT64_C(0x7ff4000000000000);
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

std::pair<std::uint64_t, std::uint64_t> extended_float_words(const std::string & text)
{
  const std::string number = unsuffixed_float_text(text);
  if(number == "snan") return std::make_pair(UINT64_C(0xa000000000000000), UINT64_C(0x7fff));
  errno = 0;
  char * end = 0;
  const long double value = std::strtold(number.c_str(), &end);
  if(errno || !end || *end) throw std::runtime_error("invalid f80 literal: " + text);
  unsigned char bytes[16] = {};
  unsigned char native[sizeof(long double)] = {};
  std::memcpy(native, &value, sizeof(value));
  const std::size_t payload = std::min<std::size_t>(10, sizeof(value));
  std::copy(native, native + payload, bytes);
  std::uint64_t low = 0, high = 0;
  std::memcpy(&low, bytes, 8);
  std::memcpy(&high, bytes + 8, 8);
  return std::make_pair(low, high);
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
    extended_float_words(text);
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
    emit_immediate_move(out, XR_R10, scalar_float_bits(text, type));
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
  out.byte(0x0f);
  out.byte(0x80 + static_cast<unsigned>(condition));
  out.relative32(target);
}

void emit_unconditional_jump(CodeBuffer & out, const std::string & target)
{
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
  require_operands(instruction, 2);
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
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
  require_operands(instruction, 2);
  const std::pair<std::string, std::string> types = conversion_types(instruction.type);
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

const char * const kEhTop = ".__cppgm_eh_top";
const char * const kEhValue = ".__cppgm_eh_value";
const char * const kEhAdjusted = ".__cppgm_eh_adjusted";
const char * const kEhType = ".__cppgm_eh_type";
const char * const kEhSelector = ".__cppgm_eh_selector";
const char * const kEhCaught = ".__cppgm_eh_caught";
const char * const kEhDispatch = ".__cppgm_eh_dispatch";
const char * const kEhResume = ".__cppgm_eh_resume";

void emit_test_register(CodeBuffer & out, X64Register reg)
{
  emit_rex(out, true, reg, reg);
  out.byte(0x85);
  emit_modrm(out, 3, reg, reg);
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
    out.byte(0xe9); out.relative32(selected);
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
    out.byte(0xe9); out.relative32(kEhDispatch); return true;
  case mir_model::MirInstruction::MI_RESUME:
    require_operands(instruction, 0);
    out.byte(0xe9); out.relative32(kEhResume); return true;
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

void emit_function(CodeBuffer & out, const mir_model::MirFunction & function)
{
  // The x86-64 member-function-pointer representation reserves bit zero of
  // the target word as the virtual-slot tag.  Keep every native function
  // entry at least two-byte aligned so a direct target cannot carry that tag.
  out.align(2);
  out.label(function.name);
  const std::string object_symbol = native_object_symbol(function.object_symbol);
  if(!object_symbol.empty() && object_symbol != function.name)
    out.label(object_symbol);
  emit_function_prologue(out, function);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    out.label(function.name + "::" + function.blocks[i].label);
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      emit_instruction(out, function.blocks[i].instructions[j], &function);
  }
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
  out.byte(0xe9); out.relative32(kEhDispatch);
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
  out.byte(0xe9); out.relative32(kEhDispatch);
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
  out.byte(0xe9); out.relative32(done);
  out.label(si);
  emit_register_move(out, XR_RDI, XR_RBX);
  emit_load(out, XR_RSI, XR_R12, 16, 64);
  emit_register_move(out, XR_RDX, XR_R13);
  out.byte(0xe8); out.relative32(helper);
  emit_store(out, XR_RBP, -48, XR_RAX, 64);
  out.byte(0xe9); out.relative32(done);
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
  out.byte(0xe9); out.relative32(loop);
  out.label(record);
  emit_store(out, XR_RBP, -48, XR_RBX, 64);
  out.byte(0xe9); out.relative32(done);
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
  out.byte(0xe9); out.relative32(kEhDispatch);
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
  out.byte(0xe9); out.relative32(kEhDispatch);
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

std::size_t type_size(const std::string & type)
{
  if(type == "i1" || type == "i8" || type == "u8") return 1;
  if(type == "i16" || type == "u16") return 2;
  if(type == "i32" || type == "u32" || type == "f32") return 4;
  if(type == "i64" || type == "f64" || type == "ptr") return 8;
  if(type == "i128") return 16;
  if(type == "f80") return 16;
  throw std::logic_error("unsupported native data type: " + type);
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
      extended_float_words(text);
    out.little(words.first, 8);
    out.little(words.second, 8);
    return;
  }
  out.little(scalar_float_bits(text, type),
             static_cast<unsigned>(type_size(type)));
}

std::size_t global_alignment(const mir_model::MirGlobalDefinition & global)
{
  std::size_t global_alignment = 1;
  if(global.storage_kind == mir_model::MirGlobalDefinition::GS_SCALAR)
    global_alignment = type_size(global.type);
  else {
    for(std::size_t i = 0; i < global.data_items.size(); ++i)
      if(global.data_items[i].kind != mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO)
        global_alignment = std::max(global_alignment, type_size(global.data_items[i].type));
  }
  return global_alignment;
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
    HostFunctionLayout & layout)
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
    site.landing_pad = landing_pad;
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
  const std::string no_landing_pad;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const mir_model::MirBlock & block = function.blocks[i];
    out.label(function.name + "::" + block.label);
    if(function.host_eh_clauses.count(block.label)) {
      emit_store(out, XR_RBP,
        actual_frame_offset(function, function.host_eh_exception_offset),
        XR_RAX, 64);
      emit_store(out, XR_RBP,
        actual_frame_offset(function, function.host_eh_selector_offset),
        XR_RDX, 64);
    }
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const std::size_t landing_block = function.host_eh_enabled ?
        region_plan.call_landing_blocks[i][j] : 0;
      emit_host_instruction(out, block.instructions[j], function,
        landing_block ? function.blocks[landing_block - 1].label :
                        no_landing_pad,
        layout);
    }
  }
  layout.size = out.size() - layout.offset;
  return layout;
}

EncodedSection encoded_section(const CodeBuffer & source,
                               const std::string & name,
                               std::uint64_t flags,
                               std::size_t alignment)
{
  EncodedSection result;
  result.name = name;
  result.flags = flags;
  result.alignment = alignment;
  result.bytes = source.bytes();
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
                            Stats * stats)
{
  ProgramLoweringSession lowering(source, target, stats);
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
    const std::vector<unsigned char> & compiler_payload,
    Stats * stats)
{
  if(target != "linux")
    throw std::runtime_error("ELF object writer requires linux target");
  ProgramLoweringSession lowering(source, target, stats);
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
      data_sections[i].content, data_sections[i].name,
      data_sections[i].flags, data_sections[i].alignment));
  const std::vector<unsigned char> image = make_linux_relocatable_image(
    source, encoded_section(text, ".text", 6, 16),
    std::move(encoded_data_sections),
    functions,
    compiler_payload, relocations);
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
