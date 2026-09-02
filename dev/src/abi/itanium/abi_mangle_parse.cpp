#include "abi/itanium/abi_mangle.h"
#include "support/exception_types.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace abi_mangle {
namespace {
using std::size_t;
using std::string;
using std::vector;

enum AbiFactErrorCode : std::uint16_t
{
  ABI_FACT_INVALID_RECORD = 1,
  ABI_FACT_INVALID_NUMBER = 2,
  ABI_FACT_NUMBER_OUT_OF_RANGE = 3
};

__attribute__((cold, noinline, noreturn))
void throw_fact_error(const string & message,
                      AbiFactErrorCode code = ABI_FACT_INVALID_RECORD)
{
  throw SerializedInputError(SerializedInputFormat::ABI_FACT,
                             message, code);
}

__attribute__((cold, noinline, noreturn))
void throw_fact_line_error(size_t line_number,
                           const SerializedInputError & error)
{
  throw SerializedInputError(SerializedInputFormat::ABI_FACT,
    "ABI fact line " + std::to_string(line_number) + ": " + error.what(),
    error.Code());
}

__attribute__((cold, noinline, noreturn))
void throw_abi_internal(const string & message)
{
  throw InternalCompilerError(message, CompilerErrorDomain::ABI);
}

string trim(const string & input)
{
  size_t first = 0;
  while(first != input.size()
        && std::isspace(static_cast<unsigned char>(input[first]))) {
    ++first;
  }
  size_t last = input.size();
  while(last != first
        && std::isspace(static_cast<unsigned char>(input[last - 1]))) {
    --last;
  }
  return input.substr(first, last - first);
}

vector<string> split_words(const string & line)
{
  std::istringstream in(line);
  vector<string> words;
  string word;
  while(in >> word) {
    words.push_back(word);
  }
  return words;
}

void append_reference_names(AbiReferenceList * references,
                            const vector<string> & words, size_t begin)
{
  for(size_t i = begin; i < words.size(); ++i)
    references->push_name(words[i]);
}

void require(bool condition, const string & message)
{
  if(!condition) throw_fact_error(message);
}

size_t parse_index(const string & word)
{
  require(!word.empty(), "empty ABI index");
  size_t value = 0;
  for(char ch : word) {
    require(ch >= '0' && ch <= '9', "invalid ABI index '" + word + "'");
    const unsigned digit = static_cast<unsigned>(ch - '0');
    require(value <= (std::numeric_limits<size_t>::max() - digit) / 10,
            "ABI index out of range '" + word + "'");
    value = value * 10 + digit;
  }
  return value;
}

long long parse_signed(const string & word)
{
  size_t used = 0;
  long long value = 0;
  try {
    value = std::stoll(word, &used, 10);
  } catch(const std::invalid_argument &) {
    throw_fact_error("invalid signed ABI value '" + word + "'",
                     ABI_FACT_INVALID_NUMBER);
  } catch(const std::out_of_range &) {
    throw_fact_error("signed ABI value out of range '" + word + "'",
                     ABI_FACT_NUMBER_OUT_OF_RANGE);
  }
  require(used == word.size(), "invalid signed ABI value '" + word + "'");
  return value;
}

unsigned long long parse_unsigned(const string & word)
{
  require(word.empty() || word[0] != '-', "negative unsigned ABI value");
  size_t used = 0;
  unsigned long long value = 0;
  try {
    value = std::stoull(word, &used, 10);
  } catch(const std::invalid_argument &) {
    throw_fact_error("invalid unsigned ABI value '" + word + "'",
                     ABI_FACT_INVALID_NUMBER);
  } catch(const std::out_of_range &) {
    throw_fact_error("unsigned ABI value out of range '" + word + "'",
                     ABI_FACT_NUMBER_OUT_OF_RANGE);
  }
  require(used == word.size(), "invalid unsigned ABI value '" + word + "'");
  return value;
}

bool parse_yes_no(const string & word)
{
  require(word == "yes" || word == "no" || word == "true" || word == "false"
          || word == "1" || word == "0",
          "expected boolean ABI word, got '" + word + "'");
  return word == "yes" || word == "true" || word == "1";
}

bool has_prefix(const string & word, size_t begin, size_t end,
                const char * prefix, size_t prefix_size)
{
  return end - begin >= prefix_size
         && word.compare(begin, prefix_size, prefix) == 0;
}

size_t member_pointer_separator(const string & word, size_t begin, size_t end)
{
  for(size_t i = begin; i < end; ++i) {
    if(word[i] != ':') {
      continue;
    }
    if((i != begin && word[i - 1] == ':')
       || (i + 1 != end && word[i + 1] == ':')) {
      continue;
    }
    return i;
  }
  throw_fact_error("invalid compact member-pointer type '"
                   + word.substr(begin, end - begin) + "'");
}

AbiType compact_type_range(const string & word, size_t begin, size_t end)
{
  vector<AbiTypeModifier> wrappers;
  for(;;) {
    if(has_prefix(word, begin, end, "ptr:", 4)) {
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_POINTER;
      wrappers.push_back(modifier);
      begin += 4;
    } else if(has_prefix(word, begin, end, "ref:", 4)) {
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_LVALUE_REFERENCE;
      wrappers.push_back(modifier);
      begin += 4;
    } else if(has_prefix(word, begin, end, "rref:", 5)) {
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_RVALUE_REFERENCE;
      wrappers.push_back(modifier);
      begin += 5;
    } else if(has_prefix(word, begin, end, "pack:", 5)) {
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_PACK_EXPANSION;
      wrappers.push_back(modifier);
      begin += 5;
    } else if(has_prefix(word, begin, end, "const:", 6)) {
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_CV;
      modifier.is_const = true;
      wrappers.push_back(modifier);
      begin += 6;
    } else if(has_prefix(word, begin, end, "volatile:", 9)) {
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_CV;
      modifier.is_volatile = true;
      wrappers.push_back(modifier);
      begin += 9;
    } else if(has_prefix(word, begin, end, "array:", 6)) {
      const size_t separator = word.find(':', begin + 6);
      require(separator != string::npos && separator < end,
              "invalid compact array type '" + word.substr(begin, end - begin) + "'");
      require(separator != begin + 6, "missing array bound");
      AbiTypeModifier modifier;
      modifier.kind = ABI_TYPE_ARRAY;
      modifier.array_bound.kind = ABI_ARRAY_BOUND_VALUE;
      modifier.array_bound.value = word.substr(begin + 6, separator - begin - 6);
      wrappers.push_back(modifier);
      begin = separator + 1;
    } else {
      break;
    }
    require(begin < end, "missing compact ABI child type");
  }

  AbiType type;
  if(has_prefix(word, begin, end, "named:", 6)) {
    type.kind = ABI_TYPE_NAMED;
    type.name = word.substr(begin + 6, end - begin - 6);
    require(!type.name.empty(), "empty named ABI type");
  } else if(has_prefix(word, begin, end, "memberptr:", 10)) {
    const size_t member_begin = begin + 10;
    const size_t separator = member_pointer_separator(word, member_begin, end);
    type.kind = ABI_TYPE_MEMBER_POINTER;
    type.types.push_back(compact_type_range(word, member_begin, separator));
    type.types.push_back(compact_type_range(word, separator + 1, end));
  } else {
    type.kind = ABI_TYPE_NAME_OR_REFERENCE;
    type.name = word.substr(begin, end - begin);
    require(!type.name.empty(), "empty ABI type");
  }

  type.modifiers.swap(wrappers);
  return type;
}

AbiType compact_type(const string & word)
{
  return compact_type_range(word, 0, word.size());
}

AbiType parse_type(const vector<string> & words, size_t begin)
{
  require(begin < words.size(), "missing ABI type");
  const string & form = words[begin];
  if(form == "name" || form == "named") {
    require(begin + 2 == words.size(), "named type takes one name");
    AbiType type;
    type.kind = ABI_TYPE_NAMED;
    type.name = words[begin + 1];
    return type;
  }
  if(form == "template-param" || form == "template-param-subst") {
    require(begin + 2 == words.size(), "template parameter takes one index");
    AbiType type;
    type.kind = ABI_TYPE_TEMPLATE_PARAMETER;
    type.index = parse_index(words[begin + 1]);
    type.substitutable = form == "template-param-subst";
    return type;
  }
  if(form == "ptr" || form == "ref" || form == "rref"
     || form == "pack" || form == "const" || form == "volatile") {
    vector<AbiTypeModifier> modifiers;
    size_t cursor = begin;
    while(cursor < words.size()) {
      const string & wrapper = words[cursor];
      if(wrapper != "ptr" && wrapper != "ref" && wrapper != "rref"
         && wrapper != "pack" && wrapper != "const" && wrapper != "volatile") {
        break;
      }
      AbiTypeModifier modifier;
      if(wrapper == "ptr") modifier.kind = ABI_TYPE_POINTER;
      if(wrapper == "ref") modifier.kind = ABI_TYPE_LVALUE_REFERENCE;
      if(wrapper == "rref") modifier.kind = ABI_TYPE_RVALUE_REFERENCE;
      if(wrapper == "pack") modifier.kind = ABI_TYPE_PACK_EXPANSION;
      if(wrapper == "const" || wrapper == "volatile") {
        modifier.kind = ABI_TYPE_CV;
        modifier.is_const = wrapper == "const";
        modifier.is_volatile = wrapper == "volatile";
      }
      modifiers.push_back(modifier);
      ++cursor;
    }
    require(cursor < words.size(), "missing ABI type after prefix modifier");
    AbiType type = parse_type(words, cursor);
    modifiers.insert(modifiers.end(), type.modifiers.begin(), type.modifiers.end());
    type.modifiers.swap(modifiers);
    return type;
  }
  if(form == "tagged") {
    require(begin + 2 < words.size(), "tagged type needs a type and tag");
    AbiType type;
    type.kind = ABI_TYPE_CV;
    type.types.push_back(compact_type(words[begin + 1]));
    for(size_t i = begin + 2; i < words.size(); ++i)
      type.presentation_names.push_tag_name(words[i]);
    return type;
  }
  if(form == "vendor") {
    require(begin + 2 < words.size(), "vendor qualifier needs a name and type");
    AbiType type;
    type.kind = ABI_TYPE_VENDOR_QUALIFIED;
    type.name = words[begin + 1];
    type.vendor_qualifier = abi_vendor_qualifier_kind(type.name);
    type.types.push_back(parse_type(words, begin + 2));
    return type;
  }
  if(form == "function-type" || form == "function-type-variadic") {
    require(begin + 1 < words.size(), "function type needs a result type");
    AbiType type;
    type.kind = ABI_TYPE_FUNCTION;
    type.variadic = form == "function-type-variadic";
    for(size_t i = begin + 1; i < words.size(); ++i) {
      type.types.push_back(compact_type(words[i]));
    }
    return type;
  }
  if(form == "member-pointer") {
    require(begin + 3 == words.size(), "member-pointer needs owner and member types");
    AbiType type;
    type.kind = ABI_TYPE_MEMBER_POINTER;
    type.types.push_back(compact_type(words[begin + 1]));
    type.types.push_back(compact_type(words[begin + 2]));
    return type;
  }
  if(form == "builtin-transform") {
    require(begin + 2 < words.size(), "builtin transform needs a name and type");
    AbiType type;
    type.kind = ABI_TYPE_BUILTIN_TRANSFORM;
    type.name = words[begin + 1];
    type.types.push_back(parse_type(words, begin + 2));
    return type;
  }
  if(form == "template" || form == "template-param-template") {
    require(begin + 1 < words.size(), "template type needs a name or index");
    AbiType type;
    type.kind = form == "template" ? ABI_TYPE_TEMPLATE_SPECIALIZATION
                                    : ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION;
    if(form == "template") type.name = words[begin + 1];
    else type.index = parse_index(words[begin + 1]);
    append_reference_names(&type.argument_refs, words, begin + 2);
    return type;
  }
  if(form == "std-template") {
    require(begin + 4 < words.size(), "std-template needs code, flag, name, and arguments");
    AbiType type;
    type.kind = ABI_TYPE_STD_TEMPLATE_SPECIALIZATION;
    type.standard_substitution = words[begin + 1];
    type.standard_substitution_code =
      abi_standard_substitution_kind(type.standard_substitution);
    type.standard_substitution_includes_arguments = parse_yes_no(words[begin + 2]);
    type.name = words[begin + 3];
    append_reference_names(&type.argument_refs, words, begin + 4);
    return type;
  }
  if(form == "member" || form == "member-template") {
    require(begin + 2 < words.size(), "member type needs owner and name");
    AbiType type;
    type.kind = form == "member" ? ABI_TYPE_MEMBER
                                  : ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION;
    type.types.push_back(compact_type(words[begin + 1]));
    type.name = words[begin + 2];
    if(form == "member-template") {
      append_reference_names(&type.argument_refs, words, begin + 3);
    } else {
      require(begin + 3 == words.size(), "member type has unexpected operands");
    }
    return type;
  }
  if(form == "decltype") {
    require(begin + 2 == words.size(), "decltype needs one expression");
    AbiType type;
    type.kind = ABI_TYPE_DECLTYPE_EXPRESSION;
    type.expression_ref = words[begin + 1];
    return type;
  }
  if(form == "lambda-closure") {
    require(begin + 3 == words.size(), "lambda-closure has invalid operands");
    AbiType type;
    type.kind = ABI_TYPE_LAMBDA_CLOSURE;
    type.context_ref = words[begin + 1];
    type.discriminator = words[begin + 2];
    return type;
  }
  if(form == "local-type") {
    require(begin + 4 == words.size(), "local-type has invalid operands");
    AbiType type;
    type.kind = ABI_TYPE_LOCAL_TYPE;
    type.context_ref = words[begin + 1];
    type.name = words[begin + 2];
    type.discriminator = words[begin + 3];
    return type;
  }
  if(form == "unnamed-local-type") {
    require(begin + 3 == words.size(), "unnamed-local-type has invalid operands");
    AbiType type;
    type.kind = ABI_TYPE_LOCAL_TYPE;
    type.context_ref = words[begin + 1];
    type.discriminator = words[begin + 2];
    return type;
  }
  if(form == "namespace-lambda") {
    require(begin + 1 < words.size(), "namespace lambda needs a source name");
    AbiType type;
    type.kind = ABI_TYPE_NAMESPACE_LAMBDA;
    type.name = words[begin + 1];
    for(size_t i = begin + 2; i < words.size(); ++i)
      type.presentation_names.push_namespace_name(words[i]);
    return type;
  }
  require(begin + 1 == words.size(), "unexpected operands after compact ABI type");
  return compact_type(form);
}

AbiFunctionTarget parse_function_target(const vector<string> & words, size_t begin)
{
  require(begin < words.size(), "missing function target");
  AbiFunctionTarget target;
  if(words[begin] == "encoding") {
    require(begin + 1 == words.size(), "function encoding takes no operands");
    target.kind = ABI_FUNCTION_TARGET_ENCODING;
    return target;
  }
  if(words[begin] == "path") {
    require(begin + 1 < words.size(), "function path needs a name");
    target.kind = ABI_FUNCTION_TARGET_PATH;
    target.qualified_name = words[begin + 1];
    for(size_t i = begin + 2; i < words.size(); ++i) {
      AbiFunctionPathOperand operand;
      operand.kind = ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT;
      operand.argument_ref = words[i];
      target.path_operands.push_back(operand);
    }
    return target;
  }
  if(words[begin] == "local") {
    require(begin + 4 < words.size(), "local function target has invalid operands");
    target.kind = ABI_FUNCTION_TARGET_LOCAL;
    target.context_ref = words[begin + 1];
    target.qualified_name = words[begin + 2];
    target.terminal = words[begin + 3];
    require(abi_find_terminal_kind(target.terminal, &target.terminal_code),
            "unknown ABI terminal '" + target.terminal + "'");
    target.discriminator = words[begin + 4];
    require(begin + 5 == words.size(), "local function target has extra operands");
    return target;
  }
  if(words[begin] == "lambda") {
    require(begin + 3 < words.size(), "lambda function target has invalid operands");
    target.kind = ABI_FUNCTION_TARGET_LAMBDA;
    target.context_ref = words[begin + 1];
    target.discriminator = words[begin + 2];
    target.terminal = words[begin + 3];
    require(abi_find_terminal_kind(target.terminal, &target.terminal_code),
            "unknown ABI terminal '" + target.terminal + "'");
    for(size_t i = begin + 4; i < words.size(); ++i) {
      target.signature_parameter_types.push_back(compact_type(words[i]));
    }
    return target;
  }
  if(words[begin] == "namespace-lambda") {
    require(begin + 2 < words.size(), "namespace lambda function has invalid operands");
    target.kind = ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA;
    target.source_name = words[begin + 1];
    target.terminal = words[begin + 2];
    require(abi_find_terminal_kind(target.terminal, &target.terminal_code),
            "unknown ABI terminal '" + target.terminal + "'");
    target.namespace_qualifiers.assign(words.begin() + begin + 3, words.end());
    return target;
  }
  if(words[begin] == "member-function") {
    require(begin + 2 < words.size(), "member function target has invalid operands");
    target.kind = ABI_FUNCTION_TARGET_MEMBER;
    target.source_name = words[begin + 1];
    target.owner_type = compact_type(words[begin + 2]);
    for(size_t i = begin + 3; i < words.size(); ++i) {
      target.signature_parameter_types.push_back(compact_type(words[i]));
    }
    return target;
  }
  target.kind = ABI_FUNCTION_TARGET_PATH;
  target.qualified_name = words[begin];
  for(size_t i = begin + 1; i < words.size(); ++i) {
    target.signature_parameter_types.push_back(compact_type(words[i]));
  }
  return target;
}

AbiDefinitionRecord parse_definition(const vector<string> & words)
{
  require(words.size() >= 3, "incomplete ABI definition");
  AbiDefinitionRecord definition;
  definition.id = words[1];
  require(!definition.id.empty(), "empty ABI definition id");
  if(words[0] == "let-type") {
    definition.set_kind(ABI_DEFINITION_TYPE);
    definition.type = parse_type(words, 2);
    return definition;
  }
  if(words[0] == "let-context") {
    definition.set_kind(ABI_DEFINITION_CONTEXT);
    if(words[2] == "raw") {
      require(words.size() == 4, "raw context takes one fragment");
      definition.context.kind = ABI_CONTEXT_RAW;
      definition.context.fragment = words[3];
    } else {
      require(words[2] == "function" || words[2] == "callable",
              "unknown context form '" + words[2] + "'");
      definition.context.kind = ABI_CONTEXT_FUNCTION;
      size_t target_begin = 3;
      if(words[2] == "callable") {
        require(words.size() >= 5, "callable context has invalid operands");
        if(words[3] == "const") {
          definition.context.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_CONST);
        } else if(words[3] == "volatile") {
          definition.context.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_VOLATILE);
        } else if(words[3] == "const-volatile") {
          definition.context.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_CONST);
          definition.context.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_VOLATILE);
        } else require(words[3] == "unqualified",
                       "unknown callable context qualifier '" + words[3] + "'");
        definition.context.target_signature_is_parameter_list = true;
        target_begin = 4;
      }
      definition.context.function = parse_function_target(words, target_begin);
    }
    return definition;
  }
  if(words[0] == "let-entity") {
    definition.set_kind(ABI_DEFINITION_ENTITY);
    const string & form = words[2];
    if(form == "symbol") {
      require(words.size() == 4, "symbol entity takes one mangled symbol");
      definition.entity.kind = ABI_ENTITY_FACT_SYMBOL;
      definition.entity.qualified_name = words[3];
    } else if(form == "variable" || form == "internal-variable") {
      require(words.size() == 4, "variable entity takes one name");
      definition.entity.kind = ABI_ENTITY_FACT_VARIABLE;
      definition.entity.qualified_name = words[3];
      definition.entity.internal_linkage = form == "internal-variable";
    } else if(form == "function") {
      definition.entity.kind = ABI_ENTITY_FACT_FUNCTION;
      definition.entity.function = parse_function_target(words, 3);
    } else {
      throw_fact_error("unknown entity form '" + form + "'");
    }
    return definition;
  }
  throw_fact_error("definition parser received invalid record");
}

