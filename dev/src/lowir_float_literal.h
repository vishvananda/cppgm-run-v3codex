#pragma once

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace lowir_model
{

inline bool parse_lowir_floating_literal(const std::string& spelling,
	long double* value)
{
	std::string number = spelling;
	std::string lower = number;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		static_cast<int (*)(int)>(std::tolower));
	if (lower.size() > 3 &&
		(lower.back() == 'f' || lower.back() == 'l'))
	{
		lower.erase(lower.size() - 1);
		number.erase(number.size() - 1);
	}
	bool negative = false;
	std::string base = lower;
	if (!base.empty() && (base[0] == '+' || base[0] == '-'))
	{
		negative = base[0] == '-';
		base.erase(0, 1);
	}
	if (base == "inf" || base == "infinity")
	{
		*value = std::numeric_limits<long double>::infinity();
		if (negative) *value = -*value;
		return true;
	}
	if (base == "nan" || base == "snan")
	{
		*value = base == "snan" ?
			std::numeric_limits<long double>::signaling_NaN() :
			std::numeric_limits<long double>::quiet_NaN();
		if (negative) *value = -*value;
		return true;
	}
	errno = 0;
	char* end = 0;
	const long double parsed = std::strtold(number.c_str(), &end);
	if (errno || !end || *end) return false;
	*value = parsed;
	return true;
}

}  // namespace lowir_model
