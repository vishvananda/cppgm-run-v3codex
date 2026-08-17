#pragma once

#include "pa15_lowir_model.h"

namespace cppgm
{

struct LowIRLoweringStats;

namespace pa15_force_inline
{

// Expand force-inline definitions while symbols, operands, blocks, and
// temporaries still have compact typed identities. The transform mutates one
// owned LowIR program and performs work only for explicit expansion.
void RewriteProgram(pa15_lowir_detail::TypedProgram* program,
	LowIRLoweringStats* stats = 0,
	bool prune_unreachable_weak_functions = false);

}
}