AbiTemplateArgument parse_argument(const vector<string> & words)
{
  require(words.size() >= 4, "incomplete template argument definition");
  AbiTemplateArgument argument;
  const string & form = words[2];
  if(form == "type") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
    argument.type = parse_type(words, 3);
  } else if(form == "value") {
    require(words.size() == 5, "value argument needs type and value");
    argument.kind = ABI_TEMPLATE_ARGUMENT_VALUE;
    argument.value_type = compact_type(words[3]);
    argument.value = parse_signed(words[4]);
    argument.has_value_type = true;
  } else if(form == "dependent-value") {
    require(words.size() == 6, "dependent value needs parameter, type, and value");
    argument.kind = ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE;
    argument.type = compact_type(words[3]);
    argument.value_type = compact_type(words[4]);
    argument.value = parse_signed(words[5]);
    argument.has_value_type = true;
  } else if(form == "expression") {
    require(words.size() == 4, "expression argument takes one expression id");
    argument.kind = ABI_TEMPLATE_ARGUMENT_EXPRESSION;
    argument.entity_ref = words[3];
  } else if(form == "template-entity") {
    require(words.size() == 4, "template entity takes one name");
    argument.kind = ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY;
    argument.name = words[3];
  } else if(form == "member-template-entity") {
    require(words.size() == 6, "member template entity has invalid operands");
    argument.kind = ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY;
    argument.type = compact_type(words[3]);
    argument.name = words[4];
    argument.substitution = words[5];
  } else if(form == "template-param-template") {
    require(words.size() == 4, "template parameter argument takes one index");
    argument.kind = ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE;
    argument.index = parse_index(words[3]);
  } else if(form == "entity-address") {
    require(words.size() == 4, "entity address takes one entity id");
    argument.kind = ABI_TEMPLATE_ARGUMENT_ENTITY;
    argument.entity_ref = words[3];
    argument.address_of = true;
  } else if(form == "entity-reference") {
    require(words.size() == 4, "entity reference takes one entity id");
    argument.kind = ABI_TEMPLATE_ARGUMENT_ENTITY;
    argument.entity_ref = words[3];
    argument.address_of = false;
  } else if(form == "member-external-address") {
    require(words.size() >= 12, "member external address has invalid operands");
    argument.kind = ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY;
    argument.symbol = words[3];
    argument.type = compact_type(words[4]);
    argument.name = words[5];
    argument.address_of = true;
    argument.member_is_function = parse_yes_no(words[6]);
    argument.member_function_const = parse_yes_no(words[7]);
    argument.member_function_volatile = parse_yes_no(words[8]);
    argument.member_function_lvalue_ref = parse_yes_no(words[9]);
    argument.member_function_rvalue_ref = parse_yes_no(words[10]);
    argument.member_function_variadic = parse_yes_no(words[11]);
    for(size_t i = 12; i < words.size(); ++i) {
      argument.parameter_types.push_back(compact_type(words[i]));
    }
  } else if(form == "pack") {
    argument.kind = ABI_TEMPLATE_ARGUMENT_PACK;
    append_reference_names(&argument.argument_refs, words, 3);
  } else {
    throw_fact_error("unknown template argument form '" + form + "'");
  }
  return argument;
}

