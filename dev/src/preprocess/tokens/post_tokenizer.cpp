#include "preprocess/tokens/post_tokenizer.h"
#include "support/exception_types.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "preprocess/tokens/IPPTokenStream.h"

namespace cppgm
{
namespace
{

__attribute__((cold, noinline, noreturn))
void ThrowPostTokenInternalError(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::LEXICAL);
}

struct SimpleEntry
{
	const char* spelling;
	SimpleTokenKind kind;
};

const SimpleEntry kSimpleEntries[] = {
	{"!", OP_LNOT}, {"!=", OP_NE}, {"%", OP_MOD}, {"%=", OP_MODASS},
	{"%>", OP_RBRACE}, {"&", OP_AMP}, {"&&", OP_LAND},
	{"&=", OP_BANDASS}, {"(", OP_LPAREN}, {")", OP_RPAREN},
	{"*", OP_STAR}, {"*=", OP_STARASS}, {"+", OP_PLUS},
	{"++", OP_INC}, {"+=", OP_PLUSASS}, {",", OP_COMMA},
	{"-", OP_MINUS}, {"--", OP_DEC}, {"-=", OP_MINUSASS},
	{"->", OP_ARROW}, {"->*", OP_ARROWSTAR}, {".", OP_DOT},
	{".*", OP_DOTSTAR}, {"...", OP_DOTS}, {"/", OP_DIV},
	{"/=", OP_DIVASS}, {":", OP_COLON}, {"::", OP_COLON2},
	{":>", OP_RSQUARE}, {";", OP_SEMICOLON}, {"<", OP_LT},
	{"<%", OP_LBRACE}, {"<:", OP_LSQUARE}, {"<<", OP_LSHIFT},
	{"<<=", OP_LSHIFTASS}, {"<=", OP_LE}, {"=", OP_ASS},
	{"==", OP_EQ}, {">", OP_GT}, {">=", OP_GE},
	{">>", OP_RSHIFT}, {">>=", OP_RSHIFTASS}, {"?", OP_QMARK},
	{"[", OP_LSQUARE}, {"]", OP_RSQUARE}, {"^", OP_XOR},
	{"^=", OP_XORASS}, {"__alignof", KW_ALIGNOF},
	{"__alignof__", KW_ALIGNOF}, {"__decltype", KW_DECLTYPE},
	{"__typeof", KW_DECLTYPE}, {"__typeof__", KW_DECLTYPE},
	{"alignas", KW_ALIGNAS},
	{"alignof", KW_ALIGNOF}, {"and", OP_LAND},
	{"and_eq", OP_BANDASS}, {"asm", KW_ASM}, {"auto", KW_AUTO},
	{"bitand", OP_AMP}, {"bitor", OP_BOR}, {"bool", KW_BOOL},
	{"break", KW_BREAK}, {"case", KW_CASE}, {"catch", KW_CATCH},
	{"char", KW_CHAR}, {"char16_t", KW_CHAR16_T},
	{"char32_t", KW_CHAR32_T}, {"class", KW_CLASS},
	{"compl", OP_COMPL}, {"const", KW_CONST},
	{"const_cast", KW_CONST_CAST}, {"constexpr", KW_CONSTEXPR},
	{"continue", KW_CONTINUE}, {"decltype", KW_DECLTYPE},
	{"default", KW_DEFAULT}, {"delete", KW_DELETE}, {"do", KW_DO},
	{"double", KW_DOUBLE}, {"dynamic_cast", KW_DYNAMIC_CAST},
	{"else", KW_ELSE}, {"enum", KW_ENUM}, {"explicit", KW_EXPLICIT},
	{"export", KW_EXPORT}, {"extern", KW_EXTERN}, {"false", KW_FALSE},
	{"float", KW_FLOAT}, {"for", KW_FOR}, {"friend", KW_FRIEND},
	{"goto", KW_GOTO}, {"if", KW_IF}, {"inline", KW_INLINE},
	{"int", KW_INT}, {"long", KW_LONG}, {"mutable", KW_MUTABLE},
	{"namespace", KW_NAMESPACE}, {"new", KW_NEW},
	{"noexcept", KW_NOEXCEPT}, {"not", OP_LNOT}, {"not_eq", OP_NE},
	{"nullptr", KW_NULLPTR}, {"operator", KW_OPERATOR},
	{"or", OP_LOR}, {"or_eq", OP_BORASS}, {"private", KW_PRIVATE},
	{"protected", KW_PROTECTED}, {"public", KW_PUBLIC},
	{"register", KW_REGISTER}, {"reinterpret_cast", KW_REINTERPET_CAST},
	{"return", KW_RETURN}, {"short", KW_SHORT}, {"signed", KW_SIGNED},
	{"sizeof", KW_SIZEOF}, {"static", KW_STATIC},
	{"static_assert", KW_STATIC_ASSERT}, {"static_cast", KW_STATIC_CAST},
	{"struct", KW_STRUCT}, {"switch", KW_SWITCH},
	{"template", KW_TEMPLATE}, {"this", KW_THIS},
	{"thread_local", KW_THREAD_LOCAL}, {"throw", KW_THROW},
	{"true", KW_TRUE}, {"try", KW_TRY}, {"typedef", KW_TYPEDEF},
	{"typeid", KW_TYPEID}, {"typename", KW_TYPENAME},
	{"union", KW_UNION}, {"unsigned", KW_UNSIGNED},
	{"using", KW_USING}, {"virtual", KW_VIRTUAL}, {"void", KW_VOID},
	{"volatile", KW_VOLATILE}, {"wchar_t", KW_WCHAR_T},
	{"while", KW_WHILE}, {"xor", OP_XOR}, {"xor_eq", OP_XORASS},
	{"{", OP_LBRACE}, {"|", OP_BOR}, {"|=", OP_BORASS},
	{"||", OP_LOR}, {"}", OP_RBRACE}, {"~", OP_COMPL}
};

struct SimpleEntryIndex
{
	std::size_t first[256];
	std::size_t end[256];

