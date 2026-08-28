#pragma once

#include "lowering/ir/model.h"

namespace cppgm
{
namespace lowering
{
struct Stats;

namespace inline_policy
{

// Expand force-inline definitions while symbols, operands, blocks, and
// temporaries still have compact typed identities. The transform mutates one
// owned LowIR program and performs work only for explicit expansion.
void RewriteProgram(ir::Program* program,
	Stats* stats = 0,
	bool prune_unreachable_weak_functions = false);

}  // namespace inline_policy
}  // namespace lowering
}  // namespace cppgm
