#pragma once

#include <string>

namespace cppgm
{
namespace hosted_extension
{

enum SpecifierKind
{
	SPECIFIER_NONE,
	SPECIFIER_ATOMIC,
	SPECIFIER_BITINT,
	SPECIFIER_CONST,
	SPECIFIER_COMPLEX,
	SPECIFIER_EXTENSION,
	SPECIFIER_FLOAT16,
	SPECIFIER_FLOAT32,
	SPECIFIER_FLOAT32X,
	SPECIFIER_FLOAT64,
	SPECIFIER_FLOAT64X,
	SPECIFIER_STDFLOAT128,
	SPECIFIER_FLOAT128,
	SPECIFIER_INLINE,
	SPECIFIER_INT128,
	SPECIFIER_SIGNED,
	SPECIFIER_THREAD_LOCAL,
	SPECIFIER_UINT128,
	SPECIFIER_VOLATILE
};

SpecifierKind FindSpecifier(const std::string& spelling);
const char* CanonicalSpecifier(SpecifierKind kind);
bool IsDeclarationOnlySpecifier(SpecifierKind kind);
bool IsTypeSpecifier(SpecifierKind kind);
bool IsCvSpecifier(SpecifierKind kind);
bool IsGnuAttributeIntroducer(const std::string& spelling);
bool IsTypeAnnotation(const std::string& spelling);

}
}
