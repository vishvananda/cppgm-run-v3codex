#include "mir_model.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mir_model {
namespace {

const char * register_name(X64Register reg)
{
  static const char * const names[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
  };
  const std::size_t index = static_cast<std::size_t>(reg);
  if(index >= sizeof(names) / sizeof(names[0]))
    throw std::logic_error("invalid MIR register");
  return names[index];
}

const char * xmm_name(XmmRegister reg)
{
  static const char * const names[] = {
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"
  };
  const std::size_t index = static_cast<std::size_t>(reg);
  if(index >= sizeof(names) / sizeof(names[0]))
    throw std::logic_error("invalid MIR XMM register");
  return names[index];
}

std::string frame_address(long long offset)
{
  std::ostringstream out;
  out << "[rbp";
  if(offset > 0) out << '+' << offset;
  else if(offset < 0) out << offset;
  out << ']';
  return out.str();
}

std::string dereference(const Operand & operand)
{
  std::ostringstream out;
  out << '[' << register_name(operand.reg);
  if(operand.has_index) {
    out << '+' << register_name(operand.index);
    if(operand.scale != 1) out << '*' << operand.scale;
  }
  if(operand.offset > 0) out << '+' << operand.offset;
  else if(operand.offset < 0) out << operand.offset;
  out << ']';
  return out.str();
}

std::string operand_text(const Operand & operand, const Program & program,
                         const Function * function)
{
  std::ostringstream out;
  switch(operand.kind) {
  case Operand::OP_REG: return register_name(operand.reg);
  case Operand::OP_XMM: return xmm_name(operand.xmm);
  case Operand::OP_IMM: out << operand.imm; return out.str();
  case Operand::OP_FLOAT_IMM: return operand.text;
  case Operand::OP_SYMBOL:
  case Operand::OP_GLOBAL: return mir_symbol_name(program, operand.symbol);
  case Operand::OP_LABEL:
    if(!function)
      throw std::logic_error("MIR label operand outside function");
    return mir_block_label(*function, operand.block);
  case Operand::OP_FRAME: return frame_address(operand.offset);
  case Operand::OP_DEREF: return dereference(operand);
  }
  throw std::logic_error("invalid MIR operand");
}

const char * condition_suffix(X86Condition condition)
{
  switch(condition) {
  case XC_O: return "o";
  case XC_NO: return "no";
  case XC_B: return "b";
  case XC_AE: return "ae";
  case XC_E: return "e";
  case XC_NE: return "ne";
  case XC_BE: return "be";
  case XC_A: return "a";
  case XC_S: return "s";
  case XC_NS: return "ns";
  case XC_P: return "p";
  case XC_NP: return "np";
  case XC_L: return "l";
  case XC_GE: return "ge";
  case XC_LE: return "le";
  case XC_G: return "g";
  }
  throw std::logic_error("invalid MIR condition");
}

const char * opcode_name(Instruction::Opcode opcode)
{
  switch(opcode) {
  case Instruction::MI_MOV: return "mov";
  case Instruction::MI_LOAD: return "load";
  case Instruction::MI_STORE: return "store";
  case Instruction::MI_MFENCE: return "mfence";
  case Instruction::MI_LOCK_XADD: return "lock_xadd";
  case Instruction::MI_XCHG: return "xchg";
  case Instruction::MI_LOCK_CMPXCHG: return "lock_cmpxchg";
  case Instruction::MI_LOCK_CMPXCHG16B: return "lock_cmpxchg16b";
  case Instruction::MI_LEA: return "lea";
  case Instruction::MI_FMOV: return "fmov";
  case Instruction::MI_FNEG: return "fneg";
  case Instruction::MI_FADD: return "fadd";
  case Instruction::MI_FSUB: return "fsub";
  case Instruction::MI_FMUL: return "fmul";
  case Instruction::MI_FDIV: return "fdiv";
  case Instruction::MI_FEQ: return "feq";
  case Instruction::MI_FNE: return "fne";
  case Instruction::MI_FLT: return "flt";
  case Instruction::MI_FGT: return "fgt";
  case Instruction::MI_FLE: return "fle";
  case Instruction::MI_FGE: return "fge";
  case Instruction::MI_FCMP: return "fcmp";
  case Instruction::MI_FSTP: return "fstp";
  case Instruction::MI_FPOP: return "fpop";
  case Instruction::MI_SITOFP: return "sitofp";
  case Instruction::MI_UITOFP: return "uitofp";
  case Instruction::MI_FPTOSI: return "fptosi";
  case Instruction::MI_FPTOUI: return "fptoui";
  case Instruction::MI_FPEXT: return "fpext";
  case Instruction::MI_FPTRUNC: return "fptrunc";
  case Instruction::MI_ADD: return "add";
  case Instruction::MI_SUB: return "sub";
  case Instruction::MI_IMUL: return "imul";
  case Instruction::MI_AND: return "and";
  case Instruction::MI_OR: return "or";
  case Instruction::MI_XOR: return "xor";
  case Instruction::MI_NEG: return "neg";
  case Instruction::MI_NOT: return "not";
  case Instruction::MI_BSWAP: return "bswap";
  case Instruction::MI_CMP: return "cmp";
  case Instruction::MI_TEST: return "test";
  case Instruction::MI_SETCC: return "setcc";
  case Instruction::MI_MOVZX: return "movzx";
  case Instruction::MI_SEXT: return "sext";
  case Instruction::MI_ZEXT: return "zext";
  case Instruction::MI_CQO: return "cqo";
  case Instruction::MI_IDIV: return "idiv";
  case Instruction::MI_DIV: return "div";
  case Instruction::MI_SHL_CL: return "shl";
  case Instruction::MI_SHR_CL: return "shr";
  case Instruction::MI_SAR_CL: return "sar";
  case Instruction::MI_TLS_ADDR: return "tls_addr";
  case Instruction::MI_CALL: return "call";
  case Instruction::MI_CALL_INDIRECT: return "call";
  case Instruction::MI_EH_PUSH: return "eh_push";
  case Instruction::MI_EH_POP: return "eh_pop";
  case Instruction::MI_EH_CATCH: return "eh_catch";
  case Instruction::MI_EH_FILTER: return "eh_filter";
  case Instruction::MI_EH_CLEANUP_CLAUSE: return "eh_cleanup_clause";
  case Instruction::MI_LOAD_EXCEPTION: return "load_exception";
  case Instruction::MI_LOAD_EXCEPTION_SELECTOR: return "load_exception_selector";
  case Instruction::MI_THROW: return "throw";
  case Instruction::MI_RESUME: return "resume";
  case Instruction::MI_JMP: return "jmp";
  case Instruction::MI_JMP_INDIRECT: return "jmp";
  case Instruction::MI_RET: return "ret";
  case Instruction::MI_FRET: return "fret";
  case Instruction::MI_EXIT: return "exit";
  default: break;
  }
  throw std::logic_error("MIR serializer does not support opcode");
}

void render_operands(std::ostringstream & out, const Instruction & instruction,
                     const Program & program,
                     const Function * function,
                     bool leading_comma = false)
{
  for(std::size_t i = 0; i < instruction.operands.size(); ++i) {
    if(i == 0) out << (leading_comma ? ", " : " ");
    else out << ", ";
    if(instruction.opcode == Instruction::MI_CALL_INDIRECT ||
       instruction.opcode == Instruction::MI_JMP_INDIRECT) out << '*';
    out << operand_text(instruction.operands[i], program, function);
  }
}

void render_call_facts(std::ostringstream & out,
                       const Instruction & instruction)
{
  if(instruction.opcode != Instruction::MI_CALL &&
     instruction.opcode != Instruction::MI_CALL_INDIRECT) return;
  const bool has_facts = instruction.call_argument_registers_known ||
    instruction.call_stack_bytes != 0 || instruction.call_variadic ||
    instruction.call_unwind_no || instruction.call_returns_noreturn;
  if(!has_facts) return;

  out << " [";
  bool needs_separator = false;
  if(instruction.call_argument_registers_known) {
    out << "args=(";
    bool needs_register_separator = false;
    for(unsigned reg = 0; reg != 16; ++reg)
      if(instruction.call_argument_register_mask & (1u << reg)) {
        if(needs_register_separator) out << ',';
        out << register_name(static_cast<X64Register>(reg));
        needs_register_separator = true;
      }
    for(unsigned reg = 0; reg != 8; ++reg)
      if(instruction.call_argument_register_mask & (1u << (16 + reg))) {
        if(needs_register_separator) out << ',';
        out << xmm_name(static_cast<XmmRegister>(reg));
        needs_register_separator = true;
      }
    out << ')';
    needs_separator = true;
  }
  if(instruction.call_stack_bytes) {
    if(needs_separator) out << ", ";
    out << "stack=" << instruction.call_stack_bytes;
    needs_separator = true;
  }
  if(instruction.call_variadic) {
    if(needs_separator) out << ", ";
    out << "variadic";
    needs_separator = true;
  }
  if(instruction.call_unwind_no) {
    if(needs_separator) out << ", ";
    out << "unwind=no";
    needs_separator = true;
  }
  if(instruction.call_returns_noreturn) {
    if(needs_separator) out << ", ";
    out << "returns=noreturn";
  }
  out << ']';
}

std::string instruction_text(const Instruction & instruction,
                             const Program & program,
                             const Function * function)
{
  std::ostringstream out;
  if(instruction.opcode == Instruction::MI_COPY_BYTES) {
    out << "copy_bytes " << instruction.byte_count << 'x'
        << instruction.byte_alignment;
    render_operands(out, instruction, program, function, true);
    return out.str();
  }
  if(instruction.opcode == Instruction::MI_ZERO_BYTES) {
    out << "zero_bytes " << instruction.byte_count << 'x'
        << instruction.byte_alignment;
    render_operands(out, instruction, program, function, true);
    return out.str();
  }
  if(instruction.opcode == Instruction::MI_JCC) {
    out << 'j' << condition_suffix(instruction.condition);
    render_operands(out, instruction, program, function);
    return out.str();
  }
  if(instruction.opcode == Instruction::MI_SETCC) {
    out << "set" << condition_suffix(instruction.condition);
    render_operands(out, instruction, program, function);
    return out.str();
  }
  if(instruction.opcode == Instruction::MI_LOCK_CMPXCHG16B) {
    out << "lock_cmpxchg16b";
    render_operands(out, instruction, program, function);
    out << ", expected=rdx:rax, desired=rcx:rbx";
    return out.str();
  }
  if(instruction.opcode == Instruction::MI_SHL_CL ||
     instruction.opcode == Instruction::MI_SHR_CL ||
     instruction.opcode == Instruction::MI_SAR_CL) {
    out << opcode_name(instruction.opcode);
    render_operands(out, instruction, program, function);
    out << ", cl";
    return out.str();
  }
  out << opcode_name(instruction.opcode);
  if(((instruction.opcode >= Instruction::MI_FMOV &&
       instruction.opcode <= Instruction::MI_FPTRUNC) ||
      instruction.opcode == Instruction::MI_FRET ||
      instruction.opcode == Instruction::MI_LOAD ||
      instruction.opcode == Instruction::MI_STORE ||
      instruction.opcode == Instruction::MI_LOCK_XADD ||
      instruction.opcode == Instruction::MI_XCHG ||
      instruction.opcode == Instruction::MI_LOCK_CMPXCHG ||
      instruction.opcode == Instruction::MI_CMP ||
      instruction.opcode == Instruction::MI_TEST ||
      instruction.opcode == Instruction::MI_SEXT ||
      instruction.opcode == Instruction::MI_ZEXT) &&
     instruction.type.kind != lowir_model::LTK_INVALID) {
    out << '.';
    if(instruction.opcode >= Instruction::MI_SITOFP &&
       instruction.opcode <= Instruction::MI_FPTRUNC &&
       instruction.source_type.kind != lowir_model::LTK_INVALID)
      out << lowir_model::lowir_type_text(instruction.source_type) << '.';
    out << lowir_model::lowir_type_text(instruction.type);
  }
  render_operands(out, instruction, program, function);
  render_call_facts(out, instruction);
  return out.str();
}

std::string rendered_instruction_text(const Instruction & instruction,
                                      const Program & program,
                                      const Function * function = 0)
{
  std::ostringstream out;
  out << instruction_text(instruction, program, function);
  if(instruction.debug_location.present())
    out << " !dbg(" << instruction.debug_location.file << ", "
        << instruction.debug_location.line << ", "
        << instruction.debug_location.column << ')';
  return out.str();
}

void render_global(std::ostringstream & out, const Program & program,
                   const GlobalDefinition & global)
{
  out << "global " << global.name;
  if(global.readonly) out << " readonly";
  if(global.thread_local_storage) out << " thread_local";
  out << '\n';
  if(global.storage_kind == GlobalDefinition::GS_DATA) {
    out << "  storage data\n";
    for(std::size_t i = 0; i < global.data_items.size(); ++i) {
      const GlobalDefinition::DataItem & item = global.data_items[i];
      out << "  item ";
      if(item.kind == GlobalDefinition::DataItem::ITEM_ZERO) {
        out << "zero " << item.zero_bytes << '\n';
      } else if(item.kind == GlobalDefinition::DataItem::ITEM_ADDR) {
        out << lowir_model::lowir_type_text(item.type) << " addr "
            << item.symbol;
        if(item.addr_addend > 0) out << '+' << item.addr_addend;
        else if(item.addr_addend < 0) out << item.addr_addend;
        out << '\n';
      } else {
        out << lowir_model::lowir_type_text(item.type) << ' ';
        if(item.kind == GlobalDefinition::DataItem::ITEM_FLOAT)
          out << item.literal_text;
		else if (item.type.kind == lowir_model::LTK_I128 &&
                 !item.literal_text.empty())
		  out << item.literal_text;
		else out << item.int_value;
        out << '\n';
      }
    }
    return;
  }
  out << "  storage scalar " << lowir_model::lowir_type_text(global.type)
      << '\n';
  out << "  init ";
  if(global.init_kind == GlobalDefinition::GI_ADDR) {
    out << "addr " << global.symbol;
    if(global.addr_addend > 0) out << '+' << global.addr_addend;
    else if(global.addr_addend < 0) out << global.addr_addend;
  } else if(global.init_kind == GlobalDefinition::GI_FLOAT) {
    out << lowir_model::lowir_type_text(global.type) << ' '
        << global.literal_text;
  } else {
    out << lowir_model::lowir_type_text(global.type) << ' ';
	if(global.type.kind == lowir_model::LTK_I128 &&
           !global.literal_text.empty())
	  out << global.literal_text;
	else out << global.int_value;
  }
  out << '\n';
}

void render_function(std::ostringstream & out, const Program & program,
                     const Function & function)
{
  out << "function " << function.name << "\n  abi\n";
  for(std::size_t i = 0; i < function.params.size(); ++i) {
    const ParamBinding & param = function.params[i];
    out << "    param " << param.name << " -> ";
    if(param.location == ParamBinding::PL_REG) out << register_name(param.reg);
    else if(param.location == ParamBinding::PL_XMM) out << xmm_name(param.xmm);
    else out << "[rbp+" << param.stack_offset << ']';
    out << " : " << lowir_model::lowir_type_text(param.type) << '\n';
  }
  out << "    return " << lowir_model::lowir_type_text(function.return_type)
      << " -> ";
  if(function.return_type.kind == lowir_model::LTK_VOID) out << "void\n";
  else if(function.return_type.kind == lowir_model::LTK_F32 ||
          function.return_type.kind == lowir_model::LTK_F64)
    out << "xmm0\n";
  else if(function.return_type.kind == lowir_model::LTK_F80) out << "st0\n";
  else out << "rax\n";
  out << "  frame\n    stack_size " << function.stack_size
      << "\n    scratch_bytes " << function.scratch_bytes << '\n';
  if(!function.callee_saved_regs.empty()) {
    out << "    preserve";
    for(std::size_t i = 0; i < function.callee_saved_regs.size(); ++i)
      out << ' ' << register_name(function.callee_saved_regs[i]);
    out << '\n';
  }
  for(std::size_t i = 0; i < function.frame_bindings.size(); ++i) {
    const FrameBinding & binding = function.frame_bindings[i];
    out << "    ";
    if(binding.kind == FrameBinding::FB_PARAM_SLOT) out << "param-slot ";
    else if(binding.kind == FrameBinding::FB_SLOT) out << "slot ";
    else out << "temp ";
    out << binding.name << " -> " << frame_address(binding.offset)
        << " : " << lowir_model::lowir_type_text(binding.type) << '\n';
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    out << "\n  block " << mir_block_label(function, function.blocks[i].id)
        << '\n';
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      out << "    " << rendered_instruction_text(
        function.blocks[i].instructions[j], program, &function) << '\n';
  }
}

}  // namespace

