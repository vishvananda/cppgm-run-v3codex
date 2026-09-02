#include "lowir/model/program.h"
#include "support/exception_types.h"
#include "lowir/io/prepare.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace lowir_model {
namespace {

LowOperation::Kind operation_kind(const std::string & text)
{
  static const std::pair<const char *, LowOperation::Kind> operations[] = {
    {"neg", LowOperation::LOP_NEG},
    {"not", LowOperation::LOP_NOT},
    {"bitnot", LowOperation::LOP_BITNOT},
    {"bswap", LowOperation::LOP_BSWAP},
    {"add", LowOperation::LOP_ADD},
    {"sub", LowOperation::LOP_SUB},
    {"mul", LowOperation::LOP_MUL},
    {"div", LowOperation::LOP_DIV},
    {"udiv", LowOperation::LOP_UDIV},
    {"mod", LowOperation::LOP_MOD},
    {"umod", LowOperation::LOP_UMOD},
    {"and", LowOperation::LOP_AND},
    {"or", LowOperation::LOP_OR},
    {"xor", LowOperation::LOP_XOR},
    {"shl", LowOperation::LOP_SHL},
    {"shr", LowOperation::LOP_SHR},
    {"ushr", LowOperation::LOP_USHR},
    {"eq", LowOperation::LOP_EQ},
    {"ne", LowOperation::LOP_NE},
    {"lt", LowOperation::LOP_LT},
    {"ult", LowOperation::LOP_ULT},
    {"le", LowOperation::LOP_LE},
    {"ule", LowOperation::LOP_ULE},
    {"gt", LowOperation::LOP_GT},
    {"ugt", LowOperation::LOP_UGT},
    {"ge", LowOperation::LOP_GE},
    {"uge", LowOperation::LOP_UGE},
    {"trunc", LowOperation::LOP_TRUNC},
    {"sext", LowOperation::LOP_SEXT},
    {"zext", LowOperation::LOP_ZEXT},
    {"sitofp", LowOperation::LOP_SITOFP},
    {"uitofp", LowOperation::LOP_UITOFP},
    {"fptosi", LowOperation::LOP_FPTOSI},
    {"fptoui", LowOperation::LOP_FPTOUI},
    {"fptrunc", LowOperation::LOP_FPTRUNC},
    {"fpext", LowOperation::LOP_FPEXT}
  };
  for(std::size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i)
    if(text == operations[i].first) return operations[i].second;
  if(text.empty()) return LowOperation::LOP_NONE;
  ThrowLowirInputError("unknown LowIR operation: " + text);
}

struct Token
{
  std::string text;
  std::size_t line;
};

bool is_punctuation(char c)
{
  return c == '{' || c == '}' || c == '(' || c == ')' || c == '[' ||
         c == ']' || c == ',' || c == ':' || c == '=' || c == '+' ||
         c == '-';
}

void lex_text(const std::string & text, std::vector<Token> & out)
{
  std::size_t i = 0;
  std::size_t line = 1;
  while(i < text.size()) {
    const char c = text[i];
    if(c == '\n') {
      ++i;
      ++line;
      continue;
    }
    if(c == ' ' || c == '\t' || c == '\r' || c == '\f') {
      ++i;
      continue;
    }
    if(c == '#') {
      while(i < text.size() && text[i] != '\n') ++i;
      continue;
    }
    if(c == '-' && i + 1 < text.size() && text[i + 1] == '>') {
      out.push_back(Token{"->", line});
      i += 2;
      continue;
    }
    if(is_punctuation(c)) {
      out.push_back(Token{std::string(1, c), line});
      ++i;
      continue;
    }
    const std::size_t begin = i;
    if(c == '%' || c == '$' || c == '@' || c == '^') ++i;
    while(i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) &&
          !is_punctuation(text[i]) && text[i] != '#') {
      ++i;
    }
    if(i == begin) ++i;
    out.push_back(Token{text.substr(begin, i - begin), line});
  }
}

bool starts_with(const std::string & text, char prefix)
{
  return text.size() > 1 && text[0] == prefix;
}

bool is_power_of_two(std::size_t value)
{
  return value && !(value & (value - 1));
}

