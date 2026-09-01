#include "semantic/model/program.h"
#include "support/exceptions.h"


namespace cppgm
{
namespace semantic
{

TypeId TypeTable::TryZeroLengthArray(TypeId type)
{
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_FUNCTION ||
		(record.kind == TYPE_FUNDAMENTAL &&
		 record.fundamental == FUND_VOID))
		return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_ARRAY;
	candidate.child = type;
	candidate.zero_length_array = true;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::ZeroLengthArray(TypeId type)
{
	const TypeId result = TryZeroLengthArray(type);
	if (result == kNoType)
		ThrowSemanticError("invalid zero-length array element type");
	return result;
}

bool TypeTable::TryCompositeArrayType(TypeId prior, TypeId declared,
	TypeId* composite) const
{
	const TypeRecord& prior_type = Get(prior);
	const TypeRecord& declared_type = Get(declared);
	if (prior_type.kind != TYPE_ARRAY || declared_type.kind != TYPE_ARRAY ||
		prior_type.dependent_bound_parameter != kNoTemplateParameter ||
		declared_type.dependent_bound_parameter != kNoTemplateParameter ||
		prior_type.child != declared_type.child ||
		(!prior_type.IsIncompleteArray() &&
		 !declared_type.IsIncompleteArray())) return false;
	if (composite)
		*composite = declared_type.IsIncompleteArray() ? prior : declared;
	return true;
}

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
		ThrowSemanticError("unsupported _BitInt width");
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
		ThrowSemanticError("invalid dependent _BitInt width");
	return result;
}

TypeId TypeTable::TryDependentVector(TypeId element, TypeId lane_count_type,
	std::uint32_t parameter)
{
	if (parameter == kNoTemplateParameter)
		ThrowInternalCompilerError("dependent vector has no lane-count parameter");
	element = RemoveTopCv(element);
	const TypeRecord& lane = Get(element);
	if (lane.kind == TYPE_LVALUE_REFERENCE ||
		lane.kind == TYPE_RVALUE_REFERENCE || lane.kind == TYPE_FUNCTION ||
		(lane.kind == TYPE_FUNDAMENTAL &&
		 (lane.fundamental == FUND_VOID ||
		  lane.fundamental == FUND_NULLPTR_T ||
		  lane.fundamental == FUND_LONG_DOUBLE))) return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_VECTOR;
	candidate.child = element;
	candidate.dependent_bound_type = lane_count_type;
	candidate.dependent_bound_parameter = parameter;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::DependentVector(TypeId element, TypeId lane_count_type,
	std::uint32_t parameter)
{
	const TypeId result =
		TryDependentVector(element, lane_count_type, parameter);
	if (result == kNoType)
		ThrowSemanticError("invalid dependent GNU vector element type");
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
		ThrowSemanticError("invalid _Complex element type");
	return result;
}

}
}