AbiDependentExpression parse_expression(const vector<string> & words)
{
  require(words.size() >= 4, "incomplete dependent expression definition");
  AbiDependentExpression expression;
  const string & form = words[2];
  if(form == "template-param" || form == "function-param") {
    require(words.size() == 4, form + " expression takes one index");
    expression.kind = form == "template-param" ? ABI_EXPRESSION_TEMPLATE_PARAMETER
                                               : ABI_EXPRESSION_FUNCTION_PARAMETER;
    expression.index = parse_index(words[3]);
  } else if(form == "literal") {
    require(words.size() == 4, "literal expression takes one value");
    expression.kind = ABI_EXPRESSION_LITERAL;
    expression.text = words[3];
  } else if(form == "unary" || form == "binary") {
    const size_t operands = form == "unary" ? 1 : 2;
    require(words.size() == 4 + operands, form + " expression has invalid operands");
    expression.kind = form == "unary" ? ABI_EXPRESSION_UNARY : ABI_EXPRESSION_BINARY;
    expression.op = words[3];
    expression.operation = abi_expression_operation_kind(expression.op);
    append_reference_names(&expression.expression_refs, words, 4);
  } else if(form == "conditional") {
    require(words.size() == 6, "conditional expression needs three operands");
    expression.kind = ABI_EXPRESSION_CONDITIONAL;
    append_reference_names(&expression.expression_refs, words, 3);
  } else if(form == "pack") {
    require(words.size() == 4, "pack expression takes one operand");
    expression.kind = ABI_EXPRESSION_PACK_EXPANSION;
    expression.expression_refs.push_name(words[3]);
  } else if(form == "call") {
    require(words.size() >= 4, "call expression needs a callee");
    expression.kind = ABI_EXPRESSION_CALL;
    append_reference_names(&expression.expression_refs, words, 3);
  } else if(form == "cast") {
    require(words.size() == 6, "cast expression needs operator, type, and operand");
    expression.kind = ABI_EXPRESSION_CAST;
    expression.op = words[3];
    expression.operation = abi_expression_operation_kind(expression.op);
    expression.type = compact_type(words[4]);
    expression.expression_refs.push_name(words[5]);
  } else if(form == "template-id") {
    require(words.size() >= 4, "template-id expression needs a name");
    expression.kind = ABI_EXPRESSION_TEMPLATE_ID;
    expression.text = words[3];
    append_reference_names(&expression.argument_refs, words, 4);
  } else if(form == "type-trait") {
    require(words.size() >= 5, "type trait needs a name and type operands");
    expression.kind = ABI_EXPRESSION_TYPE_TRAIT;
    expression.text = words[3];
    for(size_t i = 4; i < words.size(); ++i) {
      expression.type_arguments.push_back(compact_type(words[i]));
    }
  } else if(form == "sizeof-type") {
    require(words.size() == 4, "sizeof-type takes one type");
    expression.kind = ABI_EXPRESSION_SIZEOF_TYPE;
    expression.type = compact_type(words[3]);
  } else if(form == "member") {
    require(words.size() == 6, "member expression has invalid operands");
    expression.kind = ABI_EXPRESSION_MEMBER;
    expression.type = compact_type(words[3]);
    expression.close_member_owner = parse_yes_no(words[4]);
    expression.text = words[5];
  } else if(form == "object-member") {
    require(words.size() >= 6, "object member expression has invalid operands");
    expression.kind = ABI_EXPRESSION_OBJECT_MEMBER;
    expression.op = words[3];
    expression.operation = abi_expression_operation_kind(expression.op);
    expression.expression_refs.push_name(words[4]);
    expression.text = words[5];
    append_reference_names(&expression.argument_refs, words, 6);
  } else if(form == "entity-reference") {
    require(words.size() == 4, "entity reference takes one entity id");
    expression.kind = ABI_EXPRESSION_ENTITY;
    expression.entity_ref = words[3];
  } else {
    throw_fact_error("unknown dependent expression form '" + form + "'");
  }
  return expression;
}

