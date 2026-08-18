#pragma once

// Optional typed LowIR model scaffold.
//
// LowIR text is the durable compiler boundary introduced in PA13. This header
// gives one possible in-memory shape for that text. You may use it directly,
// adapt it, or replace it with your own equivalent model, but backend-visible
// facts must still serialize to and parse back from LowIR text.

#include <cstddef>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ir_symbol_model.h"

namespace lowir_model {

struct ParseError : std::runtime_error
{
  explicit ParseError(const std::string & message)
    : std::runtime_error(message)
  {}
};

// LowIR operations are semantic identity, not presentation text.  The compact
// value is carried through optimization and native lowering; text is decoded
// once by the parser and rendered only by serializers and diagnostics.
struct LowOperation
{
  enum Kind
  {
    LOP_NONE,
    LOP_NEG,
    LOP_NOT,
    LOP_BITNOT,
    LOP_BSWAP,
    LOP_ADD,
    LOP_SUB,
    LOP_MUL,
    LOP_DIV,
    LOP_UDIV,
    LOP_MOD,
    LOP_UMOD,
    LOP_AND,
    LOP_OR,
    LOP_XOR,
    LOP_SHL,
    LOP_SHR,
    LOP_USHR,
    LOP_EQ,
    LOP_NE,
    LOP_LT,
    LOP_ULT,
    LOP_LE,
    LOP_ULE,
    LOP_GT,
    LOP_UGT,
    LOP_GE,
    LOP_UGE,
    LOP_TRUNC,
    LOP_SEXT,
    LOP_ZEXT,
    LOP_SITOFP,
    LOP_UITOFP,
    LOP_FPTOSI,
    LOP_FPTOUI,
    LOP_FPTRUNC,
    LOP_FPEXT,
    LOP_DECAY
  } kind;

  LowOperation() : kind(LOP_NONE) {}
  LowOperation(Kind value) : kind(value) {}
  LowOperation(const char * text);
  LowOperation(const std::string & text);

  bool empty() const { return kind == LOP_NONE; }
  std::size_t size() const;
  char operator[](std::size_t index) const;
  operator std::string() const;
};

bool operator==(LowOperation left, LowOperation right);
bool operator!=(LowOperation left, LowOperation right);
bool operator==(LowOperation left, const char * right);
bool operator!=(LowOperation left, const char * right);
bool operator==(const char * left, LowOperation right);
bool operator!=(const char * left, LowOperation right);
std::string operator+(const char * left, LowOperation right);
std::string operator+(const std::string & left, LowOperation right);
std::string operator+(LowOperation left, const std::string & right);
std::ostream & operator<<(std::ostream & out, LowOperation operation);
const char * lowir_operation_text(LowOperation operation);
std::size_t lowir_operation_hash(LowOperation operation);

enum LowTypeKind
{
  LTK_INVALID,
  LTK_VOID,
  LTK_I1,
  LTK_I8,
  LTK_U8,
  LTK_I16,
  LTK_U16,
  LTK_I32,
  LTK_U32,
  LTK_I64,
  LTK_F32,
  LTK_F64,
  LTK_F80,
  LTK_PTR,
  LTK_OBJECT,
  LTK_I128
};

struct LowType
{
  std::string text;
  LowTypeKind kind = LTK_INVALID;
  std::size_t bit_width = 0;
  std::size_t storage_size = 0;
  std::size_t alignment = 1;
};

const LowType & builtin_lowir_type(LowTypeKind kind);
bool same_lowir_type(const LowType & left, const LowType & right);

struct Operand
{
  enum Kind
  {
    OP_TEMP,
    OP_SLOT,
    OP_GLOBAL,
    OP_LABEL,
    OP_INTEGER,
    OP_FLOAT
  } kind = OP_INTEGER;

  enum AddressBinding { ADDRESS_LOCAL, ADDRESS_PREEMPTIBLE }
    address_binding = ADDRESS_LOCAL;

