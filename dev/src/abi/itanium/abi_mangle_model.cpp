#include "abi/itanium/abi_mangle.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace abi_mangle {

AbiReferenceList::AbiReferenceList() : mode_(NAMES)
{
  new(&storage_.names) std::vector<std::string>();
}

AbiReferenceList::AbiReferenceList(const AbiReferenceList & other)
  : mode_(other.mode_)
{
  copy(other);
}

AbiReferenceList::AbiReferenceList(AbiReferenceList && other) noexcept
  : mode_(other.mode_)
{
  move(other);
}

AbiReferenceList & AbiReferenceList::operator=(const AbiReferenceList & other)
{
  if(this != &other) {
    destroy();
    mode_ = other.mode_;
    copy(other);
  }
  return *this;
}

AbiReferenceList & AbiReferenceList::operator=(AbiReferenceList && other) noexcept
{
  if(this != &other) {
    destroy();
    mode_ = other.mode_;
    move(other);
  }
  return *this;
}

AbiReferenceList::~AbiReferenceList()
{
  destroy();
}

bool AbiReferenceList::resolved() const { return mode_ == RESOLVED; }

bool AbiReferenceList::empty() const
{
  return mode_ == NAMES ? storage_.names.empty() : storage_.resolved.empty();
}

std::size_t AbiReferenceList::size() const
{
  return mode_ == NAMES ? storage_.names.size() : storage_.resolved.size();
}

void AbiReferenceList::push_name(const std::string & name)
{
  if(mode_ != NAMES) throw std::logic_error("mixed ABI reference list");
  storage_.names.push_back(name);
}

void AbiReferenceList::push_resolved(std::size_t id)
{
  if(mode_ == NAMES) {
    if(!storage_.names.empty()) throw std::logic_error("mixed ABI reference list");
    storage_.names.~vector<std::string>();
    new(&storage_.resolved) std::vector<std::size_t>();
    mode_ = RESOLVED;
  }
  storage_.resolved.push_back(id);
}

const std::vector<std::string> & AbiReferenceList::names() const
{
  if(mode_ != NAMES) throw std::logic_error("resolved ABI reference has no name");
  return storage_.names;
}

const std::vector<std::size_t> & AbiReferenceList::resolved_ids() const
{
  if(mode_ != RESOLVED) throw std::logic_error("text ABI reference has no ID");
  return storage_.resolved;
}

void AbiReferenceList::destroy()
{
  if(mode_ == NAMES) storage_.names.~vector<std::string>();
  else storage_.resolved.~vector<std::size_t>();
}

void AbiReferenceList::copy(const AbiReferenceList & other)
{
  if(mode_ == NAMES)
    new(&storage_.names) std::vector<std::string>(other.storage_.names);
  else new(&storage_.resolved)
    std::vector<std::size_t>(other.storage_.resolved);
}

void AbiReferenceList::move(AbiReferenceList & other) noexcept
{
  if(mode_ == NAMES)
    new(&storage_.names) std::vector<std::string>(
      std::move(other.storage_.names));
  else new(&storage_.resolved) std::vector<std::size_t>(
    std::move(other.storage_.resolved));
}

AbiTypePresentationNames::AbiTypePresentationNames()
  : mode_(NAMES), namespace_count_(0)
{
  new(&storage_.names) std::vector<std::string>();
}

AbiTypePresentationNames::AbiTypePresentationNames(
  const AbiTypePresentationNames & other)
  : mode_(other.mode_), namespace_count_(other.namespace_count_)
{
  copy(other);
}

AbiTypePresentationNames::AbiTypePresentationNames(
  AbiTypePresentationNames && other) noexcept
  : mode_(other.mode_), namespace_count_(other.namespace_count_)
{
  move(other);
}

AbiTypePresentationNames & AbiTypePresentationNames::operator=(
  const AbiTypePresentationNames & other)
{
  if(this != &other) {
    destroy();
    mode_ = other.mode_;
    namespace_count_ = other.namespace_count_;
    copy(other);
  }
  return *this;
}

AbiTypePresentationNames & AbiTypePresentationNames::operator=(
  AbiTypePresentationNames && other) noexcept
{
  if(this != &other) {
    destroy();
    mode_ = other.mode_;
    namespace_count_ = other.namespace_count_;
    move(other);
  }
  return *this;
}

AbiTypePresentationNames::~AbiTypePresentationNames()
{
  destroy();
}

bool AbiTypePresentationNames::resolved() const
{
  return mode_ == RESOLVED;
}

bool AbiTypePresentationNames::empty() const { return size() == 0; }