const std::string & mir_block_label(const MirFunction & function,
                                    lowir_model::BlockId block)
{
  const std::uint32_t index = block;
  if(!block.valid() || index >= function.block_labels.size())
    throw std::logic_error("invalid MIR block identity");
  return function.block_labels[index];
}

const std::string & mir_symbol_name(const MirProgram & program,
                                    lowir_model::SymbolId symbol)
{
  const std::uint32_t index = symbol;
  if(!symbol.valid() || index >= program.symbol_names.size())
    throw std::logic_error("invalid MIR symbol identity");
  return program.symbol_names[index];
}

std::string serialize_mir_program(const MirProgram & program)
{
  std::ostringstream out;
  out << "machine_ir x86_64 " << program.target << '\n';
  if(!program.startup.empty()) {
    out << "\nstartup\n";
    for(std::size_t i = 0; i < program.startup.size(); ++i)
      out << "    " << rendered_instruction_text(
        program.startup[i], program) << '\n';
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    out << '\n';
    render_global(out, program, program.globals[i]);
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    out << '\n';
    render_function(out, program, program.functions[i]);
  }
  return out.str();
}

void write_mir_program_file(const std::string & path,
                            const MirProgram & program)
{
  const std::string text = serialize_mir_program(program);
  std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
  if(!out) throw std::runtime_error("unable to open MIR output: " + path);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if(!out) throw std::runtime_error("unable to write MIR output: " + path);
}

}  // namespace mir_model
