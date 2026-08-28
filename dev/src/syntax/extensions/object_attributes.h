#pragma once

#include "syntax/model/arena.h"

#include <cstddef>
#include <vector>

namespace cppgm
{
namespace syntax
{

bool ConsumeLeadingGnuObjectAttribute(
	const std::vector<syntax::SyntaxToken>& tokens,
	const syntax::StringTable& strings,
	syntax::SyntaxArena& arena, std::size_t* position,
	std::vector<syntax::NodeId>* attributes);
bool ConsumeLeadingStandardObjectAttribute(
	const std::vector<syntax::SyntaxToken>& tokens,
	const syntax::StringTable& strings,
	syntax::SyntaxArena& arena, std::size_t* position,
	std::vector<syntax::NodeId>* attributes);

}
}
