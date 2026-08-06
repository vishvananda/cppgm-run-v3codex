#include "abi_mangle.h"

#include <new>
#include <utility>

namespace abi_mangle {

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

}  // namespace abi_mangle
