#include "abi/itanium/abi_mangle_graph_argument.h"
#include "abi/itanium/abi_mangle_hash.h"

#include <functional>

namespace abi_mangle {
namespace detail {
namespace {

std::size_t vector_hash(
  std::size_t seed, const std::vector<std::size_t> & values)
{
  for(std::size_t value : values) seed = mix_hash(seed, value);
  return seed;
}

}  // namespace

bool ArgumentNode::operator==(const ArgumentNode & other) const
{
  return kind == other.kind && type == other.type &&
    value_type == other.value_type && owner_type == other.owner_type &&
    expression == other.expression && entity == other.entity &&
    name == other.name && substitution == other.substitution &&
    symbol == other.symbol && index == other.index && value == other.value &&
    has_value_type == other.has_value_type && address_of == other.address_of &&
    pack_expansion == other.pack_expansion &&
    member_is_function == other.member_is_function &&
    member_const == other.member_const &&
    member_volatile == other.member_volatile &&
    member_lvalue_ref == other.member_lvalue_ref &&
    member_rvalue_ref == other.member_rvalue_ref &&
    member_variadic == other.member_variadic &&
    member_terminal_kind == other.member_terminal_kind &&
    member_terminal_code == other.member_terminal_code &&
    member_literal_suffix == other.member_literal_suffix &&
    member_conversion_type == other.member_conversion_type &&
    member_result_type == other.member_result_type &&
    member_has_result_type == other.member_has_result_type &&
    substitution_resolved == other.substitution_resolved &&
    entity_resolved == other.entity_resolved &&
    parameters == other.parameters && arguments == other.arguments;
}

std::size_t argument_node_hash(const ArgumentNode & argument)
{
  std::size_t hash = static_cast<std::size_t>(argument.kind);
  hash = mix_hash(hash, argument.type);
  hash = mix_hash(hash, argument.value_type);
  hash = mix_hash(hash, argument.owner_type);
  hash = mix_hash(hash, argument.expression);
  hash = mix_hash(hash, argument.entity);
  hash = mix_hash(hash, argument.name);
  hash = mix_hash(hash, argument.substitution);
  hash = mix_hash(hash, argument.symbol);
  hash = mix_hash(hash, static_cast<std::size_t>(
    argument.member_terminal_kind));
  hash = mix_hash(hash, static_cast<std::size_t>(
    argument.member_terminal_code));
  hash = mix_hash(hash, argument.member_literal_suffix);
  hash = mix_hash(hash, argument.member_conversion_type);
  hash = mix_hash(hash, argument.member_result_type);
  hash = mix_hash(hash, argument.index);
  hash = mix_hash(hash, std::hash<long long>()(argument.value));
  hash = mix_hash(hash, argument.has_value_type |
    (argument.address_of << 1) | (argument.member_is_function << 2) |
    (argument.member_const << 3) | (argument.member_volatile << 4) |
    (argument.member_lvalue_ref << 5) | (argument.member_rvalue_ref << 6) |
    (argument.member_variadic << 7) | (argument.member_has_result_type << 8) |
    (argument.pack_expansion << 9) | (argument.entity_resolved << 10) |
    (argument.substitution_resolved << 11));
  hash = vector_hash(hash, argument.parameters);
  return vector_hash(hash, argument.arguments);
}

}  // namespace detail
}  // namespace abi_mangle
