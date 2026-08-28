#pragma once

#include "syntax/model/arena.h"
#include "semantic/model/program.h"

#include <cstdint>

namespace cppgm
{
namespace pa12_semantic_detail
{

std::uint8_t FunctionControlAttributeMask(
	const syntax::SyntaxArena& arena,
	syntax::NodeId declaration);

void ApplyFunctionControlAttributes(pa11::Program* program,
	pa11::BindingId binding, std::uint8_t attributes);

}
}