std::size_t AbiTypePresentationNames::size() const
{
  return mode_ == NAMES ? storage_.names.size() : storage_.resolved.size();
}

std::size_t AbiTypePresentationNames::namespace_size() const
{
  return namespace_count_;
}

std::size_t AbiTypePresentationNames::tag_size() const
{
  return size() - namespace_count_;
}

void AbiTypePresentationNames::push_namespace_name(const std::string & name)
{
  if(mode_ != NAMES) throw std::logic_error("mixed ABI presentation names");
  if(namespace_count_ != storage_.names.size())
    throw std::logic_error("ABI namespace name follows a tag");
  require_namespace_capacity();
  storage_.names.push_back(name);
  ++namespace_count_;
}

void AbiTypePresentationNames::push_tag_name(const std::string & name)
{
  if(mode_ != NAMES) throw std::logic_error("mixed ABI presentation names");
  storage_.names.push_back(name);
}

void AbiTypePresentationNames::push_namespace_resolved(std::size_t id)
{
  prepare_resolved();
  if(namespace_count_ != storage_.resolved.size())
    throw std::logic_error("ABI namespace ID follows a tag");
  require_namespace_capacity();
  storage_.resolved.push_back(id);
  ++namespace_count_;
}

void AbiTypePresentationNames::push_tag_resolved(std::size_t id)
{
  prepare_resolved();
  storage_.resolved.push_back(id);
}

const std::vector<std::string> & AbiTypePresentationNames::names() const
{
  if(mode_ != NAMES)
    throw std::logic_error("resolved ABI presentation has no name");
  return storage_.names;
}

const std::vector<std::size_t> &
AbiTypePresentationNames::resolved_ids() const
{
  if(mode_ != RESOLVED)
    throw std::logic_error("text ABI presentation has no ID");
  return storage_.resolved;
}

void AbiTypePresentationNames::prepare_resolved()
{
  if(mode_ == RESOLVED) return;
  if(!storage_.names.empty())
    throw std::logic_error("mixed ABI presentation names");
  storage_.names.~vector<std::string>();
  new(&storage_.resolved) std::vector<std::size_t>();
  mode_ = RESOLVED;
}

void AbiTypePresentationNames::require_namespace_capacity() const
{
  if(namespace_count_ == std::numeric_limits<std::uint32_t>::max())
    throw std::runtime_error("too many ABI namespace qualifiers");
}

void AbiTypePresentationNames::destroy()
{
  if(mode_ == NAMES) storage_.names.~vector<std::string>();
  else storage_.resolved.~vector<std::size_t>();
}

void AbiTypePresentationNames::copy(
  const AbiTypePresentationNames & other)
{
  if(mode_ == NAMES)
    new(&storage_.names) std::vector<std::string>(other.storage_.names);
  else new(&storage_.resolved)
    std::vector<std::size_t>(other.storage_.resolved);
}

void AbiTypePresentationNames::move(
  AbiTypePresentationNames & other) noexcept
{
  if(mode_ == NAMES)
    new(&storage_.names) std::vector<std::string>(
      std::move(other.storage_.names));
  else new(&storage_.resolved) std::vector<std::size_t>(
    std::move(other.storage_.resolved));
  other.namespace_count_ = 0;
}

