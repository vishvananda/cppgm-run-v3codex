#include "abi_mangle.h"

#include <limits>
#include <stdexcept>

namespace abi_mangle {

size_t make_semantic_substitution(AbiSemanticSubstitutionKind kind,
                                  size_t identity)
{
  const size_t kind_count = 3;
  const size_t kind_value = static_cast<size_t>(kind);
  if(kind_value >= kind_count ||
     identity > (std::numeric_limits<size_t>::max() - kind_value - 1) /
       kind_count)
    throw std::logic_error("semantic ABI substitution identity is invalid");
  return identity * kind_count + kind_value;
}

bool AbiFunctionTarget::has_resolved_source_name() const
{
  return resolved_context == ABI_NO_RESOLVED_REFERENCE &&
    resolved_context_identity != ABI_NO_RESOLVED_REFERENCE;
}

void AbiFunctionTarget::set_resolved_source_name(size_t name)
{
  resolved_context_identity = name;
}

size_t AbiFunctionTarget::resolved_source_name() const
{
  return resolved_context_identity;
}

}  // namespace abi_mangle
