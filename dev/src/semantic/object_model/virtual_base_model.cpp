#include "semantic/model/program.h"
#include "support/exceptions.h"

#include <limits>
#include <vector>

namespace cppgm
{
namespace semantic
{

void Program::RehashVirtualBaseIndex(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < virtual_base_index_entries_.size(); ++i)
	{
		const VirtualBaseIndexEntry& entry = virtual_base_index_entries_[i];
		std::size_t slot = MixHash(entry.derived, entry.base) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	virtual_base_index_slots_.swap(replacement);
}

void Program::IndexVirtualBase(EntityId derived, EntityId base,
	std::uint32_t ordinal)
{
	if (virtual_base_index_slots_.empty())
		virtual_base_index_slots_.assign(64, 0);
	if ((virtual_base_index_entries_.size() + 1) * 10 >
		virtual_base_index_slots_.size() * 7)
		RehashVirtualBaseIndex(virtual_base_index_slots_.size() * 2);
	const std::size_t mask = virtual_base_index_slots_.size() - 1;
	std::size_t slot = MixHash(derived, base) & mask;
	while (virtual_base_index_slots_[slot] != 0)
	{
		VirtualBaseIndexEntry& entry = virtual_base_index_entries_[
			virtual_base_index_slots_[slot] - 1];
		if (entry.derived == derived && entry.base == base)
		{
			entry.ordinal = ordinal;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (virtual_base_index_entries_.size() >=
		std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many indexed virtual base layouts");
	virtual_base_index_entries_.push_back(
		VirtualBaseIndexEntry(derived, base, ordinal));
	virtual_base_index_slots_[slot] =
		static_cast<std::uint32_t>(virtual_base_index_entries_.size());
}

void Program::SetVirtualBaseLayouts(EntityId derived,
	const std::vector<VirtualBaseLayout>& layouts)
{
	if (derived >= entities.size())
		ThrowInternalCompilerError("invalid virtual base layout owner");
	EntityRecord& record = entities[derived];
	if (record.virtual_base_count != 0)
		ThrowInternalCompilerError("virtual base layouts are already fixed");
	if (layouts.size() > std::numeric_limits<std::uint32_t>::max() ||
		virtual_bases.size() > std::numeric_limits<std::uint32_t>::max() -
			layouts.size())
		ThrowSemanticResourceLimit("too many virtual base layouts");
	record.virtual_base_begin =
		static_cast<std::uint32_t>(virtual_bases.size());
	record.virtual_base_count =
		static_cast<std::uint32_t>(layouts.size());
	virtual_bases.insert(virtual_bases.end(), layouts.begin(), layouts.end());
	for (std::size_t i = 0; i < layouts.size(); ++i)
		IndexVirtualBase(derived, layouts[i].entity,
			static_cast<std::uint32_t>(i));
	if (++base_graph_versions_[derived] == 0)
		base_graph_versions_[derived] = 1;
}

const VirtualBaseLayout& Program::VirtualBase(EntityId derived,
	std::size_t ordinal) const
{
	if (derived >= entities.size() ||
		ordinal >= entities[derived].virtual_base_count)
		ThrowInternalCompilerError("invalid virtual base layout query");
	return virtual_bases[entities[derived].virtual_base_begin + ordinal];
}

bool Program::FindVirtualBase(EntityId derived, EntityId base,
	std::uint64_t* offset, std::uint32_t* ordinal) const
{
	++virtual_base_layout_lookups;
	if (derived >= entities.size() || base >= entities.size() ||
		entities[derived].virtual_base_count == 0) return false;
	const std::size_t mask = virtual_base_index_slots_.size() - 1;
	std::size_t slot = MixHash(derived, base) & mask;
	while (virtual_base_index_slots_[slot] != 0)
	{
		++virtual_base_layout_probes;
		const VirtualBaseIndexEntry& entry = virtual_base_index_entries_[
			virtual_base_index_slots_[slot] - 1];
		if (entry.derived == derived && entry.base == base)
		{
			if (entry.ordinal >= entities[derived].virtual_base_count) return false;
			const VirtualBaseLayout& layout = VirtualBase(derived, entry.ordinal);
			if (layout.entity != base) return false;
			if (offset) *offset = layout.offset;
			if (ordinal) *ordinal = entry.ordinal;
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

}
}
