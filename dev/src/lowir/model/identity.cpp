#include "lowir/model/program.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lowir_model {

struct StringPoolStorage
{
  std::vector<std::string> strings;
  std::vector<std::uint32_t> slots;
  // Empty until retain_only() creates holes.  A dense byte vector preserves
  // stable IDs without introducing a string-keyed side table.
  std::vector<unsigned char> retained;
  std::size_t spelling_bytes;
  std::size_t retained_count;
  bool sealed;

  StringPoolStorage()
    : slots(32, 0), spelling_bytes(0), retained_count(0), sealed(false)
  {
    strings.push_back(std::string());
  }
};

namespace {

const std::uint32_t kPresentationKindMask = 0xc0000000U;
const std::uint32_t kPresentationPayloadMask = 0x3fffffffU;
const std::uint32_t kPresentationPreserveCopy = 0x40000000U;
const std::uint32_t kPresentationGeneratedValue = 0x80000000U;
const std::uint32_t kPresentationFixedValue = 0xc0000000U;

}  // namespace

PresentationName::PresentationName() : encoded_(kInvalidCompactId) {}
PresentationName::PresentationName(std::uint32_t encoded) : encoded_(encoded) {}

PresentationName PresentationName::pooled(StringId spelling,
                                           bool preserve_copy)
{
  const std::uint32_t id = spelling;
  if(!spelling.valid() || id > kPresentationPayloadMask)
    ThrowLowirInternalError("invalid pooled presentation identity");
  return PresentationName(
    id | (preserve_copy ? kPresentationPreserveCopy : 0));
}

PresentationName PresentationName::generated_value(std::uint32_t ordinal)
{
  if(ordinal > kPresentationPayloadMask)
    ThrowLowirResourceLimit("too many generated presentation identities");
  return PresentationName(kPresentationGeneratedValue | ordinal);
}

PresentationName PresentationName::fixed(FixedPresentationName name)
{
  const std::uint32_t id = static_cast<std::uint32_t>(name);
  if(id > static_cast<std::uint32_t>(FPN_PHI_CYCLE_SCRATCH))
    ThrowLowirInternalError("invalid fixed presentation identity");
  return PresentationName(kPresentationFixedValue | id);
}

bool PresentationName::valid() const
{
  return encoded_ != kInvalidCompactId;
}

bool PresentationName::generated() const
{
  return valid() &&
    (encoded_ & kPresentationKindMask) == kPresentationGeneratedValue;
}

bool PresentationName::fixed() const
{
  return valid() &&
    (encoded_ & kPresentationKindMask) == kPresentationFixedValue;
}

bool PresentationName::preserves_copy() const
{
  return valid() &&
    (encoded_ & kPresentationKindMask) == kPresentationPreserveCopy;
}

StringId PresentationName::spelling() const
{
  if(!valid() || generated() || fixed())
    ThrowLowirInternalError("presentation identity has no pooled spelling");
  return StringId(encoded_ & kPresentationPayloadMask);
}

std::uint32_t PresentationName::generated_ordinal() const
{
  if(!generated())
    ThrowLowirInternalError("presentation identity has no generated ordinal");
  return encoded_ & kPresentationPayloadMask;
}

FixedPresentationName PresentationName::fixed_name() const
{
  if(!fixed())
    ThrowLowirInternalError("presentation identity has no fixed name");
  const std::uint32_t id = encoded_ & kPresentationPayloadMask;
  if(id > static_cast<std::uint32_t>(FPN_PHI_CYCLE_SCRATCH))
    ThrowLowirInternalError("invalid fixed presentation identity");
  return static_cast<FixedPresentationName>(id);
}

const char * fixed_presentation_name_text(FixedPresentationName name)
{
  switch(name) {
  case FPN_VA_REGISTER_SAVE: return "%va-register-save";
  case FPN_HOST_EH_EXCEPTION: return "%host-eh-exception";
  case FPN_HOST_EH_SELECTOR: return "%host-eh-selector";
  case FPN_XMM_CALL_SCRATCH: return "%xmm-call-scratch";
  case FPN_F80_RETURN: return "%f80-return";
  case FPN_PHI_CYCLE_SCRATCH: return "%phi-cycle-scratch";
  }
  ThrowLowirInternalError("invalid fixed presentation identity");
}

namespace {

std::uint64_t generated_name_reservation_key(
    GeneratedNameReservationKind kind, std::uint32_t ordinal)
{
  return static_cast<std::uint64_t>(kind) << 32 | ordinal;
}

}  // namespace

GeneratedNameReservations::GeneratedNameReservations()
{
  std::fill(first_available_, first_available_ + GNR_KIND_COUNT, 0);
}

void GeneratedNameReservations::clear()
{
  entries_.clear();
  std::fill(first_available_, first_available_ + GNR_KIND_COUNT, 0);
}

void GeneratedNameReservations::append(GeneratedNameReservationKind kind,
                                        std::uint32_t ordinal)
{
  entries_.push_back(generated_name_reservation_key(kind, ordinal));
}

void GeneratedNameReservations::normalize()
{
  std::sort(entries_.begin(), entries_.end());
  entries_.erase(std::unique(entries_.begin(), entries_.end()), entries_.end());
  std::fill(first_available_, first_available_ + GNR_KIND_COUNT, 0);
  for(std::size_t kind = 0; kind < GNR_KIND_COUNT; ++kind)
    update_first_available(static_cast<GeneratedNameReservationKind>(kind));
}

bool GeneratedNameReservations::contains(
    GeneratedNameReservationKind kind, std::uint32_t ordinal) const
{
  return std::binary_search(entries_.begin(), entries_.end(),
    generated_name_reservation_key(kind, ordinal));
}

void GeneratedNameReservations::reserve(
    GeneratedNameReservationKind kind, std::uint32_t ordinal)
{
  const std::uint64_t key = generated_name_reservation_key(kind, ordinal);
  const std::vector<std::uint64_t>::iterator position =
    std::lower_bound(entries_.begin(), entries_.end(), key);
  if(position == entries_.end() || *position != key)
    entries_.insert(position, key);
  if(ordinal == first_available_[kind]) update_first_available(kind);
}

