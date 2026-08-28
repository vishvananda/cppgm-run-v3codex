#include "preprocess/macros/macro_operator_code.h"

namespace cppgm
{
namespace macro_detail
{

TrackedOperatorCode TrackOperatorSpelling(const std::string& spelling)
{
	switch (spelling.size())
	{
	case 1:
		switch (spelling[0])
		{
		case '#': return TRACKED_OPERATOR_HASH;
		case '(': return TRACKED_OPERATOR_LPAREN;
		case ')': return TRACKED_OPERATOR_RPAREN;
		case ',': return TRACKED_OPERATOR_COMMA;
		case '<': return TRACKED_OPERATOR_LT;
		case '>': return TRACKED_OPERATOR_GT;
		default: return TRACKED_OPERATOR_NONE;
		}
	case 2:
		if (spelling[0] == '#' && spelling[1] == '#')
			return TRACKED_OPERATOR_HASH_HASH;
		if (spelling[0] == '%' && spelling[1] == ':')
			return TRACKED_OPERATOR_HASH;
		return TRACKED_OPERATOR_NONE;
	case 3:
		return spelling[0] == '.' && spelling[1] == '.' &&
			spelling[2] == '.' ? TRACKED_OPERATOR_DOTS :
			TRACKED_OPERATOR_NONE;
	case 4:
		return spelling[0] == '%' && spelling[1] == ':' &&
			spelling[2] == '%' && spelling[3] == ':' ?
			TRACKED_OPERATOR_HASH_HASH : TRACKED_OPERATOR_NONE;
	default:
		return TRACKED_OPERATOR_NONE;
	}
}

std::string QuoteString(const std::string& value)
{
	std::string result("\"");
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char byte = static_cast<unsigned char>(value[i]);
		if (byte == '\\' || byte == '"')
			result.push_back('\\');
		if (byte < 0x20 || byte == 0x7F)
		{
			result.push_back('\\');
			result.push_back(static_cast<char>('0' + (byte >> 6)));
			result.push_back(static_cast<char>('0' + ((byte >> 3) & 7)));
			result.push_back(static_cast<char>('0' + (byte & 7)));
		}
		else
			result.push_back(static_cast<char>(byte));
	}
	result.push_back('"');
	return result;
}

}
}