bool is_token_safe_section_name(const std::string & value)
{
  if(value.empty()) return false;
  for(std::size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    const bool alphanumeric = (c >= 'A' && c <= 'Z') ||
                              (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9');
    if(!alphanumeric && c != '_' && c != '.') return false;
  }
  return true;
}

std::size_t parse_positive_size(const std::string & text)
{
  if(text.empty() || text[0] == '-') ThrowLowirInputError("expected positive integer");
  char * end = 0;
  errno = 0;
  const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
  if(errno || !end || *end || value == 0 || value > SIZE_MAX)
    ThrowLowirInputError("invalid positive integer: " + text);
  return static_cast<std::size_t>(value);
}

void parse_span_text(const std::string & text, std::size_t & bytes,
                     std::size_t & alignment)
{
  const std::size_t split = text.find('x');
  if(split == std::string::npos) {
    bytes = parse_positive_size(text);
    alignment = 1;
    return;
  }
  if(split == 0 || split + 1 == text.size())
    ThrowLowirInputError("invalid object span: " + text);
  bytes = parse_positive_size(text.substr(0, split));
  alignment = parse_positive_size(text.substr(split + 1));
  if(!is_power_of_two(alignment))
    ThrowLowirInputError("object alignment is not a power of two");
}

LowType make_builtin_type(LowTypeKind kind, std::size_t storage_size,
                          std::uint32_t alignment)
{
  LowType result;
  result.kind = kind;
  result.storage_size = storage_size;
  result.alignment = alignment;
  return result;
}

LowType parse_type_text(const std::string & text)
{
  static const std::pair<const char *, LowTypeKind> names[] = {
    {"void", LTK_VOID}, {"i1", LTK_I1}, {"i8", LTK_I8}, {"u8", LTK_U8},
    {"i16", LTK_I16}, {"u16", LTK_U16}, {"i32", LTK_I32},
    {"u32", LTK_U32}, {"i64", LTK_I64}, {"i128", LTK_I128}, {"f32", LTK_F32},
    {"f64", LTK_F64}, {"f80", LTK_F80}, {"ptr", LTK_PTR}
  };
  for(std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    if(text == names[i].first) return builtin_lowir_type(names[i].second);
  }
  if(text.size() > 6 && text.compare(0, 4, "obj<") == 0 && text.back() == '>') {
    std::size_t bytes = 0;
    std::size_t alignment = 0;
    parse_span_text(text.substr(4, text.size() - 5), bytes, alignment);
    if(alignment > bytes) ThrowLowirInputError("object alignment exceeds size");
    if(alignment > UINT32_MAX)
      ThrowLowirInputError("object alignment exceeds LowIR limits");
    LowType result;
    result.kind = LTK_OBJECT;
    result.storage_size = bytes;
    result.alignment = static_cast<std::uint32_t>(alignment);
    return result;
  }
  ThrowLowirInputError("unknown LowIR type: " + text);
}

std::size_t integer_width(const LowType & type)
{
  return (type.kind >= LTK_I1 && type.kind <= LTK_I64) || type.kind == LTK_I128 ?
    lowir_type_bit_width(type) : 0;
}

std::size_t float_width(const LowType & type)
{
  return type.kind >= LTK_F32 && type.kind <= LTK_F80 ?
    lowir_type_bit_width(type) : 0;
}

typedef std::vector<std::pair<std::string, std::string> > Metadata;

class Parser
{
public:
  explicit Parser(const std::vector<Token> & tokens)
    : tokens_(tokens), at_(0), strings_(0) {}

  Program Parse()
  {
    Program program;
    strings_ = &program.strings;
    while(!done()) parse_top_level(program);
    strings_ = 0;
    return program;
  }

private:
  const std::vector<Token> & tokens_;
  std::size_t at_;
  StringPool * strings_;

  bool done() const { return at_ == tokens_.size(); }

  const std::string & peek(std::size_t offset = 0) const
  {
    if(at_ + offset >= tokens_.size()) ThrowLowirInputError("unexpected end of LowIR");
    return tokens_[at_ + offset].text;
  }

  std::size_t peek_line(std::size_t offset = 0) const
  {
    if(at_ + offset >= tokens_.size()) ThrowLowirInputError("unexpected end of LowIR");
    return tokens_[at_ + offset].line;
  }

  std::string take()
  {
    const std::string text = peek();
    ++at_;
    return text;
  }

  bool accept(const std::string & text)
  {
    if(!done() && tokens_[at_].text == text) {
      ++at_;
      return true;
    }
    return false;
  }

  void expect(const std::string & text)
  {
    if(!accept(text)) ThrowLowirInputError("expected '" + text + "', got '" + peek() + "'");
  }

  std::string named(char sigil, const char * description)
  {
    const std::string text = take();
    if(!starts_with(text, sigil)) ThrowLowirInputError(std::string("expected ") + description);
    return text;
  }

  StringId named_id(char sigil, const char * description)
  {
    const std::string text = named(sigil, description);
    return strings_->intern_range(text, 1, text.size() - 1);
  }

  LowType type()
  {
    return parse_type_text(take());
  }

  std::string signed_literal()
  {
    std::string result;
    if(accept("-")) result = "-";
    const std::string value = take();
    if(value.empty() || is_punctuation(value[0])) ThrowLowirInputError("expected literal");
    return result + value;
  }

  Operand operand()
  {
    Operand result;
    const bool negative = accept("-");
    std::string text = (negative ? "-" : "") + take();
    if(!text.empty() &&
       (text.back() == 'e' || text.back() == 'E' ||
        text.back() == 'p' || text.back() == 'P') &&
       (peek() == "+" || peek() == "-")) {
      text += take();
      text += take();
    }
    if(!strings_) ThrowLowirInternalError("LowIR parser has no string pool");
    const bool named_operand = !text.empty() &&
      (text[0] == '%' || text[0] == '$' || text[0] == '@' || text[0] == '^');
    result.literal = named_operand ?
      strings_->intern_range(text, 1, text.size() - 1) :
      strings_->intern(text);
    result.has_spelling = true;
    if(starts_with(text, '%')) result.kind = Operand::OP_TEMP;
    else if(starts_with(text, '$')) result.kind = Operand::OP_SLOT;
    else if(starts_with(text, '@')) result.kind = Operand::OP_GLOBAL;
    else if(starts_with(text, '^')) result.kind = Operand::OP_LABEL;
    else if(text == "nullptr") {
      result.kind = Operand::OP_INTEGER;
      result.int_value = 0;
      result.int_high = 0;
      result.has_int_value = true;
    }
    else if(text == "inf" || text == "+inf" ||
            text == "-inf" || text == "INFINITY" ||
            text == "+INFINITY" || text == "-INFINITY" ||
            text == "nan" || text == "NAN" ||
            text == "snan" || text == "SNAN" ||
            text.find_first_of(".eEpP") != std::string::npos ||
            (!text.empty() && (text.back() == 'f' || text.back() == 'L')))
    {
      result.kind = Operand::OP_FLOAT;
      result.literal_type = lowir_floating_literal_type(text);
      result.has_float_bits = parse_lowir_floating_literal_bits(
        text, result.literal_type, &result.literal_low, &result.literal_high);
    }
    else {
      result.kind = Operand::OP_INTEGER;
      result.has_int_value = parse_lowir_integer_literal(
        text, &result.int_value, &result.int_high);
    }
    return result;
  }

  Metadata metadata()
  {
    Metadata result;
    std::unordered_set<std::string> keys;
    while(accept("[")) {
      do {
        const std::string key = take();
        expect("=");
        const std::string value = take();
        if(!keys.insert(key).second)
          ThrowLowirInputError("duplicate metadata key: " + key);
        result.push_back(std::make_pair(key, value));
      } while(accept(","));
      expect("]");
    }
    return result;
  }

  InstructionDebugLocation debug_location()
  {
    InstructionDebugLocation result;
    if(!accept("!dbg")) return result;
    expect("(");
    std::string file = take();
    while(peek() != ",") file += take();
    result.file = strings_->intern(file);
    expect(",");
    result.line = parse_positive_size(signed_literal());
    expect(",");
    result.column = parse_positive_size(signed_literal());
    expect(")");
    return result;
  }

  void apply_symbol_metadata(const Metadata & items, SymbolMetadata & symbol,
                             FunctionBoundaryMetadata * boundary,
                             GlobalStorageMode * storage, bool call_signature)
  {
    for(std::size_t i = 0; i < items.size(); ++i) {
      const std::string & key = items[i].first;
      const std::string & value = items[i].second;
      if(key == "arity" || key == "effects" || key == "unwind" ||
         key == "return" || key == "query") {
        if(!boundary) ThrowLowirInputError("function metadata on non-function");
        if(call_signature && key == "query")
          ThrowLowirInputError("query metadata requires a direct function");
        apply_boundary_item(key, value, *boundary);
      } else if(call_signature) {
        ThrowLowirInputError("symbol metadata on call signature");
      } else if(key == "storage") {
        if(!storage) ThrowLowirInputError("storage metadata on non-global");
        *storage = parse_storage(value);
      } else {
        apply_symbol_item(key, value, symbol, boundary != 0);
      }
    }
  }

  void apply_boundary_item(const std::string & key, const std::string & value,
                           FunctionBoundaryMetadata & out)
  {
    if(key == "arity") {
      if(value == "variadic") out.arity = CAM_VARIADIC;
      else ThrowLowirInputError("invalid arity metadata");
    } else if(key == "effects") {
      if(value == "readnone") out.effects = CFXM_READNONE;
      else if(value == "readonly") out.effects = CFXM_READONLY;
      else ThrowLowirInputError("invalid effects metadata");
    } else if(key == "unwind") {
      if(value == "no") out.unwind = CUM_NO;
      else ThrowLowirInputError("invalid unwind metadata");
    } else if(key == "return") {
      if(value == "noreturn") out.returns = CRM_NORETURN;
      else ThrowLowirInputError("invalid return metadata");
    } else if(key == "query") {
      if(value == "stable_prefix") out.query = CQM_STABLE_PREFIX;
      else ThrowLowirInputError("invalid query metadata");
    }
  }

  void apply_symbol_item(const std::string & key, const std::string & value,
                         SymbolMetadata & out, bool function_symbol)
  {
    if(key == "role") out.role = parse_role(value);
    else if(key == "linkage") {
      if(value == "c") out.linkage = LLM_C;
      else ThrowLowirInputError("invalid linkage metadata");
    } else if(key == "binding") {
      if(value == "internal") out.binding = SBM_INTERNAL;
      else if(value == "strong") out.binding = SBM_STRONG;
      else if(value == "weak") out.binding = SBM_WEAK;
      else ThrowLowirInputError("invalid binding metadata");
    } else if(key == "object") out.object_symbol = strings_->intern(value);
    else if(key == "section") {
      if(function_symbol) ThrowLowirInputError("section metadata requires a global");
      if(!is_token_safe_section_name(value))
        ThrowLowirInputError("invalid global section name");
      out.section_name = strings_->intern(value);
    }
    else if(key == "tls_for") {
      if(!function_symbol) ThrowLowirInputError("tls_for metadata requires a function");
      out.tls_for_spelling = !value.empty() && value[0] == '@' ?
        strings_->intern_range(value, 1, value.size() - 1) :
        strings_->intern(value);
    } else if(key == "keep_alias") out.keep_internal_alias = yes_flag(value);
    else if(key == "prefer_local") {
      out.prefer_local_object_binding = yes_flag(value);
      if(out.prefer_local_object_binding && out.binding == SBM_DEFAULT)
        out.binding = SBM_STRONG;
    }
    else if(key == "object_root") out.object_output_root = yes_flag(value);
    else if(key == "force_inline") {
      if(!function_symbol) ThrowLowirInputError("force_inline metadata requires a function");
      out.force_inline = yes_flag(value);
    } else if(key == "inline_hint") {
      if(!function_symbol) ThrowLowirInputError("inline_hint metadata requires a function");
      out.inline_hint = yes_flag(value);
    } else if(key == "no_inline") {
      if(!function_symbol) ThrowLowirInputError("no_inline metadata requires a function");
      out.no_inline = yes_flag(value);
    }
    else ThrowLowirInputError("unknown symbol metadata: " + key);
  }

  bool yes_flag(const std::string & value)
  {
    if(value == "yes") return true;
    ThrowLowirInputError("metadata flag must be yes");
  }

  GlobalStorageMode parse_storage(const std::string & value)
  {
    if(value == "readonly") return GSM_READONLY;
    if(value == "thread_local") return GSM_THREAD_LOCAL;
    ThrowLowirInputError("invalid global storage metadata");
  }

  SymbolRole parse_role(const std::string & value)
  {
    static const std::pair<const char *, SymbolRole> roles[] = {
      {"entry", SR_ENTRY}, {"init", SR_INIT}, {"fini", SR_FINI},
      {"eh_allocate_exception", SR_EH_ALLOCATE_EXCEPTION},
      {"eh_begin_catch", SR_EH_BEGIN_CATCH},
      {"eh_end_catch", SR_EH_END_CATCH}, {"eh_rethrow", SR_EH_RETHROW},
      {"eh_throw", SR_EH_THROW}, {"eh_personality", SR_EH_PERSONALITY},
      {"eh_resume", SR_EH_RESUME}, {"allocate_memory", SR_ALLOCATE_MEMORY},
      {"free_memory", SR_FREE_MEMORY}, {"terminate", SR_TERMINATE},
      {"pure_virtual", SR_PURE_VIRTUAL},
      {"dynamic_cast", SR_DYNAMIC_CAST}, {"bad_cast", SR_BAD_CAST},
      {"bad_typeid", SR_BAD_TYPEID}, {"rtti_class", SR_RTTI_CLASS},
      {"rtti_si", SR_RTTI_SI}, {"rtti_vmi", SR_RTTI_VMI},
      {"rtti_data", SR_RTTI_DATA}
    };
    for(std::size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
      if(value == roles[i].first) return roles[i].second;
    ThrowLowirInputError("invalid symbol role");
  }

  Parameter parameter()
  {
    Parameter result;
    result.name = named_id('%', "parameter name");
    expect(":");
    result.type = type();
    const Metadata items = metadata();
    for(std::size_t i = 0; i < items.size(); ++i)
      apply_parameter_item(items[i].first, items[i].second, result.metadata);
    return result;
  }

  std::vector<Parameter> parameter_list()
  {
    std::vector<Parameter> result;
    if(peek() == ")") return result;
    do result.push_back(parameter()); while(accept(","));
    return result;
  }

  void apply_parameter_item(const std::string & key, const std::string & value,
                            ParameterMetadata & out)
  {
    if(key == "pass") {
      if(value == "indirect_result") out.passing = PPM_INDIRECT_RESULT;
      else if(value == "by_address") out.passing = PPM_BY_ADDRESS;
      else ThrowLowirInputError("invalid parameter pass metadata");
    } else if(key == "alias" && value == "noalias") out.alias = PALM_NOALIAS;
    else if(key == "object_bytes") out.object_bytes = parse_positive_size(value);
    else ThrowLowirInputError("invalid parameter metadata");
  }

  void parse_top_level(Program & program)
  {
    if(accept("declare")) {
      if(accept("global")) parse_global_declaration(program);
      else if(accept("function")) parse_function_declaration(program);
      else ThrowLowirInputError("expected declaration kind");
    } else if(accept("global")) parse_global_definition(program);
    else if(accept("function")) parse_function_definition(program);
    else if(accept("alias")) parse_object_alias(program);
    else ThrowLowirInputError("expected top-level LowIR item");
  }

  void parse_global_declaration(Program & program)
  {
    GlobalDeclaration result;
    result.symbol = append_lowir_symbol(
      program, named_id('@', "global name"));
    if(accept("readonly")) result.storage = GSM_READONLY;
    else if(accept("thread_local")) result.storage = GSM_THREAD_LOCAL;
    if(accept(":")) {
      result.has_type = true;
      result.type = type();
    }
    apply_symbol_metadata(metadata(), result.metadata, 0, &result.storage, false);
    program.global_declarations.push_back(result);
  }

  template <typename FunctionRecord>
  void parse_function_header(Program & program, FunctionRecord & result)
  {
    result.symbol = append_lowir_symbol(
      program, named_id('@', "function name"));
    expect("(");
    result.params = parameter_list();
    expect(")");
    expect("->");
    result.return_type = type();
    apply_symbol_metadata(
      metadata(), result.metadata, &result.boundary, 0, false);
  }

  void parse_function_declaration(Program & program)
  {
    FunctionDeclaration result;
    parse_function_header(program, result);
    program.function_declarations.push_back(result);
  }

  void parse_global_definition(Program & program)
  {
    GlobalDefinition result;
    result.symbol = append_lowir_symbol(
      program, named_id('@', "global name"));
    if(accept("readonly")) result.storage = GSM_READONLY;
    else if(accept("thread_local")) result.storage = GSM_THREAD_LOCAL;
    if(accept(":")) result.type = type();
    apply_symbol_metadata(metadata(), result.metadata, 0, &result.storage, false);
    expect("=");
    if(accept("{")) parse_structured_global(result);
    else parse_scalar_global(result);
    program.globals.push_back(result);
  }

  void parse_structured_global(GlobalDefinition & result)
  {
    result.structured = true;
    if(accept("}")) ThrowLowirInputError("structured global must contain data");
    while(!accept("}")) {
      GlobalDefinition::DataItem item;
      if(accept("zero")) {
        item.kind = GlobalDefinition::DataItem::ITEM_ZERO;
        item.zero_bytes = parse_positive_size(signed_literal());
      } else {
        item.type = type();
        if(item.type.kind == LTK_PTR && accept("addr")) {
          item.kind = GlobalDefinition::DataItem::ITEM_ADDR;
          item.symbol_spelling = named_id(
            '@', "address initializer symbol");
          item.addr_addend = address_addend();
        } else {
          item.kind = GlobalDefinition::DataItem::ITEM_INTEGER;
          item.literal_operand = operand();
        }
      }
      result.data_items.push_back(item);
    }
  }

  void parse_scalar_global(GlobalDefinition & result)
  {
    if(result.type.kind == LTK_INVALID) ThrowLowirInputError("scalar global requires type");
    if(accept("zero")) result.init_kind = GlobalDefinition::INIT_ZERO;
    else if(accept("addr")) {
      result.init_kind = GlobalDefinition::INIT_ADDR;
      result.init_operand.literal = named_id(
        '@', "address initializer symbol");
      result.init_operand.has_spelling = true;
      result.init_operand.kind = Operand::OP_GLOBAL;
      result.addr_addend = address_addend();
    } else {
      result.init_kind = GlobalDefinition::INIT_INTEGER;
      result.init_operand = operand();
    }
  }

  long long address_addend()
  {
    int sign = 0;
    if(accept("+")) sign = 1;
    else if(accept("-")) sign = -1;
    if(!sign) return 0;
    const std::string text = take();
    char * end = 0;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 0);
    if(errno || !end || *end) ThrowLowirInputError("invalid address addend");
    return sign * value;
  }

  void parse_object_alias(Program & program)
  {
    expect("object");
    ObjectAlias result;
    const std::string object_symbol = take();
    if(object_symbol.empty() || is_punctuation(object_symbol[0]))
      ThrowLowirInputError("invalid object alias spelling");
    result.object_symbol = strings_->intern(object_symbol);
    expect("=");
    result.target_spelling = named_id('@', "object alias target");
    program.object_aliases.push_back(result);
  }

  void parse_function_definition(Program & program)
  {
    Function result;
    parse_function_header(program, result);
    result.debug_location = debug_location();
    for(std::size_t i = 0; i < result.params.size(); ++i)
      result.params[i].value = append_lowir_value(
        result, result.params[i].name, result.params[i].type);
    expect("{");
    parse_function_body(result);
    program.functions.push_back(result);
  }

  void parse_function_body(Function & function)
  {
    Block * block = 0;
    bool terminated = false;
    while(!accept("}")) {
      if(accept("slot")) {
        if(block) ThrowLowirInputError("slot declaration after first block");
        const lowir_model::StringId name = named_id('$', "slot name");
        expect(":");
        append_lowir_slot(function, name, type());
      } else if(accept("block")) {
        if(block && !terminated &&
           (block->instructions.empty() ||
            (block->instructions.back().kind != Instruction::IK_CALL &&
             block->instructions.back().kind != Instruction::IK_EH_END)))
          ThrowLowirInputError("block has no terminator: " +
            lowir_block_label(*strings_, function, block->id));
        const lowir_model::StringId label = named_id('^', "block name");
        function.blocks.push_back(Block());
        block = &function.blocks.back();
        block->id = allocate_lowir_block_id(
          function, label);
        expect(":");
        terminated = false;
      } else {
        if(!block) ThrowLowirInputError("instruction outside block");
        if(terminated) ThrowLowirInputError("instruction after terminator");
        block->instructions.push_back(instruction(function));
        terminated = is_terminator(block->instructions.back().kind);
      }
    }
    if(block && !terminated &&
       (block->instructions.empty() ||
        (block->instructions.back().kind != Instruction::IK_CALL &&
         block->instructions.back().kind != Instruction::IK_EH_END)))
      ThrowLowirInputError("block has no terminator: " +
        lowir_block_label(*strings_, function, block->id));
  }

  bool is_terminator(Instruction::Kind kind) const
  {
    return kind == Instruction::IK_JUMP || kind == Instruction::IK_BRANCH ||
           kind == Instruction::IK_SWITCH || kind == Instruction::IK_RETURN ||
           kind == Instruction::IK_UNREACHABLE ||
           kind == Instruction::IK_THROW || kind == Instruction::IK_RESUME;
  }

  const LowType & parsed_result_type(const Instruction & ins) const
  {
    if(ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX)
      return builtin_lowir_type(LTK_PTR);
    if(ins.kind == Instruction::IK_CMP ||
       ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
       ins.kind == Instruction::IK_EXCEPTION_SELECTOR)
      return builtin_lowir_type(LTK_I64);
    return ins.type;
  }

  Instruction instruction(Function & function)
  {
    Instruction result;
    std::string destination;
    if(starts_with(peek(), '%') && peek(1) == "=") {
      destination = take();
      expect("=");
      parse_rvalue(result);
    } else parse_void_instruction(result);
    result.debug_location = debug_location();
    if(!destination.empty())
      result.dest = append_lowir_value(
        function, strings_->intern_range(destination, 1,
          destination.size() - 1), parsed_result_type(result),
        destination.compare(0, 5, "%dbg_") == 0);
    return result;
  }

  void parse_rvalue(Instruction & out)
  {
    const std::string op = take();
    if(op == "const") parse_typed_unary(out, Instruction::IK_CONST);
    else if(op == "copy") parse_typed_unary(out, Instruction::IK_COPY);
    else if(op == "phi") parse_phi(out);
    else if(op == "addr") {
      out.kind = Instruction::IK_ADDR;
      out.first = operand();
    } else if(op == "load") {
      out.volatile_access = accept("volatile");
      parse_typed_unary(out, Instruction::IK_LOAD);
    }
    else if(op == "atomic_load") {
      parse_typed_unary(out, Instruction::IK_ATOMIC_LOAD);
      expect(","); out.args.push_back(operand());
    } else if(op == "index") parse_index(out);
    else if(op == "unary") parse_unary(out);
    else if(op == "binary") parse_binary(out, Instruction::IK_BINARY);
    else if(op == "cmp") parse_binary(out, Instruction::IK_CMP);
	else if(op == "convert") parse_convert(out);
	else if(op == "stack_alloc") {
	  out.kind = Instruction::IK_STACK_ALLOC;
	  out.type = builtin_lowir_type(LTK_PTR);
	  out.first = operand();
	} else if(op == "va_arg") {
	  out.kind = Instruction::IK_VA_ARG;
	  out.type = type();
	  out.first = operand();
	}
	else if(op == "atomic_add_fetch") parse_atomic_three(out, Instruction::IK_ATOMIC_ADD_FETCH, 1);
    else if(op == "atomic_exchange") parse_atomic_three(out, Instruction::IK_ATOMIC_EXCHANGE, 1);
    else if(op == "atomic_compare_exchange") parse_atomic_compare_exchange(out);
    else if(op == "call") parse_call(out, false);
    else if(op == "exception") {
      out.kind = Instruction::IK_EXCEPTION; out.type = type();
    } else if(op == "exception_selector") {
      out.kind = Instruction::IK_EXCEPTION_SELECTOR; out.type = type();
    } else ThrowLowirInputError("unknown rvalue instruction: " + op);
  }

  void parse_typed_unary(Instruction & out, Instruction::Kind kind)
  {
    out.kind = kind;
    out.type = type();
    out.first = operand();
  }

  void parse_phi(Instruction & out)
  {
    out.kind = Instruction::IK_PHI;
    out.type = type();
    expect("[");
    do {
      out.args.push_back(operand());
      expect(":");
      out.args.push_back(operand());
    } while(accept(","));
    expect("]");
  }

  void parse_index(Instruction & out)
  {
    out.kind = Instruction::IK_INDEX;
    out.type = type();
    if(peek() == "[") {
      const Metadata items = metadata();
      if(items.size() != 1 || items[0].first != "projection")
        ThrowLowirInputError("invalid index metadata");
      const std::string & value = items[0].second;
      if(value == "array_element") out.index_projection = IPK_ARRAY_ELEMENT;
      else if(value == "field") out.index_projection = IPK_FIELD;
      else ThrowLowirInputError("invalid index projection");
    }
    out.first = operand();
    expect(",");
    out.second = operand();
  }

  void parse_unary(Instruction & out)
  {
    out.kind = Instruction::IK_UNARY;
    out.op = parse_lowir_operation(take());
    out.type = type();
    out.first = operand();
  }

  void parse_binary(Instruction & out, Instruction::Kind kind)
  {
    out.kind = kind;
    out.op = parse_lowir_operation(take());
    out.type = type();
    out.first = operand();
    expect(",");
    out.second = operand();
  }

  void parse_convert(Instruction & out)
  {
    out.kind = Instruction::IK_CONVERT;
    out.op = parse_lowir_operation(take());
    out.type = type();
    out.source_type = type();
    out.first = operand();
  }

  void parse_atomic_three(Instruction & out, Instruction::Kind kind,
                          std::size_t trailing)
  {
    out.kind = kind;
    out.type = type();
    out.first = operand();
    expect(","); out.second = operand();
    for(std::size_t i = 0; i < trailing; ++i) {
      expect(","); out.args.push_back(operand());
    }
  }

  void parse_atomic_compare_exchange(Instruction & out)
  {
    out.kind = Instruction::IK_ATOMIC_COMPARE_EXCHANGE;
    out.type = type();
    out.first = operand();
    expect(","); out.second = operand();
    expect(","); out.third = operand();
    expect(","); out.args.push_back(operand());
    expect(","); out.args.push_back(operand());
  }

  void parse_call(Instruction & out, bool returns_void)
  {
    out.kind = Instruction::IK_CALL;
    out.call_returns_void = returns_void;
    out.type = returns_void ? builtin_lowir_type(LTK_VOID) : type();
    out.first = operand();
    expect("(");
    if(!accept(")")) {
      do out.args.push_back(operand()); while(accept(","));
      expect(")");
    }
    if(peek() == "[") {
      const Metadata items = metadata();
      if(items.size() != 1 || items[0].first != "elision" ||
         items[0].second != "copy")
        ThrowLowirInputError("invalid call metadata");
      out.copy_elision_candidate = true;
    }
    if(accept("as")) {
      out.has_call_signature = true;
      expect("("); out.call_params = parameter_list(); expect(")");
      expect("->"); out.call_return_type = type();
      SymbolMetadata unused;
      apply_symbol_metadata(metadata(), unused, &out.call_boundary, 0, true);
    }
    if(out.copy_elision_candidate && out.has_call_signature)
      ThrowLowirInputError("copy elision requires a direct call");
  }

  void parse_void_instruction(Instruction & out)
  {
    const std::size_t instruction_line = peek_line();
    const std::string op = take();
    if(op == "store") {
      out.kind = Instruction::IK_STORE;
      out.volatile_access = accept("volatile");
      out.type = type(); out.first = operand();
      expect(","); out.second = operand();
    } else if(op == "atomic_store") {
      out.kind = Instruction::IK_ATOMIC_STORE; out.type = type(); out.first = operand();
      expect(","); out.second = operand(); expect(","); out.args.push_back(operand());
    } else if(op == "atomic_thread_fence" || op == "atomic_signal_fence") {
      out.kind = op == "atomic_thread_fence" ? Instruction::IK_ATOMIC_THREAD_FENCE :
                 Instruction::IK_ATOMIC_SIGNAL_FENCE;
      out.first = operand();
    } else if(op == "va_start") {
      out.kind = Instruction::IK_VA_START;
      out.first = operand();
    } else if(op == "call") {
      expect("void"); parse_call(out, true);
    } else if(op == "copyobj" || op == "zeroinit") parse_bulk(out, op);
    else if(op == "eh_try") {
      out.kind = Instruction::IK_EH_TRY;
      out.first = operand();
    } else if(op == "eh_cleanup") {
      if(!done() && peek_line() == instruction_line && starts_with(peek(), '^')) {
        out.kind = Instruction::IK_EH_CLEANUP;
        out.first = operand();
      } else out.kind = Instruction::IK_EH_CLEANUP_CLAUSE;
    } else if(op == "eh_catch") {
      out.kind = Instruction::IK_EH_CATCH;
      out.first = operand();
      if(accept(",")) {
        const Operand selector = operand();
        if(!selector.has_int_value) ThrowLowirInputError("invalid catch selector");
        out.has_eh_selector = true;
        out.eh_selector = selector.int_value;
      }
    } else if(op == "eh_filter") {
      out.kind = Instruction::IK_EH_FILTER;
      while(!done() && peek_line() == instruction_line && peek() != "!dbg") {
        accept(",");
        if(done() || peek_line() != instruction_line || peek() == "!dbg") break;
        out.args.push_back(operand());
      }
    } else if(op == "eh_catch_all") {
      out.kind = Instruction::IK_EH_CATCH_ALL;
      if(accept(",")) {
        const Operand selector = operand();
        if(!selector.has_int_value) ThrowLowirInputError("invalid catch-all selector");
        out.has_eh_selector = true;
        out.eh_selector = selector.int_value;
      }
    } else if(op == "eh_end") out.kind = Instruction::IK_EH_END;
    else if(op == "throw") {
      out.kind = Instruction::IK_THROW; out.type = type(); out.first = operand();
    } else if(op == "resume") out.kind = Instruction::IK_RESUME;
    else if(op == "unreachable") out.kind = Instruction::IK_UNREACHABLE;
    else if(op == "jump") { out.kind = Instruction::IK_JUMP; out.first = operand(); }
    else if(op == "branch") parse_branch(out);
    else if(op == "switch") parse_switch(out);
    else if(op == "return") parse_return(out);
    else ThrowLowirInputError("unknown instruction: " + op);
  }

  void parse_bulk(Instruction & out, const std::string & op)
  {
    out.kind = op == "copyobj" ? Instruction::IK_COPYOBJ : Instruction::IK_ZEROINIT;
    parse_span_text(take(), out.byte_count, out.byte_alignment);
    out.first = operand();
    if(out.kind == Instruction::IK_COPYOBJ) { expect(","); out.second = operand(); }
  }

  void parse_branch(Instruction & out)
  {
    out.kind = Instruction::IK_BRANCH;
    out.first = operand(); expect(","); out.second = operand();
    expect(","); out.third = operand();
  }

  void parse_switch(Instruction & out)
  {
    out.kind = Instruction::IK_SWITCH;
    out.first = operand(); expect(","); out.second = operand();
    while(accept(",")) {
      out.args.push_back(operand()); expect(":"); out.args.push_back(operand());
    }
  }

  void parse_return(Instruction & out)
  {
    out.kind = Instruction::IK_RETURN;
    out.type = type();
    if(out.type.kind != LTK_VOID) out.first = operand();
  }
};

struct FunctionInfo
{
  const std::vector<Parameter> * params;
  const LowType * result;
  const FunctionBoundaryMetadata * boundary;
};

class Validator
{
public:
  Validator(const Program & program, LowirEntryPolicy entry_policy)
    : program_(program), entry_policy_(entry_policy) {}

  void Validate()
  {
    index_top_level();
    validate_global_initializers();
    validate_roles_and_tls();
    validate_entry_definition();
    validate_aliases();
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i) {
      validate_parameters(program_.function_declarations[i].params,
                          program_.function_declarations[i].return_type);
      validate_query_boundary(program_.function_declarations[i].params,
          program_.function_declarations[i].return_type,
          program_.function_declarations[i].boundary);
    }
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      validate_function(program_.functions[i]);
  }

private:
  typedef std::unordered_map<std::string, const LowType *> TypeIndex;
  typedef std::unordered_map<std::string, std::unordered_set<std::string> >
    PredecessorIndex;

  const Program & program_;
  LowirEntryPolicy entry_policy_;
  std::unordered_set<std::string> top_symbols_;
  std::unordered_set<std::string> globals_;
  std::unordered_map<std::string, GlobalStorageMode> global_storage_;
  std::unordered_map<std::string, FunctionInfo> functions_;

  const std::string & operand_spelling(const Operand & operand) const
  {
    if(!operand.has_spelling)
      ThrowLowirInputError("operand has no input spelling");
    return program_.strings.get(operand.literal);
  }

  void add_top(const std::string & name)
  {
    if(!top_symbols_.insert(name).second)
      ThrowLowirInputError("duplicate top-level symbol: " + name);
  }

  void index_top_level()
  {
    for(std::size_t i = 0; i < program_.global_declarations.size(); ++i) {
      const GlobalDeclaration & item = program_.global_declarations[i];
      const std::string & name = lowir_symbol_name(program_, item.symbol);
      add_top(name); globals_.insert(name); global_storage_[name] = item.storage;
      validate_global_role(item.metadata.role);
    }
    for(std::size_t i = 0; i < program_.globals.size(); ++i) {
      const GlobalDefinition & item = program_.globals[i];
      const std::string & name = lowir_symbol_name(program_, item.symbol);
      add_top(name); globals_.insert(name); global_storage_[name] = item.storage;
      validate_global_role(item.metadata.role);
    }
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i) {
      const FunctionDeclaration & item = program_.function_declarations[i];
      const std::string & name = lowir_symbol_name(program_, item.symbol);
      add_top(name);
      functions_[name] = FunctionInfo{&item.params, &item.return_type, &item.boundary};
    }
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & item = program_.functions[i];
      const std::string & name = lowir_symbol_name(program_, item.symbol);
      add_top(name);
      functions_[name] = FunctionInfo{&item.params, &item.return_type, &item.boundary};
    }
  }

  void validate_global_role(SymbolRole role)
  {
    if(role != SR_NONE && role != SR_RTTI_CLASS && role != SR_RTTI_SI &&
       role != SR_RTTI_VMI && role != SR_RTTI_DATA)
      ThrowLowirInputError("function role on global");
  }

  void validate_roles_and_tls()
  {
    std::unordered_set<int> roles;
    std::unordered_set<std::string> tls_targets;
    for(std::size_t i = 0; i < program_.global_declarations.size(); ++i)
      validate_symbol_facts(program_.global_declarations[i].metadata, roles, tls_targets);
    for(std::size_t i = 0; i < program_.globals.size(); ++i)
      validate_symbol_facts(program_.globals[i].metadata, roles, tls_targets);
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i)
      validate_symbol_facts(program_.function_declarations[i].metadata, roles, tls_targets);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      validate_symbol_facts(program_.functions[i].metadata, roles, tls_targets);
  }

  void validate_entry_definition()
  {
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i) {
      if(program_.function_declarations[i].metadata.role == SR_ENTRY)
        ThrowLowirInputError("entry role requires a function definition");
    }
    bool explicit_entry = false;
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & function = program_.functions[i];
      explicit_entry = explicit_entry || function.metadata.role == SR_ENTRY;
    }
    if(entry_policy_ == LEP_REQUIRE_ENTRY && !explicit_entry)
      ThrowLowirInputError("LowIR program has no entry definition");
  }

  void validate_global_initializers()
  {
    for(std::size_t i = 0; i < program_.globals.size(); ++i) {
      const GlobalDefinition & global = program_.globals[i];
      if(global.structured) {
        for(std::size_t j = 0; j < global.data_items.size(); ++j) {
          const GlobalDefinition::DataItem & item = global.data_items[j];
          if(item.kind == GlobalDefinition::DataItem::ITEM_ADDR &&
             !top_symbols_.count(
               program_.strings.get(item.symbol_spelling)))
            ThrowLowirInputError("undefined structured global address target");
        }
      } else if(global.init_kind == GlobalDefinition::INIT_ADDR &&
                !top_symbols_.count(operand_spelling(global.init_operand))) {
        ThrowLowirInputError("undefined global address initializer target");
      }
    }
  }

  void validate_symbol_facts(const SymbolMetadata & metadata,
                             std::unordered_set<int> & roles,
                             std::unordered_set<std::string> & tls_targets)
  {
    if(metadata.role != SR_NONE && metadata.role != SR_RTTI_DATA &&
       !roles.insert(static_cast<int>(metadata.role)).second)
      ThrowLowirInputError("duplicate singleton role");
    if(metadata.tls_for_spelling.valid()) {
      const std::string & target =
        program_.strings.get(metadata.tls_for_spelling);
      if(!globals_.count(target) || global_storage_[target] != GSM_THREAD_LOCAL)
        ThrowLowirInputError("tls_for target is not thread-local global");
      if(!tls_targets.insert(target).second) ThrowLowirInputError("duplicate tls wrapper");
    }
  }

  void validate_aliases()
  {
    std::unordered_set<std::string> aliases;
    for(std::size_t i = 0; i < program_.object_aliases.size(); ++i) {
      const ObjectAlias & alias = program_.object_aliases[i];
      if(!aliases.insert(program_.strings.get(alias.object_symbol)).second)
        ThrowLowirInputError("duplicate object alias");
      if(!top_symbols_.count(program_.strings.get(alias.target_spelling)))
        ThrowLowirInputError("undefined object alias target");
    }
  }

  void validate_parameters(const std::vector<Parameter> & params,
                           const LowType & result)
  {
    std::unordered_set<std::string> names;
    for(std::size_t i = 0; i < params.size(); ++i) {
      const Parameter & param = params[i];
      if(!names.insert(lowir_parameter_name(program_, param)).second)
        ThrowLowirInputError("duplicate parameter");
      const bool pointer = param.type.kind == LTK_PTR;
      if(param.metadata.passing != PPM_DIRECT && !pointer)
        ThrowLowirInputError("non-direct passing requires ptr");
      if(param.metadata.alias != PALM_DEFAULT && !pointer)
        ThrowLowirInputError("alias metadata requires ptr");
      if(param.metadata.object_bytes && !pointer)
        ThrowLowirInputError("object_bytes metadata requires ptr");
      if(param.metadata.passing == PPM_INDIRECT_RESULT &&
         (i != 0 || result.kind != LTK_VOID))
        ThrowLowirInputError("invalid indirect result parameter");
    }
  }

  bool integer_query_type(const LowType & type) const
  {
    return type.kind == LTK_I8 || type.kind == LTK_U8 ||
      type.kind == LTK_I16 || type.kind == LTK_U16 ||
      type.kind == LTK_I32 || type.kind == LTK_U32 ||
      type.kind == LTK_I64;
  }

  void validate_query_boundary(const std::vector<Parameter> & params,
                               const LowType & result,
                               const FunctionBoundaryMetadata & boundary)
  {
    if(boundary.query != CQM_STABLE_PREFIX) return;
    if(boundary.arity != CAM_FIXED || params.empty() ||
       !integer_query_type(params.back().type) ||
       result.kind == LTK_VOID || result.kind == LTK_OBJECT ||
       result.kind == LTK_I128 || result.kind == LTK_F80)
      ThrowLowirInputError("stable-prefix query requires a fixed scalar boundary "
                       "with a final integer parameter");
  }

  void validate_function(const Function & function)
  {
    validate_parameters(function.params, function.return_type);
    validate_query_boundary(
      function.params, function.return_type, function.boundary);
    if(function.blocks.empty()) ThrowLowirInputError("function has no blocks");
    TypeIndex values;
    TypeIndex all_values;
    TypeIndex slots;
    std::unordered_set<std::string> blocks;
    for(std::size_t i = 0; i < function.params.size(); ++i)
      values[lowir_parameter_name(program_, function.params[i])] =
        all_values[lowir_parameter_name(program_, function.params[i])] =
          &function.params[i].type;
    for(std::size_t i = 0; i < function.slots.size(); ++i) {
      if(!slots.emplace(lowir_slot_name(program_.strings, function,
                                        function.slots[i]),
                        &lowir_slot_type(function, function.slots[i])).second)
        ThrowLowirInputError("duplicate slot");
    }
    for(std::size_t i = 0; i < function.blocks.size(); ++i)
      if(!blocks.insert(lowir_block_label(program_.strings, function,
                                          function.blocks[i].id)).second)
        ThrowLowirInputError("duplicate block");
    for(std::size_t i = 0; i < function.blocks.size(); ++i)
      for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
        const Instruction & ins = function.blocks[i].instructions[j];
        if(!ins.dest.valid()) continue;
        const std::string name = lowir_value_name(
          program_.strings, function, ins.dest);
        if(all_values.count(name))
          ThrowLowirInputError("duplicate temporary definition");
        all_values[name] = &lowir_value_type(function, ins.dest);
      }
    PredecessorIndex predecessors;
    std::unordered_set<std::string> exception_targets;
    for(std::size_t i = 0; i < function.blocks.size(); ++i) {
      const Block & source = function.blocks[i];
      const std::string & source_name = lowir_block_label(
        program_.strings, function, source.id);
      for(std::size_t j = 0; j < source.instructions.size(); ++j) {
        const Instruction & ins = source.instructions[j];
        if(ins.kind == Instruction::IK_EH_TRY ||
           ins.kind == Instruction::IK_EH_CLEANUP)
          exception_targets.insert(operand_spelling(ins.first));
      }
      if(source.instructions.empty()) continue;
      const Instruction & terminal = source.instructions.back();
      const auto add_predecessor = [&](const Operand & target) {
        if(target.kind == Operand::OP_LABEL)
          predecessors[operand_spelling(target)].insert(source_name);
      };
      if(terminal.kind == Instruction::IK_JUMP)
        add_predecessor(terminal.first);
      else if(terminal.kind == Instruction::IK_BRANCH) {
        add_predecessor(terminal.second);
        add_predecessor(terminal.third);
      } else if(terminal.kind == Instruction::IK_SWITCH) {
        add_predecessor(terminal.second);
        for(std::size_t j = 1; j < terminal.args.size(); j += 2)
          add_predecessor(terminal.args[j]);
      }
    }
    for(std::size_t i = 0; i < function.blocks.size(); ++i)
      validate_block(function, function.blocks[i], values, all_values, slots,
                     blocks, predecessors, exception_targets);
  }

  void validate_block(const Function & function, const Block & block,
                      TypeIndex & values,
                      const TypeIndex & all_values,
                      const TypeIndex & slots,
                      const std::unordered_set<std::string> & blocks,
                      const PredecessorIndex & predecessors,
                      const std::unordered_set<std::string> & exception_targets)
  {
    if(block.instructions.empty()) ThrowLowirInputError("empty block");
    bool saw_non_phi = false;
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const Instruction & ins = block.instructions[i];
      if(ins.kind == Instruction::IK_PHI) {
        if(saw_non_phi)
          ThrowLowirInputError("phi instructions must precede ordinary instructions");
        validate_phi(function, block, ins, all_values, slots, blocks,
                     predecessors, exception_targets);
      } else {
        saw_non_phi = true;
        validate_instruction(function, ins, values, slots, blocks);
      }
      if(ins.dest.valid()) {
        const std::string name = lowir_value_name(
          program_.strings, function, ins.dest);
        values[name] = &lowir_value_type(function, ins.dest);
      }
    }
	std::size_t terminal = block.instructions.size();
	while(terminal != 0 &&
	      block.instructions[terminal - 1].kind == Instruction::IK_EH_END)
	  --terminal;
	const bool trailing_handler_pops = terminal != block.instructions.size();
	const bool terminated = trailing_handler_pops ?
	  terminal != 0 && instruction_terminates(block.instructions[terminal - 1]) :
	  instruction_terminates(block.instructions.back());
	if(!terminated)
	  ThrowLowirInputError("block has no terminator: " +
            lowir_block_label(program_.strings, function, block.id));
  }

  bool phi_type_supported(const LowType & type) const
  {
    return type.kind == LTK_I1 || type.kind == LTK_I8 ||
      type.kind == LTK_U8 || type.kind == LTK_I16 ||
      type.kind == LTK_U16 || type.kind == LTK_I32 ||
      type.kind == LTK_U32 || type.kind == LTK_I64 ||
      type.kind == LTK_F32 || type.kind == LTK_F64 ||
      type.kind == LTK_PTR;
  }

  void validate_phi(const Function & function, const Block & block,
                    const Instruction & ins, const TypeIndex & all_values,
                    const TypeIndex & slots,
                    const std::unordered_set<std::string> & blocks,
                    const PredecessorIndex & predecessors,
                    const std::unordered_set<std::string> & exception_targets)
  {
    if(!phi_type_supported(ins.type))
      ThrowLowirInputError("phi requires a directly representable scalar type");
    if(ins.args.empty() || ins.args.size() % 2)
      ThrowLowirInputError("phi requires predecessor/value pairs");
    const std::string & block_name = lowir_block_label(
      program_.strings, function, block.id);
    if(exception_targets.count(block_name))
      ThrowLowirInputError("phi is not permitted in an exception handler block");
    const PredecessorIndex::const_iterator expected =
      predecessors.find(block_name);
    const std::size_t expected_count = expected == predecessors.end() ? 0 :
      expected->second.size();
    std::unordered_set<std::string> incoming;
    for(std::size_t i = 0; i < ins.args.size(); i += 2) {
      const Operand & predecessor = ins.args[i];
      const Operand & value = ins.args[i + 1];
      validate_target(predecessor, blocks);
      const std::string & predecessor_name = operand_spelling(predecessor);
      if(!incoming.insert(predecessor_name).second)
        ThrowLowirInputError("duplicate phi predecessor");
      if(expected == predecessors.end() ||
         !expected->second.count(predecessor_name))
        ThrowLowirInputError("phi label " + predecessor_name +
          " is not a predecessor of " + block_name + " in function @" +
          lowir_symbol_name(program_, function.symbol));
      validate_operand(value, all_values, slots);
      if(value.kind == Operand::OP_SLOT || value.kind == Operand::OP_LABEL)
        ThrowLowirInputError("phi incoming operand is not a scalar value");
      if(value.kind == Operand::OP_TEMP) {
        const TypeIndex::const_iterator found =
          all_values.find(operand_spelling(value));
        if(found == all_values.end() ||
           !same_lowir_type(*found->second, ins.type))
          ThrowLowirInputError("phi incoming temporary type mismatch");
      } else if(value.kind == Operand::OP_GLOBAL && ins.type.kind != LTK_PTR) {
        ThrowLowirInputError("phi global operand requires ptr type");
      } else if(value.kind == Operand::OP_FLOAT &&
                ins.type.kind != LTK_F32 && ins.type.kind != LTK_F64) {
        ThrowLowirInputError("phi floating literal type mismatch");
      } else if(value.kind == Operand::OP_INTEGER &&
                (ins.type.kind == LTK_F32 || ins.type.kind == LTK_F64)) {
        ThrowLowirInputError("phi integer literal type mismatch");
      }
    }
    if(incoming.size() != expected_count)
      ThrowLowirInputError("phi must name every ordinary predecessor exactly once");
  }

  bool instruction_terminates(const Instruction & ins) const
  {
    if(ins.kind == Instruction::IK_JUMP ||
       ins.kind == Instruction::IK_BRANCH ||
       ins.kind == Instruction::IK_SWITCH ||
       ins.kind == Instruction::IK_RETURN ||
       ins.kind == Instruction::IK_UNREACHABLE ||
       ins.kind == Instruction::IK_THROW ||
       ins.kind == Instruction::IK_RESUME)
      return true;
    if(ins.kind != Instruction::IK_CALL) return false;
    FunctionBoundaryMetadata boundary = ins.call_boundary;
    if(ins.first.kind == Operand::OP_GLOBAL) {
      const std::unordered_map<std::string, FunctionInfo>::const_iterator found =
        functions_.find(operand_spelling(ins.first));
      if(found != functions_.end()) boundary = *found->second.boundary;
    }
    return boundary.returns == CRM_NORETURN;
  }

  void validate_operand(const Operand & operand,
                        const TypeIndex & values,
                        const TypeIndex & slots,
                        bool allow_label = false) const
  {
    const std::string & spelling = operand_spelling(operand);
    if(operand.kind == Operand::OP_TEMP && !values.count(spelling))
      ThrowLowirInputError("undefined temporary: " + spelling);
    if(operand.kind == Operand::OP_SLOT && !slots.count(spelling))
      ThrowLowirInputError("undefined slot: " + spelling);
    if(operand.kind == Operand::OP_GLOBAL && !top_symbols_.count(spelling))
      ThrowLowirInputError("undefined top-level symbol: " + spelling);
    if(operand.kind == Operand::OP_LABEL && !allow_label)
      ThrowLowirInputError("block label used as value");
  }

  void validate_target(const Operand & target, const std::unordered_set<std::string> & blocks) const
  {
    if(target.kind != Operand::OP_LABEL ||
       !blocks.count(operand_spelling(target)))
      ThrowLowirInputError("undefined or invalid block target");
  }

  void validate_instruction(const Function & function, const Instruction & ins,
                            const TypeIndex & values,
                            const TypeIndex & slots,
                            const std::unordered_set<std::string> & blocks)
  {
    const Instruction::Kind kind = ins.kind;
    if(kind == Instruction::IK_JUMP) validate_target(ins.first, blocks);
    else if(kind == Instruction::IK_BRANCH) {
      validate_operand(ins.first, values, slots); validate_target(ins.second, blocks);
      validate_target(ins.third, blocks);
    } else if(kind == Instruction::IK_SWITCH) {
      validate_operand(ins.first, values, slots); validate_target(ins.second, blocks);
      for(std::size_t i = 0; i < ins.args.size(); i += 2) {
        validate_operand(ins.args[i], values, slots); validate_target(ins.args[i + 1], blocks);
      }
    } else if(kind == Instruction::IK_CALL) validate_call(ins, values, slots);
    else {
      validate_general_operands(ins, values, slots);
      validate_operation_types(ins);
    }
    if(kind == Instruction::IK_RETURN && !same_lowir_type(ins.type, function.return_type))
      ThrowLowirInputError("return type does not match function");
	if(kind == Instruction::IK_VA_START) {
      if(function.boundary.arity != CAM_VARIADIC)
        ThrowLowirInputError("va_start requires a variadic function");
      const TypeIndex::const_iterator value = values.find(
        operand_spelling(ins.first));
      if(ins.first.kind != Operand::OP_TEMP || value == values.end() ||
         value->second->kind != LTK_PTR)
        ThrowLowirInputError("va_start requires a pointer value");
	}
	if(kind == Instruction::IK_VA_ARG) {
	  const TypeIndex::const_iterator value = values.find(
        operand_spelling(ins.first));
	  if(ins.first.kind != Operand::OP_TEMP || value == values.end() ||
	     value->second->kind != LTK_PTR)
	    ThrowLowirInputError("va_arg requires a pointer value");
	  if(ins.type.kind != LTK_PTR && ins.type.kind != LTK_I8 &&
	     ins.type.kind != LTK_U8 && ins.type.kind != LTK_I16 &&
	     ins.type.kind != LTK_U16 && ins.type.kind != LTK_I32 &&
	     ins.type.kind != LTK_U32 && ins.type.kind != LTK_I64 &&
	     ins.type.kind != LTK_F64)
	    ThrowLowirInputError("va_arg scalar type is not supported");
	}
	if(kind == Instruction::IK_STACK_ALLOC) {
	  LowTypeKind size = ins.first.kind == Operand::OP_INTEGER ?
	    LTK_I64 : ins.first.literal_type.kind;
      const TypeIndex::const_iterator value = values.find(
        operand_spelling(ins.first));
      const TypeIndex::const_iterator slot = slots.find(
        operand_spelling(ins.first));
	  if(ins.first.kind == Operand::OP_TEMP && value != values.end())
	    size = value->second->kind;
	  else if(ins.first.kind == Operand::OP_SLOT && slot != slots.end())
	    size = slot->second->kind;
	  if(size < LTK_I1 || size > LTK_I64 || size == LTK_F32 ||
	     size == LTK_F64 || size == LTK_F80 || size == LTK_PTR)
	    ThrowLowirInputError("stack_alloc requires an integer size");
	}
    if((kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP))
      validate_target(ins.first, blocks);
  }

  void validate_general_operands(const Instruction & ins,
                                 const TypeIndex & values,
                                 const TypeIndex & slots)
  {
    const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
    for(std::size_t i = 0; i < 3; ++i)
      if(operands[i]->has_spelling) validate_operand(*operands[i], values, slots,
        ins.kind == Instruction::IK_EH_TRY || ins.kind == Instruction::IK_EH_CLEANUP);
    for(std::size_t i = 0; i < ins.args.size(); ++i)
      validate_operand(ins.args[i], values, slots);
  }

  void validate_operation_types(const Instruction & ins)
  {
    if(ins.kind == Instruction::IK_UNARY) {
      if(ins.op.kind == LowOperation::LOP_BSWAP && ins.type.kind != LTK_I16 && ins.type.kind != LTK_U16 &&
         ins.type.kind != LTK_I32 && ins.type.kind != LTK_U32 &&
         ins.type.kind != LTK_I64) ThrowLowirInputError("invalid bswap type");
    }
    if(ins.kind == Instruction::IK_CONVERT) validate_conversion(ins);
    if((ins.kind == Instruction::IK_COPYOBJ || ins.kind == Instruction::IK_ZEROINIT) &&
       (!ins.byte_count || !is_power_of_two(ins.byte_alignment)))
      ThrowLowirInputError("invalid bulk-memory span");
  }

  void validate_conversion(const Instruction & ins)
  {
    const std::size_t dst_i = integer_width(ins.type);
    const std::size_t src_i = integer_width(ins.source_type);
    const std::size_t dst_f = float_width(ins.type);
    const std::size_t src_f = float_width(ins.source_type);
    if(same_lowir_type(ins.type, ins.source_type)) return;
    if((ins.op.kind == LowOperation::LOP_SEXT || ins.op.kind == LowOperation::LOP_ZEXT) && dst_i && src_i && dst_i > src_i) return;
    if(ins.op.kind == LowOperation::LOP_TRUNC && dst_i && src_i && dst_i < src_i) return;
    if((ins.op.kind == LowOperation::LOP_SITOFP || ins.op.kind == LowOperation::LOP_UITOFP) && dst_f && src_i) return;
    if((ins.op.kind == LowOperation::LOP_FPTOSI || ins.op.kind == LowOperation::LOP_FPTOUI) && dst_i && src_f) return;
    if(ins.op.kind == LowOperation::LOP_FPEXT && dst_f && src_f && dst_f > src_f) return;
    if(ins.op.kind == LowOperation::LOP_FPTRUNC && dst_f && src_f && dst_f < src_f) return;
    ThrowLowirInputError("invalid conversion widths or categories");
  }

  void validate_call(const Instruction & ins,
                     const TypeIndex & values,
                     const TypeIndex & slots)
  {
    validate_operand(ins.first, values, slots);
    for(std::size_t i = 0; i < ins.args.size(); ++i) validate_operand(ins.args[i], values, slots);
    const std::unordered_map<std::string, FunctionInfo>::const_iterator found =
      functions_.find(operand_spelling(ins.first));
    const bool direct = ins.first.kind == Operand::OP_GLOBAL && found != functions_.end();
    if(!direct && !ins.has_call_signature) ThrowLowirInputError("indirect call requires signature");
    if(ins.copy_elision_candidate &&
       (!direct || !ins.call_returns_void || ins.args.size() < 2))
      ThrowLowirInputError(
        "copy elision requires a direct void call with destination and source");
    if(ins.has_call_signature) {
      validate_parameters(ins.call_params, ins.call_return_type);
      validate_query_boundary(
        ins.call_params, ins.call_return_type, ins.call_boundary);
      if(!same_lowir_type(ins.call_return_type, ins.type))
        ThrowLowirInputError("call signature return mismatch");
      validate_arity(ins.args.size(), ins.call_params.size(), ins.call_boundary.arity);
    } else {
      if(!same_lowir_type(*found->second.result, ins.type))
        ThrowLowirInputError("direct call return mismatch");
      validate_arity(ins.args.size(), found->second.params->size(), found->second.boundary->arity);
    }
  }

  void validate_arity(std::size_t actual, std::size_t fixed, CallArityMode mode)
  {
    if((mode == CAM_FIXED && actual != fixed) || (mode != CAM_FIXED && actual < fixed))
      ThrowLowirInputError("call arity mismatch");
  }

  const LowType & result_type(const Instruction & ins) const
  {
    if(ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX)
      return builtin_lowir_type(LTK_PTR);
    if(ins.kind == Instruction::IK_CMP || ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
       ins.kind == Instruction::IK_EXCEPTION_SELECTOR) return builtin_lowir_type(LTK_I64);
    return ins.type;
  }
};