void GeneratedNameReservations::merge_kind(
    const GeneratedNameReservations & source,
    GeneratedNameReservationKind kind)
{
  const std::uint64_t first = generated_name_reservation_key(kind, 0);
  const std::uint64_t last = first | UINT64_C(0xffffffff);
  std::vector<std::uint64_t>::const_iterator at =
    std::lower_bound(source.entries_.begin(), source.entries_.end(), first);
  const std::vector<std::uint64_t>::const_iterator end =
    std::upper_bound(source.entries_.begin(), source.entries_.end(), last);
  for(; at != end; ++at)
  {
    const std::vector<std::uint64_t>::iterator position =
      std::lower_bound(entries_.begin(), entries_.end(), *at);
    if(position == entries_.end() || *position != *at)
      entries_.insert(position, *at);
  }
  update_first_available(kind);
}

void GeneratedNameReservations::update_first_available(
    GeneratedNameReservationKind kind)
{
  std::uint32_t & next = first_available_[kind];
  while(next != std::numeric_limits<std::uint32_t>::max() &&
        contains(kind, next))
    ++next;
}

std::uint32_t GeneratedNameReservations::claim_first_available(
    GeneratedNameReservationKind kind)
{
  const std::uint32_t result = first_available_[kind];
  if(result == std::numeric_limits<std::uint32_t>::max())
    ThrowLowirResourceLimit("too many generated presentation identities");
  reserve(kind, result);
  return result;
}

std::size_t GeneratedNameReservations::size() const
{
  return entries_.size();
}

std::size_t GeneratedNameReservations::storage_bytes() const
{
  return entries_.capacity() * sizeof(std::uint64_t);
}

