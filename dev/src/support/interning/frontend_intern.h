#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{

typedef std::uint32_t InternedStringId;

struct InternedStringStats
{
	std::size_t calls;
	std::size_t hits;
	std::size_t misses;
	std::size_t hash_bytes;
	std::size_t occupied_slot_probes;
	std::size_t text_comparisons;
	std::size_t rehashes;
	std::size_t rehash_entries;
	std::size_t rehash_hash_bytes;
	std::size_t max_occupied_slot_probes;

	InternedStringStats();
	void Accumulate(const InternedStringStats& other);
};

// Translation-unit spelling ownership shared by syntax and semantics. IDs are
// stable for the lifetime of the table and are the only spelling identity used
// after text enters the front end.
class InternedStringTable
{
public:
	InternedStringTable();
	InternedStringId Intern(const std::string& text);
	InternedStringId InternRange(const std::string& text,
		std::size_t first, std::size_t count);
	void Reserve(std::size_t expected_texts);
	const std::string& Get(InternedStringId id) const;
	std::size_t Size() const;
	std::size_t SpellingBytes() const;
	std::size_t StorageBytes() const;
	InternedStringStats* AttachStats(InternedStringStats* stats);

private:
	void Rehash(std::size_t capacity);

	std::vector<std::string> texts_;
	std::vector<InternedStringId> slots_;
	std::size_t spelling_bytes_;
	InternedStringStats* stats_;
};

}