namespace {

static_assert(sizeof(AbiTypePresentationNames) == 32,
              "ABI presentation ranges must occupy one vector");
static_assert(sizeof(AbiType) == 400,
              "typed ABI presentation storage must shrink types");
static_assert(sizeof(AbiTemplateArgument) == 1912,
              "typed ABI terminal must not widen template arguments");
static_assert(sizeof(AbiDependentExpression) == 1024,
              "typed ABI operation must not widen dependent expressions");
static_assert(sizeof(AbiFunctionTarget) == 1072,
              "typed ABI terminal must not widen function targets");
static_assert(sizeof(AbiFunctionRecord) == 824,
              "typed ABI terminal must not widen function records");

std::size_t text_bytes(const std::string & value)
{
  return value.size();
}

std::size_t string_vector_bytes(const std::vector<std::string> & values)
{
  std::size_t result = values.capacity() * sizeof(std::string);
  for(const std::string & value : values) result += text_bytes(value);
  return result;
}

std::size_t reference_list_bytes(const AbiReferenceList & references)
{
  if(references.resolved())
    return references.resolved_ids().capacity() * sizeof(std::size_t);
  return string_vector_bytes(references.names());
}

std::size_t presentation_name_bytes(
  const AbiTypePresentationNames & names)
{
  if(names.resolved())
    return names.resolved_ids().capacity() * sizeof(std::size_t);
  return string_vector_bytes(names.names());
}

std::size_t type_dynamic_bytes(const AbiType & type)
{
  std::size_t result = text_bytes(type.name) + text_bytes(type.substitution)
    + text_bytes(type.standard_substitution) + text_bytes(type.expression_ref)
    + text_bytes(type.context_ref) + text_bytes(type.discriminator)
    + text_bytes(type.array_bound.value)
    + type.modifiers.capacity() * sizeof(AbiTypeModifier)
    + type.types.capacity() * sizeof(AbiType);
  for(const AbiTypeModifier & modifier : type.modifiers)
    result += text_bytes(modifier.array_bound.value);
  for(const AbiType & child : type.types) result += type_dynamic_bytes(child);
  result += reference_list_bytes(type.argument_refs);
  result += presentation_name_bytes(type.presentation_names);
  return result;
}

std::size_t argument_dynamic_bytes(const AbiTemplateArgument & argument)
{
  std::size_t result = type_dynamic_bytes(argument.type)
    + type_dynamic_bytes(argument.value_type) + text_bytes(argument.name)
    + text_bytes(argument.substitution) + text_bytes(argument.entity_ref)
    + text_bytes(argument.symbol) + text_bytes(argument.member_function_terminal)
    + text_bytes(argument.member_function_literal_suffix)
    + type_dynamic_bytes(argument.member_function_conversion_type)
    + type_dynamic_bytes(argument.member_function_result_type)
    + argument.parameter_types.capacity() * sizeof(AbiType);
  for(const AbiType & type : argument.parameter_types)
    result += type_dynamic_bytes(type);
  return result + reference_list_bytes(argument.argument_refs);
}

std::size_t expression_dynamic_bytes(const AbiDependentExpression & expression)
{
  std::size_t result = type_dynamic_bytes(expression.type)
    + type_dynamic_bytes(expression.value_type) + text_bytes(expression.text)
    + text_bytes(expression.op) + text_bytes(expression.entity_ref)
    + reference_list_bytes(expression.expression_refs)
    + reference_list_bytes(expression.argument_refs)
    + expression.type_arguments.capacity() * sizeof(AbiType);
  for(const AbiType & type : expression.type_arguments)
    result += type_dynamic_bytes(type);
  return result;
}

std::size_t function_target_dynamic_bytes(const AbiFunctionTarget & function)
{
  std::size_t result = text_bytes(function.qualified_name)
    + text_bytes(function.context_ref) + text_bytes(function.source_name)
    + text_bytes(function.discriminator) + text_bytes(function.terminal)
    + function.path_operands.capacity() * sizeof(AbiFunctionPathOperand)
    + function.signature_parameter_types.capacity() * sizeof(AbiType)
    + string_vector_bytes(function.namespace_qualifiers)
    + type_dynamic_bytes(function.owner_type)
    + type_dynamic_bytes(function.result_type);
  for(const AbiFunctionPathOperand & operand : function.path_operands)
    result += type_dynamic_bytes(operand.type) + text_bytes(operand.argument_ref);
  for(const AbiType & type : function.signature_parameter_types)
    result += type_dynamic_bytes(type);
  return result;
}

std::size_t definition_dynamic_bytes(const AbiDefinitionRecord & definition)
{
  std::size_t result = text_bytes(definition.id);
  switch(definition.kind) {
    case ABI_DEFINITION_TYPE: return result + type_dynamic_bytes(definition.type);
    case ABI_DEFINITION_TEMPLATE_ARGUMENT:
      return result + argument_dynamic_bytes(definition.template_argument);
    case ABI_DEFINITION_EXPRESSION:
      return result + expression_dynamic_bytes(definition.expression);
    case ABI_DEFINITION_CONTEXT:
      return result + text_bytes(definition.context.fragment)
        + function_target_dynamic_bytes(definition.context.function)
        + definition.context.qualifiers.capacity() * sizeof(AbiFunctionQualifier);
    case ABI_DEFINITION_ENTITY:
      return result + text_bytes(definition.entity.qualified_name)
        + function_target_dynamic_bytes(definition.entity.function);
  }
  return result;
}

std::size_t target_dynamic_bytes(const AbiTargetRecord & target)
{
  return type_dynamic_bytes(target.type) + type_dynamic_bytes(target.base_type)
    + function_target_dynamic_bytes(target.function)
    + text_bytes(target.qualified_name);
}

std::size_t function_record_dynamic_bytes(const AbiFunctionRecord & function)
{
  std::size_t result = text_bytes(function.name)
    + text_bytes(function.substitution) + text_bytes(function.complete_substitution)
    + text_bytes(function.standard_substitution) + text_bytes(function.context_ref)
    + text_bytes(function.source_name) + text_bytes(function.discriminator)
    + text_bytes(function.terminal) + text_bytes(function.literal_suffix)
    + type_dynamic_bytes(function.type)
    + function.types.capacity() * sizeof(AbiType)
    + reference_list_bytes(function.argument_refs)
    + string_vector_bytes(function.namespace_qualifiers)
    + function.qualifiers.capacity() * sizeof(AbiFunctionQualifier);
  for(const AbiType & type : function.types) result += type_dynamic_bytes(type);
  return result;
}

}  // namespace

