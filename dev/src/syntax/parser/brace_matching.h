#pragma once

#include "syntax/model/arena.h"

#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa10_syntax_detail
{

std::vector<std::uint32_t> BuildBraceMatches(
	const std::vector<SyntaxToken>& tokens);

}
}
