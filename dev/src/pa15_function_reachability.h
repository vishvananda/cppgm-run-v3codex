#pragma once

#include "pa15_lowir_model.h"

#include <cstddef>

namespace cppgm
{
namespace pa15_function_reachability
{

struct Summary
{
	std::size_t reachable_functions;
	std::size_t unreachable_weak_functions;
	std::size_t pruned_functions;

	Summary() : reachable_functions(0), unreachable_weak_functions(0),
		pruned_functions(0) {}
};

Summary Analyze(const pa15_lowir_detail::TypedProgram& program);
Summary PruneUnreachableWeakFunctions(
	pa15_lowir_detail::TypedProgram* program);

}
}
