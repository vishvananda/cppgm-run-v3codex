#pragma once

#include <iosfwd>

namespace cppgm
{
namespace lowering
{
namespace ir
{
struct Program;

// Serialize the explicit LowIR tool view without changing typed ownership.
void RenderLowIR(const Program& program, std::ostream& output);

}
}
}
