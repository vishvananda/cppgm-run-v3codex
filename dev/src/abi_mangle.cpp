#include "abi_mangle.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace abi_mangle {
namespace {

using std::size_t;
using std::string;
using std::vector;

const size_t NO_ID = std::numeric_limits<size_t>::max();

void require(bool condition, const string & message)
{
  if(!condition) throw std::logic_error(message);
}

size_t mix_hash(size_t seed, size_t value)
{
  return seed ^ (value + static_cast<size_t>(0x9e3779b9U) + (seed << 6) + (seed >> 2));
}

template<class T>
size_t vector_hash(size_t seed, const vector<T> & values)
{
  for(const T & value : values) seed = mix_hash(seed, std::hash<T>()(value));
  return seed;
}

class StringPool
{
public:
  size_t intern(const string & value)
  {
    const auto found = indexes_.find(value);
    if(found != indexes_.end()) return found->second;
    const size_t id = values_.size();
    values_.push_back(value);
    indexes_.insert(std::make_pair(value, id));
    return id;
  }

  const string & get(size_t id) const
  {
    require(id < values_.size(), "invalid interned ABI string id");
    return values_[id];
  }

private:
  vector<string> values_;
  std::unordered_map<string, size_t> indexes_;
};

struct PathNode
{
  size_t parent = NO_ID;
  size_t name = NO_ID;

  PathNode() {}
  PathNode(size_t parent_value, size_t name_value)
    : parent(parent_value), name(name_value) {}

  bool operator==(const PathNode & other) const
  {
    return parent == other.parent && name == other.name;
  }
};

struct PathHash
{
  size_t operator()(const PathNode & path) const
  {
    return mix_hash(path.parent, path.name);
  }
};

class PathPool
{
public:
  PathPool(StringPool & strings, AbiMangleStats * stats)
    : strings_(strings), stats_(stats) {}

  size_t intern(const string & qualified)
  {
    size_t begin = qualified.compare(0, 2, "::") == 0 ? 2 : 0;
    size_t path = NO_ID;
    while(begin < qualified.size()) {
      const size_t separator = qualified.find("::", begin);
      const string component = qualified.substr(begin, separator - begin);
      if(component.empty()) {
        throw std::logic_error("empty component in ABI name '" + qualified + "'");
      }
      if(stats_) ++stats_->path_components;
      path = intern(path, strings_.intern(component));
      if(separator == string::npos) break;
      begin = separator + 2;
    }
    require(path != NO_ID, "empty ABI qualified name");
    return path;
  }

  size_t intern(size_t parent, size_t name)
  {
    const PathNode key{parent, name};
    const auto found = indexes_.find(key);
    if(found != indexes_.end()) return found->second;
    const size_t id = paths_.size();
    paths_.push_back(key);
    indexes_.insert(std::make_pair(key, id));
    return id;
  }

  const PathNode & get(size_t id) const
  {
    require(id < paths_.size(), "invalid ABI path id");
    return paths_[id];
  }

  vector<size_t> components(size_t id) const
  {
    vector<size_t> reverse;
    while(id != NO_ID) {
      reverse.push_back(paths_[id].name);
      id = paths_[id].parent;
    }
    return vector<size_t>(reverse.rbegin(), reverse.rend());
  }

  vector<size_t> prefixes(size_t id) const
  {
    vector<size_t> reverse;
    while(id != NO_ID) {
      reverse.push_back(id);
      id = paths_[id].parent;
    }
    return vector<size_t>(reverse.rbegin(), reverse.rend());
  }

private:
  StringPool & strings_;
  AbiMangleStats * stats_;
  vector<PathNode> paths_;
  std::unordered_map<PathNode, size_t, PathHash> indexes_;
};

struct TypeNode
{
  AbiTypeKind kind = ABI_TYPE_BUILTIN;
  size_t symbol = NO_ID;
  size_t path = NO_ID;
  size_t expression = NO_ID;
  size_t context = NO_ID;
  size_t discriminator = NO_ID;
  size_t substitution = NO_ID;
  size_t index = 0;
  AbiArrayBoundKind bound_kind = ABI_ARRAY_BOUND_VALUE;
  bool is_const = false;
  bool is_volatile = false;
  bool variadic = false;
  bool lvalue_ref = false;
  bool rvalue_ref = false;
  bool substitutable = false;
  bool suppress_template_prefix_substitution = false;
  bool standard_includes_arguments = false;
  vector<size_t> children;
  vector<size_t> arguments;
  vector<size_t> namespaces;
  vector<size_t> tags;

  bool operator==(const TypeNode & other) const
  {
    return kind == other.kind && symbol == other.symbol && path == other.path
           && expression == other.expression && context == other.context
           && discriminator == other.discriminator
           && substitution == other.substitution
           && index == other.index && bound_kind == other.bound_kind
           && is_const == other.is_const && is_volatile == other.is_volatile
           && variadic == other.variadic && lvalue_ref == other.lvalue_ref
           && rvalue_ref == other.rvalue_ref
           && substitutable == other.substitutable
           && suppress_template_prefix_substitution ==
                other.suppress_template_prefix_substitution
           && standard_includes_arguments == other.standard_includes_arguments
           && children == other.children && arguments == other.arguments
           && namespaces == other.namespaces && tags == other.tags;
  }
};

size_t type_hash(const TypeNode & type)
{
  size_t hash = static_cast<size_t>(type.kind);
  hash = mix_hash(hash, type.symbol);
  hash = mix_hash(hash, type.path);
  hash = mix_hash(hash, type.expression);
  hash = mix_hash(hash, type.context);
  hash = mix_hash(hash, type.discriminator);
  hash = mix_hash(hash, type.substitution);
  hash = mix_hash(hash, type.index);
  hash = mix_hash(hash, static_cast<size_t>(type.bound_kind));
  hash = mix_hash(hash, type.is_const | (type.is_volatile << 1) | (type.variadic << 2)
                        | (type.standard_includes_arguments << 3)
                        | (type.substitutable << 4)
                        | (type.suppress_template_prefix_substitution << 5)
                        | (type.lvalue_ref << 6) | (type.rvalue_ref << 7));
  hash = vector_hash(hash, type.children);
  hash = vector_hash(hash, type.arguments);
  hash = vector_hash(hash, type.namespaces);
  return vector_hash(hash, type.tags);
}

struct ArgumentNode
{
  AbiTemplateArgumentKind kind = ABI_TEMPLATE_ARGUMENT_TYPE;
  size_t type = NO_ID;
  size_t value_type = NO_ID;
  size_t owner_type = NO_ID;
  size_t expression = NO_ID;
  size_t entity = NO_ID;
  size_t name = NO_ID;
  size_t substitution = NO_ID;
  size_t symbol = NO_ID;
  size_t index = 0;
  long long value = 0;
  bool has_value_type = false;
  bool address_of = false;
  bool pack_expansion = false;
  bool member_is_function = false;
  bool member_const = false;
  bool member_volatile = false;
  bool member_lvalue_ref = false;
  bool member_rvalue_ref = false;
  bool member_variadic = false;
  AbiMemberFunctionTerminalKind member_terminal_kind =
    ABI_MEMBER_FUNCTION_TERMINAL_SOURCE;
  size_t member_terminal = NO_ID;
  size_t member_literal_suffix = NO_ID;
  size_t member_conversion_type = NO_ID;
  size_t member_result_type = NO_ID;
  bool member_has_result_type = false;
  vector<size_t> parameters;
  vector<size_t> arguments;

  bool operator==(const ArgumentNode & other) const
  {
    return kind == other.kind && type == other.type && value_type == other.value_type
           && owner_type == other.owner_type && expression == other.expression
           && entity == other.entity && name == other.name
           && substitution == other.substitution && symbol == other.symbol
           && index == other.index && value == other.value
           && has_value_type == other.has_value_type && address_of == other.address_of
           && pack_expansion == other.pack_expansion
           && member_is_function == other.member_is_function
           && member_const == other.member_const && member_volatile == other.member_volatile
           && member_lvalue_ref == other.member_lvalue_ref
           && member_rvalue_ref == other.member_rvalue_ref
           && member_variadic == other.member_variadic
           && member_terminal_kind == other.member_terminal_kind
           && member_terminal == other.member_terminal
           && member_literal_suffix == other.member_literal_suffix
           && member_conversion_type == other.member_conversion_type
           && member_result_type == other.member_result_type
           && member_has_result_type == other.member_has_result_type
           && parameters == other.parameters && arguments == other.arguments;
  }
};

size_t argument_hash(const ArgumentNode & argument)
{
  size_t hash = static_cast<size_t>(argument.kind);
  hash = mix_hash(hash, argument.type);
  hash = mix_hash(hash, argument.value_type);
  hash = mix_hash(hash, argument.owner_type);
  hash = mix_hash(hash, argument.expression);
  hash = mix_hash(hash, argument.entity);
  hash = mix_hash(hash, argument.name);
  hash = mix_hash(hash, argument.substitution);
  hash = mix_hash(hash, argument.symbol);
  hash = mix_hash(hash, static_cast<size_t>(argument.member_terminal_kind));
  hash = mix_hash(hash, argument.member_terminal);
  hash = mix_hash(hash, argument.member_literal_suffix);
  hash = mix_hash(hash, argument.member_conversion_type);
  hash = mix_hash(hash, argument.member_result_type);
  hash = mix_hash(hash, argument.index);
  hash = mix_hash(hash, std::hash<long long>()(argument.value));
  hash = mix_hash(hash, argument.has_value_type | (argument.address_of << 1)
                        | (argument.member_is_function << 2) | (argument.member_const << 3)
                        | (argument.member_volatile << 4) | (argument.member_lvalue_ref << 5)
                        | (argument.member_rvalue_ref << 6) | (argument.member_variadic << 7)
                        | (argument.member_has_result_type << 8)
						| (argument.pack_expansion << 9));
  hash = vector_hash(hash, argument.parameters);
  return vector_hash(hash, argument.arguments);
}

struct ExpressionNode
{
  AbiExpressionKind kind = ABI_EXPRESSION_LITERAL;
  size_t symbol = NO_ID;
  size_t op = NO_ID;
  size_t type = NO_ID;
  size_t value_type = NO_ID;
  size_t entity = NO_ID;
  size_t index = 0;
  long long value = 0;
  bool close_member_owner = false;
  bool address_of = false;
  vector<size_t> expressions;
  vector<size_t> arguments;
  vector<size_t> types;