AbiTargetRecord parse_target(const vector<string> & words)
{
  require(!words.empty(), "empty target record");
  AbiTargetRecord target;
  const string & form = words[0];
  if(form == "type") {
    target.kind = ABI_TARGET_FACT_TYPE;
    target.type = parse_type(words, 1);
  } else if(form == "function" || form == "c-function") {
    target.kind = ABI_TARGET_FACT_FUNCTION;
    target.c_linkage = form == "c-function";
    target.function = parse_function_target(words, 1);
  } else if(form == "structured-variable") {
    require(words.size() == 1, "structured variable target takes no path");
    target.kind = ABI_TARGET_FACT_VARIABLE;
    target.function.kind = ABI_FUNCTION_TARGET_ENCODING;
  } else if(form == "variable") {
    require(words.size() == 2, "variable target takes one name");
    target.kind = ABI_TARGET_FACT_VARIABLE;
    target.qualified_name = words[1];
  } else if(form == "typeinfo" || form == "typeinfo-name"
            || form == "vtable" || form == "vtt") {
    if(form == "typeinfo") target.kind = ABI_TARGET_FACT_TYPEINFO;
    if(form == "typeinfo-name") target.kind = ABI_TARGET_FACT_TYPEINFO_NAME;
    if(form == "vtable") target.kind = ABI_TARGET_FACT_VTABLE;
    if(form == "vtt") target.kind = ABI_TARGET_FACT_VTT;
    target.type = parse_type(words, 1);
  } else if(form == "construction-vtable") {
    require(words.size() == 4, "construction vtable needs derived, offset, and base");
    target.kind = ABI_TARGET_FACT_CONSTRUCTION_VTABLE;
    target.type = compact_type(words[1]);
    target.base_offset = parse_unsigned(words[2]);
    target.base_type = compact_type(words[3]);
  } else if(form == "tls-wrapper") {
    require(words.size() == 3 && words[1] == "variable",
            "TLS wrapper must name a variable");
    target.kind = ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER;
    target.qualified_name = words[2];
  } else if(form == "thunk") {
    require(words.size() >= 4, "thunk target has invalid operands");
    target.kind = ABI_TARGET_FACT_THUNK;
    target.this_adjust = parse_signed(words[1]);
    size_t cursor = 2;
    if(words[cursor] == "virtual-result") {
      require(cursor + 3 < words.size(), "virtual result thunk has invalid offsets");
      target.has_result_adjust = true;
      target.result_adjust_virtual = true;
      target.result_adjust = parse_signed(words[cursor + 1]);
      target.result_vcall_offset = parse_signed(words[cursor + 2]);
      cursor += 3;
    } else if(words[cursor] != "function") {
      target.has_result_adjust = true;
      target.result_adjust = parse_signed(words[cursor++]);
    }
    require(words[cursor] == "function", "thunk is missing function target");
    target.function = parse_function_target(words, cursor + 1);
  } else if(form == "virtual-base-thunk") {
    require(words.size() >= 4 && words[2] == "function",
            "virtual base thunk has invalid operands");
    target.kind = ABI_TARGET_FACT_VIRTUAL_BASE_THUNK;
    target.vcall_offset = parse_signed(words[1]);
    target.function = parse_function_target(words, 3);
  } else {
    throw_fact_error("unknown ABI target form '" + form + "'");
  }
  return target;
}

