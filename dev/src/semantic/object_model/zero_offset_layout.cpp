#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace semantic
{

EntityId Analyzer::ZeroOffsetClassEntity(TypeId type) const
{
	const TypeRecord* record = &program_->types.Get(type);
	while (record->kind == TYPE_ARRAY || record->kind == TYPE_QUALIFIED)
	{
		type = record->child;
		record = &program_->types.Get(type);
	}
	if (record->kind != TYPE_NAMED) return kNoEntity;
	const NamedFlavor flavor = program_->entities[record->entity].flavor;
	return IsClassNamedFlavor(flavor) ? record->entity : kNoEntity;
}

bool Analyzer::VisitZeroOffsetSubobjects(EntityId root,
	std::uint32_t marker, std::uint32_t conflict_marker)
{
	zero_offset_subobject_scratch_.clear();
	zero_offset_subobject_scratch_.push_back(root);
	while (!zero_offset_subobject_scratch_.empty())
	{
		const EntityId entity = zero_offset_subobject_scratch_.back();
		zero_offset_subobject_scratch_.pop_back();
		++class_zero_offset_subobject_visits_;
		if (zero_offset_subobject_marks_[entity] == marker) continue;
		if (zero_offset_subobject_marks_[entity] == conflict_marker) return true;
		zero_offset_subobject_marks_[entity] = marker;
		const EntityRecord& record = program_->entities[entity];
		for (std::size_t base_index = 0;
			base_index < record.direct_base_count; ++base_index)
		{
			const DirectBaseEdge& base = program_->DirectBase(entity, base_index);
			if (base.offset == 0)
				zero_offset_subobject_scratch_.push_back(base.entity);
		}
		if (entity >= entity_layout_members_.size()) continue;
		const std::vector<ClassLayoutMember>& members =
			entity_layout_members_[entity];
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			const ClassLayoutMember& member = members[i];
			if (member.bit_field || member.binding == kNoBinding ||
				program_->BindingLayout(
					program_->bindings[member.binding]).member_offset != 0)
				continue;
			const EntityId child = ZeroOffsetClassEntity(member.type);
			if (child != kNoEntity)
				zero_offset_subobject_scratch_.push_back(child);
		}
	}
	return false;
}

std::uint32_t Analyzer::BeginClassZeroOffsetSubobjects(EntityId entity)
{
	const std::size_t member_count = entity_layout_members_[entity].size();
	if (member_count >= std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many class layout members");
	zero_offset_subobject_marks_.resize(program_->entities.size(), 0);
	const std::uint32_t reserve =
		static_cast<std::uint32_t>(member_count + 1);
	if (zero_offset_subobject_generation_ >
		std::numeric_limits<std::uint32_t>::max() - reserve)
	{
		std::fill(zero_offset_subobject_marks_.begin(),
			zero_offset_subobject_marks_.end(), 0);
		zero_offset_subobject_generation_ = 0;
	}
	const std::uint32_t occupied_marker = ++zero_offset_subobject_generation_;
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
	{
		const DirectBaseEdge& base = program_->DirectBase(entity, base_index);
		if (base.offset == 0)
			(void)VisitZeroOffsetSubobjects(
				base.entity, occupied_marker, occupied_marker);
	}
	return occupied_marker;
}

bool Analyzer::ClassZeroOffsetSubobjectConflict(TypeId member_type,
	std::uint32_t occupied_marker)
{
	const EntityId member = ZeroOffsetClassEntity(member_type);
	if (member == kNoEntity) return false;
	const std::uint32_t candidate_marker = ++zero_offset_subobject_generation_;
	return VisitZeroOffsetSubobjects(
		member, candidate_marker, occupied_marker);
}

void Analyzer::MarkClassZeroOffsetSubobject(TypeId member_type,
	std::uint32_t occupied_marker)
{
	const EntityId member = ZeroOffsetClassEntity(member_type);
	if (member != kNoEntity)
		(void)VisitZeroOffsetSubobjects(
			member, occupied_marker, occupied_marker);
}

}
}