  bool operator==(const ExpressionNode & other) const
  {
    return kind == other.kind && symbol == other.symbol && op == other.op && type == other.type
           && value_type == other.value_type && entity == other.entity
           && index == other.index && value == other.value
           && close_member_owner == other.close_member_owner
           && address_of == other.address_of && expressions == other.expressions
           && arguments == other.arguments && types == other.types;
  }
};

size_t expression_hash(const ExpressionNode & expression)
{
  size_t hash = static_cast<size_t>(expression.kind);
  hash = mix_hash(hash, expression.symbol);
  hash = mix_hash(hash, expression.op);
  hash = mix_hash(hash, expression.type);
  hash = mix_hash(hash, expression.value_type);
  hash = mix_hash(hash, expression.entity);
  hash = mix_hash(hash, expression.index);
  hash = mix_hash(hash, std::hash<long long>()(expression.value));
  hash = mix_hash(hash, expression.close_member_owner | (expression.address_of << 1));
  hash = vector_hash(hash, expression.expressions);
  hash = vector_hash(hash, expression.arguments);
  return vector_hash(hash, expression.types);
}

bool is_builtin_name(const string & name)
{
  static const char * names[] = {
    "void", "bool", "char", "schar", "uchar", "wchar", "char16", "char32",
    "short", "ushort", "int", "uint", "long", "ulong", "longlong", "ulonglong",
    "int128", "uint128",
    "float", "double", "longdouble", "float16", "float32", "float32x",
    "float64", "float64x", "stdfloat128", "float128", "complex-float",
    "complex-double", "complex-longdouble", "nullptr"
  };
  for(const char * candidate : names) {
    if(name == candidate) return true;
  }
  const size_t first = name.compare(0, 7, "ubitint") == 0 ? 7 :
    name.compare(0, 6, "bitint") == 0 ? 6 : string::npos;
  if(first != string::npos && first < name.size()) {
    for(size_t i = first; i < name.size(); ++i)
      if(name[i] < '0' || name[i] > '9') return false;
    return true;
  }
  return false;
}

class FactGraph
{
public:
  FactGraph(const AbiFactCase & fact_case, AbiMangleStats * stats)
    : paths(strings, stats), stats_(stats)
  {
    for(const AbiFactRecord & record : fact_case.records) {
      if(record.kind != ABI_FACT_RECORD_DEFINITION) continue;
      const AbiDefinitionRecord & definition = record.definition;
      require(definitions.insert(std::make_pair(definition.id, &definition)).second,
              "duplicate ABI definition id '" + definition.id + "'");
    }
  }

  size_t resolve_type(const AbiType & source)
  {
    size_t result = resolve_type_core(source);
    for(auto modifier = source.modifiers.rbegin();
        modifier != source.modifiers.rend(); ++modifier) {
      TypeNode node;
      node.kind = modifier->kind;
      require(node.kind == ABI_TYPE_POINTER || node.kind == ABI_TYPE_LVALUE_REFERENCE
              || node.kind == ABI_TYPE_RVALUE_REFERENCE
              || node.kind == ABI_TYPE_PACK_EXPANSION || node.kind == ABI_TYPE_CV
              || node.kind == ABI_TYPE_ARRAY,
              "invalid flat ABI type modifier");
      node.children.push_back(result);
      node.bound_kind = modifier->array_bound.kind;
      if(!modifier->array_bound.value.empty()) {
        if(node.bound_kind == ABI_ARRAY_BOUND_EXPRESSION)
          node.expression = resolve_expression_ref(modifier->array_bound.value);
        else
          node.symbol = strings.intern(modifier->array_bound.value);
      }
      node.is_const = modifier->is_const;
      node.is_volatile = modifier->is_volatile;
      result = canonicalize_type(node);
    }
    return result;
  }

  size_t resolve_type_core(const AbiType & source)
  {
    if(source.kind == ABI_TYPE_NAME_OR_REFERENCE) {
      const auto definition = definitions.find(source.name);
      if(definition != definitions.end()
         && definition->second->kind == ABI_DEFINITION_TYPE) {
        return resolve_type_definition(source.name, *definition->second);
      }
    }
    TypeNode node;
    node.kind = source.kind;
    if(source.kind == ABI_TYPE_NAME_OR_REFERENCE && is_builtin_name(source.name)) {
      node.kind = ABI_TYPE_BUILTIN;
      node.symbol = strings.intern(source.name);
    } else if(source.kind == ABI_TYPE_NAME_OR_REFERENCE || source.kind == ABI_TYPE_NAMED) {
      node.kind = ABI_TYPE_NAMED;
      node.path = paths.intern(source.name);
	  if(!source.substitution.empty()) node.substitution = strings.intern(source.substitution);
    } else {
      if(!source.name.empty()) node.symbol = strings.intern(source.name);
      if((source.kind == ABI_TYPE_TEMPLATE_SPECIALIZATION
          || source.kind == ABI_TYPE_STD_TEMPLATE_SPECIALIZATION)
         && !source.name.empty()) {
        node.path = paths.intern(source.name);
      }
      if(!source.standard_substitution.empty()) {
        node.symbol = strings.intern(source.standard_substitution);
      }
      if(!source.expression_ref.empty()) node.expression = resolve_expression_ref(source.expression_ref);
      if(!source.context_ref.empty()) node.context = strings.intern(source.context_ref);
      if(source.kind == ABI_TYPE_LAMBDA_CLOSURE ||
         !source.discriminator.empty())
        node.discriminator = strings.intern(source.discriminator);
      if(!source.substitution.empty()) node.substitution = strings.intern(source.substitution);
      node.index = source.index;
      node.bound_kind = source.array_bound.kind;
      if(!source.array_bound.value.empty()) {
        if(node.bound_kind == ABI_ARRAY_BOUND_EXPRESSION)
          node.expression = resolve_expression_ref(source.array_bound.value);
        else
          node.symbol = strings.intern(source.array_bound.value);
      }
      node.is_const = source.is_const;
      node.is_volatile = source.is_volatile;
      node.variadic = source.variadic;
      node.lvalue_ref = source.lvalue_ref;
      node.rvalue_ref = source.rvalue_ref;
      node.substitutable = source.substitutable;
      node.suppress_template_prefix_substitution =
        source.suppress_template_prefix_substitution;
      node.standard_includes_arguments = source.standard_substitution_includes_arguments;
      for(const AbiType & child : source.types) node.children.push_back(resolve_type(child));
      for(const string & argument : source.argument_refs) node.arguments.push_back(resolve_argument_ref(argument));
      for(const string & name : source.namespace_qualifiers) node.namespaces.push_back(strings.intern(name));
    }
    for(const string & tag : source.abi_tags) node.tags.push_back(strings.intern(tag));
    std::sort(node.tags.begin(), node.tags.end(), [this](size_t a, size_t b) {
      return strings.get(a) < strings.get(b);
    });
    node.tags.erase(std::unique(node.tags.begin(), node.tags.end()), node.tags.end());
    return canonicalize_type(node);
  }

  size_t resolve_argument_ref(const string & id)
  {
    const auto cached = argument_definitions.find(id);
    if(cached != argument_definitions.end()) {
      if(stats_) ++stats_->definition_cache_hits;
      return cached->second;
    }
    const auto found = definitions.find(id);
    require(found != definitions.end()
            && found->second->kind == ABI_DEFINITION_TEMPLATE_ARGUMENT,
            "unknown ABI template argument '" + id + "'");
    require(resolving_arguments.insert(id).second,
            "recursive ABI template argument '" + id + "'");
    const size_t result = resolve_argument(found->second->template_argument);
    resolving_arguments.erase(id);
    argument_definitions.insert(std::make_pair(id, result));
    return result;
  }

  size_t resolve_expression_ref(const string & id)
  {
    const auto cached = expression_definitions.find(id);
    if(cached != expression_definitions.end()) {
      if(stats_) ++stats_->definition_cache_hits;
      return cached->second;
    }
    const auto found = definitions.find(id);
    require(found != definitions.end() && found->second->kind == ABI_DEFINITION_EXPRESSION,
            "unknown ABI expression '" + id + "'");
    require(resolving_expressions.insert(id).second, "recursive ABI expression '" + id + "'");
    const size_t result = resolve_expression(found->second->expression);
    resolving_expressions.erase(id);
    expression_definitions.insert(std::make_pair(id, result));
    return result;
  }

  const AbiDefinitionRecord & definition(const string & id, AbiDefinitionKind kind) const
  {
    const auto found = definitions.find(id);
    require(found != definitions.end() && found->second->kind == kind,
            "unknown ABI definition '" + id + "'");
    return *found->second;
  }

  bool is_argument_definition(const string & id) const
  {
    const auto found = definitions.find(id);
    return found != definitions.end()
           && found->second->kind == ABI_DEFINITION_TEMPLATE_ARGUMENT;
  }

  const TypeNode & type(size_t id) const { return types.at(id); }
  const ArgumentNode & argument(size_t id) const { return arguments.at(id); }
  const ExpressionNode & expression(size_t id) const { return expressions.at(id); }

  StringPool strings;
  PathPool paths;

private:
  size_t resolve_type_definition(const string & id, const AbiDefinitionRecord & definition)
  {
    const auto cached = type_definitions.find(id);
    if(cached != type_definitions.end()) {
      if(stats_) ++stats_->definition_cache_hits;
      return cached->second;
    }
    require(resolving_types.insert(id).second, "recursive ABI type '" + id + "'");
    const size_t result = resolve_type(definition.type);
    resolving_types.erase(id);
    type_definitions.insert(std::make_pair(id, result));
    return result;
  }

  size_t canonicalize_type(TypeNode node)
  {
    if(node.kind == ABI_TYPE_CV && node.children.size() == 1) {
      const TypeNode & child = types[node.children[0]];
      if(!node.is_const && !node.is_volatile && !node.tags.empty()) {
        TypeNode tagged = child;
        tagged.tags.insert(tagged.tags.end(), node.tags.begin(), node.tags.end());
        std::sort(tagged.tags.begin(), tagged.tags.end());
        tagged.tags.erase(std::unique(tagged.tags.begin(), tagged.tags.end()), tagged.tags.end());
        return intern_type(tagged);
      }
      if(child.kind == ABI_TYPE_CV && child.tags.empty()) {
        node.children[0] = child.children.at(0);
        node.is_const = node.is_const || child.is_const;
        node.is_volatile = node.is_volatile || child.is_volatile;
      }
    }
    return intern_type(node);
  }

  size_t intern_type(const TypeNode & node)
  {
    const size_t hash = type_hash(node);
    vector<size_t> & bucket = type_buckets[hash];
    for(size_t id : bucket) {
      if(types[id] == node) {
        if(stats_) ++stats_->canonical_cache_hits;
        return id;
      }
    }
    const size_t id = types.size();
    types.push_back(node);
    bucket.push_back(id);
    if(stats_) ++stats_->canonical_types;
    return id;
  }

  size_t resolve_argument(const AbiTemplateArgument & source)
  {
    ArgumentNode node;
    node.kind = source.kind;
    if(source.kind == ABI_TEMPLATE_ARGUMENT_TYPE) node.type = resolve_type(source.type);
    if(source.kind == ABI_TEMPLATE_ARGUMENT_VALUE
       || source.kind == ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE
       || source.has_value_type) {
      node.value_type = resolve_type(source.value_type);
    }
    if(source.kind == ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE) node.type = resolve_type(source.type);
    if(source.kind == ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY
       || source.kind == ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY) {
      node.owner_type = resolve_type(source.type);
    }
    if(source.kind == ABI_TEMPLATE_ARGUMENT_EXPRESSION) {
      node.expression = resolve_expression_ref(source.entity_ref);
    }
    if(source.kind == ABI_TEMPLATE_ARGUMENT_ENTITY) node.entity = strings.intern(source.entity_ref);
    if(!source.name.empty()) node.name = strings.intern(source.name);
    if(!source.substitution.empty()) node.substitution = strings.intern(source.substitution);
    if(!source.symbol.empty()) node.symbol = strings.intern(source.symbol);
    node.index = source.index;
    node.value = source.value;
    node.has_value_type = source.has_value_type;
    node.address_of = source.address_of;
    node.pack_expansion = source.pack_expansion;
    node.member_is_function = source.member_is_function;
    node.member_const = source.member_function_const;
    node.member_volatile = source.member_function_volatile;
    node.member_lvalue_ref = source.member_function_lvalue_ref;
    node.member_rvalue_ref = source.member_function_rvalue_ref;
    node.member_variadic = source.member_function_variadic;
    node.member_terminal_kind = source.member_function_terminal_kind;
    if(!source.member_function_terminal.empty()) {
      node.member_terminal = strings.intern(source.member_function_terminal);
    }
    if(!source.member_function_literal_suffix.empty()) {
      node.member_literal_suffix = strings.intern(source.member_function_literal_suffix);
    }
    if(source.member_function_terminal_kind == ABI_MEMBER_FUNCTION_TERMINAL_CONVERSION) {
      node.member_conversion_type = resolve_type(source.member_function_conversion_type);
    }
    node.member_has_result_type = source.member_function_has_result_type;
    if(source.member_function_has_result_type) {
      node.member_result_type = resolve_type(source.member_function_result_type);
    }
    for(const AbiType & type : source.parameter_types) node.parameters.push_back(resolve_type(type));
    for(const string & argument : source.argument_refs) node.arguments.push_back(resolve_argument_ref(argument));
    const size_t hash = argument_hash(node);
    vector<size_t> & bucket = argument_buckets[hash];
    for(size_t id : bucket) {
      if(arguments[id] == node) {
        if(stats_) ++stats_->canonical_cache_hits;
        return id;
      }
    }
    const size_t id = arguments.size();
    arguments.push_back(node);
    bucket.push_back(id);
    if(stats_) ++stats_->canonical_arguments;
    return id;
  }