AbiFunctionRecord parse_function_record(const vector<string> & words)
{
  AbiFunctionRecord record;
  const string & form = words[0];
  if(form == "name-source") {
    require(words.size() >= 2 && words.size() <= 3, "name-source has invalid operands");
    record.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
    if(words.size() == 2 && words[1] == "-") record.name.clear();
    else record.name = words[1];
    if(words.size() == 3) record.substitution = words[2];
  } else if(form == "name-std") {
    require(words.size() == 1, "name-std takes no operands");
    record.kind = ABI_FUNCTION_RECORD_NAME_STD;
  } else if(form == "name-template") {
    require(words.size() >= 7, "name-template has invalid operands");
    record.kind = ABI_FUNCTION_RECORD_NAME_TEMPLATE;
    record.name = words[1];
    record.substitution = words[2];
    record.complete_substitution = words[3];
    record.standard_substitution = words[4];
    record.standard_substitution_code =
      abi_standard_substitution_kind(record.standard_substitution);
    record.standard_substitution_includes_arguments = parse_yes_no(words[5]);
    append_reference_names(&record.argument_refs, words, 6);
  } else if(form == "function-template-arg") {
    require(words.size() == 2, "function-template-arg takes one id");
    record.kind = ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
    record.argument_refs.push_name(words[1]);
  } else if(form == "function-template-prefix") {
    require(words.size() == 2, "function-template-prefix takes one key");
    record.kind = ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX;
    record.substitution = words[1];
  } else if(form == "local-context") {
    require(words.size() == 4, "local-context has invalid operands");
    record.kind = ABI_FUNCTION_RECORD_LOCAL_CONTEXT;
    record.context_ref = words[1];
    record.name = words[2];
    record.discriminator = words[3];
  } else if(form == "unnamed-type-context") {
    require(words.size() == 3, "unnamed-type-context has invalid operands");
    record.kind = ABI_FUNCTION_RECORD_LOCAL_CONTEXT;
    record.context_ref = words[1];
    record.discriminator = words[2];
  } else if(form == "lambda-context") {
    require(words.size() >= 3, "lambda-context has invalid operands");
    record.kind = ABI_FUNCTION_RECORD_LAMBDA_CONTEXT;
    record.context_ref = words[1];
    record.discriminator = words[2];
    for(size_t i = 3; i < words.size(); ++i) record.types.push_back(compact_type(words[i]));
  } else if(form == "namespace-lambda-context") {
    require(words.size() >= 2, "namespace-lambda-context needs a source name");
    record.kind = ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT;
    record.source_name = words[1];
    record.namespace_qualifiers.assign(words.begin() + 2, words.end());
  } else if(form == "terminal-source") {
    require(words.size() == 2, "terminal-source takes one name");
    record.kind = ABI_FUNCTION_RECORD_TERMINAL_SOURCE;
    record.name = words[1];
  } else if(form == "terminal") {
    require(words.size() == 2, "terminal takes one semantic terminal");
    record.kind = ABI_FUNCTION_RECORD_TERMINAL;
    record.terminal = words[1];
    require(abi_find_terminal_kind(record.terminal, &record.terminal_code),
            "unknown ABI terminal '" + record.terminal + "'");
  } else if(form == "variadic") {
    require(words.size() == 1, "variadic takes no operands");
    record.kind = ABI_FUNCTION_RECORD_VARIADIC;
  } else if(form == "abi-tag") {
    require(words.size() == 2, "abi-tag takes one tag");
    record.kind = ABI_FUNCTION_RECORD_ABI_TAG;
    record.name = words[1];
  } else if(form == "component-abi-tag") {
    require(words.size() == 2, "component-abi-tag takes one tag");
    record.kind = ABI_FUNCTION_RECORD_COMPONENT_ABI_TAG;
    record.name = words[1];
  } else if(form == "function-qualifier" || form == "qualifier") {
    require(words.size() >= 2, "function qualifier needs at least one qualifier");
    record.kind = ABI_FUNCTION_RECORD_QUALIFIER;
    for(size_t i = 1; i < words.size(); ++i) {
      if(words[i] == "const") record.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_CONST);
      else if(words[i] == "volatile") record.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_VOLATILE);
      else if(words[i] == "lvalue-ref") record.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE);
      else if(words[i] == "rvalue-ref") record.qualifiers.push_back(ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE);
      else throw_fact_error("unknown function qualifier '" + words[i] + "'");
    }
  } else if(form == "operator-terminal") {
    require(words.size() == 2 || (words.size() == 3 && words[1] == "literal"),
            "operator-terminal has invalid operands");
    record.kind = ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
    record.terminal = words[1];
    require(abi_find_terminal_kind(record.terminal, &record.terminal_code),
            "unknown ABI terminal '" + record.terminal + "'");
    if(words.size() == 3) record.literal_suffix = words[2];
  } else if(form == "conversion-terminal") {
    require(words.size() >= 2, "conversion-terminal needs a type");
    record.kind = ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
    record.type = parse_type(words, 1);
  } else if(form == "param" || form == "result") {
    require(words.size() >= 2, form + " needs a type");
    record.kind = form == "param" ? ABI_FUNCTION_RECORD_PARAMETER
                                   : ABI_FUNCTION_RECORD_RESULT;
    record.type = parse_type(words, 1);
  } else {
    throw_fact_error("unknown ABI function record '" + form + "'");
  }
  return record;
}

string type_text(const AbiType & type);

string unmodified_type_text(const AbiType & type)
{
  switch(type.kind) {
    case ABI_TYPE_NAME_OR_REFERENCE: return type.name;
    case ABI_TYPE_NAMED: return "named:" + type.name;
    case ABI_TYPE_BUILTIN:
      if(!type.name.empty()) return type.name;
      if(type.builtin_type == ABI_BUILTIN_TYPE_BITINT ||
         type.builtin_type == ABI_BUILTIN_TYPE_UNSIGNED_BITINT)
        return std::string(type.builtin_type == ABI_BUILTIN_TYPE_BITINT ?
          "bitint" : "ubitint") + std::to_string(type.index);
      return abi_builtin_type_word(type.builtin_type);
    case ABI_TYPE_POINTER: return "ptr:" + type_text(type.types.at(0));
    case ABI_TYPE_LVALUE_REFERENCE: return "ref:" + type_text(type.types.at(0));
    case ABI_TYPE_RVALUE_REFERENCE: return "rref:" + type_text(type.types.at(0));
    case ABI_TYPE_PACK_EXPANSION: return "pack:" + type_text(type.types.at(0));
    case ABI_TYPE_CV: {
      if(type.presentation_names.tag_size() != 0 &&
         !type.is_const && !type.is_volatile) {
        string result = "tagged " + type_text(type.types.at(0));
        const vector<string> & names = type.presentation_names.names();
        for(size_t i = type.presentation_names.namespace_size();
            i < names.size(); ++i) result += " " + names[i];
        return result;
      }
      string result;
      if(type.is_const) result += "const:";
      if(type.is_volatile) result += "volatile:";
      result += type_text(type.types.at(0));
      return result;
    }
    case ABI_TYPE_TEMPLATE_PARAMETER:
      return string(type.substitutable ? "template-param-subst " : "template-param ")
             + std::to_string(type.index);
    case ABI_TYPE_VENDOR_QUALIFIED:
      return "vendor " + (type.name.empty() ?
        std::string(abi_vendor_qualifier_word(type.vendor_qualifier)) :
        type.name) + " " + type_text(type.types.at(0));
    case ABI_TYPE_ARRAY:
      return "array:" + type.array_bound.value + ":" + type_text(type.types.at(0));
    case ABI_TYPE_VECTOR:
      return "vector:" + type.array_bound.value + ":" + type_text(type.types.at(0));
    case ABI_TYPE_BUILTIN_TRANSFORM:
      return "builtin-transform " + type.name + " " + type_text(type.types.at(0));
    case ABI_TYPE_FUNCTION: {
      string result = type.variadic ? "function-type-variadic" : "function-type";
      for(const AbiType & child : type.types) result += " " + type_text(child);
      return result;
    }
    case ABI_TYPE_MEMBER_POINTER:
      return "member-pointer " + type_text(type.types.at(0)) + " "
             + type_text(type.types.at(1));
    case ABI_TYPE_TEMPLATE_SPECIALIZATION: {
      string result = "template " + type.name;
      for(const string & argument : type.argument_refs.names()) result += " " + argument;
      return result;
    }
    case ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION: {
      string result = "template-param-template " + std::to_string(type.index);
      for(const string & argument : type.argument_refs.names()) result += " " + argument;
      return result;
    }
    case ABI_TYPE_STD_TEMPLATE_SPECIALIZATION: {
      const string standard = type.standard_substitution.empty() ?
        abi_standard_substitution_code(type.standard_substitution_code) :
        type.standard_substitution;
      string result = "std-template " + standard + " "
                      + (type.standard_substitution_includes_arguments ? "yes " : "no ")
                      + type.name;
      for(const string & argument : type.argument_refs.names()) result += " " + argument;
      return result;
    }
    case ABI_TYPE_MEMBER:
      return "member " + type_text(type.types.at(0)) + " " + type.name;
    case ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION: {
      string result = "member-template " + type_text(type.types.at(0)) + " " + type.name;
      for(const string & argument : type.argument_refs.names()) result += " " + argument;
      return result;
    }
    case ABI_TYPE_DECLTYPE_EXPRESSION: return "decltype " + type.expression_ref;
    case ABI_TYPE_LAMBDA_CLOSURE:
      return "lambda-closure " + type.context_ref + " " + type.discriminator;
    case ABI_TYPE_LOCAL_TYPE:
      if(type.name.empty()) {
        return "unnamed-local-type " + type.context_ref + " "
               + type.discriminator;
      }
      return "local-type " + type.context_ref + " " + type.name + " " + type.discriminator;
    case ABI_TYPE_NAMESPACE_LAMBDA: {
      string result = "namespace-lambda " + type.name;
      const vector<string> & names = type.presentation_names.names();
      for(size_t i = 0; i < type.presentation_names.namespace_size(); ++i)
        result += " " + names[i];
      return result;
    }
    case ABI_TYPE_RESOLVED:
      throw_abi_internal("resolved ABI type is not a fact-file form");
  }
  throw_abi_internal("unknown ABI type form in canonical serializer");
}

