#include "pa15_source_type_lowering.h"

#include <stdexcept>

namespace cppgm
{
namespace pa15_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;

SourceTypeLowering::SourceTypeLowering(const Program& program)
	: program_(program)
{
}

LowType SourceTypeLowering::Lower(TypeId type) const
{
	const TypeRecord* record = &program_.types.Get(type);
	while (record->kind == TYPE_QUALIFIED)
	{
		type = record->child;
		record = &program_.types.Get(type);
	}
	if (record->kind == TYPE_MEMBER_POINTER)
		return program_.types.IsFunction(record->child) ? LowI128() : LowI64();
	if (record->kind == TYPE_VECTOR)
		return LowObject(program_.SizeOf(type), program_.AlignOf(type));
	if (record->kind == TYPE_BITINT)
	{
		if (record->dependent_bound_parameter != kNoTemplateParameter)
			throw std::runtime_error("cannot lower dependent _BitInt type");
		const std::uint64_t width = record->bound;
		if (width <= 8) return record->bitint_unsigned ? LowU8() : LowI8();
		if (width <= 16) return record->bitint_unsigned ? LowU16() : LowI16();
		if (width <= 32) return record->bitint_unsigned ? LowU32() : LowI32();
		if (width <= 64) return record->bitint_unsigned ? LowU64() : LowI64();
		if (width <= 128) return record->bitint_unsigned ? LowU128() : LowI128();
		throw std::runtime_error("unsupported _BitInt lowering width");
	}
	if (record->kind == TYPE_LVALUE_REFERENCE ||
		record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_POINTER ||
		record->kind == TYPE_BLOCK_POINTER ||
		record->kind == TYPE_ARRAY || record->kind == TYPE_FUNCTION) return LowPtr();
	if (record->kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_.entities[record->entity];
		if ((entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS) &&
			entity.underlying != kNoType) return Lower(entity.underlying);
		return LowObject(program_.SizeOf(type), program_.AlignOf(type));
	}
	if (record->kind != TYPE_FUNDAMENTAL)
		throw std::runtime_error("invalid PA15 scalar type");
	switch (record->fundamental)
	{
	case FUND_BOOL: return LowU8();
	case FUND_CHAR: case FUND_SIGNED_CHAR: return LowI8();
	case FUND_UNSIGNED_CHAR: return LowU8();
	case FUND_SHORT_INT: return LowI16();
	case FUND_UNSIGNED_SHORT_INT: return LowU16();
	case FUND_INT: return LowI32();
	case FUND_UNSIGNED_INT: return LowU32();
	case FUND_WCHAR_T: return LowI32();
	case FUND_CHAR16_T: return LowU16();
	case FUND_CHAR32_T: return LowU32();
	case FUND_LONG_INT: case FUND_LONG_LONG_INT: return LowI64();
	case FUND_UNSIGNED_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
		return LowU64();
	case FUND_INT128: return LowI128();
	case FUND_UINT128: return LowU128();
	case FUND_FLOAT: return LowF32();
	case FUND_FLOAT16: case FUND_FLOAT32: return LowF32();
	case FUND_DOUBLE: return LowF64();
	case FUND_FLOAT32X: case FUND_FLOAT64: return LowF64();
	case FUND_LONG_DOUBLE: return LowF80();
	case FUND_FLOAT64X: case FUND_FLOAT128: return LowF80();
	case FUND_VOID: return LowVoid();
	case FUND_NULLPTR_T: return LowI64();
	}
	throw std::runtime_error("unsupported PA15 fundamental type");
}

bool SourceTypeLowering::IsReference(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(type);
	return record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE;
}

TypeId SourceTypeLowering::RemoveReference(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(type);
	return IsReference(type) ? record.child : type;
}

TypeId SourceTypeLowering::RemoveTopQualifiers(TypeId type) const
{
	while (program_.types.Get(type).kind == TYPE_QUALIFIED)
		type = program_.types.Get(type).child;
	return type;
}

TypeId SourceTypeLowering::ExpressionObject(TypeId type) const
{
	return RemoveTopQualifiers(RemoveReference(type));
}

bool SourceTypeLowering::IsArray(TypeId type) const
{
	return program_.types.Get(ExpressionObject(type)).kind == TYPE_ARRAY;
}

bool SourceTypeLowering::IsFunction(TypeId type) const
{
	return program_.types.Get(ExpressionObject(type)).kind == TYPE_FUNCTION;
}

bool SourceTypeLowering::IsClassObject(TypeId type) const
{
	type = ExpressionObject(type);
	const TypeRecord& record = program_.types.Get(type);
	if (record.kind != TYPE_NAMED) return false;
	const NamedFlavor flavor = program_.entities[record.entity].flavor;
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION;
}

LowType SourceTypeLowering::LowerExpression(TypeId type) const
{
	return Lower(RemoveReference(type));
}

LowType SourceTypeLowering::LowerStorage(TypeId type) const
{
	const TypeId object = RemoveTopQualifiers(type);
	const TypeRecord& record = program_.types.Get(object);
	if (record.kind == TYPE_ARRAY)
		return record.bound == 0 ? LowPtr() :
			LowObject(program_.SizeOf(object), program_.AlignOf(object));
	return Lower(type);
}

TypeId SourceTypeLowering::ArrayElement(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(ExpressionObject(type));
	if (record.kind != TYPE_ARRAY)
		throw std::logic_error("PA15 expected array type");
	return record.child;
}

bool SourceTypeLowering::IsPointerLike(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(ExpressionObject(type));
	return record.kind == TYPE_POINTER || record.kind == TYPE_ARRAY;
}

bool SourceTypeLowering::IsNullptr(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(ExpressionObject(type));
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_NULLPTR_T;
}

TypeId SourceTypeLowering::Pointee(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(ExpressionObject(type));
	if (record.kind != TYPE_POINTER && record.kind != TYPE_ARRAY)
		throw std::logic_error("PA15 expected pointer-like type");
	return record.child;
}

}
}