	SimpleEntryIndex()
	{
		const std::size_t entry_count =
			sizeof(kSimpleEntries) / sizeof(kSimpleEntries[0]);
		std::fill(first, first + 256, entry_count);
		std::fill(end, end + 256, entry_count);
		for (std::size_t i = 0; i < entry_count; ++i)
		{
			const unsigned char initial = static_cast<unsigned char>(
				kSimpleEntries[i].spelling[0]);
			if (first[initial] == entry_count)
				first[initial] = i;
			end[initial] = i + 1;
		}
	}
};

const SimpleEntryIndex kSimpleEntryIndex;

bool FindSimple(const std::string& spelling, SimpleTokenKind* kind)
{
	if (spelling.empty())
		return false;
	const unsigned char initial =
		static_cast<unsigned char>(spelling[0]);
	std::size_t first = kSimpleEntryIndex.first[initial];
	std::size_t count = kSimpleEntryIndex.end[initial] - first;
	while (count != 0)
	{
		const std::size_t step = count / 2;
		const std::size_t middle = first + step;
		const int comparison = spelling.compare(kSimpleEntries[middle].spelling);
		if (comparison > 0)
		{
			first = middle + 1;
			count -= step + 1;
		}
		else
			count = step;
	}
	if (first == sizeof(kSimpleEntries) / sizeof(kSimpleEntries[0]) ||
		spelling != kSimpleEntries[first].spelling)
		return false;
	*kind = kSimpleEntries[first].kind;
	return true;
}

bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool IsValidUdSuffix(const std::string& suffix)
{
	if (suffix.empty() || suffix[0] != '_')
		return false;
	for (std::size_t i = 1; i < suffix.size(); ++i)
	{
		const unsigned char current =
			static_cast<unsigned char>(suffix[i]);
		if (current < 0x80 && !IsDigit(current) &&
			!(current >= 'a' && current <= 'z') &&
			!(current >= 'A' && current <= 'Z') && current != '_')
			return false;
	}
	return true;
}

int HexDigitValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool DecodeUTF8One(const std::string& text, std::size_t end,
	std::size_t* position, std::uint32_t* value)
{
	if (*position >= end)
		return false;
	const unsigned char first =
		static_cast<unsigned char>(text[(*position)++]);
	if (first <= 0x7F)
	{
		*value = first;
		return true;
	}
	int continuation_count = 0;
	std::uint32_t result = 0;
	std::uint32_t minimum = 0;
	if (first >= 0xC2 && first <= 0xDF)
	{
		continuation_count = 1;
		result = first & 0x1F;
		minimum = 0x80;
	}
	else if (first >= 0xE0 && first <= 0xEF)
	{
		continuation_count = 2;
		result = first & 0x0F;
		minimum = 0x800;
	}
	else if (first >= 0xF0 && first <= 0xF4)
	{
		continuation_count = 3;
		result = first & 0x07;
		minimum = 0x10000;
	}
	else
		return false;
	for (int i = 0; i < continuation_count; ++i)
	{
		if (*position >= end)
			return false;
		const unsigned char next =
			static_cast<unsigned char>(text[(*position)++]);
		if ((next & 0xC0) != 0x80)
			return false;
		result = (result << 6) | (next & 0x3F);
	}
	if (result < minimum || result > 0x10FFFF ||
		(result >= 0xD800 && result <= 0xDFFF))
		return false;
	*value = result;
	return true;
}

void AppendLittleEndian(std::uint64_t value, std::size_t width,
	std::vector<unsigned char>* bytes)
{
	for (std::size_t i = 0; i < width; ++i)
	{
		bytes->push_back(static_cast<unsigned char>(value & 0xFF));
		value >>= 8;
	}
}

template <std::size_t Size>
void StoreLittleEndian(std::uint64_t value, std::size_t width,
	unsigned char (&bytes)[Size])
{
	if (width > Size)
		ThrowPostTokenInternalError("scalar literal exceeds fixed storage");
	for (std::size_t i = 0; i < width; ++i)
	{
		bytes[i] = static_cast<unsigned char>(value & 0xFF);
		value >>= 8;
	}
}

bool ParseUnsignedDigits(const std::string& text, std::size_t begin,
	std::size_t end, int base, std::uint64_t* value)
{
	std::uint64_t result = 0;
	for (std::size_t i = begin; i < end; ++i)
	{
		const int digit = HexDigitValue(text[i]);
		if (digit < 0 || digit >= base)
			return false;
		if (result > (std::numeric_limits<std::uint64_t>::max() - digit) /
			static_cast<unsigned int>(base))
			return false;
		result = result * static_cast<unsigned int>(base) + digit;
	}
	*value = result;
	return true;
}

struct IntegerSpelling
{
	int base;
	std::size_t digits_begin;
	std::size_t digits_end;
	bool is_unsigned;
	int long_rank;
};

bool ParseIntegerSuffix(const std::string& suffix,
	bool* is_unsigned, int* long_rank)
{
	*is_unsigned = false;
	*long_rank = 0;
	std::size_t position = 0;
	for (int component = 0; component < 2 && position < suffix.size();
		++component)
	{
		const char current = suffix[position];
		if ((current == 'u' || current == 'U') && !*is_unsigned)
		{
			*is_unsigned = true;
			++position;
			continue;
		}
		if ((current == 'l' || current == 'L') && *long_rank == 0)
		{
			const char letter = current;
			++position;
			*long_rank = 1;
			if (position < suffix.size() && suffix[position] == letter)
			{
				++position;
				*long_rank = 2;
			}
			continue;
		}
		return false;
	}
	return position == suffix.size();
}

bool ParseIntegerSpelling(const std::string& text, bool allow_suffix,
	IntegerSpelling* result)
{
	if (text.empty() || !IsDigit(text[0]))
		return false;
	std::size_t position = 0;
	result->base = 10;
	if (text.size() >= 2 && text[0] == '0' &&
		(text[1] == 'x' || text[1] == 'X'))
	{
		result->base = 16;
		position = 2;
		result->digits_begin = position;
		while (position < text.size() && HexDigitValue(text[position]) >= 0)
			++position;
		if (position == result->digits_begin)
			return false;
	}
	else if (text.size() >= 2 && text[0] == '0' &&
		(text[1] == 'b' || text[1] == 'B'))
	{
		result->base = 2;
		position = 2;
		result->digits_begin = position;
		while (position < text.size() &&
			(text[position] == '0' || text[position] == '1'))
			++position;
		if (position == result->digits_begin)
			return false;
	}
	else
	{
		result->base = text[0] == '0' ? 8 : 10;
		result->digits_begin = 0;
		while (position < text.size() && IsDigit(text[position]))
			++position;
	}
	result->digits_end = position;
	if (!allow_suffix && position != text.size())
		return false;
	const std::string suffix = text.substr(position);
	if (!ParseIntegerSuffix(suffix, &result->is_unsigned,
		&result->long_rank))
		return false;
	if (!allow_suffix && (result->is_unsigned || result->long_rank != 0))
		return false;
	if (result->base == 8)
	{
		for (std::size_t i = result->digits_begin;
			i < result->digits_end; ++i)
			if (text[i] < '0' || text[i] > '7')
				return false;
	}
	return true;
}

std::uint64_t FundamentalMaximum(FundamentalType type)
{
	switch (type)
	{
	case FT_INT: return 0x7FFFFFFFULL;
	case FT_UNSIGNED_INT: return 0xFFFFFFFFULL;
	case FT_LONG_INT:
	case FT_LONG_LONG_INT: return 0x7FFFFFFFFFFFFFFFULL;
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
		return std::numeric_limits<std::uint64_t>::max();
	default: ThrowPostTokenInternalError("non-integer candidate type");
	}
}

std::size_t FundamentalWidth(FundamentalType type)
{
	switch (type)
	{
	case FT_CHAR:
	case FT_SIGNED_CHAR:
	case FT_UNSIGNED_CHAR: return 1;
	case FT_CHAR16_T:
	case FT_SHORT_INT:
	case FT_UNSIGNED_SHORT_INT:
	case FT_FLOAT16: return 2;
	case FT_INT:
	case FT_UNSIGNED_INT:
	case FT_WCHAR_T:
	case FT_CHAR32_T: case FT_FLOAT: case FT_FLOAT32: return 4;
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
	case FT_DOUBLE: case FT_FLOAT32X: case FT_FLOAT64: return 8;
	case FT_LONG_DOUBLE: case FT_FLOAT64X: case FT_FLOAT128:
		return sizeof(long double);
	default: ThrowPostTokenInternalError("type has no PA2 object width");
	}
}

bool SelectIntegerType(const IntegerSpelling& spelling,
	std::uint64_t value, FundamentalType* type)
{
	FundamentalType candidates[6];
	std::size_t count = 0;
	const bool decimal = spelling.base == 10;
	if (spelling.long_rank == 0 && !spelling.is_unsigned)
	{
		candidates[count++] = FT_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_INT;
		candidates[count++] = FT_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_LONG_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (spelling.long_rank == 0)
	{
		candidates[count++] = FT_UNSIGNED_INT;
		candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (spelling.long_rank == 1 && !spelling.is_unsigned)
	{
		candidates[count++] = FT_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_LONG_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (spelling.long_rank == 1)
	{
		candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (!spelling.is_unsigned)
	{
		candidates[count++] = FT_LONG_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	for (std::size_t i = 0; i < count; ++i)
	{
		if (value <= FundamentalMaximum(candidates[i]))
		{
			*type = candidates[i];
			return true;
		}
	}
	return false;
}

bool ParseFloatingSuffix(const std::string& text, std::size_t position,
	bool allow_suffix, FundamentalType* type)
{
	if (position == text.size()) return true;
	if (!allow_suffix) return false;
	const std::string suffix = text.substr(position);
	if (suffix == "f" || suffix == "F") *type = FT_FLOAT;
	else if (suffix == "l" || suffix == "L") *type = FT_LONG_DOUBLE;
	else if (suffix == "q" || suffix == "Q") *type = FT_LONG_DOUBLE;
	else if (suffix == "f16" || suffix == "F16") *type = FT_FLOAT16;
	else if (suffix == "bf16" || suffix == "BF16") *type = FT_FLOAT16;
	else if (suffix == "f32" || suffix == "F32") *type = FT_FLOAT32;
	else if (suffix == "f32x" || suffix == "F32x") *type = FT_FLOAT32X;
	else if (suffix == "f64" || suffix == "F64") *type = FT_FLOAT64;
	else if (suffix == "f64x" || suffix == "F64x") *type = FT_FLOAT64X;
	else if (suffix == "f128" || suffix == "F128") *type = FT_FLOAT128;
	else return false;
	return true;
}

bool ParseFloatingSpelling(const std::string& text, bool allow_suffix,
	FundamentalType* type, std::size_t* numeric_end)
{
	if (text.size() >= 2 && text[0] == '0' &&
		(text[1] == 'x' || text[1] == 'X'))
	{
		std::size_t position = 2;
		bool had_digits = false;
		while (position < text.size() && HexDigitValue(text[position]) >= 0)
		{
			had_digits = true;
			++position;
		}
		if (position < text.size() && text[position] == '.')
		{
			++position;
			while (position < text.size() &&
				HexDigitValue(text[position]) >= 0)
			{
				had_digits = true;
				++position;
			}
		}
		if (!had_digits || position >= text.size() ||
			(text[position] != 'p' && text[position] != 'P'))
			return false;
		++position;
		if (position < text.size() &&
			(text[position] == '+' || text[position] == '-'))
			++position;
		const std::size_t exponent_begin = position;
		while (position < text.size() && IsDigit(text[position]))
			++position;
		if (position == exponent_begin)
			return false;
		*numeric_end = position;
		*type = FT_DOUBLE;
		return ParseFloatingSuffix(text, position, allow_suffix, type);
	}

	std::size_t position = 0;
	while (position < text.size() && IsDigit(text[position]))
		++position;
	const bool had_leading_digits = position != 0;
	bool had_dot = false;
	if (position < text.size() && text[position] == '.')
	{
		had_dot = true;
		++position;
		const std::size_t fraction_begin = position;
		while (position < text.size() && IsDigit(text[position]))
			++position;
		if (!had_leading_digits && position == fraction_begin)
			return false;
	}
	bool had_exponent = false;
	if (position < text.size() &&
		(text[position] == 'e' || text[position] == 'E'))
	{
		had_exponent = true;
		++position;
		if (position < text.size() &&
			(text[position] == '+' || text[position] == '-'))
			++position;
		const std::size_t exponent_begin = position;
		while (position < text.size() && IsDigit(text[position]))
			++position;
		if (position == exponent_begin)
			return false;
	}
	if ((!had_dot && !had_exponent) ||
		(!had_leading_digits && !had_dot))
		return false;
	*numeric_end = position;
	*type = FT_DOUBLE;
	return ParseFloatingSuffix(text, position, allow_suffix, type);
}

template <typename T>
void StoreFloatingObjectBytes(const T& value, unsigned char* bytes)
{
	std::memcpy(bytes, &value, sizeof(value));
#if defined(__x86_64__) && defined(__BYTE_ORDER__) && \
	__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	// The x86 extended format carries 80 value bits in a 16-byte object.
	// Host conversions do not specify the trailing six padding bytes, but the
	// phase-7 dump is deterministic and therefore must not expose them.
	if (sizeof(T) == 16 && std::numeric_limits<T>::digits == 64 &&
		std::numeric_limits<T>::max_exponent == 16384)
		std::memset(bytes + 10, 0, sizeof(T) - 10);
#endif
}

template <typename T>
bool DecodeFloatingValue(const std::string& spelling,
	unsigned char* bytes, std::size_t* size)
{
	T value;
	std::memset(&value, 0, sizeof(value));
	if (spelling.size() >= 2 && spelling[0] == '0' &&
		(spelling[1] == 'x' || spelling[1] == 'X'))
	{
		char* end = 0;
		const long double parsed = std::strtold(spelling.c_str(), &end);
		if (!end || *end != '\0')
			return false;
		value = static_cast<T>(parsed);
		StoreFloatingObjectBytes(value, bytes);
		*size = sizeof(value);
		return true;
	}
	std::istringstream input(spelling);
	input.imbue(std::locale::classic());
	input >> value;
	if (!input)
	{
		// A target extended-float literal can be finite in its declared type
		// while exceeding the range of the compiler host's long double.  The
		// semantic pipeline retains the source spelling and declared type, so
		// accept a syntactically complete host conversion here even when it
		// rounds to infinity in this compact phase-7 value cache.
		char* end = 0;
		const long double parsed = std::strtold(spelling.c_str(), &end);
		if (!end || end == spelling.c_str() || *end != '\0')
			return false;
		value = static_cast<T>(parsed);
	}
	StoreFloatingObjectBytes(value, bytes);
	*size = sizeof(value);
	return true;
}

bool DecodeSimpleEscape(char escaped, std::uint32_t* value)
{
	switch (escaped)
	{
	case '\'': *value = '\''; return true;
	case '"': *value = '"'; return true;
	case '?': *value = '?'; return true;
	case '\\': *value = '\\'; return true;
	case 'a': *value = 7; return true;
	case 'b': *value = 8; return true;
	case 'f': *value = 12; return true;
	case 'n': *value = 10; return true;
	case 'r': *value = 13; return true;
	case 't': *value = 9; return true;
	case 'v': *value = 11; return true;
	default: return false;
	}
}

bool DecodeEscape(const std::string& text, std::size_t end,
	std::size_t* position, std::uint32_t* value)
{
	if (*position >= end || text[*position] != '\\')
		return false;
	++*position;
	if (*position >= end)
		return false;
	if (DecodeSimpleEscape(text[*position], value))
	{
		++*position;
		return true;
	}
	if (text[*position] >= '0' && text[*position] <= '7')
	{
		std::uint32_t result = 0;
		int digits = 0;
		while (*position < end && digits < 3 &&
			text[*position] >= '0' && text[*position] <= '7')
		{
			result = result * 8 + (text[*position] - '0');
			++*position;
			++digits;
		}
		*value = result;
		return true;
	}
	if (text[*position] != 'x')
		return false;
	++*position;
	if (*position >= end || HexDigitValue(text[*position]) < 0)
		return false;
	std::uint64_t result = 0;
	while (*position < end && HexDigitValue(text[*position]) >= 0)
	{
		const unsigned int digit = HexDigitValue(text[*position]);
		if (result > (std::numeric_limits<std::uint32_t>::max() - digit) / 16)
			return false;
		result = result * 16 + digit;
		++*position;
	}
	*value = static_cast<std::uint32_t>(result);
	return true;
}

struct QuotedSyntax
{
	std::size_t content_begin;
	std::size_t content_end;
	std::string suffix;
	FundamentalType type;
};

bool ParseCharacterSyntax(const std::string& spelling, QuotedSyntax* syntax)
{
	std::size_t quote = 0;
	syntax->type = FT_CHAR;
	if (spelling.size() >= 2 && spelling[0] == 'u' && spelling[1] == '\'')
	{
		quote = 1;
		syntax->type = FT_CHAR16_T;
	}
	else if (spelling.size() >= 2 && spelling[0] == 'U' &&
		spelling[1] == '\'')
	{
		quote = 1;
		syntax->type = FT_CHAR32_T;
	}
	else if (spelling.size() >= 2 && spelling[0] == 'L' &&
		spelling[1] == '\'')
	{
		quote = 1;
		syntax->type = FT_WCHAR_T;
	}
	else if (spelling.empty() || spelling[0] != '\'')
		return false;
	syntax->content_begin = quote + 1;
	std::size_t position = syntax->content_begin;
	while (position < spelling.size())
	{
		if (spelling[position] == '\\')
		{
			position += 2;
			continue;
		}
		if (spelling[position] == '\'')
			break;
		++position;
	}
	if (position >= spelling.size())
		return false;
	syntax->content_end = position;
	syntax->suffix = spelling.substr(position + 1);
	return syntax->suffix.empty() || syntax->suffix[0] == '_';
}

bool DecodeCharacter(const std::string& spelling,
	const QuotedSyntax& syntax, std::uint32_t* value)
{
	std::size_t position = syntax.content_begin;
	int units = 0;
	while (position < syntax.content_end)
	{
		std::uint32_t current = 0;
		if (spelling[position] == '\\')
		{
			if (!DecodeEscape(spelling, syntax.content_end, &position,
				&current))
				return false;
		}
		else if (!DecodeUTF8One(spelling, syntax.content_end,
			&position, &current))
			return false;
		*value = current;
		++units;
	}
	return units == 1 && *value <= 0x10FFFF &&
		!(*value >= 0xD800 && *value <= 0xDFFF);
}

enum StringEncoding
{
	ENCODING_ORDINARY,
	ENCODING_UTF8,
	ENCODING_UTF16,
	ENCODING_UTF32,
	ENCODING_WIDE
};

struct StringPart
{
	StringEncoding encoding;
	bool raw;
	std::size_t content_begin;
	std::size_t content_end;
	std::size_t suffix_begin;
	std::size_t suffix_end;
};

bool ParseStringPart(const std::string& spelling, StringPart* part)
{
	part->encoding = ENCODING_ORDINARY;
	part->raw = false;
	std::size_t position = 0;
	if (spelling.compare(0, 2, "u8") == 0)
	{
		part->encoding = ENCODING_UTF8;
		position = 2;
	}
	else if (!spelling.empty() && spelling[0] == 'u')
	{
		part->encoding = ENCODING_UTF16;
		position = 1;
	}
	else if (!spelling.empty() && spelling[0] == 'U')
	{
		part->encoding = ENCODING_UTF32;
		position = 1;
	}
	else if (!spelling.empty() && spelling[0] == 'L')
	{
		part->encoding = ENCODING_WIDE;
		position = 1;
	}
	if (position < spelling.size() && spelling[position] == 'R')
	{
		part->raw = true;
		if (position + 1 >= spelling.size() || spelling[position + 1] != '"')
			return false;
		const std::size_t delimiter_begin = position + 2;
		const std::size_t open = spelling.find('(', delimiter_begin);
		if (open == std::string::npos)
			return false;
		const std::size_t delimiter_size = open - delimiter_begin;
		if (delimiter_size > 16)
			return false;
		std::size_t close = open + 1;
		for (; close < spelling.size(); ++close)
		{
			if (spelling[close] == ')' &&
				close + delimiter_size + 1 < spelling.size() &&
				spelling.compare(close + 1, delimiter_size, spelling,
					delimiter_begin, delimiter_size) == 0 &&
				spelling[close + delimiter_size + 1] == '"')
				break;
		}
		if (close == spelling.size())
			return false;
		part->content_begin = open + 1;
		part->content_end = close;
		part->suffix_begin = close + delimiter_size + 2;
	}
	else
	{
		if (position >= spelling.size() || spelling[position] != '"')
			return false;
		part->content_begin = ++position;
		while (position < spelling.size())
		{
			if (spelling[position] == '\\')
			{
				position += 2;
				continue;
			}
			if (spelling[position] == '"')
				break;
			++position;
		}
		if (position >= spelling.size())
			return false;
		part->content_end = position;
		part->suffix_begin = position + 1;
	}
	part->suffix_end = spelling.size();
	return part->suffix_begin == part->suffix_end ||
		spelling[part->suffix_begin] == '_';
}

std::size_t EncodingWidth(StringEncoding encoding)
{
	switch (encoding)
	{
	case ENCODING_ORDINARY:
	case ENCODING_UTF8: return 1;
	case ENCODING_UTF16: return 2;
	case ENCODING_UTF32:
	case ENCODING_WIDE: return 4;
	}
	ThrowPostTokenInternalError("unknown string encoding");
}

FundamentalType EncodingType(StringEncoding encoding)
{
	switch (encoding)
	{
	case ENCODING_ORDINARY:
	case ENCODING_UTF8: return FT_CHAR;
	case ENCODING_UTF16: return FT_CHAR16_T;
	case ENCODING_UTF32: return FT_CHAR32_T;
	case ENCODING_WIDE: return FT_WCHAR_T;
	}
	ThrowPostTokenInternalError("unknown string encoding");
}

bool AppendStringUnit(std::uint32_t value, StringEncoding encoding,
	std::vector<unsigned char>* bytes, std::size_t* units)
{
	const std::size_t width = EncodingWidth(encoding);
	const std::uint64_t maximum = width == 1 ? 0xFFULL :
		(width == 2 ? 0xFFFFULL : 0xFFFFFFFFULL);
	if (value > maximum)
		return false;
	AppendLittleEndian(value, width, bytes);
	++*units;
	return true;
}

bool AppendStringCodePoint(std::uint32_t value, StringEncoding encoding,
	std::vector<unsigned char>* bytes, std::size_t* units)
{
	if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
		return false;
	if (encoding == ENCODING_ORDINARY || encoding == ENCODING_UTF8)
	{
		if (value <= 0x7F)
			return AppendStringUnit(value, encoding, bytes, units);
		if (value <= 0x7FF)
		{
			return AppendStringUnit(0xC0 | (value >> 6), encoding,
				bytes, units) &&
				AppendStringUnit(0x80 | (value & 0x3F), encoding,
					bytes, units);
		}
		if (value <= 0xFFFF)
		{
			return AppendStringUnit(0xE0 | (value >> 12), encoding,
				bytes, units) &&
				AppendStringUnit(0x80 | ((value >> 6) & 0x3F), encoding,
					bytes, units) &&
				AppendStringUnit(0x80 | (value & 0x3F), encoding,
					bytes, units);
		}
		return AppendStringUnit(0xF0 | (value >> 18), encoding,
			bytes, units) &&
			AppendStringUnit(0x80 | ((value >> 12) & 0x3F), encoding,
				bytes, units) &&
			AppendStringUnit(0x80 | ((value >> 6) & 0x3F), encoding,
				bytes, units) &&
			AppendStringUnit(0x80 | (value & 0x3F), encoding,
				bytes, units);
	}
	if (encoding == ENCODING_UTF16 && value > 0xFFFF)
	{
		value -= 0x10000;
		return AppendStringUnit(0xD800 | (value >> 10), encoding,
			bytes, units) &&
			AppendStringUnit(0xDC00 | (value & 0x3FF), encoding,
				bytes, units);
	}
	return AppendStringUnit(value, encoding, bytes, units);
}

bool DecodeStringPart(const std::string& text, std::size_t content_begin,
	std::size_t content_end, bool raw, StringEncoding encoding,
	std::vector<unsigned char>* bytes, std::size_t* units)
{
	std::size_t position = content_begin;
	while (position < content_end)
	{
		std::uint32_t value = 0;
		if (!raw && text[position] == '\\')
		{
			if (!DecodeEscape(text, content_end, &position,
				&value))
				return false;
			if (!AppendStringUnit(value, encoding, bytes, units))
				return false;
		}
		else
		{
			if (!DecodeUTF8One(text, content_end, &position, &value) ||
				!AppendStringCodePoint(value, encoding, bytes, units))
				return false;
		}
	}
	return true;
}

// A maximal string-literal sequence needs to defer content encoding until its
// final encoding prefix is known. Retain one joined source spelling and compact
// ranges into it instead of one allocating string object per preprocessing
// token and a second joined copy at flush time.
struct PendingStringPart
{
	bool raw;
	std::size_t content_begin;
	std::size_t content_end;
};

class PostTokenAnalyzer : public IPPTokenStream
{
public:
	PostTokenAnalyzer(IPostTokenStream& output, PostTokenizationStats* stats)
		: output_(output), stats_(stats), pending_encoding_(ENCODING_ORDINARY),
		  pending_valid_(true), pending_string_tokens_(0),
		  pending_string_bytes_(0), current_line_(0), current_column_(0),
		  has_current_location_(false), pending_line_(0), pending_column_(0)
	{}

	void SetSourceLocation(const std::string& file,
		std::size_t line, std::size_t column)
	{
		if (current_file_ != file) current_file_ = file;
		current_line_ = line;
		current_column_ = column;
		has_current_location_ = true;
	}

	void emit_whitespace_sequence() {}
	void emit_new_line() {}

	void emit_header_name(const std::string& data)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		EmitInvalid(data);
	}

	void emit_identifier(const std::string& data)
	{
		emit_identifier_id(data, 0);
	}

	// Classifies each distinct producer spelling once; -1 marks a plain
	// identifier and values >= 0 are the simple-token kind.
	int ClassifyCached(const std::string& data,
		std::uint32_t producer_spelling)
	{
		if (producer_spelling == 0)
		{
			SimpleTokenKind kind;
			return FindSimple(data, &kind) ? static_cast<int>(kind) : -1;
		}
		if (classify_cache_.size() <= producer_spelling)
			classify_cache_.resize(
				static_cast<std::size_t>(producer_spelling) + 1, -2);
		std::int16_t& slot = classify_cache_[producer_spelling];
		if (slot == -2)
		{
			SimpleTokenKind kind;
			slot = FindSimple(data, &kind) ?
				static_cast<std::int16_t>(kind) : std::int16_t(-1);
		}
		return slot;
	}

	void emit_identifier_id(const std::string& data,
		std::uint32_t producer_spelling)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		const int kind = ClassifyCached(data, producer_spelling);
		if (kind >= 0)
		{
			output_.EmitSimpleId(producer_spelling, data,
				static_cast<SimpleTokenKind>(kind));
			CountOutputToken();
		}
		else
		{
			output_.EmitIdentifierId(producer_spelling, data);
			CountOutputToken();
		}
	}

	void emit_pp_number(const std::string& data)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		EmitNumber(data);
	}

	void emit_character_literal(const std::string& data)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		EmitCharacter(data, false);
	}

	void emit_user_defined_character_literal(const std::string& data)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		EmitCharacter(data, true);
	}

	void emit_string_literal(const std::string& data)
	{
		QueueString(data);
	}

	void emit_user_defined_string_literal(const std::string& data)
	{
		QueueString(data);
	}

	void emit_preprocessing_op_or_punc(const std::string& data)
	{
		emit_preprocessing_op_or_punc_id(data, 0);
	}

	void emit_preprocessing_op_or_punc_id(const std::string& data,
		std::uint32_t producer_spelling)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		const int kind = ClassifyCached(data, producer_spelling);
		if (kind >= 0)
		{
			output_.EmitSimpleId(producer_spelling, data,
				static_cast<SimpleTokenKind>(kind));
			CountOutputToken();
		}
		else
			EmitInvalid(data);
	}

