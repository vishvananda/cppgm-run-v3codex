#pragma once

#include "semantic/model/graph.h"
#include "pa15_lowering.h"
#include "lowering/ir/model.h"

namespace cppgm
{
namespace pa15_lowering_detail
{

void LowerSemanticGraph(
	const semantic::SemanticGraphView& graph,
	pa15_lowir_detail::TypedProgram& program, LowIRLoweringStats* stats,
	std::size_t source_ordinal);

}
}
