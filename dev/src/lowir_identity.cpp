#include "lowir_identity.h"

#include <stdexcept>

namespace lowir_model {
namespace {

std::size_t hash_range(const std::string & text, std::size_t first,
                       std::size_t count)
{
  std::size_t value = sizeof(std::size_t) == 8 ?
    static_cast<std::size_t>(1469598103934665603ULL) :
    static_cast<std::size_t>(2166136261U);
  const std::size_t prime = sizeof(std::size_t) == 8 ?
    static_cast<std::size_t>(1099511628211ULL) :
    static_cast<std::size_t>(16777619U);
  for(std::size_t i = first; i < first + count; ++i) {
    value ^= static_cast<unsigned char>(text[i]);
    value *= prime;
  }
  return value;
}

}  // namespace

StringPool::StringPool() : slots_(32, 0), spelling_bytes_(0)
{
  strings_.push_back(std::string());
}

StringId StringPool::intern(const std::string & text, StringPoolStats * stats)
{
  return intern_range(text, 0, text.size(), stats);
}

StringId StringPool::intern_range(const std::string & text, std::size_t first,
                                  std::size_t count, StringPoolStats * stats)
{
  if(first > text.size() || count > text.size() - first)
    throw std::logic_error("invalid LowIR string-pool range");
  if(stats) {
    ++stats->intern_calls;
    stats->hash_bytes += count;
  }
  if((strings_.size() + 1) * 10 > slots_.size() * 7)
    rehash(slots_.size() * 2);
  const std::size_t mask = slots_.size() - 1;
  std::size_t slot = hash_range(text, first, count) & mask;
  while(slots_[slot] != 0) {
    if(stats) ++stats->slot_probes;
    const std::uint32_t id = slots_[slot];
    if(strings_[id].size() == count &&
       text.compare(first, count, strings_[id]) == 0) {
      if(stats) ++stats->intern_hits;
      return StringId(id);
    }
    slot = (slot + 1) & mask;
  }
  if(strings_.size() >= kInvalidCompactId)
    throw std::runtime_error("too many pooled LowIR strings");
  const std::uint32_t id = static_cast<std::uint32_t>(strings_.size());
  strings_.push_back(text.substr(first, count));
  spelling_bytes_ += count;
  slots_[slot] = id;
  if(stats) ++stats->intern_misses;
  return StringId(id);
}

void StringPool::reserve(std::size_t expected_strings)
{
  if(expected_strings >= kInvalidCompactId)
    expected_strings = kInvalidCompactId - 1;
  strings_.reserve(expected_strings + 1);
  std::size_t capacity = slots_.size();
  while(expected_strings + 1 > capacity * 7 / 10) capacity *= 2;
  if(capacity > slots_.size()) rehash(capacity);
}

const std::string & StringPool::get(StringId id) const
{
  const std::uint32_t index = id;
  if(index >= strings_.size())
    throw std::logic_error("invalid pooled LowIR string identity");
  return strings_[index];
}

std::size_t StringPool::size() const { return strings_.size() - 1; }
std::size_t StringPool::spelling_bytes() const { return spelling_bytes_; }

std::size_t StringPool::storage_bytes() const
{
  std::size_t bytes = strings_.capacity() * sizeof(std::string) +
    slots_.capacity() * sizeof(std::uint32_t);
  for(std::size_t i = 1; i < strings_.size(); ++i)
    bytes += strings_[i].capacity();
  return bytes;
}

void StringPool::rehash(std::size_t capacity)
{
  std::vector<std::uint32_t> replacement(capacity, 0);
  const std::size_t mask = capacity - 1;
  for(std::uint32_t id = 1; id < strings_.size(); ++id) {
    std::size_t slot = hash_range(strings_[id], 0, strings_[id].size()) & mask;
    while(replacement[slot] != 0) slot = (slot + 1) & mask;
    replacement[slot] = id;
  }
  slots_.swap(replacement);
}

}  // namespace lowir_model
