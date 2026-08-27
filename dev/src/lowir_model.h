#pragma once

// Optional typed LowIR model scaffold.
//
// LowIR text is the durable compiler boundary introduced in PA13. This header
// gives one possible in-memory shape for that text. You may use it directly,
// adapt it, or replace it with your own equivalent model, but backend-visible
// facts must still serialize to and parse back from LowIR text.

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ir_symbol_model.h"
#include "lowir_identity.h"

namespace lowir_model {

struct ExportedSymbol
{
  SymbolId internal_symbol;
  StringId object_symbol;
  StringId thread_local_wrapper_object_symbol;
  bool keep_internal_alias = false;
  bool prefer_local_object_binding = false;
  ir_model::SymbolLinkage linkage = ir_model::SL_EXTERNAL;
};

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

  bool empty() const { return kind == LOP_NONE; }
};

bool operator==(LowOperation left, LowOperation right);
bool operator!=(LowOperation left, LowOperation right);
std::ostream & operator<<(std::ostream & out, LowOperation operation);
LowOperation parse_lowir_operation(const std::string & text);
const char * lowir_operation_text(LowOperation operation);
std::size_t lowir_operation_hash(LowOperation operation);

enum LowTypeKind : std::uint8_t
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
  std::size_t storage_size = 0;
  std::uint32_t alignment = 1;
  LowTypeKind kind = LTK_INVALID;
};

const LowType & builtin_lowir_type(LowTypeKind kind);
inline std::size_t lowir_type_bit_width(const LowType & type)
{
  switch(type.kind) {
  case LTK_I1: return 1;
  case LTK_I8: case LTK_U8: return 8;
  case LTK_I16: case LTK_U16: return 16;
  case LTK_I32: case LTK_U32: case LTK_F32: return 32;
  case LTK_I64: case LTK_F64: case LTK_PTR: return 64;
  case LTK_F80: return 80;
  case LTK_I128: return 128;
  case LTK_OBJECT:
    return type.storage_size <= std::numeric_limits<std::size_t>::max() / 8 ?
      type.storage_size * 8 : 0;
  case LTK_INVALID: case LTK_VOID: return 0;
  }
  return 0;
}
std::string lowir_type_text(const LowType & type);
bool same_lowir_type(const LowType & left, const LowType & right);
bool operator==(const LowType & left, const LowType & right);
bool operator!=(const LowType & left, const LowType & right);

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
  } kind;

  enum AddressBinding { ADDRESS_LOCAL, ADDRESS_PREEMPTIBLE }
    address_binding;

  bool has_int_value;
  bool has_float_bits;
  // A literal's low/high words and literal_type are its semantic payload.
  // has_spelling permits an exact input spelling to be retained only for
  // serialization. During explicit text/object parsing, named operands
  // temporarily use the same identity field until resolution replaces it
  // with the corresponding compact semantic identity.
  bool has_spelling;
  union
  {
    BlockId block;
    SlotId slot;
    ValueId value;
    SymbolId symbol;
    StringId literal;
  };
  // Integers use int_value/int_high; floating values use the same storage as
  // raw target-format literal_low/literal_high words.
  union { long long int_value; std::uint64_t literal_low; };
  union { std::uint64_t int_high; std::uint64_t literal_high; };
  LowType literal_type;

  Operand()
    : kind(OP_INTEGER), address_binding(ADDRESS_LOCAL), has_int_value(false),
      has_float_bits(false), has_spelling(false), block(), literal_low(0),
      literal_high(0) {}
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
  SR_TERMINATE,
  SR_PURE_VIRTUAL,
  SR_DYNAMIC_CAST,
  SR_BAD_CAST,
  SR_BAD_TYPEID,
  SR_RTTI_CLASS,
  SR_RTTI_SI,
  SR_RTTI_VMI,
  SR_RTTI_DATA,
  SR_UNREACHABLE
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
  CAM_VARIADIC
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
  StringId object_symbol;
  // Explicit text input keeps the unresolved spelling only until the
  // program-wide symbol resolver publishes tls_for_symbol_id.
  StringId tls_for_spelling;
  SymbolId tls_for_symbol_id;
  StringId section_name;
  bool keep_internal_alias = false;
  bool prefer_local_object_binding = false;
  bool object_output_root = false;
  bool object_trivial_lifecycle = false;
  bool force_inline = false;
  bool inline_hint = false;
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
  ValueId value;
  StringId name;
  LowType type;
  ParameterMetadata metadata;
};

enum PresentationPolicy : std::uint8_t
{
  PRESENTATION_SERIALIZABLE,
  PRESENTATION_OBJECT_ONLY
};

struct InstructionDebugLocation
{
  StringId file;
  std::size_t line = 0;
  std::size_t column = 0;

  bool present() const;
};

struct GlobalDeclaration
{
  // Semantic identity is compact; presentation lives once in Program.
  SymbolId symbol;
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
    // Explicit-text parsing may hold a pooled forward reference until it is
    // resolved to symbol_id.  Production source lowering sets symbol_id.
    StringId symbol_spelling;
    SymbolId symbol_id;
    long long addr_addend = 0;
    std::size_t zero_bytes = 0;
  };

  SymbolId symbol;
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
    IK_RETURN,
    // Kept at the end so existing serialized instruction identities remain
    // stable.  args stores alternating OP_LABEL/typed-value operands.
    IK_PHI
  } kind = IK_CONST;

  ValueId dest;
  LowType type;
  LowType source_type;
  LowOperation op;
  std::size_t byte_count = 0;
  std::size_t byte_alignment = 1;
  // A volatile access is observable behavior: passes may not remove, merge,
  // reorder, or forward it, and its storage may not be promoted away.
  bool volatile_access = false;
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
  BlockId id;
  std::vector<Instruction> instructions;
};