std::string read_file(const std::string & path)
{
  std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
  if(!input)
    throw InputOutputError("unable to open LowIR source: " + path,
                           CompilerErrorDomain::LOWIR);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void assign_literal_type(Program & program, Operand & operand,
                         const LowType & type)
{
  if(operand.kind != Operand::OP_INTEGER && operand.kind != Operand::OP_FLOAT)
    return;
  operand.literal_type = type;
  if(operand.kind != Operand::OP_FLOAT) return;
  if(!operand.has_spelling || !parse_lowir_floating_literal_bits(
      program.strings.get(operand.literal), type,
      &operand.literal_low, &operand.literal_high))
    ThrowLowirInputError("invalid floating literal");
  operand.has_float_bits = true;
}

void assign_instruction_literal_types(Program & program, Instruction & ins)
{
  const LowType & i64 = builtin_lowir_type(LTK_I64);
  const LowType & ptr = builtin_lowir_type(LTK_PTR);
  switch(ins.kind) {
  case Instruction::IK_CONST:
  case Instruction::IK_COPY:
  case Instruction::IK_UNARY:
    assign_literal_type(program, ins.first, ins.type);
    break;
  case Instruction::IK_PHI:
    for(std::size_t i = 1; i < ins.args.size(); i += 2)
      assign_literal_type(program, ins.args[i], ins.type);
    break;
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
    assign_literal_type(program, ins.first, ins.type);
    assign_literal_type(program, ins.second, ins.type);
    break;
  case Instruction::IK_CONVERT:
    assign_literal_type(program, ins.first, ins.source_type);
    break;
  case Instruction::IK_STORE:
  case Instruction::IK_ATOMIC_STORE:
    assign_literal_type(program, ins.first, ins.type);
    assign_literal_type(program, ins.second, ptr);
    break;
  case Instruction::IK_ATOMIC_EXCHANGE:
  case Instruction::IK_ATOMIC_ADD_FETCH:
    assign_literal_type(program, ins.first, ptr);
    assign_literal_type(program, ins.second, ins.type);
    break;
  case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
    assign_literal_type(program, ins.first, ptr);
    assign_literal_type(program, ins.second, ins.type);
    assign_literal_type(program, ins.third, ins.type);
    break;
  case Instruction::IK_STACK_ALLOC:
  case Instruction::IK_BRANCH:
    assign_literal_type(program, ins.first, i64);
    break;
  case Instruction::IK_RETURN:
  case Instruction::IK_THROW:
    assign_literal_type(program, ins.first, ins.type);
    break;
  case Instruction::IK_CALL:
    if(ins.has_call_signature)
      for(std::size_t i = 0; i < ins.args.size() && i < ins.call_params.size(); ++i)
        assign_literal_type(program, ins.args[i], ins.call_params[i].type);
    break;
  default: break;
  }
}

void assign_program_literal_types(Program & program)
{
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    GlobalDefinition & global = program.globals[i];
    if(global.init_kind == GlobalDefinition::INIT_INTEGER)
      assign_literal_type(program, global.init_operand, global.type);
    for(std::size_t j = 0; j < global.data_items.size(); ++j)
      if(global.data_items[j].kind == GlobalDefinition::DataItem::ITEM_INTEGER)
        assign_literal_type(program, global.data_items[j].literal_operand,
                            global.data_items[j].type);
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f)
    for(std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
      for(std::size_t i = 0;
          i < program.functions[f].blocks[b].instructions.size(); ++i)
        assign_instruction_literal_types(
          program, program.functions[f].blocks[b].instructions[i]);
}

Program parse_tokens(std::vector<Token> & tokens, LowirEntryPolicy entry_policy)
{
  Program program;
  {
    Parser parser(tokens);
    program = parser.Parse();
  }
  bool has_entry = false;
  bool has_init = false;
  bool has_fini = false;
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    has_entry = has_entry || program.functions[i].metadata.role == SR_ENTRY;
    has_init = has_init || program.functions[i].metadata.role == SR_INIT;
    has_fini = has_fini || program.functions[i].metadata.role == SR_FINI;
  }
  if(!has_entry) {
    for(std::size_t i = 0; i < program.functions.size(); ++i) {
      Function & function = program.functions[i];
      if(function.metadata.role != SR_NONE) continue;
      const std::string & name = lowir_symbol_name(program, function.symbol);
      if(name == "main") {
        function.metadata.role = SR_ENTRY;
        function.metadata.inferred_legacy_role = true;
        has_entry = true;
      } else if(!has_init && name == "__cppgm_init") {
        function.metadata.role = SR_INIT;
        function.metadata.inferred_legacy_role = true;
        has_init = true;
      } else if(!has_fini && name == "__cppgm_fini") {
        function.metadata.role = SR_FINI;
        function.metadata.inferred_legacy_role = true;
        has_fini = true;
      }
    }
  }
  const std::size_t token_count = tokens.size();
  std::vector<Token>().swap(tokens);
  Validator(program, entry_policy).Validate();
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    resolve_lowir_function_operands(program.functions[i], program.strings);
  resolve_lowir_program_symbols(program);
  assign_program_literal_types(program);
  propagate_direct_call_boundaries(program);
  program.token_count = token_count;
  finalize_lowir_object_model(program);
  return program;
}

}  // namespace