  size_t resolve_expression(const AbiDependentExpression & source)
  {
    ExpressionNode node;
    node.kind = source.kind;
    const string spelling = !source.text.empty() ? source.text : source.op;
    if(!spelling.empty()) node.symbol = strings.intern(spelling);
    if(!source.op.empty()) node.op = strings.intern(source.op);
    if(source.kind == ABI_EXPRESSION_CAST || source.kind == ABI_EXPRESSION_SIZEOF_TYPE
       || source.kind == ABI_EXPRESSION_MEMBER) node.type = resolve_type(source.type);
    if(source.kind == ABI_EXPRESSION_INTEGRAL_VALUE) node.value_type = resolve_type(source.value_type);
    if(!source.entity_ref.empty()) node.entity = strings.intern(source.entity_ref);
    node.index = source.index;
    node.value = source.value;
    node.close_member_owner = source.close_member_owner;
    node.address_of = source.address_of;
    for(const string & expression : source.expression_refs) {
      node.expressions.push_back(resolve_expression_ref(expression));
    }
    for(const string & argument : source.argument_refs) {
      node.arguments.push_back(resolve_argument_ref(argument));
    }
    for(const AbiType & type : source.type_arguments) node.types.push_back(resolve_type(type));
    const size_t hash = expression_hash(node);
    vector<size_t> & bucket = expression_buckets[hash];
    for(size_t id : bucket) {
      if(expressions[id] == node) {
        if(stats_) ++stats_->canonical_cache_hits;
        return id;
      }
    }
    const size_t id = expressions.size();
    expressions.push_back(node);
    bucket.push_back(id);
    if(stats_) ++stats_->canonical_expressions;
    return id;
  }

  AbiMangleStats * stats_;
  std::unordered_map<string, const AbiDefinitionRecord *> definitions;
  std::unordered_map<string, size_t> type_definitions;
  std::unordered_map<string, size_t> argument_definitions;
  std::unordered_map<string, size_t> expression_definitions;
  std::unordered_set<string> resolving_types;
  std::unordered_set<string> resolving_arguments;
  std::unordered_set<string> resolving_expressions;
  vector<TypeNode> types;
  vector<ArgumentNode> arguments;
  vector<ExpressionNode> expressions;
  std::unordered_map<size_t, vector<size_t> > type_buckets;
  std::unordered_map<size_t, vector<size_t> > argument_buckets;
  std::unordered_map<size_t, vector<size_t> > expression_buckets;
};

enum SubstitutionKind
{
  SUBSTITUTION_PATH,
  SUBSTITUTION_TYPE,
  SUBSTITUTION_FUNCTION_TEMPLATE_PREFIX,
  SUBSTITUTION_EXPLICIT,
  SUBSTITUTION_LOCAL_LAMBDA
};

struct SubstitutionKey
{
  SubstitutionKind kind = SUBSTITUTION_TYPE;
  size_t id = 0;

  SubstitutionKey() {}
  SubstitutionKey(SubstitutionKind kind_value, size_t id_value)
    : kind(kind_value), id(id_value) {}

  bool operator==(const SubstitutionKey & other) const
  {
    return kind == other.kind && id == other.id;
  }
};

struct SubstitutionHash
{
  size_t operator()(const SubstitutionKey & key) const
  {
    return mix_hash(static_cast<size_t>(key.kind), key.id);
  }
};

string base36(size_t value)
{
  const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  string result;
  do {
    result.push_back(digits[value % 36]);
    value /= 36;
  } while(value != 0);
  std::reverse(result.begin(), result.end());
  return result;
}

class SubstitutionTable
{
public:
  explicit SubstitutionTable(AbiMangleStats * stats = nullptr) : stats_(stats) {}

  bool emit_if_known(const SubstitutionKey & key, string & output) const
  {
    if(stats_) ++stats_->substitution_lookups;
    const auto found = indexes_.find(key);
    if(found == indexes_.end()) return false;
    if(stats_) ++stats_->substitution_hits;
    output += 'S';
    if(found->second != 0) output += base36(found->second - 1);
    output += '_';
    return true;
  }

  void add(const SubstitutionKey & key)
  {
    if(indexes_.find(key) != indexes_.end()) return;
    indexes_.insert(std::make_pair(key, indexes_.size()));
    if(stats_) ++stats_->substitution_entries;
  }

  void swap(SubstitutionTable & other)
  {
    indexes_.swap(other.indexes_);
  }

private:
  AbiMangleStats * stats_;
  std::unordered_map<SubstitutionKey, size_t, SubstitutionHash> indexes_;
};

string source_name(const string & name)
{
  return std::to_string(name.size()) + name;
}

string number(long long value)
{
  if(value >= 0) return std::to_string(value);
  const unsigned long long magnitude = static_cast<unsigned long long>(-(value + 1)) + 1;
  return "n" + std::to_string(magnitude);
}

string discriminator(const string & occurrence)
{
  if(occurrence.empty() || occurrence == "0") return string();
  size_t value = 0;
  for(char ch : occurrence) {
    if(ch < '0' || ch > '9') {
      throw std::logic_error("invalid local discriminator '" + occurrence + "'");
    }
    const size_t digit = static_cast<size_t>(ch - '0');
    if(value > (std::numeric_limits<size_t>::max() - digit) / 10) {
      throw std::logic_error("local discriminator out of range '" + occurrence + "'");
    }
    value = value * 10 + digit;
  }
  require(value != 0, "invalid local discriminator");
  --value;
  if(value < 10) return "_" + std::to_string(value);
  return "__" + std::to_string(value) + "_";
}

string builtin_code(const string & name)
{
  static const struct Entry { const char * name; const char * code; } entries[] = {
    {"void", "v"}, {"bool", "b"}, {"char", "c"}, {"schar", "a"},
    {"uchar", "h"}, {"wchar", "w"}, {"char16", "Ds"}, {"char32", "Di"},
    {"short", "s"}, {"ushort", "t"}, {"int", "i"}, {"uint", "j"},
    {"long", "l"}, {"ulong", "m"}, {"longlong", "x"}, {"ulonglong", "y"},
    {"int128", "n"}, {"uint128", "o"},
    {"float", "f"}, {"double", "d"}, {"longdouble", "e"},
    {"float16", "DF16_"}, {"float32", "DF32_"},
    {"float32x", "DF32x"}, {"float64", "DF64_"},
    {"float64x", "DF64x"}, {"stdfloat128", "DF128_"},
    {"float128", "g"},
    {"complex-float", "Cf"}, {"complex-double", "Cd"},
    {"complex-longdouble", "Ce"}, {"nullptr", "Dn"}
  };
  for(const Entry & entry : entries) if(name == entry.name) return entry.code;
  if(name.compare(0, 7, "ubitint") == 0)
    return "DU" + name.substr(7) + '_';
  if(name.compare(0, 6, "bitint") == 0)
    return "DB" + name.substr(6) + '_';
  throw std::logic_error("unknown ABI builtin type '" + name + "'");
}

class Encoder
{
public:
  Encoder(const AbiFactCase & fact_case, FactGraph & graph, AbiMangleStats * stats)
    : fact_case_(fact_case), graph_(graph), stats_(stats), substitutions_(stats) {}

  string mangle()
  {
    const AbiTargetRecord * target = nullptr;
    vector<const AbiFunctionRecord *> records;
    for(const AbiFactRecord & record : fact_case_.records) {
      if(record.kind == ABI_FACT_RECORD_TARGET) {
        require(target == nullptr, "ABI case has more than one target");
        target = &record.target;
      } else if(record.kind == ABI_FACT_RECORD_FUNCTION) {
        records.push_back(&record.function);
      }
    }
    require(target != nullptr, "ABI case has no target");
    return mangle_target(*target, records);
  }

private:
  struct FunctionFacts
  {
    vector<const AbiFunctionRecord *> components;
    vector<size_t> template_arguments;
    vector<size_t> parameters;
    vector<size_t> result_types;
    vector<AbiFunctionQualifier> qualifiers;
    vector<size_t> tags;
    std::unordered_map<const AbiFunctionRecord *, vector<size_t> > component_tags;
    const AbiFunctionRecord * local = nullptr;
    const AbiFunctionRecord * lambda = nullptr;
    const AbiFunctionRecord * namespace_lambda = nullptr;
    const AbiFunctionRecord * terminal = nullptr;
    size_t template_prefix = NO_ID;
    bool variadic = false;
  };

  SubstitutionKey local_lambda_key(const string & context,
                                   const string & discriminator)
  {
    return SubstitutionKey{
      SUBSTITUTION_LOCAL_LAMBDA,
      graph_.strings.intern(context + "\x1f" + discriminator)};
  }

