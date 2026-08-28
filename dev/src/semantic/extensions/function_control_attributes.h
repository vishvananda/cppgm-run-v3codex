#pragma once

#include "syntax/model/arena.h"
#include "semantic/model/program.h"

#include <cstdint>

namespace cppgm
{
namespace semantic
{

std::uint8_t FunctionControlAttributeMask(
	const syntax::SyntaxArena& arena,
	syntax::NodeId declaration);

void ApplyFunctionControlAttributes(semantic::Program* program,
	semantic::BindingId binding, std::uint8_t attributes);

}
}
