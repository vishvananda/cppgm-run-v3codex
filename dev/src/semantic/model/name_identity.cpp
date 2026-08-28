#include "semantic/model/program.h"

namespace cppgm
{
namespace semantic
{

std::size_t MixHash(std::size_t seed, std::uint64_t value)
{
	std::uint64_t mixed = static_cast<std::uint64_t>(seed);
	mixed ^= value + 0x9e3779b97f4a7c15ULL + (mixed << 6) + (mixed >> 2);
	mixed ^= mixed >> 30;
	mixed *= 0xbf58476d1ce4e5b9ULL;
	mixed ^= mixed >> 27;
	mixed *= 0x94d049bb133111ebULL;
	mixed ^= mixed >> 31;
	return static_cast<std::size_t>(mixed);
}

NameTable::NameTable(InternedStringTable& strings)
	: strings_(strings), size_(0)
{
}

NameId NameTable::Intern(const std::string& spelling)
{
	return InternRange(spelling, 0, spelling.size());
}

NameId NameTable::InternRange(const std::string& spelling,
	std::size_t first, std::size_t count)
{
	return UseInterned(strings_.InternRange(spelling, first, count));
}

NameId NameTable::UseInterned(NameId name)
{
	(void)strings_.Get(name);
	if (used_.size() <= name)
		used_.resize(static_cast<std::size_t>(name) + 1, 0);
	if (used_[name] == 0)
	{
		used_[name] = 1;
		++size_;
	}
	return name;
}

const std::string& NameTable::Get(NameId name) const
{
	return strings_.Get(name);
}

std::size_t NameTable::Size() const
{
	return size_;
}

std::size_t NameTable::StorageBytes() const
{
	return used_.capacity() * sizeof(std::uint8_t);
}

bool Program::IsStandardNamespace(ScopeId scope) const
{
	return scope != kNoScope && scope == standard_namespace_;
}

bool Program::IsInStandardNamespace(ScopeId scope) const
{
	for (; scope != kNoScope; scope = ParentScope(scope))
		if (scope == standard_namespace_) return true;
	return false;
}

}
}