LowOperation parse_lowir_operation(const std::string & text)
{
  return LowOperation(operation_kind(text));
}

const char * lowir_operation_text(LowOperation operation)
{
  switch(operation.kind) {
  case LowOperation::LOP_NONE: return "";
  case LowOperation::LOP_NEG: return "neg";
  case LowOperation::LOP_NOT: return "not";
  case LowOperation::LOP_BITNOT: return "bitnot";
  case LowOperation::LOP_BSWAP: return "bswap";
  case LowOperation::LOP_ADD: return "add";
  case LowOperation::LOP_SUB: return "sub";
  case LowOperation::LOP_MUL: return "mul";
  case LowOperation::LOP_DIV: return "div";
  case LowOperation::LOP_UDIV: return "udiv";
  case LowOperation::LOP_MOD: return "mod";
  case LowOperation::LOP_UMOD: return "umod";
  case LowOperation::LOP_AND: return "and";
  case LowOperation::LOP_OR: return "or";
  case LowOperation::LOP_XOR: return "xor";
  case LowOperation::LOP_SHL: return "shl";
  case LowOperation::LOP_SHR: return "shr";
  case LowOperation::LOP_USHR: return "ushr";
  case LowOperation::LOP_EQ: return "eq";
  case LowOperation::LOP_NE: return "ne";
  case LowOperation::LOP_LT: return "lt";
  case LowOperation::LOP_ULT: return "ult";
  case LowOperation::LOP_LE: return "le";
  case LowOperation::LOP_ULE: return "ule";
  case LowOperation::LOP_GT: return "gt";
  case LowOperation::LOP_UGT: return "ugt";
  case LowOperation::LOP_GE: return "ge";
  case LowOperation::LOP_UGE: return "uge";
  case LowOperation::LOP_TRUNC: return "trunc";
  case LowOperation::LOP_SEXT: return "sext";
  case LowOperation::LOP_ZEXT: return "zext";
  case LowOperation::LOP_SITOFP: return "sitofp";
  case LowOperation::LOP_UITOFP: return "uitofp";
  case LowOperation::LOP_FPTOSI: return "fptosi";
  case LowOperation::LOP_FPTOUI: return "fptoui";
  case LowOperation::LOP_FPTRUNC: return "fptrunc";
  case LowOperation::LOP_FPEXT: return "fpext";
  }
  ThrowLowirInternalError("invalid compact LowIR operation identity");
}

