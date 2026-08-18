#pragma once

// Optional typed machine-IR model scaffold.
//
// The MIR dump is the serialized view of the backend program model introduced
// by lowir2native. This header gives one possible in-memory shape for that
// model. Native emission and MIR dumping should consume the same facts.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "x86_register_model.h"

namespace mir_model {

struct GlobalDefinition
{
  struct DataItem
  {
    enum Kind
    {
      ITEM_INTEGER,
      ITEM_FLOAT,
      ITEM_ADDR,
      ITEM_ZERO
    } kind = ITEM_INTEGER;

    std::string type;
    long long int_value = 0;
    long double float_value = 0.0L;
    std::string literal_text;
    std::string symbol;
    long long addr_addend = 0;
    std::size_t zero_bytes = 0;
  };

  enum StorageKind
  {
    GS_SCALAR,
    GS_DATA
  } storage_kind = GS_SCALAR;

  enum InitKind
  {
    GI_ZERO,
    GI_INTEGER,
    GI_FLOAT,
    GI_ADDR
  } init_kind = GI_ZERO;

  std::string name;
  std::string object_symbol;
  bool readonly = false;
  bool thread_local_storage = false;
  std::string thread_local_wrapper_symbol;
  std::string section_segment;
  std::string section_name;
  std::string type;
  long long int_value = 0;
  long double float_value = 0.0L;
  std::string literal_text;
  std::string symbol;
  long long addr_addend = 0;
  std::vector<DataItem> data_items;
};

struct ParamBinding
{
  enum LocationKind
  {
    PL_REG,
    PL_XMM,
    PL_STACK
  } location = PL_REG;

  // PL_REG/PL_XMM describe the incoming ABI location. A lowering may retain
  // the parameter there when its complete live interval, including promoted
  // slot aliases, crosses no clobber; a separate home is required otherwise.
  std::string name;
  X64Register reg = XR_RDI;
  XmmRegister xmm = XMM_0;
  long long stack_offset = 0;
  long long chunk_offset = 0;
  std::string type;
};

struct FrameBinding
{
  enum Kind
  {
    FB_PARAM_SLOT,
    FB_SLOT,
    FB_TEMP
  } kind = FB_TEMP;

  std::string name;
  long long offset = 0;
  std::string type;
};

struct Operand
{
  enum Kind
  {
    OP_REG,
    OP_XMM,
    OP_IMM,
    OP_FLOAT_IMM,
    OP_SYMBOL,
    OP_FRAME,
    OP_GLOBAL,
    OP_DEREF,
    OP_LABEL
  } kind = OP_IMM;

  enum AddressBinding
  {
    ADDRESS_LOCAL,
    ADDRESS_PREEMPTIBLE
  } address_binding = ADDRESS_LOCAL;

  X64Register reg = XR_RAX;
  // OP_DEREF always names a base register in reg.  Indexed memory operands
  // additionally name an x86 scale/index pair; scale is one of 1, 2, 4, 8.
  bool has_index = false;
  X64Register index = XR_RAX;
  unsigned scale = 1;
  XmmRegister xmm = XMM_0;
  long long imm = 0;
  long double float_imm = 0.0L;
  long long offset = 0;
  std::string text;
};

struct InstructionDebugLocation
{
  std::string file;
  std::size_t line = 0;
  std::size_t column = 0;

  bool present() const
  {
    return !file.empty() && line != 0 && column != 0;
  }
};

struct Instruction
{
  enum Opcode
  {
    MI_MOV,
    MI_LOAD,
    MI_STORE,
    MI_MFENCE,
    MI_LOCK_XADD,
    MI_XCHG,
    MI_LOCK_CMPXCHG,
    MI_LOCK_CMPXCHG16B,
    MI_LEA,
    MI_FMOV,
    MI_FNEG,
    MI_FADD,
    MI_FSUB,
    MI_FMUL,
    MI_FDIV,
    MI_FEQ,
    MI_FNE,
    MI_FLT,
    MI_FGT,
    MI_FLE,
    MI_FGE,
    MI_FCMP,
    MI_FSTP,
    MI_FPOP,
    MI_SITOFP,
    MI_UITOFP,
    MI_FPTOSI,
    MI_FPTOUI,
    MI_FPEXT,
    MI_FPTRUNC,
    // Integer ALU instructions may retain an OP_IMM source. Native emission
    // materializes a scratch only when the concrete x86 encoding requires it.
    MI_ADD,
    MI_SUB,
    MI_IMUL,
    MI_MUL,
    MI_AND,
    MI_OR,
    MI_XOR,
    // A sole-use scalar unary result immediately returned should select the
    // ABI return carrier instead of an intermediate general-purpose register.
    MI_NEG,
    MI_NOT,
    MI_BSWAP,
    MI_CMP,
    MI_TEST,
    MI_JCC,
    // A sole-use comparison immediately returned should select the ABI return
    // carrier when doing so preserves both comparison inputs.
    MI_SETCC,
    MI_MOVZX,
    MI_SEXT,
    MI_ZEXT,
    MI_CQO,
    MI_IDIV,
    MI_DIV,
    // These shifts read the count implicitly from rcx/cl; MIR should place the
    // selected count there directly.
    MI_SHL_CL,
    MI_SHR_CL,
    MI_SAR_CL,
    MI_I128_SHL,
    MI_I128_SHR,
    MI_I128_SAR,
    MI_I128_UDIV,
    MI_I128_UMOD,
    MI_I128_SDIV,
    MI_I128_SMOD,
    MI_TLS_ADDR,
    // Scalar call results initially occupy their ABI return carrier. A sole-use
    // result consumed immediately by a store or direct-value argument does not
    // require a separate intermediate MIR home. Call setup may likewise read a
    // promoted parameter's fixed home without a dead temporary copy.
    MI_CALL,
    MI_CALL_INDIRECT,
    // Bulk-memory operands name logical address registers. The encoder owns
    // any parallel moves required by the concrete x86 string instruction.
    MI_COPY_BYTES,
    MI_ZERO_BYTES,
    MI_EH_PUSH,
    MI_EH_POP,
    MI_EH_CATCH,
    MI_EH_FILTER,
    MI_EH_CLEANUP_CLAUSE,
    MI_LOAD_EXCEPTION,
    MI_LOAD_EXCEPTION_SELECTOR,
    MI_THROW,
    MI_RESUME,
    MI_JMP,
    MI_JMP_INDIRECT,
    MI_JNE,
    MI_FRET,
    MI_RET,
    MI_EXIT
  } opcode = MI_MOV;

