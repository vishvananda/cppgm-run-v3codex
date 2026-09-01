#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>

namespace cppgm
{
namespace semantic
{

void Analyzer::RecordFunctionDemand(BindingId binding,
	FunctionDemandReason reason)
{
	if (binding == kNoBinding) return;
	if (reason < FUNCTION_DEMAND_EVALUATED_USE ||
		reason >= FUNCTION_DEMAND_REASON_COUNT)
		ThrowInternalCompilerError("invalid function demand reason");
	binding = program_->bindings[binding].canonical;
	program_->bindings[binding].demand_reason_mask |=
		FunctionDemandReasonMask(reason);
	BindingId caller = current_function_context_;
	if (caller != kNoBinding)
		caller = program_->bindings[caller].canonical;
	if (stats_) ++demand_reason_requests_[reason];
	if (caller == kNoBinding && !stats_) return;
	std::uint32_t next = kNoDumpEdge;
	if (caller != kNoBinding)
	{
		if (function_demand_head_by_binding_.size() <= caller)
			function_demand_head_by_binding_.resize(
				static_cast<std::size_t>(caller) + 1, kNoDumpEdge);
		next = function_demand_head_by_binding_[caller];
		if (next == kNoDumpEdge)
			functions_with_demand_edges_.push_back(caller);
		function_demand_head_by_binding_[caller] =
			static_cast<std::uint32_t>(function_demand_edges_.size());
	}
	function_demand_edges_.push_back(
		FunctionDemandEdge(caller, binding, reason, next));
}

void Analyzer::PublishFunctionDemandStats()
{
	if (!stats_) return;
	std::vector<FunctionDemandEdge> unique_edges = function_demand_edges_;
	std::sort(unique_edges.begin(), unique_edges.end());
	unique_edges.erase(std::unique(unique_edges.begin(), unique_edges.end()),
		unique_edges.end());
	stats_->demand_unique_edges = unique_edges.size();
	for (std::size_t i = 0; i < unique_edges.size(); ++i)
	{
		if (unique_edges[i].caller == kNoBinding)
			++stats_->demand_root_edges;
		else ++stats_->demand_dependency_edges;
	}
	for (std::size_t i = 0; i < FUNCTION_DEMAND_REASON_COUNT; ++i)
		stats_->demand_requests += demand_reason_requests_[i];
	stats_->demand_evaluated_use_requests =
		demand_reason_requests_[FUNCTION_DEMAND_EVALUATED_USE];
	stats_->demand_retained_call_requests =
		demand_reason_requests_[FUNCTION_DEMAND_RETAINED_CALL];
	stats_->demand_address_requests =
		demand_reason_requests_[FUNCTION_DEMAND_ADDRESS];
	stats_->demand_lifecycle_requests =
		demand_reason_requests_[FUNCTION_DEMAND_LIFECYCLE];
	stats_->demand_vtable_requests =
		demand_reason_requests_[FUNCTION_DEMAND_VTABLE];
	stats_->demand_static_lifecycle_requests =
		demand_reason_requests_[FUNCTION_DEMAND_STATIC_LIFECYCLE];
	stats_->demand_exception_cleanup_requests =
		demand_reason_requests_[FUNCTION_DEMAND_EXCEPTION_CLEANUP];
	stats_->demand_explicit_instantiation_requests =
		demand_reason_requests_[FUNCTION_DEMAND_EXPLICIT_INSTANTIATION];
	stats_->demand_abi_support_requests =
		demand_reason_requests_[FUNCTION_DEMAND_ABI_SUPPORT];
}

}
}
