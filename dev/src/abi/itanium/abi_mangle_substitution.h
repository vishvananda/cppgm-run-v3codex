#pragma once

// Per-encoding Itanium ABI substitution state.

#include "abi/itanium/abi_mangle_stats.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace abi_mangle {
namespace detail {

enum SubstitutionKind
{
  SUBSTITUTION_PATH,
  SUBSTITUTION_TYPE,
  SUBSTITUTION_FUNCTION_TEMPLATE_PREFIX,
  SUBSTITUTION_EXPLICIT,
  SUBSTITUTION_RESOLVED,
  SUBSTITUTION_MEMBER_TEMPLATE_PREFIX,
  SUBSTITUTION_COMPOSITE,
  SUBSTITUTION_LOCAL_LAMBDA,
  SUBSTITUTION_LOCAL_LAMBDA_ORDINAL
};

struct SubstitutionKey
{
  SubstitutionKind kind = SUBSTITUTION_TYPE;
  std::size_t id = 0;
  std::size_t secondary = 0;

  SubstitutionKey() {}
  SubstitutionKey(SubstitutionKind kind_value, std::size_t id_value,
                  std::size_t secondary_value = 0)
    : kind(kind_value), id(id_value), secondary(secondary_value) {}

  bool operator==(const SubstitutionKey & other) const;
};

struct SubstitutionHash
{
  std::size_t operator()(const SubstitutionKey & key) const;
};

class SubstitutionTable
{
public:
  explicit SubstitutionTable(AbiMangleStats * stats = nullptr);
  ~SubstitutionTable();

  bool emit_if_known(const SubstitutionKey & key,
                     std::string & output) const;
  void add(const SubstitutionKey & key);
  SubstitutionKey composite_key(
    const SubstitutionKey & base,
    const std::vector<std::size_t> & tags);
  void swap(SubstitutionTable & other);

private:
  struct CompositePool;

  AbiMangleStats * stats_;
  std::unordered_map<SubstitutionKey, std::size_t, SubstitutionHash> indexes_;
  std::unique_ptr<CompositePool> composites_;
};

}  // namespace detail
}  // namespace abi_mangle
