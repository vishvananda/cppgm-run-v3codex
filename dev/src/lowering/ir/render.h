#pragma once

#include <iosfwd>

namespace cppgm
{
namespace pa15_lowir_detail
{
struct TypedProgram;
}

// Serialize the explicit PA15 LowIR tool view without changing typed ownership.
void RenderLowIRProgram(const pa15_lowir_detail::TypedProgram& program,
	std::ostream& output);

}
