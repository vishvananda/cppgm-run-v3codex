#pragma once

#include "lowir/model/program.h"

#include <cstddef>

namespace lowir_model
{

struct FunctionPruningSummary
{
	std::size_t reachable_functions = 0;
	std::size_t unreachable_weak_functions = 0;
	std::size_t unreachable_internal_functions = 0;
	std::size_t pruned_functions = 0;
	std::size_t retained_external_strong = 0;
	std::size_t retained_address_or_relocation = 0;
	std::size_t retained_direct_call = 0;
	std::size_t retained_lifecycle = 0;
	std::size_t retained_object_output_root = 0;
	std::size_t retained_object_output_root_weak = 0;
	std::size_t retained_object_output_root_internal = 0;
};

FunctionPruningSummary prune_unreachable_weak_functions(Program& program);

}  // namespace lowir_model
