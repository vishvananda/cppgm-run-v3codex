#include "pa11_model.h"

#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa11
{

namespace
{

class VisitedBaseStates
{
public:
	VisitedBaseStates() : slots_(8, 0), size_(0) {}

	bool Insert(std::uint64_t value)
	{
		const std::uint64_t stored = value + 1;
		if ((size_ + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = MixHash(0, value) & mask;
		while (slots_[slot] != 0)
		{
			if (slots_[slot] == stored) return false;
			slot = (slot + 1) & mask;
		}
		slots_[slot] = stored;
		++size_;
		return true;
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<std::uint64_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::size_t i = 0; i < slots_.size(); ++i)
		{
			if (slots_[i] == 0) continue;
			const std::uint64_t value = slots_[i] - 1;
			std::size_t slot = MixHash(0, value) & mask;
			while (replacement[slot] != 0)
				slot = (slot + 1) & mask;
			replacement[slot] = slots_[i];
		}
		slots_.swap(replacement);
	}

	std::vector<std::uint64_t> slots_;
	std::size_t size_;
};

struct PendingBase
{
	EntityId entity;
	bool saw_virtual;
	PendingBase(EntityId entity_, bool saw_virtual_)
		: entity(entity_), saw_virtual(saw_virtual_) {}
};

}

bool Program::HasVirtualBasePath(EntityId derived, EntityId base) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= entities.size() || derived >= entities.size()) return false;
	std::vector<PendingBase> pending;
	VisitedBaseStates visited;
	pending.push_back(PendingBase(derived, false));
	while (!pending.empty())
	{
		const PendingBase current = pending.back();
		pending.pop_back();
		++virtual_base_path_visits;
		const std::uint64_t state =
			(static_cast<std::uint64_t>(current.entity) << 1) |
			(current.saw_virtual ? 1 : 0);
		if (!visited.Insert(state)) continue;
		if (current.entity == base && current.saw_virtual) return true;
		const EntityRecord& record = entities[current.entity];
		for (std::size_t i = 0; i < record.direct_base_count; ++i)
		{
			const DirectBaseEdge& edge = DirectBase(current.entity, i);
			pending.push_back(PendingBase(edge.entity,
				current.saw_virtual || edge.virtual_base));
		}
	}
	return false;
}

}
}
