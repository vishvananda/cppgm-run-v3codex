#include "lowir_native.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
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

void emit_load(CodeBuffer & out, X64Register destination, X64Register base,
               long long displacement)
{
  emit_rex(out, true, destination, base);
  out.byte(0x8b);
  emit_memory_modrm(out, destination, base, displacement);
}

void emit_store(CodeBuffer & out, X64Register base, long long displacement,
                X64Register source)
{
  emit_rex(out, true, source, base);
  out.byte(0x89);
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
  out.byte(0x83);
  emit_modrm(out, 3, subtract ? 5 : 0, XR_RSP);
  out.byte(bytes);
}

void emit_function_prologue(CodeBuffer & out, const mir_model::MirFunction & function)
{
  emit_push(out, XR_RBP);
  emit_register_move(out, XR_RBP, XR_RSP);
  for(std::size_t i = 0; i < function.callee_saved_regs.size(); ++i)
    emit_push(out, function.callee_saved_regs[i]);
  if(function.callee_saved_regs.size() % 2) emit_stack_adjust(out, true, 8);
}

void emit_function_return(CodeBuffer & out, const mir_model::MirFunction & function)
{
  if(function.callee_saved_regs.size() % 2) emit_stack_adjust(out, false, 8);
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
                       const mir_model::MirOperand & address)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    emit_load(out, destination, address.reg, address.offset);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text);
    emit_load(out, destination, XR_R11, 0);
  } else throw std::logic_error("unsupported native load address");
}

void emit_address_store(CodeBuffer & out, const mir_model::MirOperand & address,
                        X64Register source)
{
  if(address.kind == mir_model::MirOperand::OP_DEREF) {
    emit_store(out, address.reg, address.offset, source);
  } else if(address.kind == mir_model::MirOperand::OP_GLOBAL) {
    emit_symbol_move(out, XR_R11, address.text);
    emit_store(out, XR_R11, 0, source);
  } else throw std::logic_error("unsupported native store address");
}

void emit_alu(CodeBuffer & out, const mir_model::MirInstruction & instruction,
              unsigned register_opcode, unsigned immediate_extension)
{
  require_operands(instruction, 2);
  const X64Register destination = require_register(instruction.operands[0]);
  const mir_model::MirOperand & source = instruction.operands[1];
  if(source.kind == mir_model::MirOperand::OP_REG) {
    emit_rex(out, true, source.reg, destination);
    out.byte(register_opcode);
    emit_modrm(out, 3, source.reg, destination);
  } else if(source.kind == mir_model::MirOperand::OP_IMM &&
            source.imm >= INT32_MIN && source.imm <= INT32_MAX) {
    emit_rex(out, true, XR_RAX, destination);
    out.byte(0x81);
    emit_modrm(out, 3, immediate_extension, destination);
    out.little(static_cast<std::uint32_t>(source.imm), 4);
  } else throw std::logic_error("unsupported native ALU operand");
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
    require_operands(instruction, 2);
    emit_address_load(out, require_register(instruction.operands[0]), instruction.operands[1]);
    return;
  case mir_model::MirInstruction::MI_STORE:
    require_operands(instruction, 2);
    emit_address_store(out, instruction.operands[0], require_register(instruction.operands[1]));
    return;
  case mir_model::MirInstruction::MI_LEA:
    require_operands(instruction, 2);
    if(instruction.operands[1].kind != mir_model::MirOperand::OP_DEREF)
      throw std::logic_error("native lea source is not memory-shaped");
    emit_lea(out, require_register(instruction.operands[0]),
             instruction.operands[1].reg, instruction.operands[1].offset);
    return;
  case mir_model::MirInstruction::MI_ADD:
    emit_alu(out, instruction, 0x01, 0);
    return;
  case mir_model::MirInstruction::MI_CMP:
    emit_alu(out, instruction, 0x39, 7);
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
    else throw std::logic_error("floating global encoding is not in foundation checkpoint");
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
