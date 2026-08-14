#include "hosted_extension_registry.h"

#include <cstddef>

namespace cppgm
{
namespace hosted_extension
{
namespace
{

struct SpecifierEntry
{
	const char* spelling;
	SpecifierKind kind;
};

template <std::size_t Size>
SpecifierKind Find(const std::string& spelling,
	const SpecifierEntry (&entries)[Size])
{
	std::size_t first = 0;
	std::size_t last = Size;
	while (first < last)
	{
		const std::size_t middle = first + (last - first) / 2;
		const int order = spelling.compare(entries[middle].spelling);
		if (order < 0) last = middle;
		else if (order > 0) first = middle + 1;
		else return entries[middle].kind;
	}
	return SPECIFIER_NONE;
}

template <std::size_t Size>
bool Contains(const std::string& spelling, const char* const (&entries)[Size])
{
	std::size_t first = 0;
	std::size_t last = Size;
	while (first < last)
	{
		const std::size_t middle = first + (last - first) / 2;
		const int order = spelling.compare(entries[middle]);
		if (order < 0) last = middle;
		else if (order > 0) first = middle + 1;
		else return true;
	}
	return false;
}

}

SpecifierKind FindSpecifier(const std::string& spelling)
{
	static const SpecifierEntry entries[] = {
		{"_Atomic", SPECIFIER_ATOMIC},
		{"_BitInt", SPECIFIER_BITINT},
		{"_Complex", SPECIFIER_COMPLEX},
		{"_Float128", SPECIFIER_STDFLOAT128},
		{"_Float16", SPECIFIER_FLOAT16},
		{"_Float32", SPECIFIER_FLOAT32},
		{"_Float32x", SPECIFIER_FLOAT32X},
		{"_Float64", SPECIFIER_FLOAT64},
		{"_Float64x", SPECIFIER_FLOAT64X},
		{"__complex", SPECIFIER_COMPLEX},
		{"__complex__", SPECIFIER_COMPLEX},
		{"__const", SPECIFIER_CONST},
		{"__const__", SPECIFIER_CONST},
		{"__extension__", SPECIFIER_EXTENSION},
		{"__float128", SPECIFIER_FLOAT128},
		{"__inline", SPECIFIER_INLINE},
		{"__inline__", SPECIFIER_INLINE},
		{"__int128", SPECIFIER_INT128},
		{"__int128_t", SPECIFIER_INT128},
		{"__signed", SPECIFIER_SIGNED},
		{"__signed__", SPECIFIER_SIGNED},
		{"__thread", SPECIFIER_THREAD_LOCAL},
		{"__uint128_t", SPECIFIER_UINT128},
		{"__volatile", SPECIFIER_VOLATILE},
		{"__volatile__", SPECIFIER_VOLATILE}
	};
	return Find(spelling, entries);
}

const char* CanonicalSpecifier(SpecifierKind kind)
{
	switch (kind)
	{
	case SPECIFIER_ATOMIC: return "_Atomic";
	case SPECIFIER_BITINT: return "_BitInt";
	case SPECIFIER_CONST: return "const";
	case SPECIFIER_COMPLEX: return "_Complex";
	case SPECIFIER_FLOAT16: return "_Float16";
	case SPECIFIER_FLOAT32: return "_Float32";
	case SPECIFIER_FLOAT32X: return "_Float32x";
	case SPECIFIER_FLOAT64: return "_Float64";
	case SPECIFIER_FLOAT64X: return "_Float64x";
	case SPECIFIER_STDFLOAT128: return "_Float128";
	case SPECIFIER_FLOAT128: return "__float128";
	case SPECIFIER_INLINE: return "inline";
	case SPECIFIER_INT128: return "__int128_t";
	case SPECIFIER_SIGNED: return "signed";
	case SPECIFIER_THREAD_LOCAL: return "thread_local";
	case SPECIFIER_UINT128: return "__uint128_t";
	case SPECIFIER_VOLATILE: return "volatile";
	case SPECIFIER_EXTENSION: return "";
	case SPECIFIER_NONE: break;
	}
	return 0;
}

bool IsDeclarationOnlySpecifier(SpecifierKind kind)
{
	return kind == SPECIFIER_INLINE || kind == SPECIFIER_THREAD_LOCAL;
}

bool IsTypeSpecifier(SpecifierKind kind)
{
	return kind == SPECIFIER_ATOMIC || kind == SPECIFIER_BITINT ||
		kind == SPECIFIER_COMPLEX ||
		kind == SPECIFIER_FLOAT16 || kind == SPECIFIER_FLOAT32 ||
		kind == SPECIFIER_FLOAT32X || kind == SPECIFIER_FLOAT64 ||
		kind == SPECIFIER_FLOAT64X || kind == SPECIFIER_STDFLOAT128 ||
		kind == SPECIFIER_FLOAT128 ||
		kind == SPECIFIER_INT128 || kind == SPECIFIER_UINT128 ||
		kind == SPECIFIER_SIGNED;
}

bool IsCvSpecifier(SpecifierKind kind)
{
	return kind == SPECIFIER_CONST || kind == SPECIFIER_VOLATILE;
}

bool IsGnuAttributeIntroducer(const std::string& spelling)
{
	return spelling == "__attribute" || spelling == "__attribute__";
}

bool IsTypeAnnotation(const std::string& spelling)
{
	static const char* const entries[] = {
		"_Nonnull",
		"_Null_unspecified",
		"_Nullable",
		"_Nullable_result",
		"__restrict",
		"__restrict__"
	};
	return Contains(spelling, entries);
}

}
}
