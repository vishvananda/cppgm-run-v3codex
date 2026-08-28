#ifndef CPPGM_SEMANTIC_CONSTANTS_WIDE_INTEGER_H
#define CPPGM_SEMANTIC_CONSTANTS_WIDE_INTEGER_H

#include "semantic/model/graph.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace semantic
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