	void emit_non_whitespace_char(const std::string& data)
	{
		FlushStrings();
		RestoreCurrentLocation();
		CountPreprocessingToken();
		EmitInvalid(data);
	}

	void emit_eof()
	{
		FlushStrings();
		RestoreCurrentLocation();
		output_.EmitEof();
	}

	void FlushPendingTokens()
	{
		FlushStrings();
	}

private:
	void RestoreCurrentLocation()
	{
		if (has_current_location_)
			output_.SetSourceLocation(
				current_file_, current_line_, current_column_);
	}

	void CountPreprocessingToken()
	{
		if (stats_)
			++stats_->preprocessing_tokens;
	}

	void CountOutputToken()
	{
		if (stats_)
			++stats_->emitted_tokens;
	}

	void EmitInvalid(const std::string& source)
	{
		output_.EmitInvalid(source);
		CountOutputToken();
	}

	void EmitNumber(const std::string& source)
	{
		const std::size_t ud_position = source.find('_');
		if (ud_position != std::string::npos)
		{
			const std::string prefix = source.substr(0, ud_position);
			const std::string suffix = source.substr(ud_position);
			if (!IsValidUdSuffix(suffix))
			{
				output_.EmitInvalid(source);
				CountOutputToken();
				return;
			}
			IntegerSpelling integer;
			FundamentalType floating_type;
			std::size_t numeric_end = 0;
			if (ParseIntegerSpelling(prefix, false, &integer))
				output_.EmitUserDefinedInteger(source, suffix, prefix);
			else if (ParseFloatingSpelling(prefix, false, &floating_type,
				&numeric_end))
				output_.EmitUserDefinedFloating(source, suffix, prefix);
			else
			{
				output_.EmitInvalid(source);
				CountOutputToken();
				return;
			}
			CountOutputToken();
			return;
		}

		IntegerSpelling integer;
		if (ParseIntegerSpelling(source, true, &integer))
		{
			std::uint64_t value = 0;
			FundamentalType type;
			if (!ParseUnsignedDigits(source, integer.digits_begin,
				integer.digits_end, integer.base, &value) ||
				!SelectIntegerType(integer, value, &type))
			{
				output_.EmitInvalid(source);
				CountOutputToken();
				return;
			}
			unsigned char bytes[sizeof(long double) > sizeof(std::uint64_t) ?
				sizeof(long double) : sizeof(std::uint64_t)];
			const std::size_t size = FundamentalWidth(type);
			StoreLittleEndian(value, size, bytes);
			output_.EmitLiteral(source, type, bytes, size);
			CountOutputToken();
			return;
		}

		FundamentalType type;
		std::size_t numeric_end = 0;
		unsigned char bytes[sizeof(long double) > sizeof(std::uint64_t) ?
			sizeof(long double) : sizeof(std::uint64_t)];
		std::size_t size = 0;
		if (!ParseFloatingSpelling(source, true, &type, &numeric_end) ||
			!DecodeFloating(source.substr(0, numeric_end), type, bytes, &size))
			output_.EmitInvalid(source);
		else
			output_.EmitLiteral(source, type, bytes, size);
		CountOutputToken();
	}

