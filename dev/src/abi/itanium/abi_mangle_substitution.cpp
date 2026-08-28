#include "abi/itanium/abi_mangle_substitution.h"

#include "abi/itanium/abi_mangle_hash.h"
#include "abi/itanium/abi_mangle_presentation.h"

#include <utility>

namespace abi_mangle {
namespace detail {
namespace {

struct CompositeSubstitutionKey
{
  SubstitutionKey base;
  std::vector<std::size_t> tags;

  bool operator==(const CompositeSubstitutionKey & other) const
  {
    return base == other.base && tags == other.tags;
  }
};

struct CompositeSubstitutionHash
{
  std::size_t operator()(const CompositeSubstitutionKey & key) const
  {
    std::size_t hash = SubstitutionHash()(key.base);
    for(std::size_t tag : key.tags) hash = mix_hash(hash, tag);
    return hash;
  }
};

}  // namespace

struct SubstitutionTable::CompositePool
{
  std::unordered_map<CompositeSubstitutionKey, std::size_t,
                     CompositeSubstitutionHash> indexes;
};

bool SubstitutionKey::operator==(const SubstitutionKey & other) const
{
  return kind == other.kind && id == other.id && secondary == other.secondary;
}

std::size_t SubstitutionHash::operator()(const SubstitutionKey & key) const
{
  return mix_hash(mix_hash(static_cast<std::size_t>(key.kind), key.id),
                  key.secondary);
}

SubstitutionTable::SubstitutionTable(AbiMangleStats * stats)
  : stats_(stats) {}

SubstitutionTable::~SubstitutionTable() {}

bool SubstitutionTable::emit_if_known(const SubstitutionKey & key,
                                      std::string & output) const
{
  if(stats_) ++stats_->substitution_lookups;
  const auto found = indexes_.find(key);
  if(found == indexes_.end()) return false;
  if(stats_) ++stats_->substitution_hits;
  output += 'S';
  if(found->second != 0) output += base36(found->second - 1);
  output += '_';
  return true;
}

void SubstitutionTable::add(const SubstitutionKey & key)
{
  if(indexes_.find(key) != indexes_.end()) return;
  indexes_.insert(std::make_pair(key, indexes_.size()));
  if(stats_) ++stats_->substitution_entries;
}

SubstitutionKey SubstitutionTable::composite_key(
  const SubstitutionKey & base, const std::vector<std::size_t> & tags)
{
  if(tags.empty()) return base;
  if(!composites_) composites_.reset(new CompositePool);
  const CompositeSubstitutionKey key{base, tags};
  const auto found = composites_->indexes.find(key);
  if(found != composites_->indexes.end())
    return SubstitutionKey{SUBSTITUTION_COMPOSITE, found->second};
  const std::size_t id = composites_->indexes.size();
  composites_->indexes.insert(std::make_pair(key, id));
  return SubstitutionKey{SUBSTITUTION_COMPOSITE, id};
}

void SubstitutionTable::swap(SubstitutionTable & other)
{
  indexes_.swap(other.indexes_);
  composites_.swap(other.composites_);
}

}  // namespace detail
}  // namespace abi_mangle