bool parse_generated_name_ordinal(const std::string & name,
                                  const char * prefix,
                                  std::uint32_t * ordinal)
{
  if(!prefix || !ordinal) return false;
  const std::size_t prefix_size = std::char_traits<char>::length(prefix);
  if(name.size() <= prefix_size ||
     name.compare(0, prefix_size, prefix) != 0)
    return false;
  if(name[prefix_size] == '0' && name.size() != prefix_size + 1)
    return false;
  std::uint32_t value = 0;
  for(std::size_t i = prefix_size; i < name.size(); ++i) {
    if(name[i] < '0' || name[i] > '9') return false;
    const std::uint32_t digit = static_cast<std::uint32_t>(name[i] - '0');
    if(value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  *ordinal = value;
  return true;
}

void collect_o1_site_reservations(
    const std::string & name, GeneratedNameReservations * reservations)
{
  if(!reservations) return;
  const char marker[] = "__o1inl";
  const std::size_t marker_size = sizeof(marker) - 1;
  std::size_t at = name.find(marker);
  while(at != std::string::npos) {
    std::size_t end = at + marker_size;
    const std::size_t first_digit = end;
    std::uint32_t value = 0;
    bool overflow = false;
    while(end < name.size() && name[end] >= '0' && name[end] <= '9') {
      const std::uint32_t digit =
        static_cast<std::uint32_t>(name[end] - '0');
      if(value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
        overflow = true;
      else if(!overflow) value = value * 10 + digit;
      ++end;
    }
    if(!overflow && end != first_digit && name.compare(end, 2, "__") == 0)
      reservations->append(GNR_O1_SITE, value);
    at = name.find(marker, at + marker_size);
  }
}

namespace {

std::string unsuffixed_floating_literal(const std::string & text)
{
  if(!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                       text.back() == 'l' || text.back() == 'L'))
    return text.substr(0, text.size() - 1);
  return text;
}

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

bool parse_lowir_integer_literal(const std::string & text,
                                 long long * low, std::uint64_t * high)
{
  if(!low || !high) return false;
  if(text == "nullptr") {
    *low = 0;
    *high = 0;
    return true;
  }
  if(text.empty()) return false;
  std::size_t at = 0;
  bool negative = false;
  if(text[at] == '+' || text[at] == '-') {
    negative = text[at] == '-';
    if(++at == text.size()) return false;
  }
  unsigned base = 10;
  if(at + 2 <= text.size() && text[at] == '0' &&
     (text[at + 1] == 'x' || text[at + 1] == 'X')) {
    base = 16;
    at += 2;
  } else if(at + 1 < text.size() && text[at] == '0') {
    base = 8;
    ++at;
  }
  typedef unsigned __int128 WideUnsigned;
  WideUnsigned value = 0;
  for(; at < text.size(); ++at) {
    unsigned digit;
    if(text[at] >= '0' && text[at] <= '9')
      digit = static_cast<unsigned>(text[at] - '0');
    else if(text[at] >= 'a' && text[at] <= 'f')
      digit = static_cast<unsigned>(text[at] - 'a' + 10);
    else if(text[at] >= 'A' && text[at] <= 'F')
      digit = static_cast<unsigned>(text[at] - 'A' + 10);
    else return false;
    if(digit >= base) return false;
    value = value * base + digit;
  }
  if(negative) value = -value;
  *low = static_cast<long long>(static_cast<std::uint64_t>(value));
  *high = static_cast<std::uint64_t>(value >> 64);
  return true;
}

LowType lowir_floating_literal_type(const std::string & text)
{
  if(!text.empty() && (text.back() == 'f' || text.back() == 'F'))
    return builtin_lowir_type(LTK_F32);
  if(!text.empty() && (text.back() == 'l' || text.back() == 'L'))
    return builtin_lowir_type(LTK_F80);
  return builtin_lowir_type(LTK_F64);
}

namespace
{
std::size_t floating_bit_parse_calls = 0;
}

std::size_t floating_literal_parse_calls()
{
	return floating_bit_parse_calls;
}

bool parse_lowir_floating_literal_bits(const std::string & text,
                                       const LowType & type,
                                       std::uint64_t * low,
                                       std::uint64_t * high)
{
  ++floating_bit_parse_calls;
  if(!low || !high) return false;
  const std::string number = unsuffixed_floating_literal(text);
  *low = 0;
  *high = 0;
  if(number == "snan" && type.kind == LTK_F32) {
    *low = UINT64_C(0x7fa00000);
    return true;
  }
  if(number == "snan" && type.kind == LTK_F64) {
    *low = UINT64_C(0x7ff4000000000000);
    return true;
  }
  if(number == "snan" && type.kind == LTK_F80) {
    *low = UINT64_C(0xa000000000000000);
    *high = UINT64_C(0x7fff);
    return true;
  }
  errno = 0;
  char * end = 0;
  if(type.kind == LTK_F32) {
    const float value = std::strtof(number.c_str(), &end);
    if(errno || !end || *end) return false;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    *low = bits;
    return true;
  }
  if(type.kind == LTK_F64) {
    const double value = std::strtod(number.c_str(), &end);
    if(errno || !end || *end) return false;
    std::memcpy(low, &value, sizeof(value));
    return true;
  }
  if(type.kind != LTK_F80) return false;
  const long double value = std::strtold(number.c_str(), &end);
  if(errno || !end || *end) return false;
  lowir_floating_value_bits(value, type, low, high);
  return true;
}

void lowir_floating_value_bits(long double value, const LowType & type,
                               std::uint64_t * low,
                               std::uint64_t * high)
{
  if(!low || !high) return;
  *low = 0;
  *high = 0;
  if(type.kind == LTK_F32) {
    const float narrowed = static_cast<float>(value);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &narrowed, sizeof(bits));
    *low = bits;
  } else if(type.kind == LTK_F64) {
    const double narrowed = static_cast<double>(value);
    std::memcpy(low, &narrowed, sizeof(narrowed));
  } else if(type.kind == LTK_F80) {
    unsigned char bytes[16] = {};
    unsigned char native[sizeof(long double)] = {};
    std::memcpy(native, &value, sizeof(value));
    const std::size_t payload = std::min<std::size_t>(10, sizeof(value));
    std::copy(native, native + payload, bytes);
    std::memcpy(low, bytes, 8);
    std::memcpy(high, bytes + 8, 8);
  }
}

long double lowir_floating_value(std::uint64_t low, std::uint64_t high,
                                 const LowType & type)
{
  if(type.kind == LTK_F32) {
    const std::uint32_t bits = static_cast<std::uint32_t>(low);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  if(type.kind == LTK_F64) {
    double value = 0;
    std::memcpy(&value, &low, sizeof(value));
    return value;
  }
  if(type.kind != LTK_F80)
    ThrowLowirInternalError("non-floating LowIR literal type");
  unsigned char native[sizeof(long double)] = {};
  const std::size_t payload = std::min<std::size_t>(10, sizeof(long double));
  unsigned char bytes[16] = {};
  std::memcpy(bytes, &low, 8);
  std::memcpy(bytes + 8, &high, 8);
  std::copy(bytes, bytes + payload, native);
  long double value = 0;
  std::memcpy(&value, native, sizeof(value));
  return value;
}

namespace {

std::string generated_lowir_literal_text(const Operand & operand)
{
  if(operand.kind == Operand::OP_INTEGER) {
    return std::to_string(operand.int_value);
  }
  if(operand.kind != Operand::OP_FLOAT || !operand.has_float_bits)
    ThrowLowirInternalError("LowIR operand has no literal presentation");
  const long double value = lowir_floating_value(
    operand.literal_low, operand.literal_high, operand.literal_type);
  if(std::isinf(value)) return value < 0 ? "-inf" : "inf";
  if(std::isnan(value)) return "nan";
  std::ostringstream text;
  text.precision(20);
  text << value;
  if(operand.literal_type.kind == LTK_F32) text << 'f';
  else if(operand.literal_type.kind == LTK_F80) text << 'L';
  return text.str();
}

}  // namespace

std::string lowir_literal_text(const Operand & operand,
                               const StringPool * strings)
{
  if(operand.has_spelling) {
    if(!strings) ThrowLowirInternalError("literal spelling has no string pool");
    return strings->get(operand.literal);
  }
  return generated_lowir_literal_text(operand);
}

std::string lowir_literal_text(const Operand & operand,
                               const SealedStringPool & strings)
{
  if(operand.has_spelling) return strings.get(operand.literal);
  return generated_lowir_literal_text(operand);
}

StringPool::StringPool() : storage_(new StringPoolStorage)
{
}

StringPool::StringPool(const StringPool & other)
  : storage_(new StringPoolStorage)
{
  storage_->strings = other.storage_->strings;
  storage_->retained = other.storage_->retained;
  storage_->spelling_bytes = other.storage_->spelling_bytes;
  storage_->retained_count = other.storage_->retained_count;
  std::size_t capacity = 32;
  while(storage_->retained_count + 1 > capacity * 7 / 10) capacity *= 2;
  storage_->slots.assign(capacity, 0);
  rehash(capacity);
}

StringPool & StringPool::operator=(const StringPool & other)
{
  if(this == &other) return *this;
  StringPool replacement(other);
  storage_.swap(replacement.storage_);
  return *this;
}

StringPool::StringPool(StringPool && other) noexcept
  : storage_(std::move(other.storage_))
{
}

StringPool & StringPool::operator=(StringPool && other) noexcept
{
  if(this != &other) storage_ = std::move(other.storage_);
  return *this;
}

StringId StringPool::intern(const std::string & text, StringPoolStats * stats)
{
  return intern_range(text, 0, text.size(), stats);
}

StringId StringPool::intern_range(const std::string & text, std::size_t first,
                                  std::size_t count, StringPoolStats * stats)
{
  if(storage_->sealed)
    ThrowLowirInternalError("cannot mutate sealed LowIR presentation storage");
  if(first > text.size() || count > text.size() - first)
    ThrowLowirInternalError("invalid LowIR string-pool range");
  if(stats) {
    ++stats->intern_calls;
    stats->hash_bytes += count;
  }
  if((storage_->retained_count + 2) * 10 > storage_->slots.size() * 7)
    rehash(storage_->slots.size() * 2);
  const std::size_t mask = storage_->slots.size() - 1;
  std::size_t slot = hash_range(text, first, count) & mask;
  while(storage_->slots[slot] != 0) {
    if(stats) ++stats->slot_probes;
    const std::uint32_t id = storage_->slots[slot];
    if(storage_->strings[id].size() == count &&
       text.compare(first, count, storage_->strings[id]) == 0) {
      if(stats) ++stats->intern_hits;
      return StringId(id);
    }
    slot = (slot + 1) & mask;
  }
  if(storage_->strings.size() >= kInvalidCompactId)
    ThrowLowirResourceLimit("too many pooled LowIR strings");
  const std::uint32_t id = static_cast<std::uint32_t>(storage_->strings.size());
  storage_->strings.push_back(text.substr(first, count));
  if(!storage_->retained.empty()) storage_->retained.push_back(1);
  storage_->spelling_bytes += count;
  ++storage_->retained_count;
  storage_->slots[slot] = id;
  if(stats) ++stats->intern_misses;
  return StringId(id);
}

void StringPool::reserve(std::size_t expected_strings)
{
  if(storage_->sealed)
    ThrowLowirInternalError("cannot mutate sealed LowIR presentation storage");
  if(expected_strings >= kInvalidCompactId)
    expected_strings = kInvalidCompactId - 1;
  storage_->strings.reserve(expected_strings + 1);
  if(!storage_->retained.empty()) storage_->retained.reserve(expected_strings + 1);
  std::size_t capacity = storage_->slots.size();
  while(expected_strings + 1 > capacity * 7 / 10) capacity *= 2;
  if(capacity > storage_->slots.size()) rehash(capacity);
}

void StringPool::retain_only(const std::vector<unsigned char> & retained)
{
  if(storage_->sealed)
    ThrowLowirInternalError("cannot prune sealed LowIR presentation storage");
  if(retained.size() != storage_->strings.size())
    ThrowLowirInternalError("invalid LowIR presentation retention mask");
  const std::vector<unsigned char> previously_retained = storage_->retained;
  storage_->retained.assign(storage_->strings.size(), 0);
  storage_->retained_count = 0;
  storage_->spelling_bytes = 0;
  for(std::size_t id = 1; id < storage_->strings.size(); ++id) {
    if(retained[id] &&
       (previously_retained.empty() || previously_retained[id])) {
      storage_->retained[id] = 1;
      ++storage_->retained_count;
      storage_->spelling_bytes += storage_->strings[id].size();
    } else std::string().swap(storage_->strings[id]);
  }
  std::size_t capacity = 32;
  while(storage_->retained_count + 1 > capacity * 7 / 10) capacity *= 2;
  rehash(capacity);
}

const std::string & StringPool::get(StringId id) const
{
  const std::uint32_t index = id;
  if(index >= storage_->strings.size() ||
     (!storage_->retained.empty() && !storage_->retained[index]))
    ThrowLowirInternalError("invalid pooled LowIR string identity " +
      std::to_string(index) + " for " +
      std::to_string(storage_->strings.size()) +
      " entries");
  return storage_->strings[index];
}

std::size_t StringPool::size() const { return storage_->strings.size() - 1; }
std::size_t StringPool::retained_size() const
  { return storage_->retained_count; }
std::size_t StringPool::spelling_bytes() const
  { return storage_->spelling_bytes; }

std::size_t StringPool::storage_bytes() const
{
  std::size_t bytes = storage_->strings.capacity() * sizeof(std::string) +
    storage_->slots.capacity() * sizeof(std::uint32_t) +
    storage_->retained.capacity() * sizeof(unsigned char);
  for(std::size_t i = 1; i < storage_->strings.size(); ++i)
    if(storage_->retained.empty() || storage_->retained[i])
      bytes += storage_->strings[i].capacity();
  return bytes;
}

SealedStringPool StringPool::seal() const
{
  storage_->sealed = true;
  std::vector<std::uint32_t>().swap(storage_->slots);
  return SealedStringPool(storage_);
}

bool StringPool::sealed() const { return storage_->sealed; }

void StringPool::rehash(std::size_t capacity)
{
  std::vector<std::uint32_t> replacement(capacity, 0);
  const std::size_t mask = capacity - 1;
  for(std::uint32_t id = 1; id < storage_->strings.size(); ++id) {
    if(!storage_->retained.empty() && !storage_->retained[id]) continue;
    std::size_t slot = hash_range(storage_->strings[id], 0,
                                  storage_->strings[id].size()) & mask;
    while(replacement[slot] != 0) slot = (slot + 1) & mask;
    replacement[slot] = id;
  }
  storage_->slots.swap(replacement);
}

SealedStringPool::SealedStringPool()
{
}

SealedStringPool::SealedStringPool(
    const std::shared_ptr<const StringPoolStorage> & storage)
  : storage_(storage)
{
}

const std::string & SealedStringPool::get(StringId id) const
{
  if(!storage_)
    ThrowLowirInternalError("missing sealed LowIR presentation storage");
  const std::uint32_t index = id;
  if(index >= storage_->strings.size() ||
     (!storage_->retained.empty() && !storage_->retained[index]))
    ThrowLowirInternalError("invalid sealed LowIR string identity " +
      std::to_string(index) + " for " +
      std::to_string(storage_->strings.size()) + " entries");
  return storage_->strings[index];
}

std::size_t SealedStringPool::size() const
{
  return storage_ ? storage_->strings.size() - 1 : 0;
}

std::size_t SealedStringPool::retained_size() const
{
  return storage_ ? storage_->retained_count : 0;
}

std::size_t SealedStringPool::spelling_bytes() const
{
  return storage_ ? storage_->spelling_bytes : 0;
}

std::size_t SealedStringPool::storage_bytes() const
{
  if(!storage_) return 0;
  std::size_t bytes = storage_->strings.capacity() * sizeof(std::string);
  for(std::size_t i = 1; i < storage_->strings.size(); ++i)
    if(storage_->retained.empty() || storage_->retained[i])
      bytes += storage_->strings[i].capacity();
  bytes += storage_->retained.capacity() * sizeof(unsigned char);
  return bytes;
}

bool SealedStringPool::valid() const { return static_cast<bool>(storage_); }

BlockId allocate_lowir_block_id(Function & function, StringId label)
{
  if(function.next_block_id == kInvalidCompactId)
    ThrowLowirResourceLimit("too many LowIR blocks");
  const BlockId result(function.next_block_id++);
  function.block_labels.push_back(label);
  function.block_presentation_order.push_back(result);
  return result;
}

const std::string & lowir_block_label(const StringPool & strings,
                                      const Function & function,
                                      BlockId block)
{
  const std::uint32_t id = block;
  if(id >= function.block_labels.size())
    ThrowLowirInternalError("invalid LowIR block identity");
  return strings.get(function.block_labels[id]);
}

SlotId append_lowir_slot(Function & function, StringId name,
                         const LowType & type)
{
  if(function.slot_names.size() == kInvalidCompactId)
    ThrowLowirResourceLimit("too many LowIR slots");
  const SlotId result(static_cast<std::uint32_t>(function.slot_names.size()));
  function.slot_names.push_back(name);
  function.slot_types.push_back(type);
  function.slot_parameter_values.push_back(ValueId());
  function.slots.push_back(result);
  return result;
}

const std::string & lowir_slot_name(const StringPool & strings,
                                    const Function & function, SlotId slot)
{
  const std::uint32_t id = slot;
  if(id >= function.slot_names.size())
    ThrowLowirInternalError("invalid LowIR slot identity");
  return strings.get(function.slot_names[id]);
}

const LowType & lowir_slot_type(const Function & function, SlotId slot)
{
  const std::uint32_t id = slot;
  if(id >= function.slot_types.size())
    ThrowLowirInternalError("invalid LowIR slot identity");
  return function.slot_types[id];
}

namespace {

ValueId append_value_identity(Function & function, PresentationName name,
                              const LowType & type)
{
  if(function.value_names.size() == kInvalidCompactId)
    ThrowLowirResourceLimit("too many LowIR values");
  const ValueId result(static_cast<std::uint32_t>(function.value_names.size()));
  function.value_names.push_back(name);
  function.value_types.push_back(type);
  return result;
}

}  // namespace

ValueId append_lowir_value(Function & function, StringId name,
                           const LowType & type, bool preserve_copy)
{
  if(!name.valid()) ThrowLowirInternalError("empty named LowIR value");
  return append_value_identity(
    function, PresentationName::pooled(name, preserve_copy), type);
}

ValueId append_lowir_unnamed_value(Function & function,
                                   const LowType & type)
{
  return append_value_identity(function, PresentationName(), type);
}

ValueId append_lowir_generated_value(Function & function,
                                     std::uint32_t ordinal,
                                     const LowType & type)
{
  function.generated_name_reservations.reserve(
    GNR_GENERATED_VALUE, ordinal);
  return append_value_identity(
    function, PresentationName::generated_value(ordinal), type);
}

ValueId append_lowir_fresh_generated_value(Function & function,
                                           const LowType & type)
{
  const std::uint32_t ordinal =
    function.generated_name_reservations.claim_first_available(
      GNR_GENERATED_VALUE);
  return append_lowir_generated_value(function, ordinal, type);
}

std::string lowir_value_name(const StringPool & strings,
                             const Function & function, ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_names.size())
    ThrowLowirInternalError("invalid LowIR value identity");
  const PresentationName name = function.value_names[id];
  if(!name.valid())
    ThrowLowirInternalError("LowIR value has no presentation identity");
  if(!name.generated()) return strings.get(name.spelling());
  return "t" + std::to_string(name.generated_ordinal());
}

const LowType & lowir_value_type(const Function & function, ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_types.size())
    ThrowLowirInternalError("invalid LowIR value type identity");
  return function.value_types[id];
}

bool lowir_value_preserves_copy(const Function & function, ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_names.size())
    ThrowLowirInternalError("invalid LowIR value identity");
  return function.value_names[id].preserves_copy();
}