  FunctionFacts collect_function_facts(const vector<const AbiFunctionRecord *> & records)
  {
    FunctionFacts facts;
    const AbiFunctionRecord * component_tag_target = nullptr;
    for(const AbiFunctionRecord * record : records) {
      switch(record->kind) {
        case ABI_FUNCTION_RECORD_NAME_SOURCE:
        case ABI_FUNCTION_RECORD_NAME_STD:
        case ABI_FUNCTION_RECORD_NAME_TEMPLATE:
          facts.components.push_back(record);
          component_tag_target = record;
          break;
        case ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT:
          facts.template_arguments.push_back(graph_.resolve_argument_ref(record->argument_refs.at(0)));
          break;
        case ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX:
          require(facts.template_prefix == NO_ID, "multiple ABI function-template prefixes");
          facts.template_prefix = graph_.strings.intern(record->substitution);
          break;
        case ABI_FUNCTION_RECORD_LOCAL_CONTEXT:
          require(facts.local == nullptr, "multiple ABI local contexts");
          facts.local = record;
          component_tag_target = record;
          break;
        case ABI_FUNCTION_RECORD_LAMBDA_CONTEXT:
          require(facts.lambda == nullptr, "multiple ABI lambda contexts");
          facts.lambda = record;
          break;
        case ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT:
          require(facts.namespace_lambda == nullptr, "multiple ABI namespace-lambda contexts");
          facts.namespace_lambda = record;
          break;
        case ABI_FUNCTION_RECORD_TERMINAL_SOURCE:
        case ABI_FUNCTION_RECORD_TERMINAL:
        case ABI_FUNCTION_RECORD_OPERATOR_TERMINAL:
        case ABI_FUNCTION_RECORD_CONVERSION_TERMINAL:
          require(facts.terminal == nullptr, "multiple ABI function terminals");
          facts.terminal = record;
          break;
        case ABI_FUNCTION_RECORD_VARIADIC:
          require(!facts.variadic, "multiple ABI variadic markers");
          facts.variadic = true;
          break;
        case ABI_FUNCTION_RECORD_ABI_TAG: facts.tags.push_back(graph_.strings.intern(record->name)); break;
        case ABI_FUNCTION_RECORD_COMPONENT_ABI_TAG:
          require(component_tag_target != nullptr,
                  "component ABI tag has no preceding name component");
          facts.component_tags[component_tag_target].push_back(
            graph_.strings.intern(record->name));
          break;
        case ABI_FUNCTION_RECORD_QUALIFIER:
          facts.qualifiers.insert(facts.qualifiers.end(), record->qualifiers.begin(), record->qualifiers.end());
          break;
        case ABI_FUNCTION_RECORD_PARAMETER: facts.parameters.push_back(graph_.resolve_type(record->type)); break;
        case ABI_FUNCTION_RECORD_RESULT: facts.result_types.push_back(graph_.resolve_type(record->type)); break;
      }
    }
    std::sort(facts.tags.begin(), facts.tags.end(), [this](size_t a, size_t b) {
      return graph_.strings.get(a) < graph_.strings.get(b);
    });
    facts.tags.erase(std::unique(facts.tags.begin(), facts.tags.end()), facts.tags.end());
    for(auto & entry : facts.component_tags) {
      std::sort(entry.second.begin(), entry.second.end(), [this](size_t a, size_t b) {
        return graph_.strings.get(a) < graph_.strings.get(b);
      });
      entry.second.erase(std::unique(entry.second.begin(), entry.second.end()),
                         entry.second.end());
    }
    const size_t local_contexts = (facts.local != nullptr) + (facts.lambda != nullptr)
                                  + (facts.namespace_lambda != nullptr);
    require(local_contexts <= 1, "multiple ABI function context kinds");
    require(facts.result_types.size() <= 1, "multiple ABI function result types");
    const bool lvalue = std::find(facts.qualifiers.begin(), facts.qualifiers.end(),
                                  ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE)
                        != facts.qualifiers.end();
    const bool rvalue = std::find(facts.qualifiers.begin(), facts.qualifiers.end(),
                                  ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE)
                        != facts.qualifiers.end();
    require(!(lvalue && rvalue), "conflicting ABI function reference qualifiers");
    return facts;
  }

  string mangle_target(const AbiTargetRecord & target,
                       const vector<const AbiFunctionRecord *> & records)
  {
    const bool structured_variable = target.kind == ABI_TARGET_FACT_VARIABLE
                                     && (target.function.kind == ABI_FUNCTION_TARGET_ENCODING
                                         || target.function.kind == ABI_FUNCTION_TARGET_MEMBER);
    const bool accepts_function_records = target.kind == ABI_TARGET_FACT_FUNCTION
                                          || structured_variable
                                          || target.kind == ABI_TARGET_FACT_THUNK
                                          || target.kind == ABI_TARGET_FACT_VIRTUAL_BASE_THUNK;
    require(records.empty() || accepts_function_records,
            "function records attached to a non-function ABI target");
    if(target.kind == ABI_TARGET_FACT_TYPE) {
      encode_type(graph_.resolve_type(target.type));
      return output_;
    }
    if(target.kind == ABI_TARGET_FACT_FUNCTION) {
      if(target.c_linkage) {
        require(records.empty(), "C-linkage function cannot have ABI function records");
        require(target.function.kind == ABI_FUNCTION_TARGET_PATH,
                "C-linkage target must be a function path");
        return final_name(target.function.qualified_name);
      }
      output_ += "_Z";
      encode_function(target.function, collect_function_facts(records),
                      target.internal_linkage);
      return output_;
    }
    if(target.kind == ABI_TARGET_FACT_VARIABLE) {
      output_ += "_Z";
      if(target.function.kind == ABI_FUNCTION_TARGET_ENCODING) {
        encode_structured_object(collect_function_facts(records));
      } else if(target.function.kind == ABI_FUNCTION_TARGET_MEMBER) {
        encode_member_object(target.function, collect_function_facts(records));
      } else {
        encode_object_name(target.qualified_name, target.internal_linkage);
      }
      return output_;
    }
    if(target.kind == ABI_TARGET_FACT_TYPEINFO
       || target.kind == ABI_TARGET_FACT_TYPEINFO_NAME
       || target.kind == ABI_TARGET_FACT_VTABLE || target.kind == ABI_TARGET_FACT_VTT) {
      output_ += target.kind == ABI_TARGET_FACT_TYPEINFO ? "_ZTI"
                 : target.kind == ABI_TARGET_FACT_TYPEINFO_NAME ? "_ZTS"
                 : target.kind == ABI_TARGET_FACT_VTABLE ? "_ZTV" : "_ZTT";
      encode_type(graph_.resolve_type(target.type));
      return output_;
    }
    if(target.kind == ABI_TARGET_FACT_CONSTRUCTION_VTABLE) {
      output_ += "_ZTC";
      encode_type(graph_.resolve_type(target.type));
      output_ += std::to_string(target.base_offset) + '_';
      encode_type(graph_.resolve_type(target.base_type));
      return output_;
    }
    if(target.kind == ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER) {
      output_ += "_ZTW";
      encode_object_name(target.qualified_name, false);
      return output_;
    }
    if(target.kind == ABI_TARGET_FACT_THUNK || target.kind == ABI_TARGET_FACT_VIRTUAL_BASE_THUNK) {
      output_ += "_ZT";
      if(target.kind == ABI_TARGET_FACT_VIRTUAL_BASE_THUNK) {
        output_ += "v0_" + number(target.vcall_offset) + '_';
      } else if(target.has_result_adjust) {
        output_ += 'c';
        encode_fixed_call_offset(target.this_adjust);
        if(target.result_adjust_virtual) {
          output_ += 'v' + number(target.result_adjust) + '_'
                     + number(target.result_vcall_offset) + '_';
        } else {
          encode_fixed_call_offset(target.result_adjust);
        }
      } else {
        encode_fixed_call_offset(target.this_adjust);
      }
      encode_function(target.function, collect_function_facts(records));
      return output_;
    }
    throw std::logic_error("unsupported ABI target kind");
  }

  void encode_fixed_call_offset(long long offset)
  {
    output_ += 'h' + number(offset) + '_';
  }

  void encode_type(size_t id, bool retain_complete_substitution = true)
  {
    vector<SubstitutionKey> pending;
    for(;;) {
      const TypeNode & type = graph_.type(id);
      if(type.kind == ABI_TYPE_BUILTIN) {
        output_ += builtin_code(graph_.strings.get(type.symbol));
        break;
      }
      if(type.kind == ABI_TYPE_TEMPLATE_PARAMETER && !type.substitutable) {
        output_ += template_parameter(type.index);
        break;
      }
      const SubstitutionKey key = type.kind == ABI_TYPE_LAMBDA_CLOSURE
                                    ? local_lambda_key(
                                        graph_.strings.get(type.context),
                                        graph_.strings.get(type.discriminator))
                                    : type.substitution != NO_ID
                                    ? SubstitutionKey{SUBSTITUTION_EXPLICIT, type.substitution}
                                    : type.kind == ABI_TYPE_NAMED && type.tags.empty()
                                    ? SubstitutionKey{SUBSTITUTION_PATH, type.path}
                                    : SubstitutionKey{SUBSTITUTION_TYPE, id};
      if(substitutions_.emit_if_known(key, output_)) break;

      const bool unary = type.kind == ABI_TYPE_POINTER
                         || type.kind == ABI_TYPE_LVALUE_REFERENCE
                         || type.kind == ABI_TYPE_RVALUE_REFERENCE
                         || type.kind == ABI_TYPE_PACK_EXPANSION
                         || type.kind == ABI_TYPE_CV
                         || type.kind == ABI_TYPE_VENDOR_QUALIFIED
                         || type.kind == ABI_TYPE_ARRAY
                         || type.kind == ABI_TYPE_VECTOR;
      if(unary) {
        if(type.kind == ABI_TYPE_POINTER) output_ += 'P';
        else if(type.kind == ABI_TYPE_LVALUE_REFERENCE) output_ += 'R';
        else if(type.kind == ABI_TYPE_RVALUE_REFERENCE) output_ += 'O';
        else if(type.kind == ABI_TYPE_PACK_EXPANSION) output_ += "Dp";
        else if(type.kind == ABI_TYPE_CV) {
          if(type.is_volatile) output_ += 'V';
          if(type.is_const) output_ += 'K';
        } else if(type.kind == ABI_TYPE_VENDOR_QUALIFIED) {
          output_ += 'U' + source_name(graph_.strings.get(type.symbol));
		} else if(type.kind == ABI_TYPE_VECTOR) {
		  output_ += "Dv";
		  if(type.symbol != NO_ID) output_ += graph_.strings.get(type.symbol);
		  output_ += '_';
		} else {
		  output_ += 'A';
		  if(type.bound_kind == ABI_ARRAY_BOUND_EXPRESSION) {
			encode_expression(type.expression);
		  } else if(type.symbol != NO_ID) {
			output_ += graph_.strings.get(type.symbol);
		  }
		  output_ += '_';
        }
        pending.push_back(key);
        id = type.children.at(0);
        continue;
      }

      encode_new_type(id, type);
      if((retain_complete_substitution || !pending.empty()) &&
		 !(type.kind == ABI_TYPE_STD_TEMPLATE_SPECIALIZATION
           && type.standard_includes_arguments)) {
        substitutions_.add(key);
      }
      break;
    }
    for(size_t i = pending.size(); i != 0; --i)
      if(retain_complete_substitution || i != 1) substitutions_.add(pending[i - 1]);
  }

