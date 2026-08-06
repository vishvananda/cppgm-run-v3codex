#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{

typedef std::uint32_t InternedStringId;

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
	const std::string& Get(InternedStringId id) const;
	std::size_t Size() const;
	std::size_t SpellingBytes() const;
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);

	std::vector<std::string> texts_;
	std::vector<InternedStringId> slots_;
	std::size_t spelling_bytes_;
};

}
