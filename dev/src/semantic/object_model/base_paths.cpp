#include "semantic/model/program.h"
#include "support/exceptions.h"

#include <limits>
#include <vector>

namespace cppgm
{
namespace semantic
{

Program::BasePathState::BasePathState()
	: generation(0), distance(0), offset(0),
	  first_base(std::numeric_limits<std::uint32_t>::max()), path_count(0),
	  complete(false), all_public(false) {}

Program::BasePathFrame::BasePathFrame(EntityId entity_value,
	std::uint32_t next_base_value)
	: entity(entity_value), next_base(next_base_value) {}

Program::BasePathCacheEntry::BasePathCacheEntry(EntityId derived_value,
	EntityId base_value, std::uint32_t version_value)
	: derived(derived_value), base(base_value), version(version_value),
	  distance(0), offset(0), found(false), all_public(false),
	  ambiguous(false), complete(false) {}

bool Program::IsBaseOf(EntityId base, EntityId derived) const
{
	return QueryBasePath(derived, base, 0, 0);
}

void Program::RehashBasePathCache(std::size_t capacity) const
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < base_path_cache_entries_.size(); ++i)
	{
		const BasePathCacheEntry& entry = base_path_cache_entries_[i];
		std::size_t slot = MixHash(entry.derived, entry.base) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	base_path_cache_slots_.swap(replacement);
}

bool Program::FindBasePathCache(EntityId derived, EntityId base,
	bool* found, std::size_t* distance, bool* all_public,
	std::uint64_t* offset, bool* ambiguous, bool require_complete) const
{
	const std::size_t mask = base_path_cache_slots_.size() - 1;
	std::size_t slot = MixHash(derived, base) & mask;
	while (base_path_cache_slots_[slot] != 0)
	{
		const BasePathCacheEntry& entry = base_path_cache_entries_[
			base_path_cache_slots_[slot] - 1];
		if (entry.derived == derived && entry.base == base)
		{
			if (entry.version != base_graph_versions_[derived]) return false;
			if (require_complete && !entry.complete) return false;
			if (found) *found = entry.found;
			if (entry.found)
			{
				if (distance) *distance = entry.distance;
				if (all_public) *all_public = entry.all_public;
				if (offset) *offset = entry.offset;
				if (ambiguous) *ambiguous = entry.ambiguous;
			}
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

void Program::StoreBasePathCache(EntityId derived, EntityId base, bool found,
	std::size_t distance, bool all_public, std::uint64_t offset,
	bool ambiguous, bool complete) const
{
	if ((base_path_cache_entries_.size() + 1) * 10 >
		base_path_cache_slots_.size() * 7)
		RehashBasePathCache(base_path_cache_slots_.size() * 2);
	const std::size_t mask = base_path_cache_slots_.size() - 1;
	std::size_t slot = MixHash(derived, base) & mask;
	while (base_path_cache_slots_[slot] != 0)
	{
		BasePathCacheEntry& entry = base_path_cache_entries_[
			base_path_cache_slots_[slot] - 1];
		if (entry.derived == derived && entry.base == base)
		{
			entry.version = base_graph_versions_[derived];
			entry.found = found;
			entry.distance = distance;
			entry.all_public = all_public;
			entry.offset = offset;
			entry.ambiguous = ambiguous;
			entry.complete = complete;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (base_path_cache_entries_.size() >=
		std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many cached base paths");
	BasePathCacheEntry entry(
		derived, base, base_graph_versions_[derived]);
	entry.found = found;
	entry.distance = distance;
	entry.all_public = all_public;
	entry.offset = offset;
	entry.ambiguous = ambiguous;
	entry.complete = complete;
	base_path_cache_entries_.push_back(entry);
	base_path_cache_slots_[slot] =
		static_cast<std::uint32_t>(base_path_cache_entries_.size());
}

bool Program::QueryBasePath(EntityId derived, EntityId base,
	std::size_t* distance, bool* all_public, std::uint64_t* offset,
	bool* ambiguous, std::vector<std::uint32_t>* direct_base_ordinals) const
{
	if (direct_base_ordinals) direct_base_ordinals->clear();
	++base_path_queries;
	if (base == kNoEntity || derived == kNoEntity ||
		base >= entities.size() || derived >= entities.size()) return false;
	if (ambiguous) *ambiguous = false;
	std::uint64_t shared_virtual_offset = 0;
	const bool shared_virtual =
		FindVirtualBase(derived, base, &shared_virtual_offset);
	bool cached_found = false;
	const bool require_complete = all_public || ambiguous;
	if (!direct_base_ordinals && FindBasePathCache(derived, base,
		&cached_found, distance, all_public, offset, ambiguous,
		require_complete))
	{
		++base_path_cache_hits;
		return cached_found;
	}
	++base_path_cache_misses;
	if (derived == base)
	{
		if (distance) *distance = 0;
		if (all_public) *all_public = true;
		if (offset) *offset = 0;
		StoreBasePathCache(derived, base, true, 0, true, 0, false, true);
		return true;
	}
	if ((!entities[base].complete && entities[base].direct_base_count == 0) ||
		base_depths_[derived] < base_depths_[base])
	{
		StoreBasePathCache(derived, base, false, 0, false, 0, false, true);
		return false;
	}
	if (entities[derived].nonlinear_base_graph)
	{
		const bool detect_ambiguity = require_complete || direct_base_ordinals;
		const bool seek_public_path = all_public || direct_base_ordinals;
		if (base_path_states_.size() < entities.size())
			base_path_states_.resize(entities.size());
		if (base_path_generation_ ==
			std::numeric_limits<std::uint32_t>::max())
		{
			for (std::size_t i = 0; i < base_path_states_.size(); ++i)
				base_path_states_[i].generation = 0;
			base_path_generation_ = 0;
		}
		const std::uint32_t generation = ++base_path_generation_;
		base_path_scratch_.clear();
		BasePathState& root = base_path_states_[derived];
		root.generation = generation;
		root.distance = 0;
		root.offset = 0;
		root.first_base = std::numeric_limits<std::uint32_t>::max();
		root.path_count = derived == base ? 1 : 0;
		root.complete = derived == base;
		root.all_public = derived == base;
		if (!root.complete)
			base_path_scratch_.push_back(BasePathFrame(derived));
		while (!base_path_scratch_.empty())
		{
			BasePathFrame& frame = base_path_scratch_.back();
			BasePathState& current = base_path_states_[frame.entity];
			const EntityRecord& current_record = entities[frame.entity];
			if ((!detect_ambiguity && current.path_count != 0) ||
				(detect_ambiguity && current.path_count == 2 &&
				 (!seek_public_path || current.all_public)) ||
				frame.next_base == current_record.direct_base_count)
			{
				current.complete = true;
				base_path_scratch_.pop_back();
				continue;
			}
			const DirectBaseEdge& edge = DirectBase(
				frame.entity, frame.next_base);
			++base_path_edge_visits;
			if (base_depths_[edge.entity] < base_depths_[base])
			{
				++frame.next_base;
				continue;
			}
			BasePathState& child = base_path_states_[edge.entity];
			if (child.generation != generation)
			{
				child.generation = generation;
				child.distance = 0;
				child.offset = 0;
				child.first_base =
					std::numeric_limits<std::uint32_t>::max();
				child.path_count = edge.entity == base ? 1 : 0;
				child.complete = edge.entity == base;
				child.all_public = edge.entity == base;
				if (!child.complete)
				{
					base_path_scratch_.push_back(BasePathFrame(edge.entity));
					continue;
				}
			}
			if (!child.complete)
				ThrowInternalCompilerError("cyclic class inheritance path");
			++frame.next_base;
			if (child.path_count == 0) continue;
			const bool candidate_all_public =
				edge.access == ACCESS_PUBLIC && child.all_public;
			const bool select_candidate = current.path_count == 0 ||
				(!current.all_public && candidate_all_public);
			if (select_candidate)
			{
				if (child.distance == std::numeric_limits<std::size_t>::max())
					ThrowSemanticResourceLimit("class inheritance is too deep");
				if (child.offset >
					std::numeric_limits<std::uint64_t>::max() - edge.offset)
					ThrowSemanticResourceLimit("base-subobject offset overflow");
				current.distance = child.distance + 1;
				current.offset = edge.offset + child.offset;
				current.first_base = frame.next_base - 1;
				current.all_public = candidate_all_public;
			}
			current.path_count = current.path_count == 0 ?
				child.path_count : 2;
		}
		if (root.path_count == 0)
		{
			StoreBasePathCache(derived, base,
				false, 0, false, 0, false, true);
			return false;
		}
		if (distance) *distance = root.distance;
		if (all_public) *all_public = root.all_public;
		if (offset) *offset = shared_virtual ? shared_virtual_offset : root.offset;
		if (ambiguous) *ambiguous = !shared_virtual &&
			detect_ambiguity && root.path_count == 2;
		if (direct_base_ordinals)
		{
			EntityId current = derived;
			for (std::size_t step = 0; current != base; ++step)
			{
				if (step >= entities.size())
					ThrowInternalCompilerError("cyclic class inheritance path");
				const BasePathState& state = base_path_states_[current];
				if (state.generation != generation ||
					state.first_base ==
						std::numeric_limits<std::uint32_t>::max())
					ThrowInternalCompilerError("base path has no selected edge");
				direct_base_ordinals->push_back(state.first_base);
				current = DirectBase(current, state.first_base).entity;
			}
		}
		StoreBasePathCache(derived, base, true, root.distance,
			root.all_public,
			shared_virtual ? shared_virtual_offset : root.offset,
			!shared_virtual && detect_ambiguity && root.path_count == 2,
			detect_ambiguity);
		return true;
	}
	const std::uint32_t difference =
		base_depths_[derived] - base_depths_[base];
	EntityId current = derived;
	std::uint32_t remaining = difference;
	for (std::size_t level = 0; remaining != 0 && current != kNoEntity;
		++level, remaining >>= 1)
		if ((remaining & 1) != 0)
			current = level < base_jump_counts_[current] ?
				base_jumps_[base_jump_offsets_[current] + level] : kNoEntity;
	if (current != base)
	{
		StoreBasePathCache(derived, base, false, 0, false, 0, false, true);
		return false;
	}
	if (distance) *distance = difference;
	const bool selected_all_public =
		deepest_nonpublic_base_depths_[derived] <= base_depths_[base];
	if (all_public) *all_public = selected_all_public;
	std::uint64_t total = 0;
	current = derived;
	for (std::uint32_t step = 0; step < difference; ++step)
	{
		const DirectBaseEdge& edge = DirectBase(current, 0);
		if (total > std::numeric_limits<std::uint64_t>::max() - edge.offset)
			ThrowSemanticResourceLimit("base-subobject offset overflow");
		total += edge.offset;
		current = edge.entity;
	}
	if (offset) *offset = total;
	if (direct_base_ordinals)
		direct_base_ordinals->assign(difference, 0);
	StoreBasePathCache(derived, base, true, difference,
		selected_all_public, total, false, true);
	return true;
}

}
}
