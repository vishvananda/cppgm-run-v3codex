#pragma once

// Reusable typed vocabulary for Itanium ABI naming facts.

#include <cstddef>
#include <string>
#include <vector>

namespace abi_mangle {

enum AbiFactRecordKind
{
  ABI_FACT_RECORD_DEFINITION,
  ABI_FACT_RECORD_TARGET,
  ABI_FACT_RECORD_FUNCTION
};

enum AbiDefinitionKind
{
  ABI_DEFINITION_TYPE,
  ABI_DEFINITION_TEMPLATE_ARGUMENT,
  ABI_DEFINITION_EXPRESSION,
  ABI_DEFINITION_CONTEXT,
  ABI_DEFINITION_ENTITY
};

enum AbiTypeKind
{
  ABI_TYPE_NAME_OR_REFERENCE,
  ABI_TYPE_NAMED,
  ABI_TYPE_BUILTIN,
  ABI_TYPE_TEMPLATE_PARAMETER,
  ABI_TYPE_POINTER,
  ABI_TYPE_LVALUE_REFERENCE,
  ABI_TYPE_RVALUE_REFERENCE,
  ABI_TYPE_CV,
  ABI_TYPE_PACK_EXPANSION,
  ABI_TYPE_VENDOR_QUALIFIED,
  ABI_TYPE_ARRAY,
  ABI_TYPE_BUILTIN_TRANSFORM,
  ABI_TYPE_FUNCTION,
  ABI_TYPE_MEMBER_POINTER,
  ABI_TYPE_TEMPLATE_SPECIALIZATION,
  ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION,
  ABI_TYPE_STD_TEMPLATE_SPECIALIZATION,
  ABI_TYPE_MEMBER,
  ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION,
  ABI_TYPE_DECLTYPE_EXPRESSION,
  ABI_TYPE_LAMBDA_CLOSURE,
  ABI_TYPE_LOCAL_TYPE,
  ABI_TYPE_NAMESPACE_LAMBDA
};

enum AbiArrayBoundKind
{
  ABI_ARRAY_BOUND_VALUE,
  ABI_ARRAY_BOUND_RAW,
  ABI_ARRAY_BOUND_EXPRESSION
};

enum AbiTemplateArgumentKind
{
  ABI_TEMPLATE_ARGUMENT_TYPE,
  ABI_TEMPLATE_ARGUMENT_VALUE,
  ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE,
  ABI_TEMPLATE_ARGUMENT_UNTYPED_VALUE,
  ABI_TEMPLATE_ARGUMENT_EXPRESSION,
  ABI_TEMPLATE_ARGUMENT_TEMPLATE_ENTITY,
  ABI_TEMPLATE_ARGUMENT_MEMBER_TEMPLATE_ENTITY,
  ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE,
  ABI_TEMPLATE_ARGUMENT_EXTERNAL_ENTITY,
  ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY,
  ABI_TEMPLATE_ARGUMENT_ENTITY,
  ABI_TEMPLATE_ARGUMENT_PACK
};

enum AbiExpressionKind
{
  ABI_EXPRESSION_TEMPLATE_PARAMETER,
  ABI_EXPRESSION_FUNCTION_PARAMETER,
  ABI_EXPRESSION_LITERAL,
  ABI_EXPRESSION_INTEGRAL_VALUE,
  ABI_EXPRESSION_UNARY,
  ABI_EXPRESSION_BINARY,
  ABI_EXPRESSION_CONDITIONAL,
  ABI_EXPRESSION_PACK_EXPANSION,
  ABI_EXPRESSION_CALL,
  ABI_EXPRESSION_CONVERSION,
  ABI_EXPRESSION_CAST,
  ABI_EXPRESSION_TEMPLATE_ID,
  ABI_EXPRESSION_TYPE_TRAIT,
  ABI_EXPRESSION_SIZEOF_TYPE,
  ABI_EXPRESSION_MEMBER,
  ABI_EXPRESSION_OBJECT_MEMBER,
  ABI_EXPRESSION_EXTERNAL_ENTITY,
  ABI_EXPRESSION_ENTITY
};

enum AbiContextFactKind
{
  ABI_CONTEXT_RAW,
  ABI_CONTEXT_FUNCTION
};

enum AbiEntityFactKind
{
  ABI_ENTITY_FACT_FUNCTION,
  ABI_ENTITY_FACT_VARIABLE,
  ABI_ENTITY_FACT_SYMBOL
};

enum AbiTargetFactKind
{
  ABI_TARGET_FACT_TYPE,
  ABI_TARGET_FACT_FUNCTION,
  ABI_TARGET_FACT_VARIABLE,
  ABI_TARGET_FACT_TYPEINFO,
  ABI_TARGET_FACT_TYPEINFO_NAME,
  ABI_TARGET_FACT_VTABLE,
  ABI_TARGET_FACT_VTT,
  ABI_TARGET_FACT_CONSTRUCTION_VTABLE,
  ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER,
  ABI_TARGET_FACT_THUNK,
  ABI_TARGET_FACT_VIRTUAL_BASE_THUNK
};

enum AbiFunctionTargetKind
{
  ABI_FUNCTION_TARGET_PATH,
  ABI_FUNCTION_TARGET_ENCODING,
  ABI_FUNCTION_TARGET_LAMBDA,
  ABI_FUNCTION_TARGET_LOCAL,
  ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA
};

enum AbiFunctionPathOperandKind
{
  ABI_FUNCTION_PATH_TYPE,
  ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT,
  ABI_FUNCTION_PATH_RESULT_TYPE,
  ABI_FUNCTION_PATH_VARIADIC
};

enum AbiFunctionRecordKind
{
  ABI_FUNCTION_RECORD_NAME_SOURCE,
  ABI_FUNCTION_RECORD_NAME_STD,
  ABI_FUNCTION_RECORD_NAME_TEMPLATE,
  ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT,
  ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_PREFIX,
  ABI_FUNCTION_RECORD_LOCAL_CONTEXT,
  ABI_FUNCTION_RECORD_LAMBDA_CONTEXT,
  ABI_FUNCTION_RECORD_NAMESPACE_LAMBDA_CONTEXT,
  ABI_FUNCTION_RECORD_TERMINAL_SOURCE,
  ABI_FUNCTION_RECORD_TERMINAL,
  ABI_FUNCTION_RECORD_VARIADIC,
  ABI_FUNCTION_RECORD_ABI_TAG,
  ABI_FUNCTION_RECORD_QUALIFIER,
  ABI_FUNCTION_RECORD_OPERATOR_TERMINAL,
  ABI_FUNCTION_RECORD_CONVERSION_TERMINAL,
  ABI_FUNCTION_RECORD_PARAMETER,
  ABI_FUNCTION_RECORD_RESULT
};

enum AbiFunctionQualifier
{
  ABI_FUNCTION_QUALIFIER_CONST,
  ABI_FUNCTION_QUALIFIER_VOLATILE,
  ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE,
  ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE
};

struct AbiArrayBound
{
  AbiArrayBoundKind kind = ABI_ARRAY_BOUND_VALUE;
  std::string value;
};

// Prefix modifiers parsed from compact fact spellings are kept flat. This
// avoids constructing and recursively destroying a deep owning AbiType chain
// before the modifiers are interned into the canonical graph.
struct AbiTypeModifier
{
  AbiTypeKind kind = ABI_TYPE_POINTER;
  AbiArrayBound array_bound;
  bool is_const = false;
  bool is_volatile = false;
};

struct AbiType
{
  AbiTypeKind kind = ABI_TYPE_NAME_OR_REFERENCE;
  std::string name;
  std::string substitution;
  std::string standard_substitution;
  std::string expression_ref;
  std::string context_ref;
  std::string discriminator;
  AbiArrayBound array_bound;
  std::size_t index = 0;
  bool is_const = false;
  bool is_volatile = false;
  bool variadic = false;
  bool lvalue_ref = false;
  bool rvalue_ref = false;
  bool substitutable = false;
  // A dependent member expression spells its template-name in-place; the
  // enclosing template-id remains a candidate, but that prefix is not entered
  // independently into the surrounding function's substitution sequence.
  bool suppress_template_prefix_substitution = false;
  bool standard_substitution_includes_arguments = false;
  std::vector<AbiTypeModifier> modifiers;
  std::vector<AbiType> types;
  std::vector<std::string> argument_refs;
  std::vector<std::string> namespace_qualifiers;
  std::vector<std::string> abi_tags;
};

struct AbiTemplateArgument
{
  AbiTemplateArgumentKind kind = ABI_TEMPLATE_ARGUMENT_TYPE;
  AbiType type;
  AbiType value_type;
  std::string name;
  std::string substitution;
  std::string entity_ref;
  std::string symbol;
  long long value = 0;
  std::size_t index = 0;
  bool has_value_type = false;
  bool address_of = false;
  bool member_is_function = false;
  bool member_function_const = false;
  bool member_function_volatile = false;
  bool member_function_lvalue_ref = false;
  bool member_function_rvalue_ref = false;
  bool member_function_variadic = false;
  std::vector<AbiType> parameter_types;
  std::vector<std::string> argument_refs;
};

struct AbiDependentExpression
{
  AbiExpressionKind kind = ABI_EXPRESSION_LITERAL;
  AbiType type;
  AbiType value_type;
  std::string text;
  std::string op;
  std::string entity_ref;
  long long value = 0;
  std::size_t index = 0;
  bool close_member_owner = false;
  bool address_of = false;
  std::vector<std::string> expression_refs;
  std::vector<std::string> argument_refs;
  std::vector<AbiType> type_arguments;
};

struct AbiFunctionPathOperand
{
  AbiFunctionPathOperandKind kind = ABI_FUNCTION_PATH_TYPE;
  AbiType type;
  std::string argument_ref;
};

struct AbiFunctionTarget
{
  AbiFunctionTargetKind kind = ABI_FUNCTION_TARGET_PATH;
  std::string qualified_name;
  std::string context_ref;
  std::string source_name;
  std::string discriminator;
  std::string terminal;
  std::vector<AbiFunctionPathOperand> path_operands;
  std::vector<AbiType> signature_parameter_types;
  std::vector<std::string> namespace_qualifiers;
  AbiType result_type;
  bool has_result_type = false;
  bool variadic = false;
};

struct AbiLocalContext
{
  AbiContextFactKind kind = ABI_CONTEXT_RAW;
  std::string fragment;
  AbiFunctionTarget function;
};

struct AbiEntityFact
{
  AbiEntityFactKind kind = ABI_ENTITY_FACT_VARIABLE;
  std::string qualified_name;
  AbiFunctionTarget function;
  bool internal_linkage = false;
};


}  // namespace abi_mangle