	bool DecodeFloating(const std::string& spelling, FundamentalType type,
		unsigned char* bytes, std::size_t* size)
	{
		switch (type)
		{
		case FT_FLOAT:
		case FT_FLOAT16:
		case FT_FLOAT32:
			return DecodeFloatingValue<float>(spelling, bytes, size);
		case FT_DOUBLE:
		case FT_FLOAT32X:
		case FT_FLOAT64:
			return DecodeFloatingValue<double>(spelling, bytes, size);
		case FT_LONG_DOUBLE:
		case FT_FLOAT64X:
		case FT_FLOAT128:
			return DecodeFloatingValue<long double>(spelling, bytes, size);
		default: return false;
		}
	}

	void EmitCharacter(const std::string& source, bool user_defined)
	{
		QuotedSyntax syntax;
		std::uint32_t value = 0;
		if (!ParseCharacterSyntax(source, &syntax) ||
			!DecodeCharacter(source, syntax, &value) ||
			(user_defined != !syntax.suffix.empty()) ||
			(syntax.type == FT_CHAR16_T && value > 0xFFFF))
		{
			output_.EmitInvalid(source);
			CountOutputToken();
			return;
		}
		FundamentalType type = syntax.type;
		if (type == FT_CHAR && value > 0x7F)
			type = FT_INT;
		unsigned char bytes[sizeof(long double) > sizeof(std::uint64_t) ?
			sizeof(long double) : sizeof(std::uint64_t)];
		const std::size_t size = FundamentalWidth(type);
		StoreLittleEndian(value, size, bytes);
		if (user_defined)
			output_.EmitUserDefinedCharacter(source, syntax.suffix, type,
				bytes, size);
		else
			output_.EmitLiteral(source, type, bytes, size);
		if (stats_)
			++stats_->decoded_literal_units;
		CountOutputToken();
	}

