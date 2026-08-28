#pragma once

#include "semantic/model/graph.h"
#include "lowering/api.h"
#include "lowering/ir/model.h"

namespace cppgm
{
namespace lowering
{

void LowerSemanticGraph(
	const semantic::SemanticGraphView& graph,
	lowering::ir::TypedProgram& program, LowIRLoweringStats* stats,
	std::size_t source_ordinal);

}
}
