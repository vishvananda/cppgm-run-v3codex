#include "pa11_model.h"

#include <stdexcept>

namespace cppgm
{
namespace pa11
{

TypeId TypeTable::TryBitInt(bool is_unsigned, std::uint64_t width)
{
	if (width > 128 || width < (is_unsigned ? 1U : 2U)) return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_BITINT;
	candidate.bound = width;
	candidate.bitint_unsigned = is_unsigned;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::BitInt(bool is_unsigned, std::uint64_t width)
{
	const TypeId result = TryBitInt(is_unsigned, width);
	if (result == kNoType)
		throw std::runtime_error("unsupported _BitInt width");
	return result;
}

TypeId TypeTable::TryDependentBitInt(bool is_unsigned, TypeId width_type,
	std::uint32_t parameter)
{
	if (parameter == kNoTemplateParameter) return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_BITINT;
	candidate.dependent_bound_type = width_type;
	candidate.dependent_bound_parameter = parameter;
	candidate.bitint_unsigned = is_unsigned;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::DependentBitInt(bool is_unsigned, TypeId width_type,
	std::uint32_t parameter)
{
	const TypeId result = TryDependentBitInt(
		is_unsigned, width_type, parameter);
	if (result == kNoType)
		throw std::runtime_error("invalid dependent _BitInt width");
	return result;
}

TypeId TypeTable::TryComplex(TypeId element)
{
	if (element == kNoType) return kNoType;
	const TypeRecord& source = Get(element);
	if (source.kind != TYPE_FUNDAMENTAL || source.fundamental == FUND_VOID ||
		source.fundamental == FUND_NULLPTR_T || source.fundamental == FUND_BOOL)
		return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_COMPLEX;
	candidate.child = element;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Complex(TypeId element)
{
	const TypeId result = TryComplex(element);
	if (result == kNoType)
		throw std::runtime_error("invalid _Complex element type");
	return result;
}

}
}