AbiDefinitionRecord::AbiDefinitionRecord()
  : kind(ABI_DEFINITION_TYPE), id()
{
  construct_payload();
}

AbiDefinitionRecord::AbiDefinitionRecord(const AbiDefinitionRecord & other)
  : kind(other.kind), id(other.id)
{
  copy_payload(other);
}

AbiDefinitionRecord::AbiDefinitionRecord(AbiDefinitionRecord && other) noexcept
  : kind(other.kind), id(std::move(other.id))
{
  move_payload(other);
}

AbiDefinitionRecord & AbiDefinitionRecord::operator=(const AbiDefinitionRecord & other)
{
  if(this != &other) {
    AbiDefinitionRecord copy(other);
    *this = std::move(copy);
  }
  return *this;
}

AbiDefinitionRecord & AbiDefinitionRecord::operator=(AbiDefinitionRecord && other) noexcept
{
  if(this != &other) {
    destroy_payload();
    kind = other.kind;
    id = std::move(other.id);
    move_payload(other);
  }
  return *this;
}

AbiDefinitionRecord::~AbiDefinitionRecord()
{
  destroy_payload();
}

void AbiDefinitionRecord::set_kind(AbiDefinitionKind new_kind)
{
  if(kind == new_kind) return;
  destroy_payload();
  kind = new_kind;
  construct_payload();
}

void AbiDefinitionRecord::construct_payload()
{
  switch(kind) {
    case ABI_DEFINITION_TYPE: new(&type) AbiType(); return;
    case ABI_DEFINITION_TEMPLATE_ARGUMENT:
      new(&template_argument) AbiTemplateArgument(); return;
    case ABI_DEFINITION_EXPRESSION: new(&expression) AbiDependentExpression(); return;
    case ABI_DEFINITION_CONTEXT: new(&context) AbiLocalContext(); return;
    case ABI_DEFINITION_ENTITY: new(&entity) AbiEntityFact(); return;
  }
}

void AbiDefinitionRecord::copy_payload(const AbiDefinitionRecord & other)
{
  switch(kind) {
    case ABI_DEFINITION_TYPE: new(&type) AbiType(other.type); return;
    case ABI_DEFINITION_TEMPLATE_ARGUMENT:
      new(&template_argument) AbiTemplateArgument(other.template_argument); return;
    case ABI_DEFINITION_EXPRESSION:
      new(&expression) AbiDependentExpression(other.expression); return;
    case ABI_DEFINITION_CONTEXT: new(&context) AbiLocalContext(other.context); return;
    case ABI_DEFINITION_ENTITY: new(&entity) AbiEntityFact(other.entity); return;
  }
}

void AbiDefinitionRecord::move_payload(AbiDefinitionRecord & other) noexcept
{
  switch(kind) {
    case ABI_DEFINITION_TYPE: new(&type) AbiType(std::move(other.type)); return;
    case ABI_DEFINITION_TEMPLATE_ARGUMENT:
      new(&template_argument) AbiTemplateArgument(std::move(other.template_argument)); return;
    case ABI_DEFINITION_EXPRESSION:
      new(&expression) AbiDependentExpression(std::move(other.expression)); return;
    case ABI_DEFINITION_CONTEXT:
      new(&context) AbiLocalContext(std::move(other.context)); return;
    case ABI_DEFINITION_ENTITY: new(&entity) AbiEntityFact(std::move(other.entity)); return;
  }
}

void AbiDefinitionRecord::destroy_payload()
{
  switch(kind) {
    case ABI_DEFINITION_TYPE: type.~AbiType(); return;
    case ABI_DEFINITION_TEMPLATE_ARGUMENT:
      template_argument.~AbiTemplateArgument(); return;
    case ABI_DEFINITION_EXPRESSION: expression.~AbiDependentExpression(); return;
    case ABI_DEFINITION_CONTEXT: context.~AbiLocalContext(); return;
    case ABI_DEFINITION_ENTITY: entity.~AbiEntityFact(); return;
  }
}