PresentationName lowir_value_presentation(const Function & function,
                                          ValueId value)
{
  const std::uint32_t id = value;
  if(id >= function.value_names.size())
    ThrowLowirInternalError("invalid LowIR value identity");
  return function.value_names[id];
}

const std::string & lowir_parameter_name(const Program & program,
                                         const Parameter & parameter)
{
  if(!parameter.name.valid())
    ThrowLowirInternalError("LowIR parameter has no presentation identity");
  return program.strings.get(parameter.name);
}

namespace {

void append_ordinal_reservation(const std::string & name,
                                const char * prefix,
                                GeneratedNameReservationKind kind,
                                GeneratedNameReservations * reservations)
{
  std::uint32_t ordinal = 0;
  if(parse_generated_name_ordinal(name, prefix, &ordinal))
    reservations->append(kind, ordinal);
}

void classify_lowir_value_name(const std::string & name,
                               GeneratedNameReservations * reservations)
{
  append_ordinal_reservation(
    name, "t", GNR_GENERATED_VALUE, reservations);
  collect_o1_site_reservations(name, reservations);
  append_ordinal_reservation(name, "__force_inline_parameter_",
    GNR_FORCE_PARAMETER, reservations);
  append_ordinal_reservation(name, "__force_inline_temporary_",
    GNR_FORCE_TEMPORARY, reservations);
}

}  // namespace