string type_text(const AbiType & type)
{
  string result;
  for(size_t i = 0; i < type.modifiers.size();) {
    const AbiTypeModifier & modifier = type.modifiers[i];
    if(modifier.kind == ABI_TYPE_CV) {
      bool is_const = false;
      bool is_volatile = false;
      do {
        is_const = is_const || type.modifiers[i].is_const;
        is_volatile = is_volatile || type.modifiers[i].is_volatile;
        ++i;
      } while(i < type.modifiers.size()
              && type.modifiers[i].kind == ABI_TYPE_CV);
      if(is_const) result += "const:";
      if(is_volatile) result += "volatile:";
      continue;
    }
    if(modifier.kind == ABI_TYPE_POINTER) result += "ptr:";
    else if(modifier.kind == ABI_TYPE_LVALUE_REFERENCE) result += "ref:";
    else if(modifier.kind == ABI_TYPE_RVALUE_REFERENCE) result += "rref:";
    else if(modifier.kind == ABI_TYPE_PACK_EXPANSION) result += "pack:";
    else if(modifier.kind == ABI_TYPE_ARRAY) {
      result += "array:" + modifier.array_bound.value + ":";
    } else {
      throw_abi_internal(
        "unknown flat ABI type modifier in canonical serializer");
    }
    ++i;
  }
  result += unmodified_type_text(type);
  return result;
}

string function_target_text(const AbiFunctionTarget & target)
{
  if(target.kind == ABI_FUNCTION_TARGET_ENCODING) return "encoding";
  if(target.kind == ABI_FUNCTION_TARGET_PATH) {
    string result = target.path_operands.empty() ? target.qualified_name
                                                 : "path " + target.qualified_name;
    for(const AbiFunctionPathOperand & operand : target.path_operands) {
      result += " " + operand.argument_ref;
    }
    for(const AbiType & type : target.signature_parameter_types) {
      result += " " + type_text(type);
    }
    return result;
  }
  if(target.kind == ABI_FUNCTION_TARGET_LOCAL) {
    return "local " + target.context_ref + " " + target.qualified_name + " "
           + target.terminal + " " + target.discriminator;
  }
  if(target.kind == ABI_FUNCTION_TARGET_LAMBDA) {
    string result = "lambda " + target.context_ref + " " + target.discriminator
                    + " " + target.terminal;
    for(const AbiType & type : target.signature_parameter_types) result += " " + type_text(type);
    return result;
  }
  if(target.kind == ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA) {
    string result = "namespace-lambda " + target.source_name + " " + target.terminal;
    for(const string & qualifier : target.namespace_qualifiers) result += " " + qualifier;
    return result;
  }
  if(target.kind == ABI_FUNCTION_TARGET_MEMBER) {
    string result = "member-function " + target.source_name + " "
                    + type_text(target.owner_type);
    for(const AbiType & type : target.signature_parameter_types) {
      result += " " + type_text(type);
    }
    return result;
  }
  throw_abi_internal("unknown function target form in canonical serializer");
}

string bool_text(bool value) { return value ? "yes" : "no"; }

