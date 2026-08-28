#include "abi/itanium/abi_mangle_graph_type.h"
#include "abi/itanium/abi_mangle_hash.h"

#include <functional>

namespace abi_mangle {
namespace detail {
namespace {

template<class T>
std::size_t vector_hash(std::size_t seed, const std::vector<T> & values)
{
  for(const T & value : values)
    seed = mix_hash(seed, std::hash<T>()(value));
  return seed;
}

}  // namespace

bool TypeNode::operator==(const TypeNode & other) const
{
  return kind == other.kind && builtin_type == other.builtin_type
    && standard_substitution == other.standard_substitution
    && vendor_qualifier == other.vendor_qualifier
    && local_presentation == other.local_presentation
    && symbol == other.symbol && path == other.path
    && expression == other.expression
    && (!context_resolved ? context == other.context : true)
    && context_identity == other.context_identity
    && discriminator == other.discriminator
    && substitution == other.substitution
    && index == other.index && bound_kind == other.bound_kind
    && is_const == other.is_const && is_volatile == other.is_volatile
    && variadic == other.variadic && lvalue_ref == other.lvalue_ref
    && rvalue_ref == other.rvalue_ref
    && substitutable == other.substitutable
    && suppress_template_prefix_substitution ==
      other.suppress_template_prefix_substitution
    && standard_includes_arguments == other.standard_includes_arguments
    && substitution_resolved == other.substitution_resolved
    && context_resolved == other.context_resolved
    && children == other.children && arguments == other.arguments
    && namespaces == other.namespaces && tags == other.tags;
}

std::size_t type_node_hash(const TypeNode & type)
{
  const std::size_t no_id = ABI_NO_RESOLVED_REFERENCE;
  std::size_t hash = static_cast<std::size_t>(type.kind);
  hash = mix_hash(hash, static_cast<std::size_t>(type.builtin_type));
  hash = mix_hash(hash, static_cast<std::size_t>(type.standard_substitution));
  hash = mix_hash(hash, static_cast<std::size_t>(type.vendor_qualifier));
  hash = mix_hash(hash, static_cast<std::size_t>(type.local_presentation));
  hash = mix_hash(hash, type.symbol);
  hash = mix_hash(hash, type.path);
  hash = mix_hash(hash, type.expression);
  hash = mix_hash(hash, type.context_resolved ? no_id : type.context);
  hash = mix_hash(hash, type.context_identity);
  hash = mix_hash(hash, type.discriminator);
  hash = mix_hash(hash, type.substitution);
  hash = mix_hash(hash, type.index);
  hash = mix_hash(hash, static_cast<std::size_t>(type.bound_kind));
  hash = mix_hash(hash, type.is_const | (type.is_volatile << 1) |
    (type.variadic << 2) | (type.standard_includes_arguments << 3) |
    (type.substitutable << 4) |
    (type.suppress_template_prefix_substitution << 5) |
    (type.lvalue_ref << 6) | (type.rvalue_ref << 7) |
    (type.context_resolved << 8) | (type.substitution_resolved << 9));
  hash = vector_hash(hash, type.children);
  hash = vector_hash(hash, type.arguments);
  hash = vector_hash(hash, type.namespaces);
  return vector_hash(hash, type.tags);
}

bool has_resolved_type_substitution(const AbiType & type)
{
  return type.resolved_expression != ABI_NO_RESOLVED_REFERENCE &&
    (type.kind == ABI_TYPE_RESOLVED ||
     type.kind == ABI_TYPE_TEMPLATE_SPECIALIZATION ||
     type.kind == ABI_TYPE_STD_TEMPLATE_SPECIALIZATION);
}

static_assert(sizeof(TypeNode) == 184,
              "typed ABI vocabulary must not widen canonical types");

}  // namespace detail
}  // namespace abi_mangle