void classify_lowir_generated_name_reservations(
    Function & function, const StringPool & strings)
{
  GeneratedNameReservations & reservations =
    function.generated_name_reservations;
  reservations.clear();
  for(std::size_t i = 0; i < function.params.size(); ++i)
    classify_lowir_value_name(strings.get(function.params[i].name),
      &reservations);
  for(std::size_t i = 0; i < function.slots.size(); ++i) {
    const std::string & name =
      lowir_slot_name(strings, function, function.slots[i]);
    collect_o1_site_reservations(name, &reservations);
    append_ordinal_reservation(name, "__force_inline_local_",
      GNR_FORCE_LOCAL, &reservations);
    append_ordinal_reservation(name, "__force_inline_result_",
      GNR_FORCE_RESULT, &reservations);
    append_ordinal_reservation(name, "retmerge__",
      GNR_O1_SCALAR_MERGE_SUFFIX, &reservations);
    append_ordinal_reservation(name, "retmergeobj__",
      GNR_O1_OBJECT_MERGE_SUFFIX, &reservations);
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const Block & block = function.blocks[i];
    const std::string & label =
      lowir_block_label(strings, function, block.id);
    collect_o1_site_reservations(label, &reservations);
    append_ordinal_reservation(label, "__force_inline_block_",
      GNR_FORCE_BLOCK, &reservations);
    append_ordinal_reservation(label, "__force_inline_prologue_",
      GNR_FORCE_PROLOGUE, &reservations);
    append_ordinal_reservation(label, "__force_inline_continuation_",
      GNR_FORCE_CONTINUATION, &reservations);
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const ValueId dest = block.instructions[j].dest;
      if(!dest.valid()) continue;
      const PresentationName presentation =
        lowir_value_presentation(function, dest);
      if(presentation.generated())
        reservations.append(
          GNR_GENERATED_VALUE, presentation.generated_ordinal());
      else if(!presentation.fixed())
        classify_lowir_value_name(strings.get(presentation.spelling()),
          &reservations);
    }
  }
  reservations.normalize();
}

