#include "lowering/core/source_types.h"
#include "lowering/support/errors.h"
#include "lowir/model/program.h"

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;

std::string NormalizeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type)
{
	std::string numeric = spelling;
	if (numeric.empty() ||
		!((numeric[0] >= '0' && numeric[0] <= '9') || numeric[0] == '.'))
		return numeric;
	static const char* const suffixes[] = {
		"F128", "f128", "F32x", "f32x", "F64x", "f64x",
		"F16", "f16", "F32", "f32", "F64", "f64", "Q", "q"
	};
	for (std::size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		const std::size_t count = std::char_traits<char>::length(suffixes[i]);
		if (numeric.size() < count ||
			numeric.compare(numeric.size() - count, count, suffixes[i]) != 0)
			continue;
		numeric.erase(numeric.size() - count);
		if (type.kind == lowering::ir::LOW_F32) numeric += "f";
		else if (type.kind == lowering::ir::LOW_F80) numeric += "L";
		break;
	}
	return numeric;
}

bool DecodeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type, std::uint64_t* low,
	std::uint64_t* high)
{
	lowir_model::LowType decoded_type;
	switch (type.kind)
	{
	case lowering::ir::LOW_F32:
		decoded_type = lowir_model::builtin_lowir_type(lowir_model::LTK_F32);
		break;
	case lowering::ir::LOW_F64:
		decoded_type = lowir_model::builtin_lowir_type(lowir_model::LTK_F64);
		break;
	case lowering::ir::LOW_F80:
		decoded_type = lowir_model::builtin_lowir_type(lowir_model::LTK_F80);
		break;
	default:
		return false;
	}
	return lowir_model::parse_lowir_floating_literal_bits(
		spelling, decoded_type, low, high);
}

bool IsNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target)
{
	target = program.types.RemoveTopCv(target);
	const semantic::TypeRecord& target_record = program.types.Get(target);
	if (target_record.kind != semantic::TYPE_POINTER) return false;
	return source.integer_literal_zero;
}

bool IsIntNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target)
{
	const semantic::TypeRecord source_type = program.types.Get(
		program.types.RemoveTopCv(source.type));
	return source_type.kind == semantic::TYPE_FUNDAMENTAL &&
		source_type.fundamental == semantic::FUND_INT &&
		IsNullPointerLiteralCast(program, source, target);
}

std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed)
{
	if (width >= 64) return value;
	const std::uint64_t mask = (std::uint64_t(1) << width) - 1;
	std::uint64_t narrowed = static_cast<std::uint64_t>(value) & mask;
	if (is_signed &&
		(narrowed & (std::uint64_t(1) << (width - 1))) != 0)
		narrowed |= ~mask;
	return static_cast<std::int64_t>(narrowed);
}

SourceTypeLowering::SourceTypeLowering(const semantic::Program& program)
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
	{
		if (program_.SizeOf(type) == program_.SizeOf(record->child))
			return Lower(record->child);
		return LowObject(program_.SizeOf(type), program_.AlignOf(type));
	}
	if (record->kind == TYPE_COMPLEX)
		return LowObject(program_.SizeOf(type), program_.AlignOf(type));
	if (record->kind == TYPE_BITINT)
	{
		if (record->dependent_bound_parameter != kNoTemplateParameter)
			ThrowLoweringInternal("cannot lower dependent _BitInt type");
		const std::uint64_t width = record->bound;
		if (width <= 8) return record->bitint_unsigned ? LowU8() : LowI8();
		if (width <= 16) return record->bitint_unsigned ? LowU16() : LowI16();
		if (width <= 32) return record->bitint_unsigned ? LowU32() : LowI32();
		if (width <= 64) return record->bitint_unsigned ? LowU64() : LowI64();
		if (width <= 128) return record->bitint_unsigned ? LowU128() : LowI128();
		ThrowLoweringResourceLimit("unsupported _BitInt lowering width");
	}
	if (record->kind == TYPE_LVALUE_REFERENCE ||
		record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_POINTER ||
		record->kind == TYPE_BLOCK_POINTER ||
		record->kind == TYPE_ARRAY || record->kind == TYPE_FUNCTION) return LowPtr();
	if (record->kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_.entities[record->entity];
		if (IsEnumNamedFlavor(entity.flavor) &&
			entity.underlying != kNoType) return Lower(entity.underlying);
		return LowObject(program_.SizeOf(type), program_.AlignOf(type));
	}
	if (record->kind != TYPE_FUNDAMENTAL)
		ThrowLoweringInternal("invalid PA15 scalar type");
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
	case FUND_FLOAT64X: case FUND_STDFLOAT128: case FUND_FLOAT128:
		return LowF80();
	case FUND_VOID: return LowVoid();
	case FUND_NULLPTR_T: return LowI64();
	}
	ThrowLoweringInternal("unsupported PA15 fundamental type");
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
	return IsClassNamedFlavor(program_.entities[record.entity].flavor);
}

bool SourceTypeLowering::IsComplexObject(TypeId type) const
{
	return program_.types.Get(ExpressionObject(type)).kind == TYPE_COMPLEX;
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
		return record.IsIncompleteArray() ? LowPtr() :
			LowObject(program_.SizeOf(object), program_.AlignOf(object));
	return Lower(type);
}

TypeId SourceTypeLowering::ArrayElement(TypeId type) const
{
	const TypeRecord& record = program_.types.Get(ExpressionObject(type));
	if (record.kind != TYPE_ARRAY)
		ThrowLoweringInternal("PA15 expected array type");
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
		ThrowLoweringInternal("PA15 expected pointer-like type");
	return record.child;
}

}
}