bool operator==(LowOperation left, LowOperation right)
{
  return left.kind == right.kind;
}

bool operator!=(LowOperation left, LowOperation right)
{
  return !(left == right);
}

std::ostream & operator<<(std::ostream & out, LowOperation operation)
{
  return out << lowir_operation_text(operation);
}

std::size_t lowir_operation_hash(LowOperation operation)
{
  return static_cast<std::size_t>(operation.kind);
}

const LowType & builtin_lowir_type(LowTypeKind kind)
{
  static const LowType void_type = make_builtin_type(LTK_VOID, 0, 1);
  static const LowType i1_type = make_builtin_type(LTK_I1, 1, 1);
  static const LowType i8_type = make_builtin_type(LTK_I8, 1, 1);
  static const LowType u8_type = make_builtin_type(LTK_U8, 1, 1);
  static const LowType i16_type = make_builtin_type(LTK_I16, 2, 2);
  static const LowType u16_type = make_builtin_type(LTK_U16, 2, 2);
  static const LowType i32_type = make_builtin_type(LTK_I32, 4, 4);
  static const LowType u32_type = make_builtin_type(LTK_U32, 4, 4);
  static const LowType i64_type = make_builtin_type(LTK_I64, 8, 8);
  static const LowType i128_type = make_builtin_type(LTK_I128, 16, 16);
  static const LowType f32_type = make_builtin_type(LTK_F32, 4, 4);
  static const LowType f64_type = make_builtin_type(LTK_F64, 8, 8);
  static const LowType f80_type = make_builtin_type(LTK_F80, 16, 16);
  static const LowType ptr_type = make_builtin_type(LTK_PTR, 8, 8);

  switch(kind) {
  case LTK_VOID: return void_type;
  case LTK_I1: return i1_type;
  case LTK_I8: return i8_type;
  case LTK_U8: return u8_type;
  case LTK_I16: return i16_type;
  case LTK_U16: return u16_type;
  case LTK_I32: return i32_type;
  case LTK_U32: return u32_type;
  case LTK_I64: return i64_type;
  case LTK_I128: return i128_type;
  case LTK_F32: return f32_type;
  case LTK_F64: return f64_type;
  case LTK_F80: return f80_type;
  case LTK_PTR: return ptr_type;
  default: ThrowLowirInternalError("invalid built-in LowIR type identity");
  }
}

