#pragma once

#include "lowering/ir/model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace reachability
{

struct Summary
{
	std::size_t reachable_functions;
	std::size_t unreachable_weak_functions;
	std::size_t unreachable_internal_functions;
	std::size_t pruned_functions;
	std::size_t retained_external_strong;
	std::size_t retained_address_or_relocation;
	std::size_t retained_direct_call;
	std::size_t retained_lifecycle;
	std::size_t retained_eh_or_runtime;
	std::size_t retained_required_weak;
	std::size_t retained_object_output_root;
	std::size_t retained_object_output_root_weak;
	std::size_t retained_object_output_root_internal;
	std::size_t retained_conservative_fallback;
	std::vector<std::string> retained_conservative_fallback_names;
	std::vector<std::string> unreachable_internal_names;

	Summary() : reachable_functions(0), unreachable_weak_functions(0),
		unreachable_internal_functions(0),
		pruned_functions(0), retained_external_strong(0),
		retained_address_or_relocation(0), retained_direct_call(0),
		retained_lifecycle(0), retained_eh_or_runtime(0),
		retained_required_weak(0),
		retained_object_output_root(0),
		retained_object_output_root_weak(0),
		retained_object_output_root_internal(0),
		retained_conservative_fallback(0) {}
};

Summary Analyze(lowering::ir::Program* program);
Summary AuditWithoutInternalRoots(
	const lowering::ir::Program& program);
Summary PruneUnreachableWeakFunctions(
	lowering::ir::Program* program);

}  // namespace reachability
}  // namespace lowering
}  // namespace cppgm
