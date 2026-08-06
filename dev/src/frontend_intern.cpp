#include "frontend_intern.h"

#include <limits>
#include <stdexcept>

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

InternedStringTable::InternedStringTable()
	: slots_(32, 0), spelling_bytes_(0)
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
		throw std::logic_error("invalid interned spelling range");
	if ((texts_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = HashTextRange(text, first, count) & mask;
	while (slots_[slot] != 0)
	{
		const InternedStringId id = slots_[slot];
		if (texts_[id].size() == count &&
			text.compare(first, count, texts_[id]) == 0) return id;
		slot = (slot + 1) & mask;
	}
	if (texts_.size() > std::numeric_limits<InternedStringId>::max())
		throw std::runtime_error("too many interned front-end spellings");
	const InternedStringId id =
		static_cast<InternedStringId>(texts_.size());
	texts_.push_back(text.substr(first, count));
	spelling_bytes_ += count;
	slots_[slot] = id;
	return id;
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

void InternedStringTable::Rehash(std::size_t capacity)
{
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
