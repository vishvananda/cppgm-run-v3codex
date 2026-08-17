#pragma once

#include "lowir_model.h"

#include <cstddef>

namespace lowir_model
{

struct FunctionPruningSummary
{
	std::size_t reachable_functions = 0;
	std::size_t pruned_functions = 0;
};

FunctionPruningSummary prune_unreachable_weak_functions(Program& program);

}  // namespace lowir_model