void compute_lowir_block_presentation_order(
    Function & function, const StringPool & strings)
{
  std::vector<BlockId> order;
  order.reserve(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    order.push_back(function.blocks[i].id);
  std::sort(order.begin(), order.end(),
    [&function, &strings](BlockId left, BlockId right) {
      return lowir_block_label(strings, function, left) <
             lowir_block_label(strings, function, right);
    });
  function.block_presentation_order.resize(function.next_block_id);
  for(std::size_t rank = 0; rank < order.size(); ++rank)
    function.block_presentation_order[order[rank]] =
      static_cast<std::uint32_t>(rank);
}

SymbolId append_lowir_symbol(Program & program, const std::string & name)
{
  return append_lowir_symbol(program, program.strings.intern(name));
}

SymbolId append_lowir_symbol(Program & program, StringId name)
{
  if(program.symbol_names.size() == kInvalidCompactId)
    ThrowLowirResourceLimit("too many LowIR symbols");
  if(!name.valid()) ThrowLowirInternalError("empty LowIR symbol presentation");
  const SymbolId result(
    static_cast<std::uint32_t>(program.symbol_names.size()));
  program.symbol_names.push_back(name);
  return result;
}

StringId lowir_symbol_spelling(const Program & program, SymbolId symbol)
{
  const std::uint32_t id = symbol;
  if(id >= program.symbol_names.size())
    ThrowLowirInternalError("invalid LowIR symbol identity");
  return program.symbol_names[id];
}

const std::string & lowir_symbol_name(const Program & program, SymbolId symbol)
{
  return program.strings.get(lowir_symbol_spelling(program, symbol));
}

void resolve_lowir_function_operands(Function & function,
                                     const StringPool & strings)
{
  std::unordered_map<std::string, BlockId> blocks;
  blocks.reserve(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    Block & block = function.blocks[i];
    if(!block.id.valid()) block.id = allocate_lowir_block_id(function);
    blocks.emplace(lowir_block_label(strings, function, block.id), block.id);
  }
  std::unordered_map<std::string, SlotId> slots;
  slots.reserve(function.slots.size());
  for(std::size_t i = 0; i < function.slots.size(); ++i)
    slots.emplace(lowir_slot_name(strings, function, function.slots[i]),
                  function.slots[i]);
  std::unordered_map<std::string, ValueId> values;
  values.reserve(function.value_names.size());
  for(std::size_t i = 0; i < function.value_names.size(); ++i) {
    const ValueId value(static_cast<std::uint32_t>(i));
    values.emplace(lowir_value_name(strings, function, value), value);
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    std::vector<Instruction> & instructions = function.blocks[i].instructions;
    for(std::size_t j = 0; j < instructions.size(); ++j) {
      Instruction & instruction = instructions[j];
      Operand * fixed[] = {
        &instruction.first, &instruction.second, &instruction.third
      };
      for(std::size_t k = 0; k < 3; ++k) {
        if(!fixed[k]->has_spelling) continue;
        const std::string & spelling = strings.get(fixed[k]->literal);
        if(fixed[k]->kind == Operand::OP_LABEL) {
          const std::unordered_map<std::string, BlockId>::const_iterator found =
            blocks.find(spelling);
          if(found == blocks.end()) ThrowLowirInputError("undefined block target");
          fixed[k]->block = found->second;
        } else if(fixed[k]->kind == Operand::OP_SLOT) {
          const std::unordered_map<std::string, SlotId>::const_iterator found =
            slots.find(spelling);
          if(found == slots.end()) ThrowLowirInputError("undefined slot operand");
          fixed[k]->slot = found->second;
        } else if(fixed[k]->kind == Operand::OP_TEMP) {
          const std::unordered_map<std::string, ValueId>::const_iterator found =
            values.find(spelling);
          if(found == values.end()) ThrowLowirInputError("undefined value operand");
          fixed[k]->value = found->second;
        } else continue;
        fixed[k]->has_spelling = false;
      }
      for(std::size_t a = 0; a < instruction.args.size(); ++a) {
        Operand & operand = instruction.args[a];
        if(!operand.has_spelling) continue;
        const std::string & spelling = strings.get(operand.literal);
        if(operand.kind == Operand::OP_LABEL) {
          const std::unordered_map<std::string, BlockId>::const_iterator found =
            blocks.find(spelling);
          if(found == blocks.end()) ThrowLowirInputError("undefined block target");
          operand.block = found->second;
        } else if(operand.kind == Operand::OP_SLOT) {
          const std::unordered_map<std::string, SlotId>::const_iterator found =
            slots.find(spelling);
          if(found == slots.end()) ThrowLowirInputError("undefined slot operand");
          operand.slot = found->second;
        } else if(operand.kind == Operand::OP_TEMP) {
          const std::unordered_map<std::string, ValueId>::const_iterator found =
            values.find(spelling);
          if(found == values.end()) ThrowLowirInputError("undefined value operand");
          operand.value = found->second;
        } else continue;
        operand.has_spelling = false;
      }
    }
  }
  for(std::size_t s = 0; s < function.slots.size(); ++s) {
    const SlotId slot = function.slots[s];
    if(lowir_slot_type(function, slot).kind != LTK_OBJECT) continue;
    const std::string & slot_name = lowir_slot_name(strings, function, slot);
    for(std::size_t p = 0; p < function.params.size(); ++p) {
      const std::string & parameter_name =
        strings.get(function.params[p].name);
      if(slot_name == parameter_name &&
         same_lowir_type(lowir_slot_type(function, slot),
                         function.params[p].type)) {
        function.slot_parameter_values[slot] = function.params[p].value;
        break;
      }
    }
  }
  classify_lowir_generated_name_reservations(function, strings);
  compute_lowir_block_presentation_order(function, strings);
}

void resolve_lowir_program_symbols(Program & program)
{
  // Every spelling has already been interned at the input boundary.  Resolve
  // through that compact identity instead of owning and hashing the text a
  // second time.
  std::vector<SymbolId> symbols(program.strings.size() + 1);
  for(std::size_t i = 0; i < program.symbol_names.size(); ++i) {
    const std::uint32_t spelling = program.symbol_names[i];
    if(spelling >= symbols.size())
      ThrowLowirInternalError("invalid LowIR symbol presentation identity");
    symbols[spelling] = SymbolId(static_cast<std::uint32_t>(i));
  }

  const auto resolve_spelling = [&symbols](StringId spelling,
                                           const char * error) -> SymbolId {
    const std::uint32_t id = spelling;
    if(!spelling.valid() || id >= symbols.size() || !symbols[id].valid())
      ThrowLowirInputError(error);
    return symbols[id];
  };

  const auto resolve_tls = [&program, &resolve_spelling](
      SymbolMetadata & metadata) {
    if(!metadata.tls_for_spelling.valid()) return;
    metadata.tls_for_symbol_id = resolve_spelling(
      metadata.tls_for_spelling, "undefined TLS target");
    metadata.tls_for_spelling = StringId();
  };
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    resolve_tls(program.global_declarations[i].metadata);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    resolve_tls(program.function_declarations[i].metadata);
  for(std::size_t i = 0; i < program.globals.size(); ++i)
    resolve_tls(program.globals[i].metadata);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    resolve_tls(program.functions[i].metadata);

  const auto resolve_operand = [&resolve_spelling](Operand & operand) {
    if(operand.kind != Operand::OP_GLOBAL) return;
    if(!operand.has_spelling) return;
    operand.symbol = resolve_spelling(
      operand.literal, "undefined top-level symbol");
    operand.has_spelling = false;
  };
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    GlobalDefinition & global = program.globals[i];
    resolve_operand(global.init_operand);
    for(std::size_t j = 0; j < global.data_items.size(); ++j) {
      GlobalDefinition::DataItem & item = global.data_items[j];
      resolve_operand(item.literal_operand);
      if(item.kind != GlobalDefinition::DataItem::ITEM_ADDR) continue;
      if(item.symbol_id.valid()) continue;
      item.symbol_id = resolve_spelling(
        item.symbol_spelling, "undefined data symbol");
      item.symbol_spelling = StringId();
    }
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f)
    for(std::size_t b = 0; b < program.functions[f].blocks.size(); ++b) {
      std::vector<Instruction> & instructions =
        program.functions[f].blocks[b].instructions;
      for(std::size_t i = 0; i < instructions.size(); ++i) {
        resolve_operand(instructions[i].first);
        resolve_operand(instructions[i].second);
        resolve_operand(instructions[i].third);
        for(std::size_t a = 0; a < instructions[i].args.size(); ++a)
          resolve_operand(instructions[i].args[a]);
      }
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    if(program.object_aliases[i].target_id.valid()) continue;
    program.object_aliases[i].target_id = resolve_spelling(
      program.object_aliases[i].target_spelling, "undefined alias target");
    program.object_aliases[i].target_spelling = StringId();
  }
}

