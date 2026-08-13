#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

bool IsExtendedFloatingFundamental(FundamentalKind kind)
{
	return kind == FUND_FLOAT || kind == FUND_DOUBLE ||
		kind == FUND_LONG_DOUBLE || kind == FUND_FLOAT16 ||
		kind == FUND_FLOAT32 || kind == FUND_FLOAT32X ||
		kind == FUND_FLOAT64 || kind == FUND_FLOAT64X ||
		kind == FUND_FLOAT128;
}

int FloatingConversionRank(FundamentalKind kind)
{
	switch (kind)
	{
	case FUND_FLOAT16: return 0;
	case FUND_FLOAT: case FUND_FLOAT32: return 1;
	case FUND_DOUBLE: case FUND_FLOAT32X: case FUND_FLOAT64: return 2;
	case FUND_LONG_DOUBLE: case FUND_FLOAT64X: return 3;
	case FUND_FLOAT128: return 4;
	default: return -1;
	}
}

TypeId SemanticAnalyzer::HostedSpecifierType(
	const std::string& spelling) const
{
	FundamentalKind kind = FUND_VOID;
	if (spelling == "__int128_t") kind = FUND_INT128;
	else if (spelling == "__uint128_t") kind = FUND_UINT128;
	else if (spelling == "_Float16") kind = FUND_FLOAT16;
	else if (spelling == "_Float32") kind = FUND_FLOAT32;
	else if (spelling == "_Float32x") kind = FUND_FLOAT32X;
	else if (spelling == "_Float64") kind = FUND_FLOAT64;
	else if (spelling == "_Float64x") kind = FUND_FLOAT64X;
	else if (spelling == "__float128") kind = FUND_FLOAT128;
	else if (spelling == "__builtin_va_list")
		return program_->types.Array(
			program_->types.Fundamental(FUND_UNSIGNED_LONG_INT), 3);
	else return kNoType;
	return program_->types.Fundamental(kind);
}

}
}
