#include "abi/itanium/abi_mangle_type_vocabulary.h"

#include "abi/itanium/abi_mangle_errors.h"

#include <limits>

namespace abi_mangle {
namespace {

struct BuiltinEntry
{
  const char * word;
  const char * code;
};

const BuiltinEntry builtin_entries[] = {
  {nullptr, nullptr},
  {"void", "v"}, {"bool", "b"}, {"char", "c"},
  {"schar", "a"}, {"uchar", "h"}, {"wchar", "w"},
  {"char16", "Ds"}, {"char32", "Di"}, {"short", "s"},
  {"ushort", "t"}, {"int", "i"}, {"uint", "j"},
  {"long", "l"}, {"ulong", "m"}, {"longlong", "x"},
  {"ulonglong", "y"}, {"int128", "n"}, {"uint128", "o"},
  {"float", "f"}, {"double", "d"}, {"longdouble", "e"},
  {"float16", "DF16_"}, {"float32", "DF32_"},
  {"float32x", "DF32x"}, {"float64", "DF64_"},
  {"float64x", "DF64x"}, {"stdfloat128", "DF128_"},
  {"float128", "g"}, {"complex-float", "Cf"},
  {"complex-double", "Cd"}, {"complex-longdouble", "Ce"},
  {"nullptr", "Dn"}, {nullptr, nullptr}, {nullptr, nullptr}
};

static_assert(sizeof(builtin_entries) / sizeof(builtin_entries[0]) ==
                ABI_BUILTIN_TYPE_UNSIGNED_BITINT + 1,
              "ABI builtin vocabulary table is incomplete");

bool bitint_word(const std::string & word, std::size_t * first,
                 AbiBuiltinTypeKind * kind)
{
  if(word.compare(0, 7, "ubitint") == 0) {
    *first = 7;
    *kind = ABI_BUILTIN_TYPE_UNSIGNED_BITINT;
  } else if(word.compare(0, 6, "bitint") == 0) {
    *first = 6;
    *kind = ABI_BUILTIN_TYPE_BITINT;
  } else return false;
  if(*first == word.size()) return false;
  for(std::size_t i = *first; i < word.size(); ++i)
    if(word[i] < '0' || word[i] > '9') return false;
  return true;
}

}  // namespace

AbiBuiltinTypeKind abi_builtin_type_kind(const std::string & word,
                                         std::size_t * bitint_width)
{
  for(std::size_t i = ABI_BUILTIN_TYPE_VOID;
      i < ABI_BUILTIN_TYPE_BITINT; ++i)
    if(word == builtin_entries[i].word)
      return static_cast<AbiBuiltinTypeKind>(i);

  std::size_t first = 0;
  AbiBuiltinTypeKind kind = ABI_BUILTIN_TYPE_NONE;
  if(!bitint_word(word, &first, &kind)) return ABI_BUILTIN_TYPE_NONE;
  if(word.size() - first > 1 && word[first] == '0')
    return ABI_BUILTIN_TYPE_NONE;
  std::size_t width = 0;
  for(std::size_t i = first; i < word.size(); ++i) {
    const std::size_t digit = static_cast<std::size_t>(word[i] - '0');
    if(width > (std::numeric_limits<std::size_t>::max() - digit) / 10)
      return ABI_BUILTIN_TYPE_NONE;
    width = width * 10 + digit;
  }
  if(bitint_width) *bitint_width = width;
  return kind;
}

bool abi_is_builtin_type_word(const std::string & word)
{
  if(abi_builtin_type_kind(word, nullptr) != ABI_BUILTIN_TYPE_NONE)
    return true;
  std::size_t first = 0;
  AbiBuiltinTypeKind kind = ABI_BUILTIN_TYPE_NONE;
  return bitint_word(word, &first, &kind);
}

const char * abi_builtin_type_code(AbiBuiltinTypeKind kind)
{
  if(kind >= ABI_BUILTIN_TYPE_BITINT ||
     builtin_entries[kind].code == nullptr)
    ThrowAbiInternal("ABI builtin type has no fixed encoding");
  return builtin_entries[kind].code;
}

const char * abi_builtin_type_word(AbiBuiltinTypeKind kind)
{
  if(kind >= ABI_BUILTIN_TYPE_BITINT ||
     builtin_entries[kind].word == nullptr)
    ThrowAbiInternal("ABI builtin type has no fixed fact word");
  return builtin_entries[kind].word;
}

std::string abi_builtin_type_text_code(const std::string & word)
{
  const AbiBuiltinTypeKind kind = abi_builtin_type_kind(word, nullptr);
  if(kind != ABI_BUILTIN_TYPE_NONE && kind < ABI_BUILTIN_TYPE_BITINT)
    return abi_builtin_type_code(kind);
  std::size_t first = 0;
  AbiBuiltinTypeKind bitint = ABI_BUILTIN_TYPE_NONE;
  if(bitint_word(word, &first, &bitint))
    return std::string(bitint == ABI_BUILTIN_TYPE_UNSIGNED_BITINT ? "DU" : "DB")
      + word.substr(first) + '_';
  ThrowAbiFactInput("unknown ABI builtin type '" + word + "'");
}

AbiStandardSubstitutionKind abi_standard_substitution_kind(
  const std::string & word)
{
  if(word == "Sa") return ABI_STANDARD_SUBSTITUTION_ALLOCATOR;
  if(word == "Sb") return ABI_STANDARD_SUBSTITUTION_BASIC_STRING;
  if(word == "Ss") return ABI_STANDARD_SUBSTITUTION_STRING;
  if(word == "Si") return ABI_STANDARD_SUBSTITUTION_ISTREAM;
  if(word == "So") return ABI_STANDARD_SUBSTITUTION_OSTREAM;
  if(word == "Sd") return ABI_STANDARD_SUBSTITUTION_IOSTREAM;
  return ABI_STANDARD_SUBSTITUTION_TEXT;
}

const char * abi_standard_substitution_code(
  AbiStandardSubstitutionKind kind)
{
  static const char * codes[] = {nullptr, "Sa", "Sb", "Ss", "Si", "So", "Sd"};
  static_assert(sizeof(codes) / sizeof(codes[0]) ==
                  ABI_STANDARD_SUBSTITUTION_IOSTREAM + 1,
                "ABI standard substitution table is incomplete");
  if(kind > ABI_STANDARD_SUBSTITUTION_IOSTREAM || codes[kind] == nullptr)
    ThrowAbiInternal("ABI standard substitution has no fixed encoding");
  return codes[kind];
}

AbiVendorQualifierKind abi_vendor_qualifier_kind(const std::string & word)
{
  return word == "block_pointer" ? ABI_VENDOR_QUALIFIER_BLOCK_POINTER :
    ABI_VENDOR_QUALIFIER_TEXT;
}

const char * abi_vendor_qualifier_word(AbiVendorQualifierKind kind)
{
  if(kind != ABI_VENDOR_QUALIFIER_BLOCK_POINTER)
    ThrowAbiInternal("ABI vendor qualifier has no fixed word");
  return "block_pointer";
}

const char * abi_vendor_qualifier_code(AbiVendorQualifierKind kind)
{
  if(kind != ABI_VENDOR_QUALIFIER_BLOCK_POINTER)
    ThrowAbiInternal("ABI vendor qualifier has no fixed encoding");
  return "U13block_pointer";
}

}  // namespace abi_mangle