void remap_lowir_program_strings(Program & program,
                                 StringPool & destination)
{
  const auto remap_string = [&program, &destination](StringId & string) {
    if(string.valid()) string = destination.intern(program.strings.get(string));
  };
  const auto remap = [&program, &destination](Operand & operand) {
    if(!operand.has_spelling) return;
    operand.literal = destination.intern(program.strings.get(operand.literal));
  };
  const auto remap_parameters = [&remap_string](
      std::vector<Parameter> & parameters) {
    for(std::size_t i = 0; i < parameters.size(); ++i)
      remap_string(parameters[i].name);
  };
  for(std::size_t i = 0; i < program.symbol_names.size(); ++i)
    remap_string(program.symbol_names[i]);
  const auto remap_metadata = [&remap_string](SymbolMetadata & metadata) {
    remap_string(metadata.object_symbol);
    remap_string(metadata.tls_for_spelling);
    remap_string(metadata.section_name);
  };
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    remap_metadata(program.global_declarations[i].metadata);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
  {
    remap_metadata(program.function_declarations[i].metadata);
    remap_parameters(program.function_declarations[i].params);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    remap_metadata(program.globals[i].metadata);
    remap(program.globals[i].init_operand);
    for(std::size_t j = 0; j < program.globals[i].data_items.size(); ++j) {
      remap(program.globals[i].data_items[j].literal_operand);
      remap_string(program.globals[i].data_items[j].symbol_spelling);
    }
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f)
  {
    Function & function = program.functions[f];
    remap_metadata(function.metadata);
    remap_parameters(function.params);
    for(std::size_t i = 0; i < function.slot_names.size(); ++i)
      remap_string(function.slot_names[i]);
    for(std::size_t i = 0; i < function.value_names.size(); ++i) {
      PresentationName & name = function.value_names[i];
      if(name.valid() && !name.generated())
        name = PresentationName::pooled(
          destination.intern(program.strings.get(name.spelling())),
          name.preserves_copy());
    }
    for(std::size_t i = 0; i < function.block_labels.size(); ++i)
      if(function.block_labels[i].valid()) remap_string(function.block_labels[i]);
    remap_string(function.debug_location.file);
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0;
          i < function.blocks[b].instructions.size(); ++i) {
        Instruction & instruction =
          function.blocks[b].instructions[i];
        remap_parameters(instruction.call_params);
        remap_string(instruction.debug_location.file);
        remap(instruction.first);
        remap(instruction.second);
        remap(instruction.third);
        for(std::size_t a = 0; a < instruction.args.size(); ++a)
          remap(instruction.args[a]);
      }
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    remap_string(program.object_aliases[i].object_symbol);
    remap_string(program.object_aliases[i].target_spelling);
  }
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i) {
    remap_string(program.exported_symbols[i].object_symbol);
    remap_string(
      program.exported_symbols[i].thread_local_wrapper_object_symbol);
  }
}

