#pragma once

// Canonical type records used by the numeric ABI mangling graph.

#include "abi/itanium/abi_mangle_facts.h"

#include <cstddef>
#include <vector>

namespace abi_mangle {
namespace detail {

struct TypeNode
{
  AbiTypeKind kind = ABI_TYPE_BUILTIN;
  AbiBuiltinTypeKind builtin_type = ABI_BUILTIN_TYPE_NONE;
  AbiStandardSubstitutionKind standard_substitution =
    ABI_STANDARD_SUBSTITUTION_TEXT;
  AbiVendorQualifierKind vendor_qualifier = ABI_VENDOR_QUALIFIER_TEXT;
  AbiLocalPresentationKind local_presentation = ABI_LOCAL_PRESENTATION_TEXT;
  std::size_t symbol = ABI_NO_RESOLVED_REFERENCE;
  std::size_t path = ABI_NO_RESOLVED_REFERENCE;
  std::size_t expression = ABI_NO_RESOLVED_REFERENCE;
  std::size_t context = ABI_NO_RESOLVED_REFERENCE;
  std::size_t context_identity = ABI_NO_RESOLVED_REFERENCE;
  std::size_t discriminator = ABI_NO_RESOLVED_REFERENCE;
  std::size_t substitution = ABI_NO_RESOLVED_REFERENCE;
  std::size_t index = 0;
  AbiArrayBoundKind bound_kind = ABI_ARRAY_BOUND_VALUE;
  bool is_const = false;
  bool is_volatile = false;
  bool variadic = false;
  bool lvalue_ref = false;
  bool rvalue_ref = false;
  bool substitutable = false;
  bool suppress_template_prefix_substitution = false;
  bool standard_includes_arguments = false;
  bool substitution_resolved = false;
  bool context_resolved = false;
  bool uses_case_facts = false;
  std::vector<std::size_t> children;
  std::vector<std::size_t> arguments;
  std::vector<std::size_t> namespaces;
  std::vector<std::size_t> tags;

  bool operator==(const TypeNode & other) const;
};

std::size_t type_node_hash(const TypeNode & type);
bool has_resolved_type_substitution(const AbiType & type);

}  // namespace detail
}  // namespace abi_mangle