  void encode_new_type(size_t id, const TypeNode & type)
  {
    switch(type.kind) {
      case ABI_TYPE_NAMED: encode_named_type(type); return;
      case ABI_TYPE_TEMPLATE_PARAMETER: output_ += template_parameter(type.index); return;
      case ABI_TYPE_POINTER: output_ += 'P'; encode_type(type.children.at(0)); return;
      case ABI_TYPE_LVALUE_REFERENCE: output_ += 'R'; encode_type(type.children.at(0)); return;
      case ABI_TYPE_RVALUE_REFERENCE: output_ += 'O'; encode_type(type.children.at(0)); return;
      case ABI_TYPE_PACK_EXPANSION: output_ += "Dp"; encode_type(type.children.at(0)); return;
      case ABI_TYPE_CV:
        if(type.is_volatile) output_ += 'V';
        if(type.is_const) output_ += 'K';
        encode_type(type.children.at(0));
        return;
      case ABI_TYPE_VENDOR_QUALIFIED:
        output_ += 'U' + source_name(graph_.strings.get(type.symbol));
        encode_type(type.children.at(0));
        return;
	  case ABI_TYPE_ARRAY:
		output_ += 'A';
		if(type.bound_kind == ABI_ARRAY_BOUND_EXPRESSION) {
		  encode_expression(type.expression);
		} else if(type.symbol != NO_ID) {
		  output_ += graph_.strings.get(type.symbol);
		}
		output_ += '_';
        encode_type(type.children.at(0));
        return;
	  case ABI_TYPE_VECTOR:
		output_ += "Dv";
		if(type.symbol != NO_ID) output_ += graph_.strings.get(type.symbol);
		output_ += '_';
		encode_type(type.children.at(0));
		return;
      case ABI_TYPE_BUILTIN_TRANSFORM:
        output_ += 'u' + source_name(graph_.strings.get(type.symbol)) + 'I';
        for(size_t child : type.children) encode_type(child);
        output_ += 'E';
        return;
      case ABI_TYPE_FUNCTION:
        if(type.is_volatile) output_ += 'V';
        if(type.is_const) output_ += 'K';
        output_ += 'F';
        for(size_t child : type.children) encode_type(child);
        if(type.children.size() == 1) output_ += 'v';
        if(type.variadic) output_ += 'z';
        if(type.lvalue_ref) output_ += 'R';
        if(type.rvalue_ref) output_ += 'O';
        output_ += 'E';
        return;
      case ABI_TYPE_MEMBER_POINTER:
        output_ += 'M';
        encode_type(type.children.at(0));
        encode_type(type.children.at(1));
        return;
      case ABI_TYPE_TEMPLATE_SPECIALIZATION:
        encode_template_type(id, type);
        return;
      case ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION:
        output_ += template_parameter(type.index) + 'I';
        encode_arguments(type.arguments);
        output_ += 'E';
        return;
      case ABI_TYPE_STD_TEMPLATE_SPECIALIZATION:
        output_ += graph_.strings.get(type.symbol);
        emit_tags(type.tags);
        if(!type.standard_includes_arguments) {
          output_ += 'I'; encode_arguments(type.arguments); output_ += 'E';
        }
        return;
      case ABI_TYPE_MEMBER:
      case ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION:
        output_ += 'N';
        encode_prefix_type(type.children.at(0));
        output_ += source_name(graph_.strings.get(type.symbol));
        emit_tags(type.tags);
        if(type.kind == ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION) {
          substitutions_.add(member_template_prefix_key(type));
          output_ += 'I'; encode_arguments(type.arguments); output_ += 'E';
        }
        output_ += 'E';
        return;
      case ABI_TYPE_DECLTYPE_EXPRESSION:
        output_ += "DT";
        encode_expression(type.expression);
        output_ += 'E';
        return;
      case ABI_TYPE_LAMBDA_CLOSURE:
      case ABI_TYPE_LOCAL_TYPE:
        encode_local_type(type);
        return;
      case ABI_TYPE_NAMESPACE_LAMBDA: {
        vector<size_t> components = type.namespaces;
        components.push_back(type.symbol);
        encode_component_name(components, vector<size_t>());
        return;
      }
      default: break;
    }
    throw std::logic_error("unsupported canonical ABI type kind " + std::to_string(type.kind));
  }

  void encode_named_type(const TypeNode & type)
  {
    encode_path_name(type.path, type.tags, true, vector<AbiFunctionQualifier>());
  }

  void encode_template_type(size_t id, const TypeNode & type)
  {
    const vector<size_t> components = graph_.paths.components(type.path);
    const vector<size_t> prefixes = graph_.paths.prefixes(type.path);
    const bool std_unscoped = components.size() == 2 && graph_.strings.get(components[0]) == "std";
    if(components.size() == 1 || std_unscoped) {
	  if(type.suppress_template_prefix_substitution) {
	    if(std_unscoped) output_ += "St";
	    output_ += source_name(graph_.strings.get(components.back()));
	    emit_tags(type.tags);
	  } else {
	    encode_template_prefix(type.path, components, prefixes, type.tags);
	  }
      output_ += 'I'; encode_arguments(type.arguments); output_ += 'E';
      return;
    }
    output_ += 'N';
	if(type.suppress_template_prefix_substitution) {
	  encode_path_prefix(components, prefixes, components.size() - 1);
	  output_ += source_name(graph_.strings.get(components.back()));
	  emit_tags(type.tags);
	} else {
	  encode_template_prefix(type.path, components, prefixes, type.tags);
	}
    output_ += 'I'; encode_arguments(type.arguments); output_ += "EE";
  }

  void encode_prefix_type(size_t id)
  {
    const TypeNode & type = graph_.type(id);
    const SubstitutionKey key = type.substitution != NO_ID
                                  ? SubstitutionKey{SUBSTITUTION_EXPLICIT, type.substitution}
                                  : type.kind == ABI_TYPE_NAMED && type.tags.empty()
                                  ? SubstitutionKey{SUBSTITUTION_PATH, type.path}
                                  : SubstitutionKey{SUBSTITUTION_TYPE, id};
    if(substitutions_.emit_if_known(key, output_)) return;
    if(type.kind == ABI_TYPE_NAMED) {
      const vector<size_t> components = graph_.paths.components(type.path);
      const vector<size_t> prefixes = graph_.paths.prefixes(type.path);
      if(type.tags.empty()) {
        encode_path_prefix(components, prefixes, components.size());
      } else {
        encode_path_prefix(components, prefixes, components.size() - 1);
        output_ += source_name(graph_.strings.get(components.back()));
        emit_tags(type.tags);
      }
    } else if(type.kind == ABI_TYPE_TEMPLATE_SPECIALIZATION) {
      const vector<size_t> components = graph_.paths.components(type.path);
      const vector<size_t> prefixes = graph_.paths.prefixes(type.path);
      encode_template_prefix(type.path, components, prefixes, type.tags);
      output_ += 'I'; encode_arguments(type.arguments); output_ += 'E';
    } else if(type.kind == ABI_TYPE_MEMBER
              || type.kind == ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION) {
      encode_prefix_type(type.children.at(0));
      output_ += source_name(graph_.strings.get(type.symbol));
      emit_tags(type.tags);
      if(type.kind == ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION) {
        substitutions_.add(member_template_prefix_key(type));
        output_ += 'I'; encode_arguments(type.arguments); output_ += 'E';
      }
    } else {
      encode_new_type(id, type);
    }
    substitutions_.add(key);
  }

  void encode_template_prefix(size_t path, const vector<size_t> & components,
                              const vector<size_t> & prefixes,
                              const vector<size_t> & tags)
  {
    const SubstitutionKey key = tags.empty() ?
      SubstitutionKey{SUBSTITUTION_PATH, path} : tagged_path_key(path, tags);
    if(substitutions_.emit_if_known(key, output_)) return;
    encode_path_prefix(components, prefixes, components.size() - 1);
    output_ += source_name(graph_.strings.get(components.back()));
    emit_tags(tags);
    substitutions_.add(key);
  }

  void encode_path_name(size_t path, const vector<size_t> & tags, bool add_full,
                        const vector<AbiFunctionQualifier> & qualifiers)
  {
    const vector<size_t> components = graph_.paths.components(path);
    const vector<size_t> prefixes = graph_.paths.prefixes(path);
    const bool std_unscoped = qualifiers.empty() && components.size() == 2
                              && graph_.strings.get(components[0]) == "std";
    if(components.size() == 1 || std_unscoped) {
      if(std_unscoped) output_ += "St";
      output_ += source_name(graph_.strings.get(components.back()));
      emit_tags(tags);
    } else {
      output_ += 'N';
      emit_qualifiers(qualifiers);
      encode_path_prefix(components, prefixes, components.size() - 1);
      output_ += source_name(graph_.strings.get(components.back()));
      emit_tags(tags);
      output_ += 'E';
    }
    if(add_full && tags.empty())
      substitutions_.add(SubstitutionKey{SUBSTITUTION_PATH, path});
  }

  void encode_path_prefix(const vector<size_t> & components,
                          const vector<size_t> & prefixes, size_t count)
  {
    size_t cursor = 0;
    for(size_t known = count; known != 0; --known) {
      if(substitutions_.emit_if_known(
           SubstitutionKey{SUBSTITUTION_PATH, prefixes[known - 1]}, output_)) {
        cursor = known;
        break;
      }
    }
    while(cursor < count) {
      if(cursor == 0 && graph_.strings.get(components[0]) == "std") {
        output_ += "St";
        ++cursor;
        continue;
      }
      output_ += source_name(graph_.strings.get(components[cursor]));
      substitutions_.add(SubstitutionKey{SUBSTITUTION_PATH, prefixes[cursor]});
      ++cursor;
    }
  }

  void encode_arguments(const vector<size_t> & arguments)
  {
    for(size_t argument : arguments) encode_argument(argument);
  }

  void encode_argument(size_t id)
  {
    const ArgumentNode & argument = graph_.argument(id);
	if(argument.pack_expansion) output_ += "Dp";
    switch(argument.kind) {
      case ABI_TEMPLATE_ARGUMENT_TYPE: encode_type(argument.type); return;
      case ABI_TEMPLATE_ARGUMENT_VALUE:
        output_ += 'L';
        encode_type(argument.value_type);
        output_ += integral_value(argument.value_type, argument.value) + 'E';
        return;
      case ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE:
        output_ += "Tn";
        encode_type(argument.type, false);
        output_ += 'L'; encode_type(argument.value_type);
        output_ += integral_value(argument.value_type, argument.value) + 'E';
        return;
      case ABI_TEMPLATE_ARGUMENT_EXPRESSION:
        output_ += 'X'; encode_expression(argument.expression); output_ += 'E'; return;
      case ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY:
        encode_path_name(graph_.paths.intern(graph_.strings.get(argument.name)),
                         vector<size_t>(), true, vector<AbiFunctionQualifier>());
        return;
      case ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY:
        encode_member_template_argument(argument);
        return;
      case ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE:
        output_ += template_parameter(argument.index); return;
      case ABI_TEMPLATE_ARGUMENT_ENTITY:
        if(argument.address_of) {
          output_ += "Xad";
          encode_entity_reference(graph_.strings.get(argument.entity));
          output_ += 'E';
        } else encode_entity_reference(graph_.strings.get(argument.entity));
        return;
      case ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY:
        if(argument.symbol != NO_ID) {
          output_ += "XadL" + graph_.strings.get(argument.symbol) + "EE";
          return;
        }
        require(argument.owner_type != NO_ID,
                "structured member external entity is incomplete");
        require(!(argument.member_lvalue_ref && argument.member_rvalue_ref),
                "structured member external entity has conflicting ref qualifiers");
        output_ += "XadL_ZN";
        if(argument.member_volatile) output_ += 'V';
        if(argument.member_const) output_ += 'K';
        if(argument.member_lvalue_ref) output_ += 'R';
        if(argument.member_rvalue_ref) output_ += 'O';
        encode_prefix_type(argument.owner_type);
        if(argument.member_terminal_kind == ABI_MEMBER_FUNCTION_TERMINAL_SOURCE) {
          require(argument.name != NO_ID,
                  "structured member external entity has no source terminal");
          output_ += source_name(graph_.strings.get(argument.name));
        } else if(argument.member_terminal_kind ==
                  ABI_MEMBER_FUNCTION_TERMINAL_OPERATOR) {
          require(argument.member_terminal != NO_ID,
                  "structured member operator has no typed terminal");
          const string & terminal = graph_.strings.get(argument.member_terminal);
          if(terminal == "literal") {
            require(argument.member_literal_suffix != NO_ID,
                    "structured literal operator has no suffix");
            output_ += "li" + source_name(
              graph_.strings.get(argument.member_literal_suffix));
          } else {
            output_ += operator_code(terminal, true, argument.parameters.size());
          }
        } else {
          require(argument.member_terminal_kind ==
                    ABI_MEMBER_FUNCTION_TERMINAL_CONVERSION
                  && argument.member_conversion_type != NO_ID,
                  "structured member conversion has no target type");
          output_ += "cv";
          encode_type(argument.member_conversion_type);
        }
        if(!argument.arguments.empty()) {
          require(argument.substitution != NO_ID,
                  "structured member function template has no canonical prefix");
          substitutions_.add(
            SubstitutionKey{SUBSTITUTION_EXPLICIT, argument.substitution});
          output_ += 'I';
          encode_arguments(argument.arguments);
          output_ += 'E';
        }
        output_ += 'E';
        if(argument.member_is_function) {
          if(argument.member_has_result_type) {
            require(!argument.arguments.empty(),
                    "ordinary member function unexpectedly encodes a result");
            encode_type(argument.member_result_type);
          }
          for(size_t parameter : argument.parameters) encode_type(parameter);
          if(argument.parameters.empty()) output_ += 'v';
          if(argument.member_variadic) output_ += 'z';
        }
        output_ += "EE";
        return;
      case ABI_TEMPLATE_ARGUMENT_PACK:
        output_ += 'J'; encode_arguments(argument.arguments); output_ += 'E'; return;
      default: break;
    }
    throw std::logic_error("unsupported ABI template argument kind");
  }