string definition_text(const AbiDefinitionRecord & definition)
{
  string result;
  if(definition.kind == ABI_DEFINITION_TYPE) {
    return "let-type " + definition.id + " " + type_text(definition.type);
  }
  if(definition.kind == ABI_DEFINITION_TEMPLATE_ARGUMENT) {
    const AbiTemplateArgument & argument = definition.template_argument;
    result = "let-arg " + definition.id + " ";
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_TYPE) return result + "type " + type_text(argument.type);
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_VALUE) {
      return result + "value " + type_text(argument.value_type) + " " + std::to_string(argument.value);
    }
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE) {
      return result + "dependent-value " + type_text(argument.type) + " "
             + type_text(argument.value_type) + " " + std::to_string(argument.value);
    }
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_EXPRESSION) return result + "expression " + argument.entity_ref;
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY) return result + "template-entity " + argument.name;
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY) {
      return result + "member-template-entity " + type_text(argument.type) + " "
             + argument.name + " " + argument.substitution;
    }
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE) {
      return result + "template-param-template " + std::to_string(argument.index);
    }
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_ENTITY) {
      return result + (argument.address_of ? "entity-address " :
                                             "entity-reference ") + argument.entity_ref;
    }
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY) {
      result += "member-external-address " + argument.symbol + " " + type_text(argument.type)
                + " " + argument.name + " " + bool_text(argument.member_is_function)
                + " " + bool_text(argument.member_function_const)
                + " " + bool_text(argument.member_function_volatile)
                + " " + bool_text(argument.member_function_lvalue_ref)
                + " " + bool_text(argument.member_function_rvalue_ref)
                + " " + bool_text(argument.member_function_variadic);
      for(const AbiType & type : argument.parameter_types) result += " " + type_text(type);
      return result;
    }
    if(argument.kind == ABI_TEMPLATE_ARGUMENT_PACK) {
      result += "pack";
      for(const string & argument_ref : argument.argument_refs.names()) result += " " + argument_ref;
      return result;
    }
  }
  if(definition.kind == ABI_DEFINITION_EXPRESSION) {
    const AbiDependentExpression & expression = definition.expression;
    result = "let-expr " + definition.id + " ";
    if(expression.kind == ABI_EXPRESSION_TEMPLATE_PARAMETER
       || expression.kind == ABI_EXPRESSION_FUNCTION_PARAMETER) {
      return result + (expression.kind == ABI_EXPRESSION_TEMPLATE_PARAMETER
                       ? "template-param " : "function-param ") + std::to_string(expression.index);
    }
    if(expression.kind == ABI_EXPRESSION_LITERAL) return result + "literal " + expression.text;
    if(expression.kind == ABI_EXPRESSION_UNARY || expression.kind == ABI_EXPRESSION_BINARY) {
      result += expression.kind == ABI_EXPRESSION_UNARY ? "unary " : "binary ";
      result += expression.op;
      for(const string & child : expression.expression_refs.names()) result += " " + child;
      return result;
    }
    if(expression.kind == ABI_EXPRESSION_CONDITIONAL) result += "conditional";
    else if(expression.kind == ABI_EXPRESSION_PACK_EXPANSION) result += "pack";
    else if(expression.kind == ABI_EXPRESSION_CALL) result += "call";
    else if(expression.kind == ABI_EXPRESSION_CAST) {
      result += "cast " + expression.op + " " + type_text(expression.type);
    } else if(expression.kind == ABI_EXPRESSION_TEMPLATE_ID) result += "template-id " + expression.text;
    else if(expression.kind == ABI_EXPRESSION_TYPE_TRAIT) {
      result += "type-trait " + expression.text;
      for(const AbiType & type : expression.type_arguments) result += " " + type_text(type);
      return result;
    } else if(expression.kind == ABI_EXPRESSION_SIZEOF_TYPE) return result + "sizeof-type " + type_text(expression.type);
    else if(expression.kind == ABI_EXPRESSION_MEMBER) {
      return result + "member " + type_text(expression.type) + " "
             + bool_text(expression.close_member_owner) + " " + expression.text;
    } else if(expression.kind == ABI_EXPRESSION_OBJECT_MEMBER) {
      result += "object-member " + expression.op;
    } else if(expression.kind == ABI_EXPRESSION_ENTITY) return result + "entity-reference " + expression.entity_ref;
    else throw_abi_internal("unknown expression form in canonical serializer");
    for(const string & child : expression.expression_refs.names()) result += " " + child;
    if(expression.kind == ABI_EXPRESSION_OBJECT_MEMBER) result += " " + expression.text;
    if(expression.kind == ABI_EXPRESSION_TEMPLATE_ID
       || expression.kind == ABI_EXPRESSION_OBJECT_MEMBER) {
      for(const string & argument : expression.argument_refs.names()) result += " " + argument;
    }
    return result;
  }
  if(definition.kind == ABI_DEFINITION_CONTEXT) {
    if(definition.context.kind == ABI_CONTEXT_MAIN) {
      return "let-context " + definition.id + " raw Z4mainE";
    }
    if(definition.context.kind == ABI_CONTEXT_RAW) {
      return "let-context " + definition.id + " raw " + definition.context.fragment;
    }
    string form = "function";
    if(definition.context.target_signature_is_parameter_list
       || !definition.context.qualifiers.empty()) {
      const bool is_const = std::find(definition.context.qualifiers.begin(),
                                      definition.context.qualifiers.end(),
                                      ABI_FUNCTION_QUALIFIER_CONST)
                            != definition.context.qualifiers.end();
      const bool is_volatile = std::find(definition.context.qualifiers.begin(),
                                         definition.context.qualifiers.end(),
                                         ABI_FUNCTION_QUALIFIER_VOLATILE)
                               != definition.context.qualifiers.end();
      form = "callable ";
      form += is_const && is_volatile ? "const-volatile"
              : is_const ? "const" : is_volatile ? "volatile" : "unqualified";
    }
    return "let-context " + definition.id + " " + form + " "
           + function_target_text(definition.context.function);
  }
  if(definition.kind == ABI_DEFINITION_ENTITY) {
    if(definition.entity.kind == ABI_ENTITY_FACT_SYMBOL) {
      return "let-entity " + definition.id + " symbol " + definition.entity.qualified_name;
    }
    if(definition.entity.kind == ABI_ENTITY_FACT_FUNCTION) {
      return "let-entity " + definition.id + " function "
             + function_target_text(definition.entity.function);
    }
    return "let-entity " + definition.id
           + (definition.entity.internal_linkage ? " internal-variable " : " variable ")
           + definition.entity.qualified_name;
  }
  throw_abi_internal("unknown ABI definition form in canonical serializer");
}

string target_text(const AbiTargetRecord & target)
{
  if(target.kind == ABI_TARGET_FACT_TYPE) return "type " + type_text(target.type);
  if(target.kind == ABI_TARGET_FACT_FUNCTION) {
    return string(target.c_linkage ? "c-function " : "function ")
           + function_target_text(target.function);
  }
  if(target.kind == ABI_TARGET_FACT_VARIABLE) {
    return target.function.kind == ABI_FUNCTION_TARGET_ENCODING ?
             "structured-variable" : "variable " + target.qualified_name;
  }
  if(target.kind == ABI_TARGET_FACT_TYPEINFO) return "typeinfo " + type_text(target.type);
  if(target.kind == ABI_TARGET_FACT_TYPEINFO_NAME) {
    return "typeinfo-name " + type_text(target.type);
  }
  if(target.kind == ABI_TARGET_FACT_VTABLE) return "vtable " + type_text(target.type);
  if(target.kind == ABI_TARGET_FACT_VTT) return "vtt " + type_text(target.type);
  if(target.kind == ABI_TARGET_FACT_CONSTRUCTION_VTABLE) {
    return "construction-vtable " + type_text(target.type) + " "
           + std::to_string(target.base_offset) + " " + type_text(target.base_type);
  }
  if(target.kind == ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER) {
    return "tls-wrapper variable " + target.qualified_name;
  }
  if(target.kind == ABI_TARGET_FACT_VIRTUAL_BASE_THUNK) {
    return "virtual-base-thunk " + std::to_string(target.vcall_offset) + " function "
           + function_target_text(target.function);
  }
  if(target.kind == ABI_TARGET_FACT_THUNK) {
    string result = "thunk " + std::to_string(target.this_adjust) + " ";
    if(target.has_result_adjust && target.result_adjust_virtual) {
      result += "virtual-result " + std::to_string(target.result_adjust) + " "
                + std::to_string(target.result_vcall_offset) + " ";
    } else if(target.has_result_adjust) result += std::to_string(target.result_adjust) + " ";
    return result + "function " + function_target_text(target.function);
  }
  throw_abi_internal("unknown ABI target form in canonical serializer");
}

string qualifier_text(AbiFunctionQualifier qualifier)
{
  if(qualifier == ABI_FUNCTION_QUALIFIER_CONST) return "const";
  if(qualifier == ABI_FUNCTION_QUALIFIER_VOLATILE) return "volatile";
  if(qualifier == ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE) return "lvalue-ref";
  return "rvalue-ref";
}

string function_record_text(const AbiFunctionRecord & function)
{
  if(function.kind == ABI_FUNCTION_RECORD_NAME_SOURCE) {
    string result = "name-source " + (function.name.empty() ? string("-") : function.name);
    if(!function.substitution.empty()) result += " " + function.substitution;
    return result;
  }
  if(function.kind == ABI_FUNCTION_RECORD_NAME_STD) return "name-std";
  if(function.kind == ABI_FUNCTION_RECORD_NAME_TEMPLATE) {
    const string standard = function.standard_substitution_code !=
      ABI_STANDARD_SUBSTITUTION_TEXT ?
      abi_standard_substitution_code(function.standard_substitution_code) :
      function.standard_substitution.empty() ? "-" :
      function.standard_substitution;
    string result = "name-template " + function.name + " " + function.substitution + " "
                    + function.complete_substitution + " " + standard
                    + " " + bool_text(function.standard_substitution_includes_arguments);
    for(const string & argument : function.argument_refs.names()) result += " " + argument;
    return result;
  }
  if(function.kind == ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT) {
    return "function-template-arg " + function.argument_refs.names().at(0);
  }
  if(function.kind == ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX) {
    return "function-template-prefix " + function.substitution;
  }
  if(function.kind == ABI_FUNCTION_RECORD_LOCAL_CONTEXT) {
    if(function.name.empty()) {
      return "unnamed-type-context " + function.context_ref + " "
             + function.discriminator;
    }
    return "local-context " + function.context_ref + " " + function.name + " " + function.discriminator;
  }
  if(function.kind == ABI_FUNCTION_RECORD_LAMBDA_CONTEXT) {
    string result = "lambda-context " + function.context_ref + " " + function.discriminator;
    for(const AbiType & type : function.types) result += " " + type_text(type);
    return result;
  }
  if(function.kind == ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT) {
    string result = "namespace-lambda-context " + function.source_name;
    for(const string & qualifier : function.namespace_qualifiers) result += " " + qualifier;
    return result;
  }
  if(function.kind == ABI_FUNCTION_RECORD_TERMINAL_SOURCE) return "terminal-source " + function.name;
  if(function.kind == ABI_FUNCTION_RECORD_TERMINAL) return "terminal " + function.terminal;
  if(function.kind == ABI_FUNCTION_RECORD_VARIADIC) return "variadic";
  if(function.kind == ABI_FUNCTION_RECORD_ABI_TAG) return "abi-tag " + function.name;
  if(function.kind == ABI_FUNCTION_RECORD_COMPONENT_ABI_TAG) {
    return "component-abi-tag " + function.name;
  }
  if(function.kind == ABI_FUNCTION_RECORD_QUALIFIER) {
    string result = "function-qualifier";
    for(AbiFunctionQualifier qualifier : function.qualifiers) result += " " + qualifier_text(qualifier);
    return result;
  }
  if(function.kind == ABI_FUNCTION_RECORD_OPERATOR_TERMINAL) {
    return "operator-terminal " + function.terminal
           + (function.literal_suffix.empty() ? string() : " " + function.literal_suffix);
  }
  if(function.kind == ABI_FUNCTION_RECORD_CONVERSION_TERMINAL) {
    return "conversion-terminal " + type_text(function.type);
  }
  if(function.kind == ABI_FUNCTION_RECORD_PARAMETER) return "param " + type_text(function.type);
  if(function.kind == ABI_FUNCTION_RECORD_RESULT) return "result " + type_text(function.type);
  throw_abi_internal("unknown function record in canonical serializer");
}

