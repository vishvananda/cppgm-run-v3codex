#ifndef CPPGM_PA34_WIDE_CONSTANT_SEMANTIC_H
#define CPPGM_PA34_WIDE_CONSTANT_SEMANTIC_H

#include "pa12_semantic_model.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

ConstexprScalarValue NormalizeWideConstant(
	const ConstexprScalarValue& value, std::size_t width, bool is_unsigned);
ConstexprScalarValue ApplyWideConstantBinary(const std::string& operation,
	const ConstexprScalarValue& left, const ConstexprScalarValue& right,
	std::size_t width, bool is_unsigned);
ConstexprScalarValue NegateWideConstant(
	const ConstexprScalarValue& value, std::size_t width, bool is_unsigned);
ConstexprScalarValue ComplementWideConstant(
	const ConstexprScalarValue& value, std::size_t width, bool is_unsigned);

}
}

#endif
