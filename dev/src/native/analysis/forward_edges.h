#pragma once

#include "native/analysis/function.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace lowir_native
{
namespace analysis
{

void mark_exact_forward_edge_values(FunctionFacts& facts,
	const lowir_model::LowirFunction& function,
	const std::vector<std::size_t>& definition_blocks,
	const std::vector<std::pair<lowir_model::ValueId, std::size_t> >& block_uses,
	Stats* stats);

}
}