	void QueueString(const std::string& source)
	{
		CountPreprocessingToken();
		if (pending_string_tokens_ == 0 && has_current_location_)
		{
			pending_file_ = current_file_;
			pending_line_ = current_line_;
			pending_column_ = current_column_;
		}
		if (pending_string_tokens_ != 0)
			pending_source_.push_back(' ');
		const std::size_t source_begin = pending_source_.size();
		pending_source_ += source;
		++pending_string_tokens_;
		pending_string_bytes_ += source.size();

		if (pending_valid_)
		{
			StringPart parsed;
			if (!ParseStringPart(source, &parsed))
				pending_valid_ = false;
			else
			{
				if (parsed.encoding != ENCODING_ORDINARY)
				{
					if (pending_encoding_ != ENCODING_ORDINARY &&
						pending_encoding_ != parsed.encoding)
						pending_valid_ = false;
					else
						pending_encoding_ = parsed.encoding;
				}
				const std::size_t suffix_size =
					parsed.suffix_end - parsed.suffix_begin;
				if (suffix_size != 0)
				{
					if (pending_suffix_.empty())
						pending_suffix_.assign(source, parsed.suffix_begin,
							suffix_size);
					else if (pending_suffix_.size() != suffix_size ||
						source.compare(parsed.suffix_begin, suffix_size,
							pending_suffix_) != 0)
						pending_valid_ = false;
				}
				if (pending_valid_ && parsed.content_begin != parsed.content_end)
				{
					PendingStringPart part;
					part.raw = parsed.raw;
					part.content_begin = source_begin + parsed.content_begin;
					part.content_end = source_begin + parsed.content_end;
					pending_parts_.push_back(part);
				}
			}
			if (!pending_valid_)
				pending_parts_.clear();
		}
		if (stats_)
		{
			stats_->max_pending_string_tokens = std::max(
				stats_->max_pending_string_tokens, pending_string_tokens_);
			stats_->peak_pending_string_bytes = std::max(
				stats_->peak_pending_string_bytes, pending_string_bytes_);
		}
		ObserveStorage();
	}