  void encode_member_template_argument(const ArgumentNode & argument)
  {
    output_ += 'N';
    const TypeNode & owner = graph_.type(argument.owner_type);
    if(owner.kind == ABI_TYPE_TEMPLATE_SPECIALIZATION) {
      const vector<size_t> components = graph_.paths.components(owner.path);
      const vector<size_t> prefixes = graph_.paths.prefixes(owner.path);
      encode_template_prefix(owner.path, components, prefixes, owner.tags);
      output_ += 'I'; encode_arguments(owner.arguments); output_ += 'E';
    } else {
      encode_prefix_type(argument.owner_type);
    }
    const SubstitutionKey member_key{
      SUBSTITUTION_EXPLICIT,
      argument.substitution != NO_ID ? argument.substitution : argument.name
    };
    if(!substitutions_.emit_if_known(member_key, output_)) {
      output_ += source_name(graph_.strings.get(argument.name));
      substitutions_.add(member_key);
    }
    output_ += 'E';
  }

  string integral_value(size_t type_id, long long value) const
  {
    const TypeNode & type = graph_.type(type_id);
    if(type.kind == ABI_TYPE_BUILTIN && value < 0) {
      const string & name = graph_.strings.get(type.symbol);
      if(name == "uint") return std::to_string(static_cast<uint32_t>(value));
      if(name == "ulong" || name == "ulonglong" || name == "uint128") {
        return std::to_string(static_cast<uint64_t>(value));
      }
    }
    return number(value);
  }

  void encode_expression(size_t id)
  {
    const ExpressionNode & expression = graph_.expression(id);
    switch(expression.kind) {
      case ABI_EXPRESSION_TEMPLATE_PARAMETER:
        output_ += template_parameter(expression.index); return;
      case ABI_EXPRESSION_FUNCTION_PARAMETER:
        output_ += "fp";
        if(expression.index != 0) output_ += base36(expression.index - 1);
        output_ += '_';
        return;
      case ABI_EXPRESSION_LITERAL:
        output_ += "Li" + graph_.strings.get(expression.symbol) + 'E'; return;
      case ABI_EXPRESSION_UNARY:
      case ABI_EXPRESSION_BINARY:
        output_ += graph_.strings.get(expression.symbol);
        for(size_t child : expression.expressions) encode_expression(child);
        return;
      case ABI_EXPRESSION_CONDITIONAL:
        output_ += "qu";
        for(size_t child : expression.expressions) encode_expression(child);
        return;
      case ABI_EXPRESSION_PACK_EXPANSION:
        output_ += "sp"; encode_expression(expression.expressions.at(0)); return;
      case ABI_EXPRESSION_CALL:
        output_ += "cl";
        for(size_t child : expression.expressions) encode_expression(child);
        output_ += 'E';
        return;
      case ABI_EXPRESSION_CAST:
        output_ += graph_.strings.get(expression.symbol);
        encode_type(expression.type);
        encode_expression(expression.expressions.at(0));
        return;
      case ABI_EXPRESSION_TEMPLATE_ID:
        output_ += source_name(graph_.strings.get(expression.symbol)) + 'I';
        encode_arguments(expression.arguments);
        output_ += 'E';
        return;
      case ABI_EXPRESSION_TYPE_TRAIT:
        output_ += 'u' + source_name(graph_.strings.get(expression.symbol));
        for(size_t type : expression.types) encode_type(type);
        output_ += 'E';
        return;
      case ABI_EXPRESSION_SIZEOF_TYPE:
        output_ += "st"; encode_type(expression.type); return;
      case ABI_EXPRESSION_MEMBER:
        output_ += "sr"; encode_type(expression.type);
        if(expression.close_member_owner) output_ += 'E';
        output_ += source_name(graph_.strings.get(expression.symbol));
        return;
      case ABI_EXPRESSION_OBJECT_MEMBER:
        output_ += graph_.strings.get(expression.op);
        encode_expression(expression.expressions.at(0));
        output_ += source_name(graph_.strings.get(expression.symbol));
        if(!expression.arguments.empty()) {
          output_ += 'I'; encode_arguments(expression.arguments); output_ += 'E';
        }
        return;
      case ABI_EXPRESSION_ENTITY:
        encode_entity_reference(graph_.strings.get(expression.entity)); return;
      default: break;
    }
    throw std::logic_error("unsupported ABI dependent expression kind");
  }

  void encode_entity_reference(const string & id)
  {
    const AbiDefinitionRecord & definition = graph_.definition(id, ABI_DEFINITION_ENTITY);
    output_ += 'L';
    if(definition.entity.kind == ABI_ENTITY_FACT_SYMBOL) {
      output_ += definition.entity.qualified_name;
    } else {
      SubstitutionTable outer_substitutions;
      substitutions_.swap(outer_substitutions);
      if(stats_) ++stats_->isolated_entity_encodings;
      output_ += "_Z";
      if(definition.entity.kind == ABI_ENTITY_FACT_FUNCTION) {
        encode_function(definition.entity.function, FunctionFacts());
      } else {
        encode_object_name(definition.entity.qualified_name, definition.entity.internal_linkage);
      }
      substitutions_.swap(outer_substitutions);
    }
    output_ += 'E';
  }

  void encode_object_name(const string & qualified_name, bool internal)
  {
    const size_t path = graph_.paths.intern(qualified_name);
    const vector<size_t> components = graph_.paths.components(path);
    const vector<size_t> prefixes = graph_.paths.prefixes(path);
    const bool std_unscoped = components.size() == 2 && graph_.strings.get(components[0]) == "std";
    if(components.size() == 1 || std_unscoped) {
      if(std_unscoped) output_ += "St";
      if(internal) output_ += 'L';
      output_ += source_name(graph_.strings.get(components.back()));
      return;
    }
    output_ += 'N';
    encode_path_prefix(components, prefixes, components.size() - 1);
    if(internal) output_ += 'L';
    output_ += source_name(graph_.strings.get(components.back())) + 'E';
  }

  void encode_function(const AbiFunctionTarget & target, const FunctionFacts & facts,
                       bool internal = false)
  {
    if(target.kind != ABI_FUNCTION_TARGET_ENCODING) {
      require(facts.components.empty() && facts.local == nullptr
              && facts.lambda == nullptr && facts.namespace_lambda == nullptr,
              "structured ABI name records require a function encoding target");
    }
    if(target.kind == ABI_FUNCTION_TARGET_ENCODING) {
      encode_structured_function(facts);
      return;
    }
    if(target.kind == ABI_FUNCTION_TARGET_LOCAL || target.kind == ABI_FUNCTION_TARGET_LAMBDA) {
      encode_direct_local_function(target, facts);
      return;
    }
    if(target.kind == ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA) {
      encode_namespace_lambda_function(target, facts);
      return;
    }
    if(target.kind == ABI_FUNCTION_TARGET_MEMBER) {
      encode_member_function(target, facts);
      return;
    }
    encode_path_function(target, facts, internal);
  }

  void encode_path_function(const AbiFunctionTarget & target, const FunctionFacts & facts,
                            bool internal)
  {
    const size_t path = graph_.paths.intern(target.qualified_name);
    vector<size_t> template_arguments;
    vector<size_t> operand_parameters;
    for(const AbiFunctionPathOperand & operand : target.path_operands) {
      if(operand.kind == ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT) {
        if(graph_.is_argument_definition(operand.argument_ref)) {
          template_arguments.push_back(graph_.resolve_argument_ref(operand.argument_ref));
        } else {
          AbiType type;
          type.kind = ABI_TYPE_NAME_OR_REFERENCE;
          type.name = operand.argument_ref;
          operand_parameters.push_back(graph_.resolve_type(type));
        }
      }
    }
    template_arguments.insert(template_arguments.end(), facts.template_arguments.begin(),
                              facts.template_arguments.end());
    vector<size_t> parameters;
    parameters.insert(parameters.end(), operand_parameters.begin(), operand_parameters.end());
    for(const AbiType & type : target.signature_parameter_types) {
      parameters.push_back(graph_.resolve_type(type));
    }
    parameters.insert(parameters.end(), facts.parameters.begin(), facts.parameters.end());
    encode_function_name_path(path, facts, template_arguments, parameters.size(), internal);
    if(!template_arguments.empty() &&
       (target.has_result_type || !facts.result_types.empty())
       && !(facts.terminal && facts.terminal->kind == ABI_FUNCTION_RECORD_CONVERSION_TERMINAL)) {
      encode_type(target.has_result_type ?
        graph_.resolve_type(target.result_type) : facts.result_types.front());
    }
    encode_bare_parameters(parameters, target.variadic || facts.variadic);
  }

  void encode_member_function(const AbiFunctionTarget & target,
                              const FunctionFacts & facts)
  {
    output_ += 'N';
    emit_qualifiers(facts.qualifiers);
    encode_prefix_type(graph_.resolve_type(target.owner_type));
    if(facts.terminal) {
      emit_function_terminal(nullptr, facts, true, facts.parameters.size());
    } else {
      output_ += source_name(target.source_name);
      emit_tags(facts.tags);
    }
    if(!facts.template_arguments.empty()) {
      const size_t path = graph_.paths.intern(target.qualified_name);
      encode_function_template_arguments(path, facts, facts.template_arguments);
    }
    output_ += 'E';
    if(!facts.template_arguments.empty()
       && (target.has_result_type || !facts.result_types.empty())
       && !(facts.terminal
            && facts.terminal->kind == ABI_FUNCTION_RECORD_CONVERSION_TERMINAL)) {
      encode_type(target.has_result_type ?
        graph_.resolve_type(target.result_type) : facts.result_types.front());
    }
    encode_bare_parameters(facts.parameters, facts.variadic);
  }

  void encode_member_object(const AbiFunctionTarget & target,
                            const FunctionFacts & facts)
  {
    require(facts.local == nullptr && facts.lambda == nullptr
            && facts.namespace_lambda == nullptr && facts.terminal == nullptr
            && facts.qualifiers.empty() && facts.parameters.empty()
            && facts.result_types.empty() && !facts.variadic,
            "member ABI variable has function-only name facts");
    output_ += 'N';
    encode_prefix_type(graph_.resolve_type(target.owner_type));
    output_ += source_name(target.source_name);
    if(!facts.template_arguments.empty()) {
      const size_t path = graph_.paths.intern(target.qualified_name);
      encode_function_template_arguments(path, facts, facts.template_arguments);
    }
    output_ += 'E';
  }