  bool has_int_value = false;
  std::string text;
  long long int_value = 0;
  long double float_value = 0.0L;
  LowType literal_type;
};

enum SymbolRole
{
  SR_NONE,
  SR_ENTRY,
  SR_INIT,
  SR_FINI,
  SR_EH_TOP,
  SR_EH_VALUE,
  SR_EH_TYPE,
  SR_EH_UNHANDLED,
  SR_EH_ALLOCATE_EXCEPTION,
  SR_EH_BEGIN_CATCH,
  SR_EH_CALL_UNEXPECTED,
  SR_EH_CURRENT_EXCEPTION_TYPE,
  SR_EH_END_CATCH,
  SR_EH_RETHROW,
  SR_EH_THROW,
  SR_EH_PERSONALITY,
  SR_EH_RESUME,
  SR_ALLOCATE_MEMORY,
  SR_FREE_MEMORY,
  SR_PURE_VIRTUAL,
  SR_DYNAMIC_CAST,
  SR_BAD_CAST,
  SR_BAD_TYPEID,
  SR_RTTI_CLASS,
  SR_RTTI_SI,
  SR_RTTI_VMI,
  SR_RTTI_DATA
};

enum LanguageLinkageMode
{
  LLM_DEFAULT,
  LLM_C,
  LLM_CPP
};

enum SymbolBindingMode
{
  SBM_DEFAULT,
  SBM_INTERNAL,
  SBM_STRONG,
  SBM_WEAK
};

enum ParamPassingMode
{
  PPM_DIRECT,
  PPM_INDIRECT_RESULT,
  PPM_BY_ADDRESS,
  PPM_REFERENCE,
  PPM_DECAY
};

enum ParamCaptureMode
{
  PCM_DEFAULT,
  PCM_NOCAPTURE,
  PCM_MAYCAPTURE
};

enum ParamAccessMode
{
  PAM_DEFAULT,
  PAM_NONE,
  PAM_READ,
  PAM_WRITE,
  PAM_READWRITE
};

enum ParamAliasMode
{
  PALM_DEFAULT,
  PALM_NOALIAS
};

enum CallArityMode
{
  CAM_FIXED,
  CAM_VARIADIC,
  CAM_PROTOTYPE_RELAXED
};

enum CallEffectsMode
{
  CFXM_DEFAULT,
  CFXM_READNONE,
  CFXM_READONLY,
  CFXM_READWRITE
};

enum CallUnwindMode
{
  CUM_DEFAULT,
  CUM_MAY,
  CUM_NO
};

enum CallReturnMode
{
  CRM_DEFAULT,
  CRM_RETURNS,
  CRM_NORETURN
};

enum GlobalStorageMode
{
  GSM_DEFAULT,
  GSM_WRITABLE,
  GSM_READONLY,
  GSM_THREAD_LOCAL
};

enum IndexProjectionKind
{
  IPK_NONE,
  IPK_ARRAY_ELEMENT,
  IPK_FIELD,
  IPK_BASE_SUBOBJECT,
  IPK_REFERENCE_FIELD
};

struct SymbolMetadata
{
  SymbolRole role = SR_NONE;
  LanguageLinkageMode linkage = LLM_DEFAULT;
  SymbolBindingMode binding = SBM_DEFAULT;
  std::string object_symbol;
  std::string tls_for_symbol;
  std::string section_segment;
  std::string section_name;
  bool keep_internal_alias = false;
  bool prefer_local_object_binding = false;
  bool object_output_root = false;
  bool object_trivial_lifecycle = false;
  bool force_inline = false;
  bool no_inline = false;
  bool inferred_legacy_role = false;
};

struct FunctionBoundaryMetadata
{
  CallArityMode arity = CAM_FIXED;
  CallEffectsMode effects = CFXM_DEFAULT;
  CallUnwindMode unwind = CUM_DEFAULT;
  CallReturnMode returns = CRM_DEFAULT;
};

struct ParameterMetadata
{
  ParamPassingMode passing = PPM_DIRECT;
  ParamCaptureMode capture = PCM_DEFAULT;
  ParamAccessMode access = PAM_DEFAULT;
  ParamAliasMode alias = PALM_DEFAULT;
};

struct Parameter
{
  std::string name;
  LowType type;
  ParameterMetadata metadata;
};

struct InstructionDebugLocation
{
  std::string file;
  std::size_t line = 0;
  std::size_t column = 0;

  bool present() const;
};

struct GlobalDeclaration
{
  std::string name;
  bool has_type = false;
  LowType type;
  GlobalStorageMode storage = GSM_DEFAULT;
  SymbolMetadata metadata;
};

struct GlobalDefinition
{
  struct DataItem
  {
    enum Kind
    {
      ITEM_INTEGER,
      ITEM_ADDR,
      ITEM_ZERO
    } kind = ITEM_INTEGER;

    LowType type;
    Operand literal_operand;
    std::string symbol;
    long long addr_addend = 0;
    std::size_t zero_bytes = 0;
  };