	void ObserveStorage()
	{
		if (!stats_)
			return;
		stats_->peak_literal_bytes = std::max(stats_->peak_literal_bytes,
			pending_bytes_.capacity());
		const std::size_t phase_storage = pending_source_.capacity() +
			pending_suffix_.capacity() +
			pending_parts_.capacity() * sizeof(PendingStringPart) +
			pending_bytes_.capacity();
		stats_->peak_phase_storage_bytes = std::max(
			stats_->peak_phase_storage_bytes, phase_storage);
	}

	// This routine is shared by nearly every token callback.  Keep one copy:
	// small changes to its cold error edges otherwise make GCC duplicate the
	// string-flush state machine into every callback.
	__attribute__((noinline)) void FlushStrings()
	{
		if (pending_string_tokens_ == 0)
			return;
		if (!pending_file_.empty())
			output_.SetSourceLocation(
				pending_file_, pending_line_, pending_column_);
		if (stats_)
			++stats_->string_sequences;
		pending_bytes_.clear();
		std::size_t units = 0;
		bool valid = pending_valid_;
		for (std::size_t i = 0; valid && i < pending_parts_.size(); ++i)
		{
			valid = DecodeStringPart(pending_source_,
				pending_parts_[i].content_begin,
				pending_parts_[i].content_end, pending_parts_[i].raw,
				pending_encoding_, &pending_bytes_, &units);
			ObserveStorage();
		}
		if (valid)
			valid = AppendStringUnit(0, pending_encoding_, &pending_bytes_, &units);
		ObserveStorage();
		if (!valid)
			output_.EmitInvalid(pending_source_);
		else if (pending_suffix_.empty())
			output_.EmitLiteralArray(pending_source_, units,
				EncodingType(pending_encoding_),
				&pending_bytes_[0], pending_bytes_.size());
		else
			output_.EmitUserDefinedString(pending_source_, pending_suffix_,
				units, EncodingType(pending_encoding_), &pending_bytes_[0],
				pending_bytes_.size());
		if (stats_ && valid)
			stats_->decoded_literal_units += units;
		CountOutputToken();
		pending_source_.clear();
		pending_suffix_.clear();
		pending_parts_.clear();
		pending_bytes_.clear();
		pending_encoding_ = ENCODING_ORDINARY;
		pending_valid_ = true;
		pending_string_tokens_ = 0;
		pending_string_bytes_ = 0;
		pending_file_.clear();
		pending_line_ = pending_column_ = 0;
	}

	IPostTokenStream& output_;
	PostTokenizationStats* stats_;
	std::vector<std::int16_t> classify_cache_;
	std::string pending_source_;
	std::string pending_suffix_;
	std::vector<PendingStringPart> pending_parts_;
	std::vector<unsigned char> pending_bytes_;
	StringEncoding pending_encoding_;
	bool pending_valid_;
	std::size_t pending_string_tokens_;
	std::size_t pending_string_bytes_;
	std::string current_file_;
	std::size_t current_line_, current_column_;
	bool has_current_location_;
	std::string pending_file_;
	std::size_t pending_line_, pending_column_;
};

}