string record_text(const AbiFactRecord & record)
{
  if(record.kind == ABI_FACT_RECORD_TARGET) {
    return target_text(record.target);
  }
  if(record.kind == ABI_FACT_RECORD_DEFINITION) return definition_text(record.definition);
  if(record.kind == ABI_FACT_RECORD_FUNCTION) return function_record_text(record.function);
  throw_abi_internal("unknown ABI record form in canonical serializer");
}

template<class Consumer>
size_t parse_fact_stream(std::istream & lines, Consumer consume,
                         AbiMangleStats * stats)
{
  AbiFactCase current;
  bool has_current = false;
  std::unordered_set<string> ids;
  string line;
  size_t line_number = 0;
  size_t case_count = 0;
  const auto flush = [&]() {
    if(!has_current) return;
    consume(std::move(current));
    current = AbiFactCase();
    has_current = false;
    ++case_count;
  };
  while(std::getline(lines, line)) {
    ++line_number;
    if(stats) {
      stats->source_bytes += line.size() + (lines.eof() ? 0 : 1);
      stats->peak_input_bytes = std::max(stats->peak_input_bytes, line.capacity());
    }
    line = trim(line);
    if(line.empty() || line[0] == '#') continue;
    const vector<string> words = split_words(line);
    if(words[0] == "case") {
      try {
        require(words.size() == 2, "case takes one label");
      } catch(const SerializedInputError & error) {
        throw_fact_line_error(line_number, error);
      }
      // Encoding the preceding case is outside the parse-error translation:
      // allocation, output, and encoder failures must retain their own type.
      flush();
      has_current = true;
      current.label = words[1];
      ids.clear();
      continue;
    }
    try {
      if(!has_current) {
        has_current = true;
        ids.clear();
      }
      AbiFactRecord record = parse_fact_record_words(words);
      if(record.kind == ABI_FACT_RECORD_DEFINITION) {
        require(ids.insert(record.definition.id).second,
                "duplicate ABI definition id '" + record.definition.id + "'");
      }
      current.records.push_back(std::move(record));
    } catch(const SerializedInputError & error) {
      throw_fact_line_error(line_number, error);
    }
  }
  if(lines.bad())
    throw InputOutputError("unable to read ABI fact stream",
                           CompilerErrorDomain::ABI);
  flush();
  require(case_count != 0, "ABI fact file contains no cases");
  return case_count;
}

}  // namespace

AbiFactRecord parse_fact_record_words(const vector<string> & words)
{
  require(!words.empty(), "empty ABI fact record");
  AbiFactRecord record;
  if(words[0].compare(0, 4, "let-") == 0) {
    record.set_kind(ABI_FACT_RECORD_DEFINITION);
    if(words[0] == "let-arg") {
      require(words.size() >= 2, "incomplete template argument definition");
      record.definition.id = words[1];
      record.definition.set_kind(ABI_DEFINITION_TEMPLATE_ARGUMENT);
      record.definition.template_argument = parse_argument(words);
    } else if(words[0] == "let-expr") {
      require(words.size() >= 2, "incomplete expression definition");
      record.definition.id = words[1];
      record.definition.set_kind(ABI_DEFINITION_EXPRESSION);
      record.definition.expression = parse_expression(words);
    } else {
      record.definition = parse_definition(words);
    }
    return record;
  }
  const string & form = words[0];
  if(form == "type" || form == "function" || form == "c-function"
     || form == "variable" || form == "typeinfo" || form == "typeinfo-name" || form == "vtable"
     || form == "vtt" || form == "construction-vtable"
     || form == "tls-wrapper" || form == "thunk"
     || form == "virtual-base-thunk") {
    record.set_kind(ABI_FACT_RECORD_TARGET);
    record.target = parse_target(words);
  } else {
    record.set_kind(ABI_FACT_RECORD_FUNCTION);
    record.function = parse_function_record(words);
  }
  return record;
}

AbiFactFile parse_fact_text(const string & text)
{
  AbiFactFile file;
  std::istringstream lines(text);
  parse_fact_stream(lines, [&file](AbiFactCase fact_case) {
    file.cases.push_back(std::move(fact_case));
  }, nullptr);
  return file;
}

string serialize_fact_file(const AbiFactFile & file)
{
  std::ostringstream out;
  for(size_t i = 0; i < file.cases.size(); ++i) {
    const AbiFactCase & fact_case = file.cases[i];
    if(!fact_case.label.empty()) out << "case " << fact_case.label << '\n';
    for(const AbiFactRecord & record : fact_case.records) {
      out << record_text(record) << '\n';
    }
    if(i + 1 != file.cases.size()) out << '\n';
  }
  return out.str();
}

string mangle_fact_files(const vector<string> & input_paths)
{
  std::ostringstream result;
  mangle_fact_files_to_stream(input_paths, result);
  return result.str();
}

void mangle_fact_files_to_stream(const vector<string> & input_paths,
                                 std::ostream & output,
                                 AbiMangleStats * stats)
{
  for(const string & path : input_paths) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if(!input)
      throw InputOutputError("unable to open ABI fact file '" + path + "'",
                             CompilerErrorDomain::ABI);
    if(stats) ++stats->source_files;
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    unsigned long long encode_nanoseconds = 0;
    parse_fact_stream(input, [&](AbiFactCase fact_case) {
      AbiFactFile one_case;
      one_case.cases.push_back(std::move(fact_case));
      const std::chrono::steady_clock::time_point encode_start =
        std::chrono::steady_clock::now();
      mangle_fact_file_to_stream(one_case, output, stats);
      const std::chrono::steady_clock::time_point encode_end =
        std::chrono::steady_clock::now();
      encode_nanoseconds += static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          encode_end - encode_start).count());
    }, stats);
    const std::chrono::steady_clock::time_point end =
      std::chrono::steady_clock::now();
    if(stats) {
      const unsigned long long total_nanoseconds = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          end - start).count());
      stats->parse_nanoseconds += total_nanoseconds - encode_nanoseconds;
      stats->encode_nanoseconds += encode_nanoseconds;
    }
  }
  if(!output)
    throw InputOutputError("unable to write mangled ABI output",
                           CompilerErrorDomain::ABI);
}

}  // namespace abi_mangle
