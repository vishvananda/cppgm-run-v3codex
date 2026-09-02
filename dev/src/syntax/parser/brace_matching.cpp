#include "syntax/parser/brace_matching.h"

#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace syntax
{

std::vector<std::uint32_t> BuildBraceMatches(
	const std::vector<SyntaxToken>& tokens)
{
	const std::uint32_t no_match = std::numeric_limits<std::uint32_t>::max();
	if (tokens.size() >= no_match - 1)
		ThrowSyntaxResourceLimit("too many syntax tokens");
	std::vector<std::uint32_t> matches(tokens.size(), no_match);
	std::vector<std::uint32_t> open_braces;
	for (std::size_t i = 0; i < tokens.size(); ++i)
	{
		const std::uint16_t kind = tokens[i].Kind();
		if (kind == static_cast<std::uint16_t>(OP_LBRACE))
			open_braces.push_back(static_cast<std::uint32_t>(i));
		else if (kind == static_cast<std::uint16_t>(OP_RBRACE) &&
			!open_braces.empty())
		{
			matches[open_braces.back()] = static_cast<std::uint32_t>(i);
			open_braces.pop_back();
		}
	}
	return matches;
}

}
}
