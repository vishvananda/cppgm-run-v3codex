#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace semantic
{

namespace
{

std::size_t AlignVirtualBase(std::size_t value, std::size_t alignment)
{
	if (alignment <= 1) return value;
	const std::size_t remainder = value % alignment;
	return remainder == 0 ? value : value + alignment - remainder;
}

}

void Analyzer::CollectVirtualBaseLayouts(EntityId entity,
	std::vector<VirtualBaseLayout>* layouts)
{
	layouts->clear();
	if (virtual_base_layout_entity_marks_.size() < program_->entities.size())
	{
		virtual_base_layout_entity_marks_.resize(program_->entities.size(), 0);
		virtual_base_layout_fact_marks_.resize(program_->entities.size(), 0);
	}
	if (virtual_base_layout_generation_ ==
		std::numeric_limits<std::uint32_t>::max())
	{
		std::fill(virtual_base_layout_entity_marks_.begin(),
			virtual_base_layout_entity_marks_.end(), 0);
		std::fill(virtual_base_layout_fact_marks_.begin(),
			virtual_base_layout_fact_marks_.end(), 0);
		virtual_base_layout_generation_ = 0;
	}
	const std::uint32_t generation = ++virtual_base_layout_generation_;
	virtual_base_layout_scratch_.clear();
	virtual_base_layout_entity_marks_[entity] = generation;
	virtual_base_layout_scratch_.push_back(
		std::make_pair(entity, static_cast<std::uint32_t>(0)));
	while (!virtual_base_layout_scratch_.empty())
	{
		std::pair<EntityId, std::uint32_t>& frame =
			virtual_base_layout_scratch_.back();
		const EntityRecord& current = program_->entities[frame.first];
		if (frame.second == current.direct_base_count)
		{
			virtual_base_layout_scratch_.pop_back();
			continue;
		}
		const DirectBaseEdge& edge = program_->DirectBase(
			frame.first, frame.second++);
		++virtual_base_layout_edge_visits_;
		if (edge.virtual_base &&
			virtual_base_layout_fact_marks_[edge.entity] != generation)
		{
			virtual_base_layout_fact_marks_[edge.entity] = generation;
			layouts->push_back(VirtualBaseLayout(edge.entity));
			++virtual_base_layout_facts_;
		}
		if (virtual_base_layout_entity_marks_[edge.entity] == generation)
			continue;
		virtual_base_layout_entity_marks_[edge.entity] = generation;
		virtual_base_layout_scratch_.push_back(
			std::make_pair(edge.entity, static_cast<std::uint32_t>(0)));
	}
}

void Analyzer::FinalizeClassVirtualBaseLayout(EntityId entity,
	std::size_t packing_alignment, std::size_t* size,
	std::size_t* alignment, std::size_t* natural_alignment, bool* empty_class)
{
	EntityRecord& owner = program_->entities[entity];
	std::vector<VirtualBaseLayout> layouts;
	CollectVirtualBaseLayouts(entity, &layouts);
	const std::size_t own_nonvirtual_alignment = *alignment;
	for (std::size_t i = 0; i < layouts.size(); ++i)
	{
		const EntityRecord& base = program_->entities[layouts[i].entity];
		const std::size_t base_alignment =
			static_cast<std::size_t>(base.object_alignment);
		const std::size_t effective = packing_alignment == 0 ? base_alignment :
			std::min(base_alignment, packing_alignment);
		*natural_alignment = std::max(*natural_alignment, base_alignment);
		*alignment = std::max(*alignment, effective);
	}
	if (owner.requested_alignment != 0 &&
		owner.requested_alignment < *natural_alignment)
		ThrowSemanticError(
			"requested alignment is weaker than the natural class alignment");
	*alignment = std::max(*alignment,
		static_cast<std::size_t>(owner.requested_alignment));
	if (*size == 0) *size = 1;
	owner.nonvirtual_size = *size;
	owner.nonvirtual_alignment = std::max(own_nonvirtual_alignment,
		static_cast<std::size_t>(owner.requested_alignment));
	for (std::size_t i = 0; i < layouts.size(); ++i)
	{
		const EntityRecord& base = program_->entities[layouts[i].entity];
		const std::size_t base_alignment = static_cast<std::size_t>(
			base.nonvirtual_alignment == 0 ? base.object_alignment :
			base.nonvirtual_alignment);
		const std::size_t effective = packing_alignment == 0 ? base_alignment :
			std::min(base_alignment, packing_alignment);
		*size = AlignVirtualBase(*size, effective);
		layouts[i].offset = *size;
		const std::size_t base_size = static_cast<std::size_t>(
			base.nonvirtual_size == 0 ? base.object_size : base.nonvirtual_size);
		if (*size > std::numeric_limits<std::size_t>::max() - base_size)
			ThrowSemanticResourceLimit("class layout is too large");
		*size += base_size;
	}
	program_->SetVirtualBaseLayouts(entity, layouts);
	for (std::size_t i = 0; i < owner.direct_base_count; ++i)
	{
		DirectBaseEdge& edge = program_->MutableDirectBase(entity, i);
		if (!edge.virtual_base) continue;
		std::uint64_t offset = 0;
		if (!program_->FindVirtualBase(entity, edge.entity, &offset))
			ThrowInternalCompilerError("direct virtual base has no layout fact");
		edge.offset = offset;
	}
	if (!layouts.empty())
	{
		*empty_class = false;
		owner.trivial_default_constructor = false;
	}
	*size = AlignVirtualBase(*size, *alignment);
	owner.object_size = *size;
	owner.object_alignment = *alignment;
	owner.natural_alignment = *natural_alignment;
	owner.empty_class = *empty_class;
	owner.layout_complete = true;
}

void Analyzer::AddVirtualBaseInitializationActions(EntityId entity,
	ScopeId function_scope, const std::vector<NodeId>& initializers,
	const std::vector<ScopeId>& initializer_scopes,
	const std::vector<std::uint8_t>& initializer_expanded, std::uint32_t body)
{
	const EntityRecord& owner = program_->entities[entity];
	std::vector<std::uint32_t> direct_ordinals(owner.virtual_base_count,
		owner.direct_base_count);
	for (std::size_t candidate = 0;
		candidate < owner.direct_base_count; ++candidate)
	{
		const DirectBaseEdge& edge = program_->DirectBase(entity, candidate);
		if (!edge.virtual_base) continue;
		std::uint32_t virtual_ordinal = 0;
		if (!program_->FindVirtualBase(
			entity, edge.entity, 0, &virtual_ordinal))
			ThrowInternalCompilerError("direct virtual base has no layout fact");
		direct_ordinals[virtual_ordinal] = static_cast<std::uint32_t>(candidate);
	}
	for (std::size_t virtual_ordinal = 0;
		virtual_ordinal < owner.virtual_base_count; ++virtual_ordinal)
	{
		const VirtualBaseLayout& layout =
			program_->VirtualBase(entity, virtual_ordinal);
		const std::size_t direct_ordinal = direct_ordinals[virtual_ordinal];
		if (direct_ordinal != owner.direct_base_count)
			AddBaseInitializationAction(entity, direct_ordinal,
				initializers[direct_ordinal], initializer_scopes[direct_ordinal],
				body, initializer_expanded[direct_ordinal] != 0);
		else AddBaseInitializationActionAt(entity, layout.entity,
			layout.offset, kNoNode, function_scope, body);
	}
}

}
}
