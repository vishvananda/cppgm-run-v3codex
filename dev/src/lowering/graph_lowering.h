#pragma once

#include "semantic/model/graph.h"
#include "lowering/api.h"
#include "lowering/ir/model.h"

namespace cppgm
{
namespace lowering
{

void LowerGraph(
	const semantic::SemanticGraphView& graph,
	lowering::ir::Program& program, lowering::Stats* stats,
	std::size_t source_ordinal);

}
}
