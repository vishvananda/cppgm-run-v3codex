#pragma once

#include <iosfwd>

namespace cppgm
{
namespace lowering
{
namespace ir
{
struct TypedProgram;
}
}

// Serialize the explicit PA15 LowIR tool view without changing typed ownership.
void RenderLowIRProgram(const lowering::ir::TypedProgram& program,
	std::ostream& output);

}
