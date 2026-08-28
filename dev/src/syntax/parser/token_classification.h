#pragma once

#include "syntax/model/arena.h"

#include <string>

namespace cppgm
{
namespace syntax
{

inline bool IsFundamentalKind(std::uint16_t kind)
{
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case KW_BOOL: case KW_CHAR: case KW_CHAR16_T: case KW_CHAR32_T:
	case KW_DOUBLE: case KW_FLOAT: case KW_INT: case KW_LONG: case KW_SHORT:
	case KW_SIGNED: case KW_UNSIGNED: case KW_VOID: case KW_WCHAR_T:
		return true;
	default: return false;
	}
}

inline bool IsFundamentalTypeSpelling(const std::string& spelling)
{
	static const char* const names[] = {"bool", "char", "char16_t", "char32_t",
		"double", "float", "int", "long", "short", "signed", "unsigned",
		"void", "wchar_t"};
	for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
		if (spelling == names[i]) return true;
	return false;
}

inline bool IsDeclSpecifierKeyword(std::uint16_t kind)
{
	if (kind < kSimpleTokenCount && IsFundamentalKind(kind)) return true;
	if (kind >= kSimpleTokenCount) return false;
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case KW_CONST: case KW_VOLATILE: case KW_TYPEDEF: case KW_EXTERN:
	case KW_STATIC: case KW_INLINE: case KW_VIRTUAL: case KW_CONSTEXPR:
	case KW_THREAD_LOCAL: case KW_AUTO: case KW_FRIEND: case KW_MUTABLE:
	case KW_REGISTER: return true;
	default: return false;
	}
}

inline bool IsTypeSpecifierStartKind(std::uint16_t kind)
{
	if (IsDeclSpecifierKeyword(kind)) return true;
	if (kind >= kSimpleTokenCount) return false;
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case KW_DECLTYPE: case KW_TYPENAME: case KW_CLASS: case KW_STRUCT:
	case KW_UNION: case KW_ENUM: return true;
	default: return false;
	}
}

inline bool IsAssignmentOperator(std::uint16_t kind)
{
	if (kind >= kSimpleTokenCount) return false;
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case OP_ASS: case OP_PLUSASS: case OP_MINUSASS: case OP_STARASS:
	case OP_DIVASS: case OP_MODASS: case OP_XORASS: case OP_BANDASS:
	case OP_BORASS: case OP_LSHIFTASS: case OP_RSHIFTASS: return true;
	default: return false;
	}
}

inline bool IsOperatorNameToken(std::uint16_t kind)
{
	if (kind >= static_cast<std::uint16_t>(OP_PLUS) &&
		kind <= static_cast<std::uint16_t>(OP_ARROW)) return true;
	return kind == static_cast<std::uint16_t>(OP_BOR) ||
		kind == static_cast<std::uint16_t>(OP_XOR) ||
		kind == static_cast<std::uint16_t>(OP_COMPL) ||
		kind == static_cast<std::uint16_t>(OP_AMP) ||
		kind == static_cast<std::uint16_t>(OP_LNOT);
}

}
}