  std::string name;
  bool structured = false;
  GlobalStorageMode storage = GSM_DEFAULT;
  LowType type;
  enum InitKind
  {
    INIT_ZERO,
    INIT_INTEGER,
    INIT_ADDR
  } init_kind = INIT_ZERO;
  Operand init_operand;
  long long addr_addend = 0;
  std::vector<DataItem> data_items;
  SymbolMetadata metadata;
};

struct Instruction
{
  enum Kind
  {
    IK_CONST,
    IK_COPY,
    IK_ADDR,
    IK_LOAD,
    IK_ATOMIC_LOAD,
    IK_STORE,
    IK_ATOMIC_STORE,
    IK_ATOMIC_EXCHANGE,
    IK_INDEX,
    IK_UNARY,
    IK_BINARY,
    IK_CMP,
    IK_CONVERT,
    IK_ATOMIC_ADD_FETCH,
    IK_ATOMIC_COMPARE_EXCHANGE,
    IK_ATOMIC_THREAD_FENCE,
    IK_ATOMIC_SIGNAL_FENCE,
    IK_VA_START,
    IK_VA_ARG,
    IK_STACK_ALLOC,
    IK_CALL,
    IK_COPYOBJ,
    IK_ZEROINIT,
    IK_EH_TRY,
    IK_EH_CLEANUP,
    IK_EH_CLEANUP_CLAUSE,
    IK_EH_CATCH,
    IK_EH_FILTER,
    IK_EH_CATCH_ALL,
    IK_EH_END,
    IK_THROW,
    IK_EXCEPTION,
    IK_EXCEPTION_SELECTOR,
    IK_RESUME,
    IK_JUMP,
    IK_BRANCH,
    IK_SWITCH,
    IK_RETURN
  } kind = IK_CONST;

  std::string dest;
  LowType type;
  LowType source_type;
  LowOperation op;
  std::size_t byte_count = 0;
  std::size_t byte_alignment = 1;
  bool has_eh_selector = false;
  long long eh_selector = 0;
  IndexProjectionKind index_projection = IPK_NONE;
  Operand first;
  Operand second;
  Operand third;
  std::vector<Operand> args;
  bool call_returns_void = false;
  bool has_call_signature = false;
  std::vector<Parameter> call_params;
  LowType call_return_type;
  FunctionBoundaryMetadata call_boundary;
  InstructionDebugLocation debug_location;
};

struct Block
{
  std::string label;
  std::vector<Instruction> instructions;
};

struct Function
{
  std::string name;
  std::vector<Parameter> params;
  LowType return_type;
  std::vector<std::pair<std::string, LowType> > slots;
  std::vector<Block> blocks;
  InstructionDebugLocation debug_location;
  FunctionBoundaryMetadata boundary;
  SymbolMetadata metadata;
};

struct FunctionDeclaration
{
  std::string name;
  std::vector<Parameter> params;
  LowType return_type;
  FunctionBoundaryMetadata boundary;
  SymbolMetadata metadata;
};

struct ObjectAlias
{
  std::string object_symbol;
  std::string target;
};

struct Program
{
  std::vector<GlobalDeclaration> global_declarations;
  std::vector<GlobalDefinition> globals;
  std::vector<FunctionDeclaration> function_declarations;
  std::vector<Function> functions;
  std::vector<ObjectAlias> object_aliases;
  std::vector<ir_model::ExportedSymbol> exported_symbols;
  std::size_t source_bytes = 0;
  std::size_t token_count = 0;
};

using LowirType = LowType;
using LowirOperand = Operand;
using LowirParameter = Parameter;
using LowirInstruction = Instruction;
using LowirBlock = Block;
using LowirFunction = Function;
using LowirFunctionDeclaration = FunctionDeclaration;
using LowirGlobalDeclaration = GlobalDeclaration;
using LowirGlobalDefinition = GlobalDefinition;
using LowirObjectAlias = ObjectAlias;
using LowirProgram = Program;

enum LowirEntryPolicy
{
  LEP_REQUIRE_ENTRY,
  LEP_ALLOW_HELPERS_ONLY
};

LowirProgram parse_lowir_program_text(const std::string & text,
                                      const std::string & source_name = std::string("<memory>"),
                                      LowirEntryPolicy entry_policy = LEP_REQUIRE_ENTRY);
LowirProgram parse_lowir_program_files(
    const std::vector<std::string> & paths,
    LowirEntryPolicy entry_policy = LEP_REQUIRE_ENTRY);
std::string serialize_lowir_program(const LowirProgram & program);
void write_lowir_program_file(const std::string & path,
                              const LowirProgram & program);

}  // namespace lowir_model