  void encode_function_name_path(size_t path, const FunctionFacts & facts,
                                 const vector<size_t> & template_arguments,
                                 size_t parameter_count, bool internal)
  {
    const vector<size_t> components = graph_.paths.components(path);
    const vector<size_t> prefixes = graph_.paths.prefixes(path);
    const bool has_custom_terminal = facts.terminal != nullptr;
    const bool std_unscoped = facts.qualifiers.empty() && components.size() == 2
                              && graph_.strings.get(components[0]) == "std";
    if(components.size() == 1 || std_unscoped) {
      if(std_unscoped) output_ += "St";
      if(internal) output_ += 'L';
      emit_function_terminal(has_custom_terminal ? nullptr : &components.back(), facts,
                             components.size() > 1, parameter_count);
      encode_function_template_arguments(path, facts, template_arguments);
    } else {
      output_ += 'N';
      emit_qualifiers(facts.qualifiers);
      encode_path_prefix(components, prefixes, components.size() - 1);
      if(internal) output_ += 'L';
      emit_function_terminal(has_custom_terminal ? nullptr : &components.back(), facts,
                             true, parameter_count);
      encode_function_template_arguments(path, facts, template_arguments);
      output_ += 'E';
    }
  }

  void encode_function_template_arguments(size_t path, const FunctionFacts & facts,
                                          const vector<size_t> & template_arguments)
  {
    if(template_arguments.empty()) return;
    const size_t key_id = facts.template_prefix != NO_ID ? facts.template_prefix
                                                          : path;
    const SubstitutionKind key_kind = facts.template_prefix != NO_ID
                                        ? SUBSTITUTION_EXPLICIT
                                        : SUBSTITUTION_FUNCTION_TEMPLATE_PREFIX;
    substitutions_.add(SubstitutionKey{key_kind, key_id});
    output_ += 'I'; encode_arguments(template_arguments); output_ += 'E';
  }

  void emit_function_terminal(const size_t * source, const FunctionFacts & facts, bool member,
                              size_t parameter_count)
  {
    if(facts.terminal == nullptr) {
      require(source != nullptr, "missing ABI function terminal");
      output_ += source_name(graph_.strings.get(*source));
    } else {
      const AbiFunctionRecord & terminal = *facts.terminal;
      if(terminal.kind == ABI_FUNCTION_RECORD_TERMINAL_SOURCE) {
        output_ += source_name(terminal.name);
      } else if(terminal.kind == ABI_FUNCTION_RECORD_TERMINAL) {
        output_ += semantic_terminal(terminal.terminal);
      } else if(terminal.kind == ABI_FUNCTION_RECORD_OPERATOR_TERMINAL) {
        output_ += operator_terminal(terminal, member, parameter_count);
      } else if(terminal.kind == ABI_FUNCTION_RECORD_CONVERSION_TERMINAL) {
        output_ += "cv";
        encode_type(graph_.resolve_type(terminal.type));
      }
    }
    emit_tags(facts.tags);
  }

  void encode_structured_function(const FunctionFacts & facts)
  {
    if(facts.local || facts.lambda) {
      encode_structured_local(facts);
      return;
    }
    if(facts.namespace_lambda) {
      AbiFunctionTarget target;
      target.kind = ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA;
      target.source_name = facts.namespace_lambda->source_name;
      target.namespace_qualifiers = facts.namespace_lambda->namespace_qualifiers;
      encode_namespace_lambda_function(target, facts);
      return;
    }
    encode_structured_nonlocal(facts);
  }

  void encode_structured_nonlocal(const FunctionFacts & facts)
  {
    const bool is_template = encode_structured_nonlocal_name(facts);
    if(is_template && !facts.result_types.empty()
       && !(facts.terminal && facts.terminal->kind == ABI_FUNCTION_RECORD_CONVERSION_TERMINAL)) {
      encode_type(facts.result_types.front());
    }
    encode_bare_parameters(facts.parameters, facts.variadic);
  }

  bool encode_structured_nonlocal_name(const FunctionFacts & facts)
  {
    bool std_prefix = false;
    vector<const AbiFunctionRecord *> components;
    for(const AbiFunctionRecord * record : facts.components) {
      if(record->kind == ABI_FUNCTION_RECORD_NAME_STD) std_prefix = true;
      else if(record->kind != ABI_FUNCTION_RECORD_NAME_SOURCE || !record->name.empty()) {
        components.push_back(record);
      }
    }
    if(facts.terminal && !components.empty()
       && components.back()->kind == ABI_FUNCTION_RECORD_NAME_SOURCE
       && components.back()->name == "operator") {
      components.pop_back();
    }
    const bool separate_terminal = facts.terminal != nullptr;
    const size_t prefix_count = components.size() - (separate_terminal ? 0 : 1);
    require(!components.empty() || separate_terminal, "structured function has no name");
    const bool nested = !facts.qualifiers.empty() || prefix_count != 0
                        || (std_prefix && components.size() > 1);
    if(nested) {
      output_ += 'N'; emit_qualifiers(facts.qualifiers);
      if(std_prefix) output_ += "St";
      for(size_t i = 0; i < prefix_count; ++i) {
        encode_structured_prefix(*components[i], component_tags(facts, components[i]));
      }
      const AbiFunctionRecord * final = separate_terminal ? nullptr : components.back();
      emit_structured_terminal(final, facts, nested);
      encode_structured_template_arguments(final, facts);
      output_ += 'E';
    } else {
      if(std_prefix) output_ += "St";
      const AbiFunctionRecord * final = separate_terminal ? nullptr : components.back();
      emit_structured_terminal(final, facts, false);
      encode_structured_template_arguments(final, facts);
    }
    const bool is_template = !facts.template_arguments.empty()
                             || (!components.empty()
                                 && components.back()->kind == ABI_FUNCTION_RECORD_NAME_TEMPLATE
                                 && !components.back()->argument_refs.empty());
    return is_template;
  }

  void encode_structured_object(const FunctionFacts & facts)
  {
    require(facts.local == nullptr && facts.lambda == nullptr
            && facts.namespace_lambda == nullptr,
            "structured ABI variable cannot have a local function context");
    require(facts.terminal == nullptr && facts.qualifiers.empty()
            && facts.parameters.empty() && facts.result_types.empty()
            && !facts.variadic,
            "structured ABI variable has function-only name facts");
    encode_structured_nonlocal_name(facts);
  }

  void encode_structured_template_arguments(const AbiFunctionRecord * final,
                                            const FunctionFacts & facts)
  {
    vector<size_t> arguments = facts.template_arguments;
    if(final && final->kind == ABI_FUNCTION_RECORD_NAME_TEMPLATE) {
      arguments.clear();
      for(const string & argument : final->argument_refs) {
        arguments.push_back(graph_.resolve_argument_ref(argument));
      }
    }
    if(arguments.empty()) return;
    if(facts.template_prefix != NO_ID) {
      substitutions_.add(SubstitutionKey{SUBSTITUTION_EXPLICIT, facts.template_prefix});
    } else if(final && !final->substitution.empty()) {
      substitutions_.add(explicit_or_path_key(final->substitution, final->name));
    }
    output_ += 'I'; encode_arguments(arguments); output_ += 'E';
  }

  const vector<size_t> & component_tags(const FunctionFacts & facts,
                                        const AbiFunctionRecord * component) const
  {
    static const vector<size_t> empty;
    const auto found = facts.component_tags.find(component);
    return found == facts.component_tags.end() ? empty : found->second;
  }

  void encode_structured_prefix(const AbiFunctionRecord & component,
                                const vector<size_t> & tags)
  {
    if(component.kind == ABI_FUNCTION_RECORD_NAME_SOURCE) {
      const SubstitutionKey key = structured_component_key(component, tags);
      if(substitutions_.emit_if_known(key, output_)) return;
      output_ += source_name(component.name);
      emit_tags(tags);
      substitutions_.add(key);
      return;
    }
    require(component.kind == ABI_FUNCTION_RECORD_NAME_TEMPLATE,
            "invalid structured ABI prefix component");
    if(component.standard_substitution != "-"
       && component.standard_substitution_includes_arguments) {
      output_ += component.standard_substitution;
      return;
    }
    const SubstitutionKey prefix = structured_component_key(component, tags);
    if(substitutions_.emit_if_known(prefix, output_)) return;
    if(component.standard_substitution != "-") output_ += component.standard_substitution;
    else output_ += source_name(component.name);
    emit_tags(tags);
    substitutions_.add(prefix);
    output_ += 'I';
    for(const string & argument : component.argument_refs) encode_argument(graph_.resolve_argument_ref(argument));
    output_ += 'E';
    if(!component.complete_substitution.empty() && component.complete_substitution != "-") {
      substitutions_.add(SubstitutionKey{SUBSTITUTION_EXPLICIT,
                                          graph_.strings.intern(component.complete_substitution)});
    }
  }

  void emit_structured_terminal(const AbiFunctionRecord * final,
                                const FunctionFacts & facts, bool member)
  {
    if(final != nullptr) {
      if(final->kind == ABI_FUNCTION_RECORD_NAME_SOURCE) output_ += source_name(final->name);
      else if(final->kind == ABI_FUNCTION_RECORD_NAME_TEMPLATE) {
        output_ += source_name(final->name);
      } else throw std::logic_error("invalid final structured ABI name component");
      emit_tags(component_tags(facts, final));
      emit_tags(facts.tags);
    } else {
      emit_function_terminal(nullptr, facts, member, facts.parameters.size());
    }
  }

  void encode_direct_local_function(const AbiFunctionTarget & target,
                                    const FunctionFacts & facts)
  {
    encode_local_prefix(target.context_ref);
    output_ += 'N'; emit_qualifiers(facts.qualifiers);
    if(target.kind == ABI_FUNCTION_TARGET_LAMBDA) {
      output_ += "Ul";
      vector<size_t> signature;
      for(const AbiType & type : target.signature_parameter_types) signature.push_back(graph_.resolve_type(type));
      encode_bare_parameters(signature, false);
      output_ += 'E' + target.discriminator + '_';
      substitutions_.add(local_lambda_key(
        target.context_ref, target.discriminator));
    } else {
      output_ += source_name(target.qualified_name) + discriminator(target.discriminator);
    }
    output_ += terminal_from_word(target.terminal, facts, true);
    if(!facts.template_arguments.empty()) {
      output_ += 'I'; encode_arguments(facts.template_arguments); output_ += 'E';
    }
    output_ += 'E';
    if(!facts.template_arguments.empty() && !facts.result_types.empty())
      encode_type(facts.result_types.front());
    encode_bare_parameters(facts.parameters, facts.variadic);
  }

  void encode_structured_local(const FunctionFacts & facts)
  {
    const AbiFunctionRecord & local = facts.local ? *facts.local : *facts.lambda;
    encode_local_prefix(local.context_ref);
    output_ += 'N'; emit_qualifiers(facts.qualifiers);
    if(facts.lambda) {
      output_ += "Ul";
      vector<size_t> signature;
      for(const AbiType & type : local.types) signature.push_back(graph_.resolve_type(type));
      encode_bare_parameters(signature, false);
      output_ += 'E' + local.discriminator + '_';
      substitutions_.add(local_lambda_key(
        local.context_ref, local.discriminator));
    } else if(local.name.empty()) {
      output_ += "Ut";
      const size_t ordinal = static_cast<size_t>(std::stoul(local.discriminator));
      if(ordinal != 0) output_ += base36(ordinal - 1);
      output_ += '_';
    } else {
      output_ += source_name(local.name);
      emit_tags(component_tags(facts, &local));
      if(!local.discriminator_after_terminal)
        output_ += discriminator(local.discriminator);
    }
    if(facts.terminal) {
      emit_function_terminal(nullptr, facts, true, facts.parameters.size());
    }
    else {
      require(facts.local && local.name.compare(0, 2, "$_") == 0,
              "missing ABI function terminal");
      output_ += "cl";
    }
    if(!facts.template_arguments.empty()) {
      output_ += 'I'; encode_arguments(facts.template_arguments); output_ += 'E';
    }
    output_ += 'E';
    if(local.discriminator_after_terminal)
      output_ += discriminator(local.discriminator);
    if(!facts.template_arguments.empty() && !facts.result_types.empty())
      encode_type(facts.result_types.front());
    encode_bare_parameters(facts.parameters, facts.variadic);
  }

