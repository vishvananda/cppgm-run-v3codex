#include "semantic/analysis/analyzer.h"
#include "semantic/constants/wide_integer.h"
#include "support/exceptions.h"

#include <cstdint>

namespace cppgm
{
namespace semantic
{

namespace
{

struct WideBits
{
	std::uint64_t low, high;
	WideBits() : low(0), high(0) {}
	WideBits(std::uint64_t low_value, std::uint64_t high_value)
		: low(low_value), high(high_value) {}
};

WideBits Bits(const ConstexprScalarValue& value)
{
	return WideBits(static_cast<std::uint64_t>(value.integral),
		value.integral_high);
}

ConstexprScalarValue Scalar(const WideBits& value)
{
	return ConstexprScalarValue::IntegralBits(value.low, value.high);
}

WideBits Add(const WideBits& left, const WideBits& right)
{
	const std::uint64_t low = left.low + right.low;
	return WideBits(low, left.high + right.high + (low < left.low));
}

WideBits Subtract(const WideBits& left, const WideBits& right)
{
	return WideBits(left.low - right.low,
		left.high - right.high - (left.low < right.low));
}

WideBits Negate(const WideBits& value)
{
	return Add(WideBits(~value.low, ~value.high), WideBits(1, 0));
}

int CompareUnsigned(const WideBits& left, const WideBits& right)
{
	if (left.high != right.high) return left.high < right.high ? -1 : 1;
	if (left.low != right.low) return left.low < right.low ? -1 : 1;
	return 0;
}

bool SignBit(const WideBits& value, std::size_t width)
{
	return width <= 64 ?
		(value.low & (std::uint64_t(1) << (width - 1))) != 0 :
		(value.high & (std::uint64_t(1) << (width - 65))) != 0;
}

WideBits NormalizeBits(WideBits value, std::size_t width, bool is_unsigned)
{
	if (width == 0 || width > 128)
		ThrowInternalCompilerError("unsupported wide constant width");
	if (width < 64)
	{
		const std::uint64_t mask = (std::uint64_t(1) << width) - 1;
		value.low &= mask;
		value.high = 0;
		if (!is_unsigned && SignBit(value, width))
		{
			value.low |= ~mask;
			value.high = ~std::uint64_t(0);
		}
	}
	else if (width == 64)
		value.high = !is_unsigned && SignBit(value, width) ?
			~std::uint64_t(0) : 0;
	else if (width < 128)
	{
		const std::size_t high_width = width - 64;
		const std::uint64_t mask =
			(std::uint64_t(1) << high_width) - 1;
		value.high &= mask;
		if (!is_unsigned && SignBit(value, width)) value.high |= ~mask;
	}
	return value;
}

WideBits WidthMaximum(std::size_t width)
{
	if (width == 128) return WideBits(~std::uint64_t(0), ~std::uint64_t(0));
	if (width > 64)
		return WideBits(~std::uint64_t(0),
			(std::uint64_t(1) << (width - 64)) - 1);
	if (width == 64) return WideBits(~std::uint64_t(0), 0);
	return WideBits((std::uint64_t(1) << width) - 1, 0);
}

WideBits ShiftLeft(const WideBits& value, std::size_t count)
{
	if (count == 0) return value;
	if (count < 64)
		return WideBits(value.low << count,
			(value.high << count) | (value.low >> (64 - count)));
	if (count < 128) return WideBits(0, value.low << (count - 64));
	return WideBits();
}

WideBits ShiftRight(const WideBits& value, std::size_t count)
{
	if (count == 0) return value;
	if (count < 64)
		return WideBits((value.low >> count) | (value.high << (64 - count)),
			value.high >> count);
	if (count < 128) return WideBits(value.high >> (count - 64), 0);
	return WideBits();
}

bool Bit(const WideBits& value, std::size_t index)
{
	return index < 64 ? ((value.low >> index) & 1) != 0 :
		((value.high >> (index - 64)) & 1) != 0;
}

void SetBit(WideBits* value, std::size_t index)
{
	if (index < 64) value->low |= std::uint64_t(1) << index;
	else value->high |= std::uint64_t(1) << (index - 64);
}

void DivideUnsigned(const WideBits& dividend, const WideBits& divisor,
	WideBits* quotient, WideBits* remainder)
{
	if (divisor.low == 0 && divisor.high == 0)
		ThrowSemanticError("division by zero");
	*quotient = WideBits();
	*remainder = WideBits();
	for (std::size_t bit = 128; bit-- != 0;)
	{
		*remainder = ShiftLeft(*remainder, 1);
		if (Bit(dividend, bit)) remainder->low |= 1;
		if (CompareUnsigned(*remainder, divisor) >= 0)
		{
			*remainder = Subtract(*remainder, divisor);
			SetBit(quotient, bit);
		}
	}
}

struct Product256
{
	std::uint32_t word[8];
	Product256()
	{
		for (std::size_t i = 0; i < 8; ++i) word[i] = 0;
	}
};

Product256 MultiplyFull(const WideBits& left, const WideBits& right)
{
	std::uint32_t a[4] = {
		static_cast<std::uint32_t>(left.low),
		static_cast<std::uint32_t>(left.low >> 32),
		static_cast<std::uint32_t>(left.high),
		static_cast<std::uint32_t>(left.high >> 32)
	};
	std::uint32_t b[4] = {
		static_cast<std::uint32_t>(right.low),
		static_cast<std::uint32_t>(right.low >> 32),
		static_cast<std::uint32_t>(right.high),
		static_cast<std::uint32_t>(right.high >> 32)
	};
	Product256 result;
	for (std::size_t i = 0; i < 4; ++i)
	{
		std::uint64_t carry = 0;
		for (std::size_t j = 0; j < 4; ++j)
		{
			const std::size_t target = i + j;
			const std::uint64_t sum =
				static_cast<std::uint64_t>(result.word[target]) +
				static_cast<std::uint64_t>(a[i]) * b[j] + carry;
			result.word[target] = static_cast<std::uint32_t>(sum);
			carry = sum >> 32;
		}
		result.word[i + 4] = static_cast<std::uint32_t>(carry);
	}
	return result;
}

WideBits ProductLow(const Product256& value)
{
	return WideBits(
		static_cast<std::uint64_t>(value.word[0]) |
			(static_cast<std::uint64_t>(value.word[1]) << 32),
		static_cast<std::uint64_t>(value.word[2]) |
			(static_cast<std::uint64_t>(value.word[3]) << 32));
}

bool ProductExceeds(const Product256& value, const WideBits& limit)
{
	for (std::size_t i = 8; i-- > 4;)
		if (value.word[i] != 0) return true;
	return CompareUnsigned(ProductLow(value), limit) > 0;
}

int CompareSigned(const WideBits& left, const WideBits& right,
	std::size_t width)
{
	const bool left_negative = SignBit(left, width);
	const bool right_negative = SignBit(right, width);
	if (left_negative != right_negative) return left_negative ? -1 : 1;
	return CompareUnsigned(left, right);
}

}

ConstexprScalarValue NormalizeWideConstant(
	const ConstexprScalarValue& value, std::size_t width, bool is_unsigned)
{
	if (value.kind != CONSTEXPR_SCALAR_INTEGRAL)
		ThrowInternalCompilerError(
			"normalizing a non-integral wide constant");
	return Scalar(NormalizeBits(Bits(value), width, is_unsigned));
}

ConstexprScalarValue NegateWideConstant(
	const ConstexprScalarValue& value, std::size_t width, bool is_unsigned)
{
	const WideBits normalized = NormalizeBits(Bits(value), width, is_unsigned);
	if (!is_unsigned && SignBit(normalized, width))
	{
		const WideBits minimum = NormalizeBits(
			width <= 64 ? WideBits(std::uint64_t(1) << (width - 1), 0) :
			WideBits(0, std::uint64_t(1) << (width - 65)),
			width, false);
		if (CompareUnsigned(normalized, minimum) == 0)
			ThrowSemanticError("signed constant unary negation overflow");
	}
	return Scalar(NormalizeBits(Negate(normalized), width, is_unsigned));
}

ConstexprScalarValue ComplementWideConstant(
	const ConstexprScalarValue& value, std::size_t width, bool is_unsigned)
{
	const WideBits bits = Bits(value);
	return Scalar(NormalizeBits(WideBits(~bits.low, ~bits.high),
		width, is_unsigned));
}

ConstexprScalarValue ApplyWideConstantBinary(const std::string& operation,
	const ConstexprScalarValue& left_value,
	const ConstexprScalarValue& right_value,
	std::size_t width, bool is_unsigned)
{
	const WideBits left = NormalizeBits(Bits(left_value), width, is_unsigned);
	const WideBits right = NormalizeBits(Bits(right_value), width, is_unsigned);
	const bool left_negative = !is_unsigned && SignBit(left, width);
	const bool right_negative = !is_unsigned && SignBit(right, width);
	const int op = ClassifyOperationSpelling(operation);
	if (op == OP_EQ || op == OP_NE || op == OP_LT ||
		op == OP_GT || op == OP_LE || op == OP_GE)
	{
		const int compared = is_unsigned ? CompareUnsigned(left, right) :
			CompareSigned(left, right, width);
		const bool result = op == OP_EQ ? compared == 0 :
			op == OP_NE ? compared != 0 : op == OP_LT ? compared < 0 :
			op == OP_GT ? compared > 0 : op == OP_LE ? compared <= 0 :
			compared >= 0;
		return ConstexprScalarValue(static_cast<std::int64_t>(result));
	}
	if (op == OP_AMP) return Scalar(NormalizeBits(
		WideBits(left.low & right.low, left.high & right.high), width, is_unsigned));
	if (op == OP_BOR) return Scalar(NormalizeBits(
		WideBits(left.low | right.low, left.high | right.high), width, is_unsigned));
	if (op == OP_XOR) return Scalar(NormalizeBits(
		WideBits(left.low ^ right.low, left.high ^ right.high), width, is_unsigned));
	if (op == OP_LSHIFT || op == OP_RSHIFT)
	{
		if (right.high != 0 || right_negative || right.low >= width)
			ThrowSemanticError("invalid constant shift count");
		const std::size_t count = static_cast<std::size_t>(right.low);
		if (op == OP_LSHIFT)
		{
			if (left_negative)
				ThrowSemanticError("invalid negative constant left shift");
			if (!is_unsigned &&
				CompareUnsigned(left, ShiftRight(WidthMaximum(width), count)) > 0)
				ThrowSemanticError("constant left shift overflow");
			return Scalar(NormalizeBits(
				ShiftLeft(left, count), width, is_unsigned));
		}
		WideBits shifted = ShiftRight(left, count);
		if (left_negative && count != 0)
		{
			const WideBits fill = ShiftLeft(WidthMaximum(width), width - count);
			shifted.low |= fill.low;
			shifted.high |= fill.high;
		}
		return Scalar(NormalizeBits(shifted, width, is_unsigned));
	}
	WideBits result;
	if (op == OP_PLUS || op == OP_MINUS)
	{
		result = op == OP_PLUS ? Add(left, right) : Subtract(left, right);
		result = NormalizeBits(result, width, is_unsigned);
		if (!is_unsigned)
		{
			const bool result_negative = SignBit(result, width);
			const bool overflow = op == OP_PLUS ?
				left_negative == right_negative && result_negative != left_negative :
				left_negative != right_negative && result_negative != left_negative;
			if (overflow)
				ThrowSemanticError("signed constant arithmetic overflow");
		}
		return Scalar(result);
	}
	if (op == OP_STAR)
	{
		WideBits left_magnitude = left_negative ? Negate(left) : left;
		WideBits right_magnitude = right_negative ? Negate(right) : right;
		const Product256 product = MultiplyFull(left_magnitude, right_magnitude);
		if (!is_unsigned)
		{
			const bool negative = left_negative != right_negative;
			WideBits limit = width <= 64 ?
				WideBits(std::uint64_t(1) << (width - 1), 0) :
				WideBits(0, std::uint64_t(1) << (width - 65));
			if (!negative) limit = Subtract(limit, WideBits(1, 0));
			if (ProductExceeds(product, limit))
				ThrowSemanticError("signed constant arithmetic overflow");
			result = ProductLow(product);
			if (negative) result = Negate(result);
		}
		else result = ProductLow(product);
		return Scalar(NormalizeBits(result, width, is_unsigned));
	}
	if (op == OP_DIV || op == OP_MOD)
	{
		WideBits left_magnitude = left_negative ? Negate(left) : left;
		WideBits right_magnitude = right_negative ? Negate(right) : right;
		WideBits quotient, remainder;
		DivideUnsigned(left_magnitude, right_magnitude, &quotient, &remainder);
		if (!is_unsigned && left_negative && right_magnitude.low == 1 &&
			right_magnitude.high == 0 && right_negative &&
			SignBit(quotient, width))
			ThrowSemanticError("signed division overflow");
		result = op == OP_DIV ? quotient : remainder;
		if (op == OP_DIV ? left_negative != right_negative : left_negative)
			result = Negate(result);
		return Scalar(NormalizeBits(result, width, is_unsigned));
	}
	ThrowInternalCompilerError("unsupported wide constant binary operator");
}

ConstexprScalarValue Analyzer::ApplyConstantIntegralUnary(
	const std::string& operation, const ConstexprScalarValue& value,
	TypeId type) const
{
	const std::size_t width = IntegralWidth(type);
	const bool is_unsigned = IsUnsignedIntegral(type);
	const int op = ClassifyOperationSpelling(operation);
	if (op == OP_MINUS)
	{
		if (width > 64) return NegateWideConstant(
			value, width, is_unsigned);
		const std::int64_t minimum = width == 64 ? INT64_MIN :
			- static_cast<std::int64_t>(
				std::uint64_t(1) << (width - 1));
		if (!is_unsigned && value.integral == minimum)
			ThrowSemanticError(
				"signed constant unary negation overflow");
		ConstexprScalarValue result = value;
		result.integral = static_cast<std::int64_t>(
			- static_cast<std::uint64_t>(value.integral));
		return NormalizeScalarConstant(type, result);
	}
	if (op == OP_COMPL)
	{
		if (width > 64) return ComplementWideConstant(
			value, width, is_unsigned);
		ConstexprScalarValue result = value;
		result.integral = ~result.integral;
		return NormalizeScalarConstant(type, result);
	}
	return NormalizeScalarConstant(type, value);
}

bool Analyzer::TryLoadConstexprIntegralAddress(
	std::uint32_t address, TypeId target, ConstexprScalarValue* value) const
{
	const ConstexprAddressValue* pointed = ConstexprAddressAt(address);
	if (!pointed || pointed->kind != CONSTEXPR_ADDRESS_BINDING ||
		pointed->identity >= program_->bindings.size() || pointed->offset < 0 ||
		!IsIntegral(target, true)) return false;
	const BindingId binding = static_cast<BindingId>(pointed->identity);
	const BindingRecord& source = program_->bindings[binding];
	const TypeId source_type = EffectiveType(source.type);
	const std::size_t source_size = program_->SizeOf(source_type);
	const std::size_t target_size = program_->SizeOf(target);
	const std::uint64_t offset = static_cast<std::uint64_t>(pointed->offset);
	if (!source.constant || !IsIntegral(source_type, true) ||
		source_size > 16 || target_size > 16 || offset > source_size ||
		target_size > source_size - offset) return false;
	const ConstexprScalarValue source_scalar = BindingScalar(binding);
	std::uint64_t low = 0, high = 0;
	for (std::size_t i = 0; i < target_size; ++i)
	{
		const std::size_t source_byte = static_cast<std::size_t>(offset) + i;
		const std::uint64_t word = source_byte < 8 ?
			static_cast<std::uint64_t>(source_scalar.integral) :
			source_scalar.integral_high;
		const std::uint64_t byte =
			(word >> ((source_byte & 7) * 8)) & 0xff;
		if (i < 8) low |= byte << (i * 8);
		else high |= byte << ((i - 8) * 8);
	}
	*value = NormalizeWideConstant(
		ConstexprScalarValue::IntegralBits(low, high), IntegralWidth(target),
		IsUnsignedIntegral(target));
	return true;
}

void Analyzer::SetFunctionalScalarCast(ExpressionInfo* result,
	const ExpressionInfo& operand, TypeId target) const
{
	if (operand.constant &&
		(IsIntegral(target, true) || IsFloating(target)) &&
		(IsIntegral(operand.type, true) || IsFloating(operand.type)))
		SetExpressionScalar(result, ConvertScalarConstant(
			operand.type, target, ExpressionScalar(operand)));
	else
	{
		result->constant = operand.constant;
		result->value = operand.value;
		result->integral_high = operand.integral_high;
		result->integral_high_valid = operand.integral_high_valid;
	}
}

}
}
