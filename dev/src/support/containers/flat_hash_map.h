#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace cppgm
{
namespace detail
{

inline std::size_t MixedHash(std::size_t hash)
{
	std::uint64_t value = static_cast<std::uint64_t>(hash);
	value ^= value >> 30;
	value *= UINT64_C(0xBF58476D1CE4E5B9);
	value ^= value >> 27;
	value *= UINT64_C(0x94D049BB133111EB);
	value ^= value >> 31;
	return static_cast<std::size_t>(value);
}

template <typename Key, typename Value, typename Hash = std::hash<Key> >
class FlatHashMap
{
public:
	FlatHashMap() : size_(0), tombstones_(0), slots_(16) {}

	Value* Find(const Key& key)
	{
		return const_cast<Value*>(
			static_cast<const FlatHashMap&>(*this).Find(key));
	}

	const Value* Find(const Key& key) const
	{
		std::size_t position = Bucket(key);
		while (slots_[position].state != EMPTY)
		{
			if (slots_[position].state == OCCUPIED &&
				slots_[position].key == key)
				return &slots_[position].value;
			position = (position + 1) & (slots_.size() - 1);
		}
		return 0;
	}

	Value* Insert(const Key& key, Value value)
	{
		PrepareInsert();
		std::size_t position = Bucket(key);
		std::size_t available = slots_.size();
		while (slots_[position].state != EMPTY)
		{
			if (slots_[position].state == OCCUPIED &&
				slots_[position].key == key)
				return &slots_[position].value;
			if (available == slots_.size() &&
				slots_[position].state == TOMBSTONE)
				available = position;
			position = (position + 1) & (slots_.size() - 1);
		}
		if (available != slots_.size())
			position = available;
		Slot& slot = slots_[position];
		if (slot.state == TOMBSTONE)
			--tombstones_;
		slot.key = key;
		slot.value = std::move(value);
		slot.state = OCCUPIED;
		++size_;
		return &slot.value;
	}

	bool Erase(const Key& key)
	{
		std::size_t position = Bucket(key);
		while (slots_[position].state != EMPTY)
		{
			Slot& slot = slots_[position];
			if (slot.state == OCCUPIED && slot.key == key)
			{
				slot.value = Value();
				slot.state = TOMBSTONE;
				--size_;
				++tombstones_;
				return true;
			}
			position = (position + 1) & (slots_.size() - 1);
		}
		return false;
	}

private:
	enum SlotState
	{
		EMPTY,
		OCCUPIED,
		TOMBSTONE
	};

	struct Slot
	{
		Key key;
		Value value;
		SlotState state;

		Slot() : key(), value(), state(EMPTY) {}
	};

	std::size_t Bucket(const Key& key) const
	{
		return MixedHash(Hash()(key)) & (slots_.size() - 1);
	}

	void PrepareInsert()
	{
		if ((size_ + tombstones_ + 1) * 10 < slots_.size() * 7)
			return;
		const std::size_t capacity = size_ * 10 < slots_.size() * 3 ?
			slots_.size() : slots_.size() * 2;
		Rehash(capacity);
	}

	void Rehash(std::size_t capacity)
	{
		std::vector<Slot> old;
		old.swap(slots_);
		slots_.resize(capacity);
		size_ = 0;
		tombstones_ = 0;
		for (std::size_t i = 0; i < old.size(); ++i)
		{
			if (old[i].state != OCCUPIED)
				continue;
			std::size_t position = Bucket(old[i].key);
			while (slots_[position].state == OCCUPIED)
				position = (position + 1) & (slots_.size() - 1);
			slots_[position].key = old[i].key;
			slots_[position].value = std::move(old[i].value);
			slots_[position].state = OCCUPIED;
			++size_;
		}
	}

	std::size_t size_;
	std::size_t tombstones_;
	std::vector<Slot> slots_;
};

}
}
