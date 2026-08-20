#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::IsVolatileSubobjectType(TypeId type) const
{
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_QUALIFIED)
		return (record.cv & (CV_VOLATILE | CV_ATOMIC)) != 0 ||
			IsVolatileSubobjectType(record.child);
	if (record.kind == TYPE_ARRAY)
		return IsVolatileSubobjectType(record.child);
	if (record.kind != TYPE_NAMED || record.entity >= program_->entities.size())
		return false;
	const EntityRecord& entity = program_->entities[record.entity];
	return entity.flavor != NAMED_ENUM && entity.flavor != NAMED_ENUM_CLASS &&
		entity.has_volatile_subobject;
}

bool SemanticAnalyzer::TypeContainsUnionSubobject(TypeId type) const
{
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_ARRAY)
		return TypeContainsUnionSubobject(record.child);
	if (record.kind != TYPE_NAMED || record.entity >= program_->entities.size())
		return false;
	const EntityRecord& entity = program_->entities[record.entity];
	return entity.flavor == NAMED_UNION || entity.has_union_subobject;
}

void SemanticAnalyzer::InitializeClassZeroSpanFacts(EntityId entity)
{
	EntityRecord& owner = program_->entities[entity];
	owner.has_volatile_subobject = false;
	owner.has_union_subobject = owner.flavor == NAMED_UNION;
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
	{
		const EntityRecord& base = program_->entities[
			program_->DirectBase(entity, base_index).entity];
		owner.has_volatile_subobject = owner.has_volatile_subobject ||
			base.has_volatile_subobject;
		owner.has_union_subobject = owner.has_union_subobject ||
			base.has_union_subobject || base.flavor == NAMED_UNION;
	}
}

void SemanticAnalyzer::AccumulateClassZeroSpanFacts(
	EntityId entity, TypeId type)
{
	EntityRecord& owner = program_->entities[entity];
	owner.has_volatile_subobject = owner.has_volatile_subobject ||
		IsVolatileSubobjectType(type);
	owner.has_union_subobject = owner.has_union_subobject ||
		TypeContainsUnionSubobject(type);
}

bool SemanticAnalyzer::ClassBasesAreEmpty(EntityId entity) const
{
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
		if (!program_->entities[
			program_->DirectBase(entity, base_index).entity].empty_class)
			return false;
	return true;
}

void SemanticAnalyzer::SetBindingRequestedAlignment(
	BindingRecord& binding, std::size_t alignment)
{
	if (alignment != 0)
		program_->MutableBindingLayout(binding).requested_alignment = alignment;
}

}
}
