#include "lowering/support/identity_maps.h"

#include "lowering/support/errors.h"

namespace cppgm
{
namespace lowering
{
namespace support
{

FlatIdMap::FlatIdMap() : slots_(16, 0) {}

std::size_t FlatIdMap::Hash(std::uint32_t key)
{
	std::uint64_t value = key;
	value ^= value >> 16;
	value *= UINT64_C(0x7feb352d);
	value ^= value >> 15;
	return static_cast<std::size_t>(value);
}

bool FlatIdMap::Find(std::uint32_t key, std::uint32_t* value) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (keys_[entry] == key)
		{
			*value = values_[entry];
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

void FlatIdMap::Insert(std::uint32_t key, std::uint32_t value)
{
	if ((keys_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (keys_[entry] == key)
		{
			values_[entry] = value;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (keys_.size() >= UINT32_MAX)
		ThrowLoweringResourceLimit("too many flat identity map entries");
	keys_.push_back(key);
	values_.push_back(value);
	slots_[slot] = static_cast<std::uint32_t>(keys_.size());
	occupied_slots_.push_back(slot);
}

void FlatIdMap::Clear()
{
	keys_.clear();
	values_.clear();
	for (std::size_t i = 0; i < occupied_slots_.size(); ++i)
		slots_[occupied_slots_[i]] = 0;
	occupied_slots_.clear();
}

void FlatIdMap::Rehash(std::size_t capacity)
{
	slots_.assign(capacity, 0);
	occupied_slots_.clear();
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < keys_.size(); ++i)
	{
		std::size_t slot = Hash(keys_[i]) & mask;
		while (slots_[slot] != 0) slot = (slot + 1) & mask;
		slots_[slot] = static_cast<std::uint32_t>(i + 1);
		occupied_slots_.push_back(slot);
	}
}

std::size_t FlatIdMap::StorageBytes() const
{
	return keys_.capacity() * sizeof(std::uint32_t) +
		values_.capacity() * sizeof(std::uint32_t) +
		slots_.capacity() * sizeof(std::uint32_t) +
		occupied_slots_.capacity() * sizeof(std::size_t);
}

FlatIdPairMap::FlatIdPairMap() : slots_(16, 0) {}

std::size_t FlatIdPairMap::Hash(std::uint32_t first, std::uint32_t second)
{
	std::uint64_t value = (static_cast<std::uint64_t>(first) << 32) | second;
	value ^= value >> 30;
	value *= UINT64_C(0xbf58476d1ce4e5b9);
	value ^= value >> 27;
	value *= UINT64_C(0x94d049bb133111eb);
	value ^= value >> 31;
	return static_cast<std::size_t>(value);
}

bool FlatIdPairMap::Find(std::uint32_t first, std::uint32_t second,
	std::uint32_t* value) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(first, second) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (first_keys_[entry] == first && second_keys_[entry] == second)
		{
			*value = values_[entry];
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

void FlatIdPairMap::Insert(std::uint32_t first, std::uint32_t second,
	std::uint32_t value)
{
	if ((first_keys_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(first, second) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (first_keys_[entry] == first && second_keys_[entry] == second)
		{
			values_[entry] = value;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (first_keys_.size() >= UINT32_MAX)
		ThrowLoweringResourceLimit("too many flat identity-pair map entries");
	first_keys_.push_back(first);
	second_keys_.push_back(second);
	values_.push_back(value);
	slots_[slot] = static_cast<std::uint32_t>(first_keys_.size());
	occupied_slots_.push_back(slot);
}

void FlatIdPairMap::Clear()
{
	first_keys_.clear();
	second_keys_.clear();
	values_.clear();
	for (std::size_t i = 0; i < occupied_slots_.size(); ++i)
		slots_[occupied_slots_[i]] = 0;
	occupied_slots_.clear();
}

void FlatIdPairMap::Rehash(std::size_t capacity)
{
	slots_.assign(capacity, 0);
	occupied_slots_.clear();
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < first_keys_.size(); ++i)
	{
		std::size_t slot = Hash(first_keys_[i], second_keys_[i]) & mask;
		while (slots_[slot] != 0) slot = (slot + 1) & mask;
		slots_[slot] = static_cast<std::uint32_t>(i + 1);
		occupied_slots_.push_back(slot);
	}
}

std::size_t FlatIdPairMap::StorageBytes() const
{
	return first_keys_.capacity() * sizeof(std::uint32_t) +
		second_keys_.capacity() * sizeof(std::uint32_t) +
		values_.capacity() * sizeof(std::uint32_t) +
		slots_.capacity() * sizeof(std::uint32_t) +
		occupied_slots_.capacity() * sizeof(std::size_t);
}
}  // namespace support
}  // namespace lowering
}  // namespace cppgm
