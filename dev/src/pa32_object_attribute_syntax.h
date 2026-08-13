#pragma once

#include "pa10_syntax_model.h"

#include <cstddef>
#include <vector>

namespace cppgm
{
namespace pa32_syntax_detail
{

bool ConsumeLeadingGnuObjectAttribute(
	const std::vector<pa10_syntax_detail::SyntaxToken>& tokens,
	const pa10_syntax_detail::StringTable& strings,
	pa10_syntax_detail::SyntaxArena& arena, std::size_t* position,
	std::vector<pa10_syntax_detail::NodeId>* attributes);
bool ConsumeLeadingStandardObjectAttribute(
	const std::vector<pa10_syntax_detail::SyntaxToken>& tokens,
	const pa10_syntax_detail::StringTable& strings,
	pa10_syntax_detail::SyntaxArena& arena, std::size_t* position,
	std::vector<pa10_syntax_detail::NodeId>* attributes);

}
}
