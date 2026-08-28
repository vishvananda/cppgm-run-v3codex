#pragma once

#include "semantic/lifetime/demand_reason.h"
#include "semantic/model/program.h"

namespace cppgm
{
namespace semantic
{

struct FunctionDemandEdge
{
	semantic::BindingId caller, callee;
	FunctionDemandReason reason;
	std::uint32_t next;

	FunctionDemandEdge(semantic::BindingId caller_value,
		semantic::BindingId callee_value,
		FunctionDemandReason reason_value, std::uint32_t next_value)
		: caller(caller_value), callee(callee_value), reason(reason_value),
		  next(next_value) {}
	bool operator<(const FunctionDemandEdge& other) const
	{
		if (caller != other.caller) return caller < other.caller;
		if (callee != other.callee) return callee < other.callee;
		return reason < other.reason;
	}
	bool operator==(const FunctionDemandEdge& other) const
	{
		return caller == other.caller && callee == other.callee &&
			reason == other.reason;
	}
};

}
}
