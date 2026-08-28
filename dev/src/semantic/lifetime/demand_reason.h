#pragma once

#include <cstdint>

namespace cppgm
{
namespace semantic
{

enum FunctionDemandReason
{
	FUNCTION_DEMAND_EVALUATED_USE,
	FUNCTION_DEMAND_RETAINED_CALL,
	FUNCTION_DEMAND_ADDRESS,
	FUNCTION_DEMAND_LIFECYCLE,
	FUNCTION_DEMAND_VTABLE,
	FUNCTION_DEMAND_STATIC_LIFECYCLE,
	FUNCTION_DEMAND_EXCEPTION_CLEANUP,
	FUNCTION_DEMAND_EXPLICIT_INSTANTIATION,
	FUNCTION_DEMAND_ABI_SUPPORT,
	FUNCTION_DEMAND_REASON_COUNT
};

inline std::uint16_t FunctionDemandReasonMask(FunctionDemandReason reason)
{
	return static_cast<std::uint16_t>(1U << static_cast<unsigned>(reason));
}

}
}