std::string lowir_type_text(const LowType & type)
{
  switch(type.kind) {
  case LTK_INVALID: return std::string();
  case LTK_VOID: return "void";
  case LTK_I1: return "i1";
  case LTK_I8: return "i8";
  case LTK_U8: return "u8";
  case LTK_I16: return "i16";
  case LTK_U16: return "u16";
  case LTK_I32: return "i32";
  case LTK_U32: return "u32";
  case LTK_I64: return "i64";
  case LTK_I128: return "i128";
  case LTK_F32: return "f32";
  case LTK_F64: return "f64";
  case LTK_F80: return "f80";
  case LTK_PTR: return "ptr";
  case LTK_OBJECT:
    return "obj<" + std::to_string(type.storage_size) + "x" +
      std::to_string(type.alignment) + ">";
  }
  ThrowLowirInternalError("invalid compact LowIR type identity");
}

bool InstructionDebugLocation::present() const
{
  return file.valid() && line != 0 && column != 0;
}

bool same_lowir_type(const LowType & left, const LowType & right)
{
  if(left.kind != right.kind) return false;
  if(left.kind != LTK_OBJECT) return true;
  return left.storage_size == right.storage_size && left.alignment == right.alignment;
}

bool operator==(const LowType & left, const LowType & right)
{
  return same_lowir_type(left, right);
}

bool operator!=(const LowType & left, const LowType & right)
{
  return !same_lowir_type(left, right);
}

LowirProgram parse_lowir_program_text(const std::string & text,
                                      const std::string & source_name,
                                      LowirEntryPolicy entry_policy)
{
  (void) source_name;
  std::vector<Token> tokens;
  lex_text(text, tokens);
  Program program = parse_tokens(tokens, entry_policy);
  program.source_bytes = text.size();
  return program;
}

LowirProgram parse_lowir_program_files(const std::vector<std::string> & paths,
                                       LowirEntryPolicy entry_policy)
{
  if(paths.empty()) throw InvocationError("no LowIR source files");
  std::vector<Token> tokens;
  std::size_t source_bytes = 0;
  for(std::size_t i = 0; i < paths.size(); ++i) {
    const std::string text = read_file(paths[i]);
    source_bytes += text.size();
    lex_text(text, tokens);
  }
  Program program = parse_tokens(tokens, entry_policy);
  program.source_bytes = source_bytes;
  return program;
}

}  // namespace lowir_model
