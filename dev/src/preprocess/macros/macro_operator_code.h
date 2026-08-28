#ifndef CPPGM_MACRO_OPERATOR_CODE_H
#define CPPGM_MACRO_OPERATOR_CODE_H

#include <cstdint>
#include <string>

namespace cppgm
{
namespace macro_detail
{

enum TrackedOperatorCode : std::uint8_t
{
	TRACKED_OPERATOR_NONE,
	TRACKED_OPERATOR_HASH,
	TRACKED_OPERATOR_HASH_HASH,
	TRACKED_OPERATOR_LPAREN,
	TRACKED_OPERATOR_RPAREN,
	TRACKED_OPERATOR_COMMA,
	TRACKED_OPERATOR_DOTS,
	TRACKED_OPERATOR_LT,
	TRACKED_OPERATOR_GT
};

TrackedOperatorCode TrackOperatorSpelling(const std::string& spelling);
std::string QuoteString(const std::string& value);

inline TrackedOperatorCode ExpectedOperatorCode(const char* spelling)
{
	switch (spelling[0])
	{
	case '#':
		if (spelling[1] == '\0') return TRACKED_OPERATOR_HASH;
		if (spelling[1] == '#' && spelling[2] == '\0')
			return TRACKED_OPERATOR_HASH_HASH;
		break;
	case '(': return spelling[1] == '\0' ? TRACKED_OPERATOR_LPAREN :
		TRACKED_OPERATOR_NONE;
	case ')': return spelling[1] == '\0' ? TRACKED_OPERATOR_RPAREN :
		TRACKED_OPERATOR_NONE;
	case ',': return spelling[1] == '\0' ? TRACKED_OPERATOR_COMMA :
		TRACKED_OPERATOR_NONE;
	case '<': return spelling[1] == '\0' ? TRACKED_OPERATOR_LT :
		TRACKED_OPERATOR_NONE;
	case '>': return spelling[1] == '\0' ? TRACKED_OPERATOR_GT :
		TRACKED_OPERATOR_NONE;
	case '.':
		if (spelling[1] == '.' && spelling[2] == '.' &&
			spelling[3] == '\0')
			return TRACKED_OPERATOR_DOTS;
		break;
	}
	return TRACKED_OPERATOR_NONE;
}

}
}

#endif
