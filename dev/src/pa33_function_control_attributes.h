#pragma once

#include "syntax/model/arena.h"
#include "pa11_model.h"

#include <cstdint>

namespace cppgm
{
namespace pa12_semantic_detail
{

std::uint8_t FunctionControlAttributeMask(
	const pa10_syntax_detail::SyntaxArena& arena,
	pa10_syntax_detail::NodeId declaration);

void ApplyFunctionControlAttributes(pa11::Program* program,
	pa11::BindingId binding, std::uint8_t attributes);

}
}
