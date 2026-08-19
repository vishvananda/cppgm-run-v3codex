#pragma once

// Compact fixed vocabulary used by ABI type facts and the Itanium encoder.

#include <cstddef>
#include <cstdint>
#include <string>

namespace abi_mangle {

enum AbiBuiltinTypeKind : std::uint8_t
{
  ABI_BUILTIN_TYPE_NONE,
  ABI_BUILTIN_TYPE_VOID,
  ABI_BUILTIN_TYPE_BOOL,
  ABI_BUILTIN_TYPE_CHAR,
  ABI_BUILTIN_TYPE_SIGNED_CHAR,
  ABI_BUILTIN_TYPE_UNSIGNED_CHAR,
  ABI_BUILTIN_TYPE_WCHAR,
  ABI_BUILTIN_TYPE_CHAR16,
  ABI_BUILTIN_TYPE_CHAR32,
  ABI_BUILTIN_TYPE_SHORT,
  ABI_BUILTIN_TYPE_UNSIGNED_SHORT,
  ABI_BUILTIN_TYPE_INT,
  ABI_BUILTIN_TYPE_UNSIGNED_INT,
  ABI_BUILTIN_TYPE_LONG,
  ABI_BUILTIN_TYPE_UNSIGNED_LONG,
  ABI_BUILTIN_TYPE_LONG_LONG,
  ABI_BUILTIN_TYPE_UNSIGNED_LONG_LONG,
  ABI_BUILTIN_TYPE_INT128,
  ABI_BUILTIN_TYPE_UINT128,
  ABI_BUILTIN_TYPE_FLOAT,
  ABI_BUILTIN_TYPE_DOUBLE,
  ABI_BUILTIN_TYPE_LONG_DOUBLE,
  ABI_BUILTIN_TYPE_FLOAT16,
  ABI_BUILTIN_TYPE_FLOAT32,
  ABI_BUILTIN_TYPE_FLOAT32X,
  ABI_BUILTIN_TYPE_FLOAT64,
  ABI_BUILTIN_TYPE_FLOAT64X,
  ABI_BUILTIN_TYPE_STDFLOAT128,
  ABI_BUILTIN_TYPE_FLOAT128,
  ABI_BUILTIN_TYPE_COMPLEX_FLOAT,
  ABI_BUILTIN_TYPE_COMPLEX_DOUBLE,
  ABI_BUILTIN_TYPE_COMPLEX_LONG_DOUBLE,
  ABI_BUILTIN_TYPE_NULLPTR,
  ABI_BUILTIN_TYPE_BITINT,
  ABI_BUILTIN_TYPE_UNSIGNED_BITINT
};

enum AbiStandardSubstitutionKind : std::uint8_t
{
  ABI_STANDARD_SUBSTITUTION_TEXT,
  ABI_STANDARD_SUBSTITUTION_ALLOCATOR,
  ABI_STANDARD_SUBSTITUTION_BASIC_STRING,
  ABI_STANDARD_SUBSTITUTION_STRING,
  ABI_STANDARD_SUBSTITUTION_ISTREAM,
  ABI_STANDARD_SUBSTITUTION_OSTREAM,
  ABI_STANDARD_SUBSTITUTION_IOSTREAM
};

enum AbiVendorQualifierKind : std::uint8_t
{
  ABI_VENDOR_QUALIFIER_TEXT,
  ABI_VENDOR_QUALIFIER_BLOCK_POINTER
};

AbiBuiltinTypeKind abi_builtin_type_kind(const std::string & word,
                                         std::size_t * bitint_width);
bool abi_is_builtin_type_word(const std::string & word);
const char * abi_builtin_type_code(AbiBuiltinTypeKind kind);
const char * abi_builtin_type_word(AbiBuiltinTypeKind kind);
std::string abi_builtin_type_text_code(const std::string & word);

AbiStandardSubstitutionKind abi_standard_substitution_kind(
  const std::string & word);
const char * abi_standard_substitution_code(
  AbiStandardSubstitutionKind kind);

AbiVendorQualifierKind abi_vendor_qualifier_kind(const std::string & word);
const char * abi_vendor_qualifier_word(AbiVendorQualifierKind kind);
const char * abi_vendor_qualifier_code(AbiVendorQualifierKind kind);

}  // namespace abi_mangle