void discard_unreferenced_lowir_strings(Program & program)
{
  std::vector<unsigned char> retained(program.strings.size() + 1, 0);
  const auto retain = [&retained](StringId id) {
    if(!id.valid()) return;
    const std::uint32_t index = id;
    if(index >= retained.size())
      ThrowLowirInternalError("invalid retained LowIR presentation identity");
    retained[index] = 1;
  };
  const auto retain_operand = [&retain](const Operand & operand) {
    if(operand.has_spelling) retain(operand.literal);
  };
  const auto retain_parameters = [&retain](
      const std::vector<Parameter> & parameters) {
    for(std::size_t i = 0; i < parameters.size(); ++i)
      retain(parameters[i].name);
  };
  const auto retain_metadata = [&retain](const SymbolMetadata & metadata) {
    retain(metadata.object_symbol);
    retain(metadata.tls_for_spelling);
    retain(metadata.section_name);
  };

  for(std::size_t i = 0; i < program.symbol_names.size(); ++i)
    retain(program.symbol_names[i]);
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    retain_metadata(program.global_declarations[i].metadata);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    retain_metadata(program.function_declarations[i].metadata);
    retain_parameters(program.function_declarations[i].params);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    const GlobalDefinition & global = program.globals[i];
    retain_metadata(global.metadata);
    retain_operand(global.init_operand);
    for(std::size_t j = 0; j < global.data_items.size(); ++j) {
      retain_operand(global.data_items[j].literal_operand);
      retain(global.data_items[j].symbol_spelling);
    }
  }
  for(std::size_t f = 0; f < program.functions.size(); ++f) {
    const Function & function = program.functions[f];
    retain_metadata(function.metadata);
    retain_parameters(function.params);
    for(std::size_t i = 0; i < function.slot_names.size(); ++i)
      retain(function.slot_names[i]);
    for(std::size_t i = 0; i < function.value_names.size(); ++i) {
      const PresentationName name = function.value_names[i];
      if(name.valid() && !name.generated() && !name.fixed())
        retain(name.spelling());
    }
    for(std::size_t i = 0; i < function.block_labels.size(); ++i)
      retain(function.block_labels[i]);
    retain(function.debug_location.file);
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t i = 0;
          i < function.blocks[b].instructions.size(); ++i) {
        const Instruction & instruction = function.blocks[b].instructions[i];
        retain_parameters(instruction.call_params);
        retain(instruction.debug_location.file);
        retain_operand(instruction.first);
        retain_operand(instruction.second);
        retain_operand(instruction.third);
        for(std::size_t a = 0; a < instruction.args.size(); ++a)
          retain_operand(instruction.args[a]);
      }
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    retain(program.object_aliases[i].object_symbol);
    retain(program.object_aliases[i].target_spelling);
  }
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i) {
    retain(program.exported_symbols[i].object_symbol);
    retain(program.exported_symbols[i].thread_local_wrapper_object_symbol);
  }
  program.strings.retain_only(retained);
}

std::size_t lowir_program_storage_bytes(const Program & program)
{
	std::size_t bytes = program.strings.storage_bytes() +
		program.symbol_names.capacity() * sizeof(StringId) +
		program.global_declarations.capacity() * sizeof(GlobalDeclaration) +
		program.globals.capacity() * sizeof(GlobalDefinition) +
		program.function_declarations.capacity() * sizeof(FunctionDeclaration) +
		program.functions.capacity() * sizeof(Function) +
		program.object_aliases.capacity() * sizeof(ObjectAlias) +
		program.exported_symbols.capacity() * sizeof(ExportedSymbol);
	for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
		bytes += program.function_declarations[i].params.capacity() *
			sizeof(Parameter);
	for(std::size_t i = 0; i < program.globals.size(); ++i)
		bytes += program.globals[i].data_items.capacity() *
			sizeof(GlobalDefinition::DataItem);
	for(std::size_t f = 0; f < program.functions.size(); ++f) {
		const Function & function = program.functions[f];
		bytes += function.params.capacity() * sizeof(Parameter) +
			function.slots.capacity() * sizeof(SlotId) +
			function.slot_names.capacity() * sizeof(StringId) +
			function.slot_types.capacity() * sizeof(LowType) +
			function.slot_parameter_values.capacity() * sizeof(ValueId) +
			function.value_names.capacity() * sizeof(PresentationName) +
			function.value_types.capacity() * sizeof(LowType) +
			function.blocks.capacity() * sizeof(Block) +
			function.block_labels.capacity() * sizeof(StringId) +
			function.block_presentation_order.capacity() * sizeof(std::uint32_t) +
			function.generated_name_reservations.storage_bytes();
		for(std::size_t b = 0; b < function.blocks.size(); ++b) {
			const std::vector<Instruction> & instructions =
				function.blocks[b].instructions;
			bytes += instructions.capacity() * sizeof(Instruction);
			for(std::size_t i = 0; i < instructions.size(); ++i)
				bytes += instructions[i].args.capacity() * sizeof(Operand) +
					instructions[i].call_params.capacity() * sizeof(Parameter);
		}
	}
	return bytes;
}

void remap_lowir_program_symbols(
    Program & program, const std::vector<SymbolId> & symbols)
{
  if(symbols.size() != program.symbol_names.size())
    ThrowLowirInternalError("invalid LowIR symbol remap");
  const auto remap_symbol = [&symbols](SymbolId & symbol) {
    if(!symbol.valid() || static_cast<std::uint32_t>(symbol) >= symbols.size())
      ThrowLowirInternalError("invalid LowIR symbol identity during remap");
    symbol = symbols[symbol];
  };
  const auto remap_metadata = [&remap_symbol](SymbolMetadata & metadata) {
    if(metadata.tls_for_symbol_id.valid())
      remap_symbol(metadata.tls_for_symbol_id);
  };
  const auto remap_operand = [&remap_symbol](Operand & operand) {
    if(operand.kind == Operand::OP_GLOBAL && !operand.has_spelling)
      remap_symbol(operand.symbol);
  };
  const auto remap_instruction = [&remap_operand](Instruction & instruction) {
    remap_operand(instruction.first);
    remap_operand(instruction.second);
    remap_operand(instruction.third);
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      remap_operand(instruction.args[i]);
  };
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i) {
    remap_symbol(program.global_declarations[i].symbol);
    remap_metadata(program.global_declarations[i].metadata);
  }
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    remap_symbol(program.function_declarations[i].symbol);
    remap_metadata(program.function_declarations[i].metadata);
  }
  for(std::size_t i = 0; i < program.globals.size(); ++i) {
    GlobalDefinition & global = program.globals[i];
    remap_symbol(global.symbol);
    remap_metadata(global.metadata);
    remap_operand(global.init_operand);
    for(std::size_t j = 0; j < global.data_items.size(); ++j) {
      remap_operand(global.data_items[j].literal_operand);
      if(global.data_items[j].symbol_id.valid())
        remap_symbol(global.data_items[j].symbol_id);
    }
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    Function & function = program.functions[i];
    remap_symbol(function.symbol);
    remap_metadata(function.metadata);
    for(std::size_t b = 0; b < function.blocks.size(); ++b)
      for(std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j)
        remap_instruction(function.blocks[b].instructions[j]);
  }
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i)
    remap_symbol(program.object_aliases[i].target_id);
  for(std::size_t i = 0; i < program.exported_symbols.size(); ++i)
    remap_symbol(program.exported_symbols[i].internal_symbol);
}

}  // namespace lowir_model
