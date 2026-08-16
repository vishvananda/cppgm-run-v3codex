#ifndef CPPGM_DECIMAL_SPELLING_H
#define CPPGM_DECIMAL_SPELLING_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppgm
{
namespace detail
{

inline std::string PrefixedUnsignedDecimal(const char* prefix,
	std::size_t prefix_size, std::uint32_t value)
{
	char storage[10];
	char* first = storage + sizeof(storage);
	do
	{
		*--first = static_cast<char>('0' + value % 10);
		value /= 10;
	}
	while (value != 0);
	std::string result(prefix, prefix_size);
	result.append(first, storage + sizeof(storage));
	return result;
}

}
}

#endif
