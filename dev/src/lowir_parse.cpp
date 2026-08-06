#include "lowir_model.h"

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

struct Token
{
  std::string text;
  std::string source;
  std::size_t line;
};

bool is_punctuation(char c)
{
  return c == '{' || c == '}' || c == '(' || c == ')' || c == '[' ||
         c == ']' || c == ',' || c == ':' || c == '=' || c == '+' ||
         c == '-';
}

void lex_text(const std::string & text, const std::string & source,
              std::vector<Token> & out)
{
  std::size_t i = 0;
  std::size_t line = 1;
  while(i < text.size()) {
    const char c = text[i];
    if(c == '\n') {
      ++line;
      ++i;
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
      out.push_back(Token{"->", source, line});
      i += 2;
      continue;
    }
    if(is_punctuation(c)) {
      out.push_back(Token{std::string(1, c), source, line});
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
    out.push_back(Token{text.substr(begin, i - begin), source, line});
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

std::size_t parse_positive_size(const std::string & text)
{
  if(text.empty()) throw ParseError("expected positive integer");
  char * end = 0;
  errno = 0;
  const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
  if(errno || !end || *end || value == 0 || value > SIZE_MAX)
    throw ParseError("invalid positive integer: " + text);
  return static_cast<std::size_t>(value);
}

void parse_span_text(const std::string & text, std::size_t & bytes,
                     std::size_t & alignment)
{
  const std::size_t split = text.find('x');
  if(split == std::string::npos || split == 0 || split + 1 == text.size())
    throw ParseError("invalid object span: " + text);
  bytes = parse_positive_size(text.substr(0, split));
  alignment = parse_positive_size(text.substr(split + 1));
  if(!is_power_of_two(alignment))
    throw ParseError("object alignment is not a power of two");
}

bool is_basic_type(const std::string & text)
{
  static const char * const names[] = {
    "void", "i1", "i8", "u8", "i16", "u16", "i32", "u32",
    "i64", "f32", "f64", "f80", "ptr"
  };
  for(std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    if(text == names[i]) return true;
  return false;
}

void validate_type_text(const std::string & text)
{
  if(is_basic_type(text)) return;
  if(text.size() > 6 && text.compare(0, 4, "obj<") == 0 && text.back() == '>') {
    std::size_t bytes = 0;
    std::size_t alignment = 0;
    parse_span_text(text.substr(4, text.size() - 5), bytes, alignment);
    if(alignment > bytes) throw ParseError("object alignment exceeds size");
    return;
  }
  throw ParseError("unknown LowIR type: " + text);
}

std::size_t integer_width(const LowType & type)
{
  if(type.text == "i1") return 1;
  if(type.text == "i8" || type.text == "u8") return 8;
  if(type.text == "i16" || type.text == "u16") return 16;
  if(type.text == "i32" || type.text == "u32") return 32;
  if(type.text == "i64") return 64;
  return 0;
}

std::size_t float_width(const LowType & type)
{
  if(type.text == "f32") return 32;
  if(type.text == "f64") return 64;
  if(type.text == "f80") return 80;
  return 0;
}

typedef std::vector<std::pair<std::string, std::string> > Metadata;

class Parser
{
public:
  explicit Parser(const std::vector<Token> & tokens) : tokens_(tokens), at_(0) {}

  Program Parse()
  {
    Program program;
    while(!done()) parse_top_level(program);
    return program;
  }

private:
  const std::vector<Token> & tokens_;
  std::size_t at_;

  bool done() const { return at_ == tokens_.size(); }

  const std::string & peek(std::size_t offset = 0) const
  {
    if(at_ + offset >= tokens_.size()) throw ParseError("unexpected end of LowIR");
    return tokens_[at_ + offset].text;
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
    if(!accept(text)) throw ParseError("expected '" + text + "', got '" + peek() + "'");
  }

  std::string named(char sigil, const char * description)
  {
    const std::string text = take();
    if(!starts_with(text, sigil)) throw ParseError(std::string("expected ") + description);
    return text;
  }

  LowType type()
  {
    LowType result;
    result.text = take();
    validate_type_text(result.text);
    return result;
  }

  std::string signed_literal()
  {
    std::string result;
    if(accept("-")) result = "-";
    const std::string value = take();
    if(value.empty() || is_punctuation(value[0])) throw ParseError("expected literal");
    return result + value;
  }

  Operand operand()
  {
    Operand result;
    if(accept("-")) {
      result.kind = Operand::OP_INTEGER;
      result.text = "-" + take();
      return result;
    }
    result.text = take();
    if(starts_with(result.text, '%')) result.kind = Operand::OP_TEMP;
    else if(starts_with(result.text, '$')) result.kind = Operand::OP_SLOT;
    else if(starts_with(result.text, '@')) result.kind = Operand::OP_GLOBAL;
    else if(starts_with(result.text, '^')) result.kind = Operand::OP_LABEL;
    else if(result.text.find_first_of(".eEpP") != std::string::npos ||
            (!result.text.empty() && (result.text.back() == 'f' || result.text.back() == 'L')))
      result.kind = Operand::OP_FLOAT;
    else result.kind = Operand::OP_INTEGER;
    return result;
  }

  Metadata metadata()
  {
    Metadata result;
    if(!accept("[")) return result;
    std::unordered_set<std::string> keys;
    do {
      const std::string key = take();
      expect("=");
      const std::string value = take();
      if(!keys.insert(key).second) throw ParseError("duplicate metadata key: " + key);
      result.push_back(std::make_pair(key, value));
    } while(accept(","));
    expect("]");
    return result;
  }

  InstructionDebugLocation debug_location()
  {
    InstructionDebugLocation result;
    if(!accept("!dbg")) return result;
    expect("(");
    result.file = take();
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
      if(key == "arity" || key == "effects" || key == "unwind" || key == "return") {
        if(!boundary) throw ParseError("function metadata on non-function");
        apply_boundary_item(key, value, *boundary);
      } else if(call_signature) {
        throw ParseError("symbol metadata on call signature");
      } else if(key == "storage") {
        if(!storage) throw ParseError("storage metadata on non-global");
        *storage = parse_storage(value);
      } else {
        apply_symbol_item(key, value, symbol);
      }
    }
  }

  void apply_boundary_item(const std::string & key, const std::string & value,
                           FunctionBoundaryMetadata & out)
  {
    if(key == "arity") {
      if(value == "fixed") out.arity = CAM_FIXED;
      else if(value == "variadic") out.arity = CAM_VARIADIC;
      else if(value == "prototype_relaxed") out.arity = CAM_PROTOTYPE_RELAXED;
      else throw ParseError("invalid arity metadata");
    } else if(key == "effects") {
      if(value == "readnone") out.effects = CFXM_READNONE;
      else if(value == "readonly") out.effects = CFXM_READONLY;
      else if(value == "readwrite") out.effects = CFXM_READWRITE;
      else throw ParseError("invalid effects metadata");
    } else if(key == "unwind") {
      if(value == "may") out.unwind = CUM_MAY;
      else if(value == "no") out.unwind = CUM_NO;
      else throw ParseError("invalid unwind metadata");
    } else if(key == "return") {
      if(value == "returns") out.returns = CRM_RETURNS;
      else if(value == "noreturn") out.returns = CRM_NORETURN;
      else throw ParseError("invalid return metadata");
    }
  }

  void apply_symbol_item(const std::string & key, const std::string & value,
                         SymbolMetadata & out)
  {
    if(key == "role") out.role = parse_role(value);
    else if(key == "linkage") {
      if(value == "c") out.linkage = LLM_C;
      else if(value == "cpp") out.linkage = LLM_CPP;
      else throw ParseError("invalid linkage metadata");
    } else if(key == "binding") {
      if(value == "internal") out.binding = SBM_INTERNAL;
      else if(value == "strong") out.binding = SBM_STRONG;
      else if(value == "weak") out.binding = SBM_WEAK;
      else throw ParseError("invalid binding metadata");
    } else if(key == "object") out.object_symbol = value;
    else if(key == "tls_for") out.tls_for_symbol = value;
    else if(key == "keep_alias") out.keep_internal_alias = yes_no(value);
    else if(key == "prefer_local") out.prefer_local_object_binding = yes_no(value);
    else if(key == "trivial_lifecycle") out.object_trivial_lifecycle = yes_no(value);
    else if(key == "force_inline") out.force_inline = yes_no(value);
    else throw ParseError("unknown symbol metadata: " + key);
  }

  bool yes_no(const std::string & value)
  {
    if(value == "yes") return true;
    if(value == "no") return false;
    throw ParseError("metadata flag must be yes or no");
  }

  GlobalStorageMode parse_storage(const std::string & value)
  {
    if(value == "writable") return GSM_WRITABLE;
    if(value == "readonly") return GSM_READONLY;
    if(value == "thread_local") return GSM_THREAD_LOCAL;
    throw ParseError("invalid global storage metadata");
  }

  SymbolRole parse_role(const std::string & value)
  {
    static const std::pair<const char *, SymbolRole> roles[] = {
      {"entry", SR_ENTRY}, {"init", SR_INIT}, {"fini", SR_FINI},
      {"eh_top", SR_EH_TOP}, {"eh_value", SR_EH_VALUE}, {"eh_type", SR_EH_TYPE},
      {"eh_unhandled", SR_EH_UNHANDLED}, {"eh_allocate_exception", SR_EH_ALLOCATE_EXCEPTION},
      {"eh_begin_catch", SR_EH_BEGIN_CATCH}, {"eh_call_unexpected", SR_EH_CALL_UNEXPECTED},
      {"eh_current_exception_type", SR_EH_CURRENT_EXCEPTION_TYPE},
      {"eh_end_catch", SR_EH_END_CATCH}, {"eh_rethrow", SR_EH_RETHROW},
      {"eh_throw", SR_EH_THROW}, {"eh_personality", SR_EH_PERSONALITY},
      {"eh_resume", SR_EH_RESUME}
    };
    for(std::size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
      if(value == roles[i].first) return roles[i].second;
    throw ParseError("invalid symbol role");
  }

  Parameter parameter()
  {
    Parameter result;
    result.name = named('%', "parameter name");
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
      if(value == "direct") out.passing = PPM_DIRECT;
      else if(value == "indirect_result") out.passing = PPM_INDIRECT_RESULT;
      else if(value == "by_address") out.passing = PPM_BY_ADDRESS;
      else if(value == "reference") out.passing = PPM_REFERENCE;
      else if(value == "decay") out.passing = PPM_DECAY;
      else throw ParseError("invalid parameter pass metadata");
    } else if(key == "capture") {
      if(value == "nocapture") out.capture = PCM_NOCAPTURE;
      else if(value == "maycapture") out.capture = PCM_MAYCAPTURE;
      else throw ParseError("invalid parameter capture metadata");
    } else if(key == "access") {
      if(value == "none") out.access = PAM_NONE;
      else if(value == "read") out.access = PAM_READ;
      else if(value == "write") out.access = PAM_WRITE;
      else if(value == "readwrite") out.access = PAM_READWRITE;
      else throw ParseError("invalid parameter access metadata");
    } else if(key == "alias" && value == "noalias") out.alias = PALM_NOALIAS;
    else throw ParseError("invalid parameter metadata");
  }

  void parse_top_level(Program & program)
  {
    if(accept("declare")) {
      if(accept("global")) parse_global_declaration(program);
      else if(accept("function")) parse_function_declaration(program);
      else throw ParseError("expected declaration kind");
    } else if(accept("global")) parse_global_definition(program);
    else if(accept("function")) parse_function_definition(program);
    else if(accept("alias")) parse_object_alias(program);
    else throw ParseError("expected top-level LowIR item");
  }

  void parse_global_declaration(Program & program)
  {
    GlobalDeclaration result;
    result.name = named('@', "global name");
    if(accept("readonly")) result.storage = GSM_READONLY;
    else if(accept("thread_local")) result.storage = GSM_THREAD_LOCAL;
    if(accept(":")) {
      result.has_type = true;
      result.type = type();
    }
    apply_symbol_metadata(metadata(), result.metadata, 0, &result.storage, false);
    program.global_declarations.push_back(result);
  }

  void parse_function_declaration(Program & program)
  {
    FunctionDeclaration result;
    result.name = named('@', "function name");
    expect("(");
    result.params = parameter_list();
    expect(")");
    expect("->");
    result.return_type = type();
    apply_symbol_metadata(metadata(), result.metadata, &result.boundary, 0, false);
    program.function_declarations.push_back(result);
  }

  void parse_global_definition(Program & program)
  {
    GlobalDefinition result;
    result.name = named('@', "global name");
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
    if(accept("}")) throw ParseError("structured global must contain data");
    while(!accept("}")) {
      GlobalDefinition::DataItem item;
      if(accept("zero")) {
        item.kind = GlobalDefinition::DataItem::ITEM_ZERO;
        item.zero_bytes = parse_positive_size(signed_literal());
      } else {
        item.type = type();
        if(item.type.text == "ptr" && accept("addr")) {
          item.kind = GlobalDefinition::DataItem::ITEM_ADDR;
          item.symbol = named('@', "address initializer symbol");
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
    if(result.type.text.empty()) throw ParseError("scalar global requires type");
    if(accept("zero")) result.init_kind = GlobalDefinition::INIT_ZERO;
    else if(accept("addr")) {
      result.init_kind = GlobalDefinition::INIT_ADDR;
      result.init_operand.text = named('@', "address initializer symbol");
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
    if(errno || !end || *end) throw ParseError("invalid address addend");
    return sign * value;
  }

  void parse_object_alias(Program & program)
  {
    expect("object");
    ObjectAlias result;
    result.object_symbol = take();
    if(result.object_symbol.empty() || is_punctuation(result.object_symbol[0]))
      throw ParseError("invalid object alias spelling");
    expect("=");
    result.target = named('@', "object alias target");
    program.object_aliases.push_back(result);
  }

  void parse_function_definition(Program & program)
  {
    Function result;
    result.name = named('@', "function name");
    expect("(");
    result.params = parameter_list();
    expect(")");
    expect("->");
    result.return_type = type();
    apply_symbol_metadata(metadata(), result.metadata, &result.boundary, 0, false);
    result.debug_location = debug_location();
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
        if(block) throw ParseError("slot declaration after first block");
        const std::string name = named('$', "slot name");
        expect(":");
        function.slots.push_back(std::make_pair(name, type()));
      } else if(accept("block")) {
        if(block && !terminated) throw ParseError("block has no terminator");
        function.blocks.push_back(Block());
        block = &function.blocks.back();
        block->label = named('^', "block name");
        expect(":");
        terminated = false;
      } else {
        if(!block) throw ParseError("instruction outside block");
        if(terminated) throw ParseError("instruction after terminator");
        block->instructions.push_back(instruction());
        terminated = is_terminator(block->instructions.back().kind);
      }
    }
    if(block && !terminated) throw ParseError("block has no terminator");
  }

  bool is_terminator(Instruction::Kind kind) const
  {
    return kind == Instruction::IK_JUMP || kind == Instruction::IK_BRANCH ||
           kind == Instruction::IK_SWITCH || kind == Instruction::IK_RETURN ||
           kind == Instruction::IK_THROW || kind == Instruction::IK_RESUME;
  }

  Instruction instruction()
  {
    Instruction result;
    if(starts_with(peek(), '%') && peek(1) == "=") {
      result.dest = take();
      expect("=");
      parse_rvalue(result);
    } else parse_void_instruction(result);
    result.debug_location = debug_location();
    return result;
  }

  void parse_rvalue(Instruction & out)
  {
    const std::string op = take();
    if(op == "const") parse_typed_unary(out, Instruction::IK_CONST);
    else if(op == "copy") parse_typed_unary(out, Instruction::IK_COPY);
    else if(op == "addr") {
      out.kind = Instruction::IK_ADDR;
      out.first = operand();
    } else if(op == "load") parse_typed_unary(out, Instruction::IK_LOAD);
    else if(op == "atomic_load") {
      parse_typed_unary(out, Instruction::IK_ATOMIC_LOAD);
      expect(","); out.args.push_back(operand());
    } else if(op == "index") parse_index(out);
    else if(op == "unary") parse_unary(out);
    else if(op == "binary") parse_binary(out, Instruction::IK_BINARY);
    else if(op == "cmp") parse_binary(out, Instruction::IK_CMP);
    else if(op == "convert") parse_convert(out);
    else if(op == "atomic_add_fetch") parse_atomic_three(out, Instruction::IK_ATOMIC_ADD_FETCH, 1);
    else if(op == "atomic_exchange") parse_atomic_three(out, Instruction::IK_ATOMIC_EXCHANGE, 1);
    else if(op == "atomic_compare_exchange") parse_atomic_compare_exchange(out);
    else if(op == "call") parse_call(out, false);
    else if(op == "exception") {
      out.kind = Instruction::IK_EXCEPTION; out.type = type();
    } else if(op == "exception_selector") {
      out.kind = Instruction::IK_EXCEPTION_SELECTOR; out.type = type();
    } else throw ParseError("unknown rvalue instruction: " + op);
  }

  void parse_typed_unary(Instruction & out, Instruction::Kind kind)
  {
    out.kind = kind;
    out.type = type();
    out.first = operand();
  }

  void parse_index(Instruction & out)
  {
    out.kind = Instruction::IK_INDEX;
    out.type = type();
    if(peek() == "[") {
      const Metadata items = metadata();
      if(items.size() != 1 || items[0].first != "projection")
        throw ParseError("invalid index metadata");
      const std::string & value = items[0].second;
      if(value == "array_element") out.index_projection = IPK_ARRAY_ELEMENT;
      else if(value == "field") out.index_projection = IPK_FIELD;
      else if(value == "base_subobject") out.index_projection = IPK_BASE_SUBOBJECT;
      else if(value == "reference_field") out.index_projection = IPK_REFERENCE_FIELD;
      else throw ParseError("invalid index projection");
    }
    out.first = operand();
    expect(",");
    out.second = operand();
  }

  void parse_unary(Instruction & out)
  {
    out.kind = Instruction::IK_UNARY;
    out.op = take();
    out.type = type();
    out.first = operand();
  }

  void parse_binary(Instruction & out, Instruction::Kind kind)
  {
    out.kind = kind;
    out.op = take();
    out.type = type();
    out.first = operand();
    expect(",");
    out.second = operand();
  }

  void parse_convert(Instruction & out)
  {
    out.kind = Instruction::IK_CONVERT;
    out.op = take();
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
    out.type.text = returns_void ? "void" : type().text;
    out.first = operand();
    expect("(");
    if(!accept(")")) {
      do out.args.push_back(operand()); while(accept(","));
      expect(")");
    }
    if(accept("as")) {
      out.has_call_signature = true;
      expect("("); out.call_params = parameter_list(); expect(")");
      expect("->"); out.call_return_type = type();
      SymbolMetadata unused;
      apply_symbol_metadata(metadata(), unused, &out.call_boundary, 0, true);
    }
  }

  void parse_void_instruction(Instruction & out)
  {
    const std::string op = take();
    if(op == "store") {
      out.kind = Instruction::IK_STORE; out.type = type(); out.first = operand();
      expect(","); out.second = operand();
    } else if(op == "atomic_store") {
      out.kind = Instruction::IK_ATOMIC_STORE; out.type = type(); out.first = operand();
      expect(","); out.second = operand(); expect(","); out.args.push_back(operand());
    } else if(op == "atomic_thread_fence" || op == "atomic_signal_fence") {
      out.kind = op == "atomic_thread_fence" ? Instruction::IK_ATOMIC_THREAD_FENCE :
                 Instruction::IK_ATOMIC_SIGNAL_FENCE;
      out.first = operand();
    } else if(op == "call") {
      expect("void"); parse_call(out, true);
    } else if(op == "copyobj" || op == "zeroinit") parse_bulk(out, op);
    else if(op == "eh_try" || op == "eh_cleanup") {
      out.kind = op == "eh_try" ? Instruction::IK_EH_TRY : Instruction::IK_EH_CLEANUP;
      out.first = operand();
    } else if(op == "eh_end") out.kind = Instruction::IK_EH_END;
    else if(op == "throw") {
      out.kind = Instruction::IK_THROW; out.type = type(); out.first = operand();
    } else if(op == "resume") out.kind = Instruction::IK_RESUME;
    else if(op == "jump") { out.kind = Instruction::IK_JUMP; out.first = operand(); }
    else if(op == "branch") parse_branch(out);
    else if(op == "switch") parse_switch(out);
    else if(op == "return") parse_return(out);
    else throw ParseError("unknown instruction: " + op);
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
    if(out.type.text != "void") out.first = operand();
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
  explicit Validator(const Program & program) : program_(program) {}

  void Validate()
  {
    index_top_level();
    validate_global_initializers();
    validate_roles_and_tls();
    validate_aliases();
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i)
      validate_parameters(program_.function_declarations[i].params,
                          program_.function_declarations[i].return_type);
    for(std::size_t i = 0; i < program_.functions.size(); ++i)
      validate_function(program_.functions[i]);
  }

private:
  const Program & program_;
  std::unordered_set<std::string> top_symbols_;
  std::unordered_set<std::string> globals_;
  std::unordered_map<std::string, GlobalStorageMode> global_storage_;
  std::unordered_map<std::string, FunctionInfo> functions_;

  void add_top(const std::string & name)
  {
    if(!top_symbols_.insert(name).second)
      throw ParseError("duplicate top-level symbol: " + name);
  }

  void index_top_level()
  {
    for(std::size_t i = 0; i < program_.global_declarations.size(); ++i) {
      const GlobalDeclaration & item = program_.global_declarations[i];
      add_top(item.name); globals_.insert(item.name); global_storage_[item.name] = item.storage;
      validate_global_role(item.metadata.role);
    }
    for(std::size_t i = 0; i < program_.globals.size(); ++i) {
      const GlobalDefinition & item = program_.globals[i];
      add_top(item.name); globals_.insert(item.name); global_storage_[item.name] = item.storage;
      validate_global_role(item.metadata.role);
    }
    for(std::size_t i = 0; i < program_.function_declarations.size(); ++i) {
      const FunctionDeclaration & item = program_.function_declarations[i];
      add_top(item.name);
      functions_[item.name] = FunctionInfo{&item.params, &item.return_type, &item.boundary};
      validate_function_role(item.metadata.role);
    }
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      const Function & item = program_.functions[i];
      add_top(item.name);
      functions_[item.name] = FunctionInfo{&item.params, &item.return_type, &item.boundary};
      validate_function_role(item.metadata.role);
    }
  }

  void validate_global_role(SymbolRole role)
  {
    if(role != SR_NONE && role != SR_EH_TOP && role != SR_EH_VALUE && role != SR_EH_TYPE)
      throw ParseError("function role on global");
  }

  void validate_function_role(SymbolRole role)
  {
    if(role == SR_EH_TOP || role == SR_EH_VALUE || role == SR_EH_TYPE)
      throw ParseError("global role on function");
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

  void validate_global_initializers()
  {
    for(std::size_t i = 0; i < program_.globals.size(); ++i) {
      const GlobalDefinition & global = program_.globals[i];
      if(global.structured) {
        for(std::size_t j = 0; j < global.data_items.size(); ++j) {
          const GlobalDefinition::DataItem & item = global.data_items[j];
          if(item.kind == GlobalDefinition::DataItem::ITEM_ADDR &&
             !top_symbols_.count(item.symbol))
            throw ParseError("undefined structured global address target");
        }
      } else if(global.init_kind == GlobalDefinition::INIT_ADDR &&
                !top_symbols_.count(global.init_operand.text)) {
        throw ParseError("undefined global address initializer target");
      }
    }
  }

  void validate_symbol_facts(const SymbolMetadata & metadata,
                             std::unordered_set<int> & roles,
                             std::unordered_set<std::string> & tls_targets)
  {
    if(metadata.role != SR_NONE && !roles.insert(static_cast<int>(metadata.role)).second)
      throw ParseError("duplicate singleton role");
    if(!metadata.tls_for_symbol.empty()) {
      const std::string & target = metadata.tls_for_symbol;
      if(!globals_.count(target) || global_storage_[target] != GSM_THREAD_LOCAL)
        throw ParseError("tls_for target is not thread-local global");
      if(!tls_targets.insert(target).second) throw ParseError("duplicate tls wrapper");
    }
  }

  void validate_aliases()
  {
    std::unordered_set<std::string> aliases;
    for(std::size_t i = 0; i < program_.object_aliases.size(); ++i) {
      const ObjectAlias & alias = program_.object_aliases[i];
      if(!aliases.insert(alias.object_symbol).second) throw ParseError("duplicate object alias");
      if(!top_symbols_.count(alias.target)) throw ParseError("undefined object alias target");
    }
  }

  void validate_parameters(const std::vector<Parameter> & params,
                           const LowType & result)
  {
    std::unordered_set<std::string> names;
    for(std::size_t i = 0; i < params.size(); ++i) {
      const Parameter & param = params[i];
      if(!names.insert(param.name).second) throw ParseError("duplicate parameter");
      const bool pointer = param.type.text == "ptr";
      if(param.metadata.passing != PPM_DIRECT && !pointer)
        throw ParseError("non-direct passing requires ptr");
      if(param.metadata.capture != PCM_DEFAULT && !pointer)
        throw ParseError("capture metadata requires ptr");
      if(param.metadata.access != PAM_DEFAULT && !pointer)
        throw ParseError("access metadata requires ptr");
      if(param.metadata.alias != PALM_DEFAULT && !pointer)
        throw ParseError("alias metadata requires ptr");
      if(param.metadata.passing == PPM_INDIRECT_RESULT &&
         (i != 0 || result.text != "void"))
        throw ParseError("invalid indirect result parameter");
    }
  }

  void validate_function(const Function & function)
  {
    validate_parameters(function.params, function.return_type);
    if(function.blocks.empty()) throw ParseError("function has no blocks");
    std::unordered_map<std::string, LowType> values;
    std::unordered_map<std::string, LowType> slots;
    std::unordered_set<std::string> blocks;
    for(std::size_t i = 0; i < function.params.size(); ++i)
      values[function.params[i].name] = function.params[i].type;
    for(std::size_t i = 0; i < function.slots.size(); ++i) {
      if(!slots.emplace(function.slots[i].first, function.slots[i].second).second)
        throw ParseError("duplicate slot");
    }
    for(std::size_t i = 0; i < function.blocks.size(); ++i)
      if(!blocks.insert(function.blocks[i].label).second) throw ParseError("duplicate block");
    for(std::size_t i = 0; i < function.blocks.size(); ++i)
      validate_block(function, function.blocks[i], values, slots, blocks);
  }

  void validate_block(const Function & function, const Block & block,
                      std::unordered_map<std::string, LowType> & values,
                      const std::unordered_map<std::string, LowType> & slots,
                      const std::unordered_set<std::string> & blocks)
  {
    if(block.instructions.empty()) throw ParseError("empty block");
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const Instruction & ins = block.instructions[i];
      validate_instruction(function, ins, values, slots, blocks);
      if(!ins.dest.empty()) {
        if(values.count(ins.dest)) throw ParseError("duplicate temporary definition");
        values[ins.dest] = result_type(ins);
      }
    }
  }

  void validate_operand(const Operand & operand,
                        const std::unordered_map<std::string, LowType> & values,
                        const std::unordered_map<std::string, LowType> & slots,
                        bool allow_label = false) const
  {
    if(operand.kind == Operand::OP_TEMP && !values.count(operand.text))
      throw ParseError("undefined temporary: " + operand.text);
    if(operand.kind == Operand::OP_SLOT && !slots.count(operand.text))
      throw ParseError("undefined slot: " + operand.text);
    if(operand.kind == Operand::OP_GLOBAL && !top_symbols_.count(operand.text))
      throw ParseError("undefined top-level symbol: " + operand.text);
    if(operand.kind == Operand::OP_LABEL && !allow_label)
      throw ParseError("block label used as value");
  }

  void validate_target(const Operand & target, const std::unordered_set<std::string> & blocks) const
  {
    if(target.kind != Operand::OP_LABEL || !blocks.count(target.text))
      throw ParseError("undefined or invalid block target");
  }

  void validate_instruction(const Function & function, const Instruction & ins,
                            const std::unordered_map<std::string, LowType> & values,
                            const std::unordered_map<std::string, LowType> & slots,
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
    if(kind == Instruction::IK_RETURN && ins.type.text != function.return_type.text)
      throw ParseError("return type does not match function");
    if((kind == Instruction::IK_EH_TRY || kind == Instruction::IK_EH_CLEANUP))
      validate_target(ins.first, blocks);
  }

  void validate_general_operands(const Instruction & ins,
                                 const std::unordered_map<std::string, LowType> & values,
                                 const std::unordered_map<std::string, LowType> & slots)
  {
    const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
    for(std::size_t i = 0; i < 3; ++i)
      if(!operands[i]->text.empty()) validate_operand(*operands[i], values, slots,
        ins.kind == Instruction::IK_EH_TRY || ins.kind == Instruction::IK_EH_CLEANUP);
    for(std::size_t i = 0; i < ins.args.size(); ++i)
      validate_operand(ins.args[i], values, slots);
  }

  void validate_operation_types(const Instruction & ins)
  {
    if(ins.kind == Instruction::IK_UNARY) {
      if(ins.op == "decay" && ins.type.text != "ptr") throw ParseError("decay requires ptr");
      if(ins.op == "bswap" && ins.type.text != "i16" && ins.type.text != "i32" &&
         ins.type.text != "i64") throw ParseError("invalid bswap type");
    }
    if(ins.kind == Instruction::IK_CONVERT) validate_conversion(ins);
    if((ins.kind == Instruction::IK_COPYOBJ || ins.kind == Instruction::IK_ZEROINIT) &&
       (!ins.byte_count || !is_power_of_two(ins.byte_alignment)))
      throw ParseError("invalid bulk-memory span");
  }

  void validate_conversion(const Instruction & ins)
  {
    const std::size_t dst_i = integer_width(ins.type);
    const std::size_t src_i = integer_width(ins.source_type);
    const std::size_t dst_f = float_width(ins.type);
    const std::size_t src_f = float_width(ins.source_type);
    if((ins.op == "sext" || ins.op == "zext") && dst_i && src_i && dst_i > src_i) return;
    if(ins.op == "trunc" && dst_i && src_i && dst_i < src_i) return;
    if((ins.op == "sitofp" || ins.op == "uitofp") && dst_f && src_i) return;
    if((ins.op == "fptosi" || ins.op == "fptoui") && dst_i && src_f) return;
    if(ins.op == "fpext" && dst_f && src_f && dst_f > src_f) return;
    if(ins.op == "fptrunc" && dst_f && src_f && dst_f < src_f) return;
    throw ParseError("invalid conversion widths or categories");
  }

  void validate_call(const Instruction & ins,
                     const std::unordered_map<std::string, LowType> & values,
                     const std::unordered_map<std::string, LowType> & slots)
  {
    validate_operand(ins.first, values, slots);
    for(std::size_t i = 0; i < ins.args.size(); ++i) validate_operand(ins.args[i], values, slots);
    const std::unordered_map<std::string, FunctionInfo>::const_iterator found =
      functions_.find(ins.first.text);
    const bool direct = ins.first.kind == Operand::OP_GLOBAL && found != functions_.end();
    if(!direct && !ins.has_call_signature) throw ParseError("indirect call requires signature");
    if(ins.has_call_signature) {
      validate_parameters(ins.call_params, ins.call_return_type);
      if(ins.call_return_type.text != ins.type.text) throw ParseError("call signature return mismatch");
      validate_arity(ins.args.size(), ins.call_params.size(), ins.call_boundary.arity);
    } else {
      if(found->second.result->text != ins.type.text) throw ParseError("direct call return mismatch");
      validate_arity(ins.args.size(), found->second.params->size(), found->second.boundary->arity);
    }
  }

  void validate_arity(std::size_t actual, std::size_t fixed, CallArityMode mode)
  {
    if((mode == CAM_FIXED && actual != fixed) || (mode != CAM_FIXED && actual < fixed))
      throw ParseError("call arity mismatch");
  }

  LowType result_type(const Instruction & ins) const
  {
    LowType result = ins.type;
    if(ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX) result.text = "ptr";
    if(ins.kind == Instruction::IK_CMP || ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
       ins.kind == Instruction::IK_EXCEPTION_SELECTOR) result.text = "i64";
    return result;
  }
};

std::string read_file(const std::string & path)
{
  std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
  if(!input) throw ParseError("unable to open LowIR source: " + path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

Program parse_tokens(const std::vector<Token> & tokens)
{
  Parser parser(tokens);
  Program program = parser.Parse();
  Validator(program).Validate();
  program.token_count = tokens.size();
  return program;
}

}  // namespace

LowirProgram parse_lowir_program_text(const std::string & text,
                                      const std::string & source_name)
{
  std::vector<Token> tokens;
  lex_text(text, source_name, tokens);
  Program program = parse_tokens(tokens);
  program.source_bytes = text.size();
  return program;
}

LowirProgram parse_lowir_program_files(const std::vector<std::string> & paths)
{
  if(paths.empty()) throw ParseError("no LowIR source files");
  std::vector<Token> tokens;
  std::size_t source_bytes = 0;
  for(std::size_t i = 0; i < paths.size(); ++i) {
    const std::string text = read_file(paths[i]);
    source_bytes += text.size();
    lex_text(text, paths[i], tokens);
  }
  Program program = parse_tokens(tokens);
  program.source_bytes = source_bytes;
  return program;
}

}  // namespace lowir_model
