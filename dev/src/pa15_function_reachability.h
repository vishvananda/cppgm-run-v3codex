#pragma once

#include "pa15_lowir_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa15_function_reachability
{

struct Summary
{
	std::size_t reachable_functions;
	std::size_t unreachable_weak_functions;
	std::size_t pruned_functions;
	std::size_t retained_external_strong;
	std::size_t retained_address_or_relocation;
	std::size_t retained_direct_call;
	std::size_t retained_lifecycle;
	std::size_t retained_eh_or_runtime;
	std::size_t retained_required_weak;
	std::size_t retained_conservative_fallback;
	std::vector<std::string> retained_conservative_fallback_names;

	Summary() : reachable_functions(0), unreachable_weak_functions(0),
		pruned_functions(0), retained_external_strong(0),
		retained_address_or_relocation(0), retained_direct_call(0),
		retained_lifecycle(0), retained_eh_or_runtime(0),
		retained_required_weak(0), retained_conservative_fallback(0) {}
};

Summary Analyze(const pa15_lowir_detail::TypedProgram& program);
Summary PruneUnreachableWeakFunctions(
	pa15_lowir_detail::TypedProgram* program);

}
}
