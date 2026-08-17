#include "pa12_semantic_detail.h"

#include <algorithm>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::RecordFunctionDemand(BindingId binding,
	FunctionDemandReason reason)
{
	if (!stats_ || binding == kNoBinding) return;
	if (reason < FUNCTION_DEMAND_EVALUATED_USE ||
		reason >= FUNCTION_DEMAND_REASON_COUNT)
		throw std::logic_error("invalid function demand reason");
	binding = program_->bindings[binding].canonical;
	BindingId caller = current_function_context_;
	if (caller != kNoBinding)
		caller = program_->bindings[caller].canonical;
	++demand_reason_requests_[reason];
	function_demand_edges_.push_back(
		FunctionDemandEdge(caller, binding, reason));
}

void SemanticAnalyzer::PublishFunctionDemandStats()
{
	if (!stats_) return;
	std::sort(function_demand_edges_.begin(), function_demand_edges_.end());
	function_demand_edges_.erase(std::unique(function_demand_edges_.begin(),
		function_demand_edges_.end()), function_demand_edges_.end());
	stats_->demand_unique_edges = function_demand_edges_.size();
	for (std::size_t i = 0; i < function_demand_edges_.size(); ++i)
	{
		if (function_demand_edges_[i].caller == kNoBinding)
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