  void encode_namespace_lambda_function(const AbiFunctionTarget & target,
                                        const FunctionFacts & facts)
  {
    vector<size_t> components;
    for(const string & name : target.namespace_qualifiers) components.push_back(graph_.strings.intern(name));
    components.push_back(graph_.strings.intern(target.source_name));
    output_ += 'N'; emit_qualifiers(facts.qualifiers);
    for(size_t i = 0; i + 1 < components.size(); ++i) output_ += source_name(graph_.strings.get(components[i]));
    output_ += source_name(graph_.strings.get(components.back()));
    if(facts.terminal) {
      emit_function_terminal(nullptr, facts, true, facts.parameters.size());
    }
    else output_ += operator_code(target.terminal, true, facts.parameters.size());
    if(!facts.template_arguments.empty()) {
      output_ += 'I'; encode_arguments(facts.template_arguments); output_ += 'E';
    }
    output_ += 'E';
    if(!facts.template_arguments.empty() && !facts.result_types.empty())
      encode_type(facts.result_types.front());
    encode_bare_parameters(facts.parameters, facts.variadic);
  }

  void encode_local_type(const TypeNode & type)
  {
    // Resolving the local function context can lazily canonicalize its
    // signature types and grow FactGraph::types.  Preserve this node instead
    // of retaining a reference into that re-entrant vector.
    const TypeNode stable = type;
    encode_local_prefix(graph_.strings.get(stable.context));
    if(stable.kind == ABI_TYPE_LAMBDA_CLOSURE) {
      output_ += "Ul";
      encode_bare_parameters(stable.children, false);
      output_ += 'E' + graph_.strings.get(stable.discriminator) + '_';
      substitutions_.add(local_lambda_key(
        graph_.strings.get(stable.context),
        graph_.strings.get(stable.discriminator)));
    } else if(stable.symbol == NO_ID) {
      output_ += "Ut";
      const size_t ordinal = static_cast<size_t>(
        std::stoul(graph_.strings.get(stable.discriminator)));
      if(ordinal != 0) output_ += base36(ordinal - 1);
      output_ += '_';
    } else {
      output_ += source_name(graph_.strings.get(stable.symbol))
                 + discriminator(graph_.strings.get(stable.discriminator));
    }
    emit_tags(stable.tags);
  }

  void encode_local_prefix(const string & context_id)
  {
    const AbiDefinitionRecord & definition = graph_.definition(context_id, ABI_DEFINITION_CONTEXT);
    if(definition.context.kind == ABI_CONTEXT_RAW) {
      output_ += definition.context.fragment;
    } else {
      FunctionFacts facts;
      facts.qualifiers = definition.context.qualifiers;
      if(definition.context.target_signature_is_parameter_list) {
        for(const AbiType & type :
            definition.context.function.signature_parameter_types) {
          facts.parameters.push_back(graph_.resolve_type(type));
        }
        facts.variadic = definition.context.function.variadic;
      }
      output_ += 'Z';
      encode_function(definition.context.function, facts);
      output_ += 'E';
    }
  }

  void encode_component_name(const vector<size_t> & components, const vector<size_t> & tags)
  {
    if(components.size() == 1) {
      output_ += source_name(graph_.strings.get(components[0])); emit_tags(tags); return;
    }
    output_ += 'N';
    for(size_t component : components) output_ += source_name(graph_.strings.get(component));
    emit_tags(tags);
    output_ += 'E';
  }

  void encode_bare_parameters(const vector<size_t> & parameters, bool variadic)
  {
    if(parameters.empty() && !variadic) output_ += 'v';
    else for(size_t type : parameters) encode_type(type);
    if(variadic) output_ += 'z';
  }

  void emit_qualifiers(const vector<AbiFunctionQualifier> & qualifiers)
  {
    if(std::find(qualifiers.begin(), qualifiers.end(), ABI_FUNCTION_QUALIFIER_VOLATILE)
       != qualifiers.end()) output_ += 'V';
    if(std::find(qualifiers.begin(), qualifiers.end(), ABI_FUNCTION_QUALIFIER_CONST)
       != qualifiers.end()) output_ += 'K';
    if(std::find(qualifiers.begin(), qualifiers.end(), ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE)
       != qualifiers.end()) output_ += 'R';
    if(std::find(qualifiers.begin(), qualifiers.end(), ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE)
       != qualifiers.end()) output_ += 'O';
  }

  void emit_tags(const vector<size_t> & tags)
  {
    for(size_t tag : tags) output_ += 'B' + source_name(graph_.strings.get(tag));
  }

  string semantic_terminal(const string & terminal) const
  {
    if(terminal == "constructor-complete") return "C1";
    if(terminal == "constructor-base") return "C2";
    if(terminal == "destructor-deleting") return "D0";
    if(terminal == "destructor-complete") return "D1";
    if(terminal == "destructor-base") return "D2";
    if(terminal == "operator-call") return "cl";
    throw std::logic_error("unknown semantic ABI terminal '" + terminal + "'");
  }

  string terminal_from_word(const string & terminal, const FunctionFacts & facts, bool member)
  {
    if(terminal == "operator-call") return "cl";
    if(terminal.compare(0, 12, "constructor-") == 0
       || terminal.compare(0, 11, "destructor-") == 0) return semantic_terminal(terminal);
    AbiFunctionRecord record;
    record.kind = ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
    record.terminal = terminal;
    return operator_terminal(record, member, facts.parameters.size());
  }

  string operator_terminal(const AbiFunctionRecord & terminal, bool member,
                           size_t parameter_count) const
  {
    if(terminal.terminal == "literal") return "li" + source_name(terminal.literal_suffix);
    return operator_code(terminal.terminal, member, parameter_count);
  }

  string operator_code(const string & name, bool member, size_t parameter_count) const
  {
    static const struct Entry { const char * name; const char * code; } entries[] = {
      {"unary-plus", "ps"}, {"binary-plus", "pl"}, {"unary-minus", "ng"},
      {"binary-minus", "mi"}, {"address-of", "ad"}, {"deref", "de"},
      {"new", "nw"}, {"new-array", "na"}, {"delete", "dl"}, {"delete-array", "da"},
      {"multiply", "ml"}, {"divide", "dv"}, {"remainder", "rm"}, {"bit-and", "an"},
      {"bit-or", "or"}, {"bit-xor", "eo"}, {"assign", "aS"}, {"plus-assign", "pL"},
      {"minus-assign", "mI"}, {"multiply-assign", "mL"}, {"divide-assign", "dV"},
      {"remainder-assign", "rM"}, {"and-assign", "aN"}, {"or-assign", "oR"},
      {"xor-assign", "eO"}, {"left-shift", "ls"}, {"right-shift", "rs"},
      {"left-shift-assign", "lS"}, {"right-shift-assign", "rS"}, {"equal", "eq"},
      {"not-equal", "ne"}, {"less", "lt"}, {"greater", "gt"}, {"less-equal", "le"},
      {"greater-equal", "ge"}, {"logical-not", "nt"}, {"logical-and", "aa"},
      {"logical-or", "oo"}, {"increment", "pp"}, {"decrement", "mm"},
      {"comma", "cm"}, {"member-pointer", "pm"}, {"arrow", "pt"},
      {"call", "cl"}, {"operator-call", "cl"}, {"index", "ix"}
    };
    if(name == "plus") return (member ? parameter_count == 0 : parameter_count == 1) ? "ps" : "pl";
    if(name == "minus") return (member ? parameter_count == 0 : parameter_count == 1) ? "ng" : "mi";
    for(const Entry & entry : entries) if(name == entry.name) return entry.code;
    throw std::logic_error("unknown ABI operator terminal '" + name + "'");
  }

  string template_parameter(size_t index) const
  {
    return index == 0 ? "T_" : "T" + base36(index - 1) + '_';
  }

  string final_name(const string & qualified) const
  {
    const size_t separator = qualified.rfind("::");
    const string result = separator == string::npos ? qualified : qualified.substr(separator + 2);
    require(!result.empty(), "empty C-linkage ABI function name");
    return result;
  }

  SubstitutionKey explicit_or_path_key(const string & key, const string & fallback)
  {
    const string spelling = key.empty() || key == "-" ? fallback : key;
    if(spelling.find('<') == string::npos && spelling.compare(0, 14, "operator-name:") != 0) {
      return SubstitutionKey{SUBSTITUTION_PATH, graph_.paths.intern(spelling)};
    }
    return SubstitutionKey{SUBSTITUTION_EXPLICIT, graph_.strings.intern(spelling)};
  }

  SubstitutionKey structured_component_key(const AbiFunctionRecord & component,
                                           const vector<size_t> & tags)
  {
    const SubstitutionKey base =
      explicit_or_path_key(component.substitution, component.name);
    if(tags.empty()) return base;
    string identity = "__cppgm_abi_tagged_component_";
    identity += std::to_string(static_cast<unsigned>(base.kind));
    identity += '_' + std::to_string(base.id);
    for(size_t tag : tags) identity += '\x1f' + graph_.strings.get(tag);
    return SubstitutionKey{
      SUBSTITUTION_EXPLICIT, graph_.strings.intern(identity)};
  }

  SubstitutionKey tagged_path_key(size_t path, const vector<size_t> & tags)
  {
    string identity = "__cppgm_abi_tagged_path_" + std::to_string(path);
    for(size_t tag : tags) identity += '\x1f' + graph_.strings.get(tag);
    return SubstitutionKey{
      SUBSTITUTION_EXPLICIT, graph_.strings.intern(identity)};
  }

  SubstitutionKey member_template_prefix_key(const TypeNode & type)
  {
    string identity = "__cppgm_abi_member_template_prefix_";
    identity += std::to_string(type.children.at(0));
    identity += '_' + std::to_string(type.symbol);
    for(size_t tag : type.tags) identity += '\x1f' + graph_.strings.get(tag);
    return SubstitutionKey{
      SUBSTITUTION_EXPLICIT, graph_.strings.intern(identity)};
  }

  const AbiFactCase & fact_case_;
  FactGraph & graph_;
  AbiMangleStats * stats_;
  string output_;
  SubstitutionTable substitutions_;
};

}  // namespace

string mangle_fact_file(const AbiFactFile & file)
{
  std::ostringstream output;
  mangle_fact_file_to_stream(file, output);
  return output.str();
}

void mangle_fact_file_to_stream(const AbiFactFile & file, std::ostream & output,
                                AbiMangleStats * stats)
{
  for(const AbiFactCase & fact_case : file.cases) {
    if(stats) {
      ++stats->cases;
      stats->records += fact_case.records.size();
    }
    FactGraph graph(fact_case, stats);
    const string name = Encoder(fact_case, graph, stats).mangle();
    output.write(name.data(), static_cast<std::streamsize>(name.size()));
    output.put('\n');
    if(stats) stats->output_bytes += name.size() + 1;
  }
  if(!output) throw std::logic_error("unable to write mangled ABI output");
}

}  // namespace abi_mangle
