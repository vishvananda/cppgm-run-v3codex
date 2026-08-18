#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace lowir_model {

const std::uint32_t kInvalidCompactId =
  std::numeric_limits<std::uint32_t>::max();

template <typename Tag>
class CompactId
{
public:
  CompactId() : value_(kInvalidCompactId) {}
  CompactId(std::uint32_t value) : value_(value) {}
  operator std::uint32_t() const { return value_; }
  bool valid() const { return value_ != kInvalidCompactId; }

private:
  std::uint32_t value_;
};

struct SymbolIdTag {};
struct ObjectSymbolIdTag {};
struct TypeIdTag {};
struct StringIdTag {};
struct ValueIdTag {};
struct ParameterIdTag {};
struct SlotIdTag {};
struct BlockIdTag {};
struct FrameBindingIdTag {};
struct LocalLabelIdTag {};

typedef CompactId<SymbolIdTag> SymbolId;
typedef CompactId<ObjectSymbolIdTag> ObjectSymbolId;
typedef CompactId<TypeIdTag> TypeId;
typedef CompactId<StringIdTag> StringId;
typedef CompactId<ValueIdTag> ValueId;
typedef CompactId<ParameterIdTag> ParameterId;
typedef CompactId<SlotIdTag> SlotId;
typedef CompactId<BlockIdTag> BlockId;
typedef CompactId<FrameBindingIdTag> FrameBindingId;
typedef CompactId<LocalLabelIdTag> LocalLabelId;

// A display name is either one pooled spelling or a generated `%tN` ordinal.
// Two tag bits also retain the explicit debug-copy presentation contract.
class PresentationName
{
public:
  PresentationName();
  static PresentationName pooled(StringId spelling,
                                 bool preserve_copy = false);
  static PresentationName generated_value(std::uint32_t ordinal);

  bool valid() const;
  bool generated() const;
  bool preserves_copy() const;
  StringId spelling() const;
  std::uint32_t generated_ordinal() const;

private:
  explicit PresentationName(std::uint32_t encoded);
  std::uint32_t encoded_;
};

struct StringPoolStats
{
  std::size_t intern_calls = 0;
  std::size_t intern_hits = 0;
  std::size_t intern_misses = 0;
  std::size_t hash_bytes = 0;
  std::size_t slot_probes = 0;
};

// Program-owned presentation storage. Compiler identities carry compact IDs;
// only serializers, diagnostics, and output writers resolve their spelling.
class StringPool
{
public:
  StringPool();

  StringId intern(const std::string & text, StringPoolStats * stats = 0);
  StringId intern_range(const std::string & text, std::size_t first,
                        std::size_t count, StringPoolStats * stats = 0);
  void reserve(std::size_t expected_strings);
  const std::string & get(StringId id) const;
  std::size_t size() const;
  std::size_t spelling_bytes() const;
  std::size_t storage_bytes() const;

private:
  void rehash(std::size_t capacity);

  std::vector<std::string> strings_;
  std::vector<std::uint32_t> slots_;
  std::size_t spelling_bytes_;
};

}  // namespace lowir_model