struct PostTokenizationSession::Impl
{
	Impl(IPostTokenStream& output, PostTokenizationStats* stats)
		: analyzer(output, stats), output_stream(output)
	{}

	PostTokenAnalyzer analyzer;
	IPostTokenStream& output_stream;
};

PostTokenizationSession::PostTokenizationSession(IPostTokenStream& output,
	PostTokenizationStats* stats)
	: impl_(new Impl(output, stats))
{}

PostTokenizationSession::~PostTokenizationSession()
{
	delete impl_;
}

void PostTokenizationSession::FlushPendingTokens()
{
	impl_->analyzer.FlushPendingTokens();
}

void PostTokenizationSession::SetSourceLocation(const std::string& file,
	std::size_t line, std::size_t column)
{
	impl_->analyzer.SetSourceLocation(file, line, column);
}

void PostTokenizationSession::emit_whitespace_sequence()
{
	impl_->analyzer.emit_whitespace_sequence();
}

void PostTokenizationSession::emit_new_line()
{
	impl_->analyzer.emit_new_line();
}

void PostTokenizationSession::emit_header_name(const std::string& data)
{
	impl_->analyzer.emit_header_name(data);
}

void PostTokenizationSession::emit_identifier(const std::string& data)
{
	impl_->analyzer.emit_identifier(data);
}

void PostTokenizationSession::emit_identifier_id(const std::string& data,
	std::uint32_t producer_spelling)
{
	impl_->analyzer.emit_identifier_id(data, producer_spelling);
}

void PostTokenizationSession::emit_pp_number(const std::string& data)
{
	impl_->analyzer.emit_pp_number(data);
}

void PostTokenizationSession::emit_character_literal(const std::string& data)
{
	impl_->analyzer.emit_character_literal(data);
}

void PostTokenizationSession::emit_user_defined_character_literal(
	const std::string& data)
{
	impl_->analyzer.emit_user_defined_character_literal(data);
}

void PostTokenizationSession::emit_string_literal(const std::string& data)
{
	impl_->analyzer.emit_string_literal(data);
}

void PostTokenizationSession::emit_user_defined_string_literal(
	const std::string& data)
{
	impl_->analyzer.emit_user_defined_string_literal(data);
}

void PostTokenizationSession::emit_preprocessing_op_or_punc(
	const std::string& data)
{
	impl_->analyzer.emit_preprocessing_op_or_punc(data);
}

void PostTokenizationSession::emit_preprocessing_op_or_punc_id(
	const std::string& data, std::uint32_t producer_spelling)
{
	impl_->analyzer.emit_preprocessing_op_or_punc_id(
		data, producer_spelling);
}

void PostTokenizationSession::emit_non_whitespace_char(
	const std::string& data)
{
	impl_->analyzer.emit_non_whitespace_char(data);
}

void PostTokenizationSession::emit_eof()
{
	impl_->analyzer.emit_eof();
}

void PostTokenizationSession::EmitPragmaPackPush(std::size_t alignment)
{
	impl_->analyzer.FlushPendingTokens();
	impl_->output_stream.EmitPragmaPackPush(alignment);
}

void PostTokenizationSession::EmitPragmaPackPop()
{
	impl_->analyzer.FlushPendingTokens();
	impl_->output_stream.EmitPragmaPackPop();
}

bool DecodeOrdinaryStringLiteral(const std::string& source,
	std::string* value)
{
	if (!value)
		ThrowPostTokenInternalError("missing decoded string destination");
	StringPart parsed;
	if (!ParseStringPart(source, &parsed) ||
		parsed.encoding != ENCODING_ORDINARY || parsed.raw ||
		parsed.suffix_begin != parsed.suffix_end)
		return false;
	std::vector<unsigned char> bytes;
	std::size_t units = 0;
	if (!DecodeStringPart(source, parsed.content_begin, parsed.content_end,
		parsed.raw, parsed.encoding, &bytes, &units))
		return false;
	value->assign(bytes.begin(), bytes.end());
	return true;
}

bool DecodeNarrowStringLiteral(const std::string& source,
	std::string* value)
{
	if (!value)
		ThrowPostTokenInternalError("missing decoded string destination");
	StringPart parsed;
	if (!ParseStringPart(source, &parsed) ||
		(parsed.encoding != ENCODING_ORDINARY &&
		 parsed.encoding != ENCODING_UTF8) ||
		parsed.suffix_begin != parsed.suffix_end)
		return false;
	std::vector<unsigned char> bytes;
	std::size_t units = 0;
	if (!DecodeStringPart(source, parsed.content_begin, parsed.content_end,
		parsed.raw, parsed.encoding, &bytes, &units))
		return false;
	value->assign(bytes.begin(), bytes.end());
	return true;
}

bool DecodeNarrowStringLiteralSequence(const std::string& source,
	std::string* value)
{
	if (!value)
		ThrowPostTokenInternalError("missing decoded string destination");
	FundamentalType type = FT_VOID;
	std::vector<std::uint32_t> units;
	if (!DecodeStringLiteralCodeUnits(source, &type, &units) ||
		type != FT_CHAR || units.empty() || units.back() != 0)
		return false;
	value->clear();
	value->reserve(units.size() - 1);
	for (std::size_t i = 0; i + 1 < units.size(); ++i)
	{
		if (units[i] > 0xff) return false;
		value->push_back(static_cast<char>(units[i]));
	}
	return true;
}

namespace
{
std::size_t string_decode_calls = 0;
}

std::size_t StringLiteralDecodeCalls()
{
	return string_decode_calls;
}

bool DecodeStringLiteralCodeUnits(const std::string& source,
	FundamentalType* type, std::vector<std::uint32_t>* units)
{
	++string_decode_calls;
	if (!type || !units)
		ThrowPostTokenInternalError("missing typed string literal destination");
	std::vector<StringPart> parts;
	StringEncoding encoding = ENCODING_ORDINARY;
	std::size_t position = 0;
	while (position < source.size())
	{
		while (position < source.size() && source[position] == ' ') ++position;
		if (position == source.size()) break;
		const std::size_t token_begin = position;
		if (source.compare(position, 2, "u8") == 0) position += 2;
		else if (source[position] == 'u' || source[position] == 'U' ||
			source[position] == 'L') ++position;
		const bool raw = position < source.size() && source[position] == 'R';
		if (raw) ++position;
		if (position >= source.size() || source[position] != '"') return false;
		if (raw)
		{
			const std::size_t delimiter_begin = position + 1;
			const std::size_t open = source.find('(', delimiter_begin);
			if (open == std::string::npos || open - delimiter_begin > 16)
				return false;
			const std::string delimiter = source.substr(
				delimiter_begin, open - delimiter_begin);
			const std::string close = ")" + delimiter + "\"";
			const std::size_t close_position = source.find(close, open + 1);
			if (close_position == std::string::npos) return false;
			position = close_position + close.size();
		}
		else
		{
			++position;
			bool closed = false;
			while (position < source.size())
			{
				if (source[position] == '\\')
				{
					position += 2;
					continue;
				}
				if (source[position++] == '"')
				{
					closed = true;
					break;
				}
			}
			if (!closed) return false;
		}
		while (position < source.size() && source[position] != ' ') ++position;
		const std::string token = source.substr(token_begin,
			position - token_begin);
		StringPart parsed;
		if (!ParseStringPart(token, &parsed) ||
			parsed.suffix_begin != parsed.suffix_end)
			return false;
		if (parsed.encoding != ENCODING_ORDINARY)
		{
			if (encoding != ENCODING_ORDINARY && encoding != parsed.encoding)
				return false;
			encoding = parsed.encoding;
		}
		parsed.content_begin += token_begin;
		parsed.content_end += token_begin;
		parts.push_back(parsed);
	}
	if (parts.empty()) return false;
	std::vector<unsigned char> bytes;
	std::size_t count = 0;
	for (std::size_t i = 0; i < parts.size(); ++i)
		if (!DecodeStringPart(source, parts[i].content_begin,
			parts[i].content_end, parts[i].raw, encoding, &bytes, &count))
			return false;
	if (!AppendStringUnit(0, encoding, &bytes, &count)) return false;
	const std::size_t width = EncodingWidth(encoding);
	if (bytes.size() != count * width) return false;
	units->clear();
	units->reserve(count);
	for (std::size_t unit = 0; unit < count; ++unit)
	{
		std::uint32_t value = 0;
		for (std::size_t byte = 0; byte < width; ++byte)
			value |= static_cast<std::uint32_t>(
				bytes[unit * width + byte]) << (byte * 8);
		units->push_back(value);
	}
	*type = EncodingType(encoding);
	return true;
}

