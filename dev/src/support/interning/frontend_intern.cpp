#include "support/interning/frontend_intern.h"

#include "support/exceptions.h"

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace
{

std::size_t HashTextRange(const std::string& text, std::size_t first,
	std::size_t count)
{
	std::size_t value = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1469598103934665603ULL) :
		static_cast<std::size_t>(2166136261U);
	const std::size_t prime = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1099511628211ULL) :
		static_cast<std::size_t>(16777619U);
	for (std::size_t i = first; i < first + count; ++i)
	{
		value ^= static_cast<unsigned char>(text[i]);
		value *= prime;
	}
	return value;
}

}

InternedStringStats::InternedStringStats()
	: calls(0), hits(0), misses(0), hash_bytes(0),
	  occupied_slot_probes(0), text_comparisons(0), rehashes(0),
	  rehash_entries(0), rehash_hash_bytes(0),
	  max_occupied_slot_probes(0)
{
}

void InternedStringStats::Accumulate(const InternedStringStats& other)
{
	calls += other.calls;
	hits += other.hits;
	misses += other.misses;
	hash_bytes += other.hash_bytes;
	occupied_slot_probes += other.occupied_slot_probes;
	text_comparisons += other.text_comparisons;
	rehashes += other.rehashes;
	rehash_entries += other.rehash_entries;
	rehash_hash_bytes += other.rehash_hash_bytes;
	max_occupied_slot_probes =
		std::max(max_occupied_slot_probes, other.max_occupied_slot_probes);
}

InternedStringTable::InternedStringTable()
	: slots_(32, 0), spelling_bytes_(0), stats_(0)
{
	texts_.push_back(std::string());
}

InternedStringId InternedStringTable::Intern(const std::string& text)
{
	return InternRange(text, 0, text.size());
}

InternedStringId InternedStringTable::InternRange(const std::string& text,
	std::size_t first, std::size_t count)
{
	if (first > text.size() || count > text.size() - first)
		ThrowInternalCompilerError("invalid interned spelling range");
	if (stats_)
	{
		++stats_->calls;
		stats_->hash_bytes += count;
	}
	if ((texts_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = HashTextRange(text, first, count) & mask;
	std::size_t occupied_probes = 0;
	while (slots_[slot] != 0)
	{
		++occupied_probes;
		if (stats_) ++stats_->occupied_slot_probes;
		const InternedStringId id = slots_[slot];
		const bool same_size = texts_[id].size() == count;
		if (stats_ && same_size) ++stats_->text_comparisons;
		if (same_size && text.compare(first, count, texts_[id]) == 0)
		{
			if (stats_)
			{
				++stats_->hits;
				stats_->max_occupied_slot_probes = std::max(
					stats_->max_occupied_slot_probes, occupied_probes);
			}
			return id;
		}
		slot = (slot + 1) & mask;
	}
	if (stats_)
	{
		++stats_->misses;
		stats_->max_occupied_slot_probes = std::max(
			stats_->max_occupied_slot_probes, occupied_probes);
	}
	if (texts_.size() > std::numeric_limits<InternedStringId>::max())
		ThrowGeneralResourceLimit("too many interned front-end spellings");
	const InternedStringId id =
		static_cast<InternedStringId>(texts_.size());
	texts_.push_back(text.substr(first, count));
	spelling_bytes_ += count;
	slots_[slot] = id;
	return id;
}

void InternedStringTable::Reserve(std::size_t expected_texts)
{
	if (expected_texts >= std::numeric_limits<InternedStringId>::max())
		expected_texts = std::numeric_limits<InternedStringId>::max() - 1;
	texts_.reserve(expected_texts + 1);
	std::size_t capacity = slots_.size();
	while (expected_texts + 1 > capacity * 7 / 10) capacity *= 2;
	if (capacity > slots_.size()) Rehash(capacity);
}

const std::string& InternedStringTable::Get(InternedStringId id) const
{
	return texts_[id];
}

std::size_t InternedStringTable::Size() const
{
	return texts_.size() - 1;
}

std::size_t InternedStringTable::SpellingBytes() const
{
	return spelling_bytes_;
}

std::size_t InternedStringTable::StorageBytes() const
{
	std::size_t bytes = texts_.capacity() * sizeof(std::string) +
		slots_.capacity() * sizeof(InternedStringId);
	for (std::size_t i = 1; i < texts_.size(); ++i)
		bytes += texts_[i].capacity();
	return bytes;
}

InternedStringStats* InternedStringTable::AttachStats(
	InternedStringStats* stats)
{
	InternedStringStats* previous = stats_;
	stats_ = stats;
	return previous;
}

void InternedStringTable::Rehash(std::size_t capacity)
{
	if (stats_)
	{
		++stats_->rehashes;
		stats_->rehash_entries += texts_.size() - 1;
		stats_->rehash_hash_bytes += spelling_bytes_;
	}
	std::vector<InternedStringId> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (InternedStringId id = 1; id < texts_.size(); ++id)
	{
		std::size_t slot = HashTextRange(texts_[id], 0,
			texts_[id].size()) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = id;
	}
	slots_.swap(replacement);
}

}