  std::string type;
  X86Condition condition = XC_E;
  std::size_t byte_count = 0;
  std::size_t byte_alignment = 1;
  std::string tls_storage_symbol;
  bool call_unwind_no = false;
  bool call_returns_noreturn = false;
  bool call_variadic = false;
  // Calls name their target in operands.  The fixed-size mask records the
  // physical SysV argument registers that are also read by the call: GPR bit
  // N names X64Register N and bit 16+N names XmmRegister N.  Keeping this fact
  // in MIR lets allocation, optimization, serialization, and encoding share
  // the same call-liveness contract.
  bool call_argument_registers_known = false;
  unsigned call_argument_register_mask = 0;
  // Bytes of caller-owned stack arguments that remain live across unwinding.
  std::size_t call_stack_bytes = 0;
  bool has_source_position = false;
  std::size_t source_position = 0;
  InstructionDebugLocation debug_location;
  std::vector<Operand> operands;
};

struct DebugVariable
{
  struct Range
  {
    enum LocationKind
    {
      LK_FRAME,
      LK_REG,
      LK_XMM
    } location = LK_FRAME;

    std::size_t start_source_position = 0;
    std::size_t end_source_position = 0;
    long long frame_offset = 0;
    X64Register reg = XR_RAX;
    XmmRegister xmm = XMM_0;
  };

  std::string name;
  std::string type;
  InstructionDebugLocation decl_location;
  std::vector<Range> ranges;
};

struct Block
{
  std::string label;
  std::vector<Instruction> instructions;
};

struct HostEhClause
{
  enum Kind
  {
    HC_CLEANUP,
    HC_CATCH,
    HC_FILTER
  } kind = HC_CATCH;
  bool catch_all = false;
  long long selector = 0;
  std::string type_symbol;
  std::vector<std::string> filter_type_symbols;
};

struct Function
{
  std::string name;
  std::string object_symbol;
  std::vector<ParamBinding> params;
  std::string return_type;
  // Lowering-to-optimizer frame requirements.  The optimizer combines these
  // with the surviving save set and publishes the authoritative encoded total
  // in stack_size; native encoding does not consume these transient inputs.
  std::size_t stack_frame_bytes = 0;
  std::size_t stack_floor_bytes = 0;
  std::size_t stack_size = 0;
  std::size_t scratch_bytes = 0;
  bool has_dynamic_stack = false;
  bool host_eh_enabled = false;
  long long host_eh_exception_offset = 0;
  long long host_eh_selector_offset = 0;
  InstructionDebugLocation debug_location;
  std::vector<X64Register> callee_saved_regs;
  std::vector<FrameBinding> frame_bindings;
  std::vector<DebugVariable> debug_variables;
  std::map<std::string, std::vector<HostEhClause> > host_eh_clauses;
  std::vector<Block> blocks;
};

struct ObjectAlias
{
  std::string object_symbol;
  std::string target;
};

struct RuntimeFunction
{
  enum Kind
  {
    RF_EH_ALLOCATE,
    RF_EH_BEGIN_CATCH,
    RF_EH_END_CATCH,
    RF_EH_RETHROW,
    RF_EH_THROW,
    RF_EH_PERSONALITY,
    RF_EH_RESUME,
    RF_ALLOCATE_MEMORY,
    RF_FREE_MEMORY,
    RF_PURE_VIRTUAL,
    RF_DYNAMIC_CAST,
    RF_BAD_CAST,
    RF_BAD_TYPEID
  } kind = RF_EH_PERSONALITY;

  std::string name;
  std::string object_symbol;
};

struct RuntimeData
{
  enum Kind { RD_OPAQUE, RD_RTTI_CLASS, RD_RTTI_SI, RD_RTTI_VMI }
    kind = RD_OPAQUE;
  std::string name;
  std::string object_symbol;
};

struct Program
{
  std::string target;
  bool uses_eh = false;
  std::vector<Instruction> startup;
  std::vector<GlobalDefinition> globals;
  std::vector<Function> functions;
  std::vector<ObjectAlias> object_aliases;
  std::vector<RuntimeFunction> runtime_functions;
  std::vector<RuntimeData> runtime_data;
};

using MirGlobalDefinition = GlobalDefinition;
using MirParamBinding = ParamBinding;
using MirFrameBinding = FrameBinding;
using MirOperand = Operand;
using MirInstructionDebugLocation = InstructionDebugLocation;
using MirInstruction = Instruction;
using MirDebugVariable = DebugVariable;
using MirBlock = Block;
using MirHostEhClause = HostEhClause;
using MirFunction = Function;
using MirObjectAlias = ObjectAlias;
using MirRuntimeFunction = RuntimeFunction;
using MirRuntimeData = RuntimeData;
using MirProgram = Program;

std::string serialize_mir_program(const MirProgram & program);
void write_mir_program_file(const std::string & path,
                            const MirProgram & program);

}  // namespace mir_model