bool DecodeOrdinaryMulticharacterLiteral(const std::string& source,
	std::uint32_t* value)
{
	if (!value)
		ThrowPostTokenInternalError("missing multicharacter destination");
	QuotedSyntax syntax;
	if (!ParseCharacterSyntax(source, &syntax) || syntax.type != FT_CHAR ||
		!syntax.suffix.empty()) return false;
	std::size_t position = syntax.content_begin;
	std::size_t count = 0;
	std::uint32_t result = 0;
	while (position < syntax.content_end)
	{
		std::uint32_t current = 0;
		if (source[position] == '\\')
		{
			if (!DecodeEscape(source, syntax.content_end, &position, &current))
				return false;
		}
		else if (!DecodeUTF8One(source, syntax.content_end, &position, &current))
			return false;
		if (current > 0xFF || count == sizeof(result)) return false;
		result = (result << 8) | current;
		++count;
	}
	if (count < 2) return false;
	*value = result;
	return true;
}

const char* FundamentalTypeName(FundamentalType type)
{
	static const char* names[] = {
		"signed char", "short int", "int", "long int", "long long int",
		"unsigned char", "unsigned short int", "unsigned int",
		"unsigned long int", "unsigned long long int", "wchar_t", "char",
		"char16_t", "char32_t", "bool", "float", "double", "long double",
		"void", "nullptr_t", "_Float16", "_Float32", "_Float32x",
		"_Float64", "_Float64x", "_Float128"
	};
	if (type < FT_SIGNED_CHAR || type > FT_FLOAT128)
		ThrowPostTokenInternalError("unknown fundamental type");
	return names[type];
}

bool ClassifySimpleSpelling(const std::string& spelling,
	SimpleTokenKind* kind)
{
	return FindSimple(spelling, kind);
}

const char* SimpleTokenKindName(SimpleTokenKind kind)
{
	static const char* names[] = {
		"KW_ALIGNAS", "KW_ALIGNOF", "KW_ASM", "KW_AUTO", "KW_BOOL",
		"KW_BREAK", "KW_CASE", "KW_CATCH", "KW_CHAR", "KW_CHAR16_T",
		"KW_CHAR32_T", "KW_CLASS", "KW_CONST", "KW_CONSTEXPR",
		"KW_CONST_CAST", "KW_CONTINUE", "KW_DECLTYPE", "KW_DEFAULT",
		"KW_DELETE", "KW_DO", "KW_DOUBLE", "KW_DYNAMIC_CAST", "KW_ELSE",
		"KW_ENUM", "KW_EXPLICIT", "KW_EXPORT", "KW_EXTERN", "KW_FALSE",
		"KW_FLOAT", "KW_FOR", "KW_FRIEND", "KW_GOTO", "KW_IF",
		"KW_INLINE", "KW_INT", "KW_LONG", "KW_MUTABLE", "KW_NAMESPACE",
		"KW_NEW", "KW_NOEXCEPT", "KW_NULLPTR", "KW_OPERATOR", "KW_PRIVATE",
		"KW_PROTECTED", "KW_PUBLIC", "KW_REGISTER", "KW_REINTERPET_CAST",
		"KW_RETURN", "KW_SHORT", "KW_SIGNED", "KW_SIZEOF", "KW_STATIC",
		"KW_STATIC_ASSERT", "KW_STATIC_CAST", "KW_STRUCT", "KW_SWITCH",
		"KW_TEMPLATE", "KW_THIS", "KW_THREAD_LOCAL", "KW_THROW", "KW_TRUE",
		"KW_TRY", "KW_TYPEDEF", "KW_TYPEID", "KW_TYPENAME", "KW_UNION",
		"KW_UNSIGNED", "KW_USING", "KW_VIRTUAL", "KW_VOID", "KW_VOLATILE",
		"KW_WCHAR_T", "KW_WHILE", "OP_LBRACE", "OP_RBRACE", "OP_LSQUARE",
		"OP_RSQUARE", "OP_LPAREN", "OP_RPAREN", "OP_BOR", "OP_XOR",
		"OP_COMPL", "OP_AMP", "OP_LNOT", "OP_SEMICOLON", "OP_COLON",
		"OP_DOTS", "OP_QMARK", "OP_COLON2", "OP_DOT", "OP_DOTSTAR",
		"OP_PLUS", "OP_MINUS", "OP_STAR", "OP_DIV", "OP_MOD", "OP_ASS",
		"OP_LT", "OP_GT", "OP_PLUSASS", "OP_MINUSASS", "OP_STARASS",
		"OP_DIVASS", "OP_MODASS", "OP_XORASS", "OP_BANDASS", "OP_BORASS",
		"OP_LSHIFT", "OP_RSHIFT", "OP_RSHIFTASS", "OP_LSHIFTASS", "OP_EQ",
		"OP_NE", "OP_LE", "OP_GE", "OP_LAND", "OP_LOR", "OP_INC",
		"OP_DEC", "OP_COMMA", "OP_ARROWSTAR", "OP_ARROW"
	};
	if (kind < KW_ALIGNAS || kind > OP_ARROW)
		ThrowPostTokenInternalError("unknown simple token kind");
	return names[kind];
}

PostTokenizationStats::PostTokenizationStats()
	: preprocessing_tokens(0), emitted_tokens(0), decoded_literal_units(0),
	  string_sequences(0), max_pending_string_tokens(0),
	  peak_pending_string_bytes(0), peak_literal_bytes(0),
	  peak_phase_storage_bytes(0), elapsed_nanoseconds(0)
{}

void TokenizePostTokens(const std::string& source, IPostTokenStream& output,
	PostTokenizationStats* stats)
{
	const std::chrono::steady_clock::time_point start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	if (stats)
		*stats = PostTokenizationStats();
	PostTokenAnalyzer analyzer(output, stats);
	TokenizePreprocessingFile(source, analyzer,
		stats ? &stats->preprocessing : 0);
	if (stats)
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count());
}

}