AbiFactRecord::AbiFactRecord() : kind(ABI_FACT_RECORD_TARGET)
{
  construct_payload();
}

AbiFactRecord::AbiFactRecord(const AbiFactRecord & other) : kind(other.kind)
{
  copy_payload(other);
}

AbiFactRecord::AbiFactRecord(AbiFactRecord && other) noexcept : kind(other.kind)
{
  move_payload(other);
}

AbiFactRecord & AbiFactRecord::operator=(const AbiFactRecord & other)
{
  if(this != &other) {
    AbiFactRecord copy(other);
    *this = std::move(copy);
  }
  return *this;
}

AbiFactRecord & AbiFactRecord::operator=(AbiFactRecord && other) noexcept
{
  if(this != &other) {
    destroy_payload();
    kind = other.kind;
    move_payload(other);
  }
  return *this;
}

AbiFactRecord::~AbiFactRecord()
{
  destroy_payload();
}

void AbiFactRecord::set_kind(AbiFactRecordKind new_kind)
{
  if(kind == new_kind) return;
  destroy_payload();
  kind = new_kind;
  construct_payload();
}

void AbiFactRecord::construct_payload()
{
  switch(kind) {
    case ABI_FACT_RECORD_DEFINITION: new(&definition) AbiDefinitionRecord(); return;
    case ABI_FACT_RECORD_TARGET: new(&target) AbiTargetRecord(); return;
    case ABI_FACT_RECORD_FUNCTION: new(&function) AbiFunctionRecord(); return;
  }
}

void AbiFactRecord::copy_payload(const AbiFactRecord & other)
{
  switch(kind) {
    case ABI_FACT_RECORD_DEFINITION:
      new(&definition) AbiDefinitionRecord(other.definition); return;
    case ABI_FACT_RECORD_TARGET: new(&target) AbiTargetRecord(other.target); return;
    case ABI_FACT_RECORD_FUNCTION: new(&function) AbiFunctionRecord(other.function); return;
  }
}

void AbiFactRecord::move_payload(AbiFactRecord & other) noexcept
{
  switch(kind) {
    case ABI_FACT_RECORD_DEFINITION:
      new(&definition) AbiDefinitionRecord(std::move(other.definition)); return;
    case ABI_FACT_RECORD_TARGET:
      new(&target) AbiTargetRecord(std::move(other.target)); return;
    case ABI_FACT_RECORD_FUNCTION:
      new(&function) AbiFunctionRecord(std::move(other.function)); return;
  }
}

void AbiFactRecord::destroy_payload()
{
  switch(kind) {
    case ABI_FACT_RECORD_DEFINITION: definition.~AbiDefinitionRecord(); return;
    case ABI_FACT_RECORD_TARGET: target.~AbiTargetRecord(); return;
    case ABI_FACT_RECORD_FUNCTION: function.~AbiFunctionRecord(); return;
  }
}

std::size_t abi_fact_storage_bytes(const AbiFactFile & file)
{
  std::size_t result = sizeof(file)
    + file.cases.capacity() * sizeof(AbiFactCase);
  for(const AbiFactCase & fact_case : file.cases) {
    result += text_bytes(fact_case.label)
      + fact_case.records.capacity() * sizeof(AbiFactRecord);
    for(const AbiFactRecord & record : fact_case.records) {
      if(record.kind == ABI_FACT_RECORD_DEFINITION)
        result += definition_dynamic_bytes(record.definition);
      else if(record.kind == ABI_FACT_RECORD_TARGET)
        result += target_dynamic_bytes(record.target);
      else result += function_record_dynamic_bytes(record.function);
    }
  }
  return result;
}

std::size_t abi_typed_case_storage_bytes(const AbiTypedCase & fact_case)
{
  std::size_t result = sizeof(fact_case)
    + fact_case.definitions.capacity() * sizeof(AbiDefinitionRecord)
    + fact_case.functions.capacity() * sizeof(AbiFunctionRecord)
    + fact_case.contexts.capacity() * sizeof(AbiResolvedContextBinding)
    + target_dynamic_bytes(fact_case.target);
  for(const AbiDefinitionRecord & definition : fact_case.definitions)
    result += definition_dynamic_bytes(definition);
  for(const AbiFunctionRecord & function : fact_case.functions)
    result += function_record_dynamic_bytes(function);
  return result;
}

}  // namespace abi_mangle
