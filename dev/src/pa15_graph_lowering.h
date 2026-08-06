#pragma once

#include "pa12_semantic_model.h"
#include "pa15_lowering.h"
#include "pa15_lowir_model.h"

namespace cppgm
{
namespace pa15_lowering_detail
{

void LowerSemanticGraph(
	const pa12_semantic_detail::SemanticGraphView& graph,
	pa15_lowir_detail::TypedProgram& program, LowIRLoweringStats* stats,
	std::size_t source_ordinal);

}
}