struct Function
{
  SymbolId symbol;
  std::vector<Parameter> params;
  LowType return_type;
  std::vector<SlotId> slots;
  // Function tables keep compact presentation identities. Generated values
  // use their ordinal and leave the pooled name invalid.
  std::vector<StringId> slot_names;
  std::vector<LowType> slot_types;
  std::vector<ValueId> slot_parameter_values;
  std::vector<PresentationName> value_names;
  std::vector<LowType> value_types;
  std::vector<Block> blocks;
  std::vector<StringId> block_labels;
  // Dense lexical rank captured while block presentation is available.  EH
  // layout consumes this compact order and never needs the label bytes.
  std::vector<std::uint32_t> block_presentation_order;
  GeneratedNameReservations generated_name_reservations;
  std::uint32_t next_block_id = 0;
  InstructionDebugLocation debug_location;
  FunctionBoundaryMetadata boundary;
  SymbolMetadata metadata;
};

struct FunctionDeclaration
{
  SymbolId symbol;
  std::vector<Parameter> params;
  LowType return_type;
  FunctionBoundaryMetadata boundary;
  SymbolMetadata metadata;
};

struct ObjectAlias
{
  StringId object_symbol;
  SymbolId target_id;
  // Boundary-only unresolved presentation; invalid in a resolved program.
  StringId target_spelling;
};

struct Program
{
  StringPool strings;
  std::vector<StringId> symbol_names;
  std::vector<GlobalDeclaration> global_declarations;
  std::vector<GlobalDefinition> globals;
  std::vector<FunctionDeclaration> function_declarations;
  std::vector<Function> functions;
  std::vector<ObjectAlias> object_aliases;
  std::vector<ExportedSymbol> exported_symbols;
  std::size_t source_bytes = 0;
  std::size_t token_count = 0;
  PresentationPolicy presentation_policy = PRESENTATION_SERIALIZABLE;
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

BlockId allocate_lowir_block_id(Function & function,
                               StringId label = StringId());
const std::string & lowir_block_label(const StringPool & strings,
                                      const Function & function,
                                      BlockId block);
SlotId append_lowir_slot(Function & function, StringId name,
                         const LowType & type);
const std::string & lowir_slot_name(const StringPool & strings,
                                    const Function & function, SlotId slot);
const LowType & lowir_slot_type(const Function & function, SlotId slot);
ValueId append_lowir_value(Function & function, StringId name,
                           const LowType & type,
                           bool preserve_copy = false);
ValueId append_lowir_unnamed_value(Function & function,
                                   const LowType & type);
ValueId append_lowir_generated_value(Function & function,
                                     std::uint32_t ordinal,
                                     const LowType & type);
ValueId append_lowir_fresh_generated_value(Function & function,
                                           const LowType & type);
std::string lowir_value_name(const StringPool & strings,
                             const Function & function, ValueId value);
const LowType & lowir_value_type(const Function & function, ValueId value);
bool lowir_value_preserves_copy(const Function & function, ValueId value);
PresentationName lowir_value_presentation(const Function & function,
                                          ValueId value);
SymbolId append_lowir_symbol(Program & program, const std::string & name);
SymbolId append_lowir_symbol(Program & program, StringId name);
StringId lowir_symbol_spelling(const Program & program, SymbolId symbol);
const std::string & lowir_symbol_name(const Program & program, SymbolId symbol);
const std::string & lowir_parameter_name(const Program & program,
                                         const Parameter & parameter);
bool parse_lowir_integer_literal(const std::string & text,
                                 long long * low, std::uint64_t * high);
LowType lowir_floating_literal_type(const std::string & text);
std::size_t floating_literal_parse_calls();
bool parse_lowir_floating_literal_bits(const std::string & text,
                                       const LowType & type,
                                       std::uint64_t * low,
                                       std::uint64_t * high);
void lowir_floating_value_bits(long double value, const LowType & type,
                               std::uint64_t * low,
                               std::uint64_t * high);
long double lowir_floating_value(std::uint64_t low, std::uint64_t high,
                                 const LowType & type);
std::string lowir_literal_text(const Operand & operand,
                               const StringPool * strings = 0);
std::string lowir_literal_text(const Operand & operand,
                               const SealedStringPool & strings);
void resolve_lowir_function_operands(Function & function,
                                     const StringPool & strings);
void classify_lowir_generated_name_reservations(
  Function & function, const StringPool & strings);
void compute_lowir_block_presentation_order(
  Function & function, const StringPool & strings);
void resolve_lowir_program_symbols(Program & program);
void remap_lowir_program_strings(Program & program,
                                 StringPool & destination);
void discard_unreferenced_lowir_strings(Program & program);
void remap_lowir_program_symbols(
  Program & program, const std::vector<SymbolId> & symbols);
std::size_t lowir_program_storage_bytes(const Program & program);

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
