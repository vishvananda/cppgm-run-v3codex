#include "lowir_opt.h"
#include "lowir_cleanup_o1.h"
#include "lowir_inline_o1.h"
#include "lowir_float_literal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Block;
using lowir_model::Function;
using lowir_model::FunctionBoundaryMetadata;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::LowTypeKind;
using lowir_model::LowirProgram;
using lowir_model::Operand;

typedef std::unordered_map<std::string, std::size_t> BlockIndex;
const std::size_t kNoBlock = static_cast<std::size_t>(-1);

class PassArena
{
public:
  PassArena() : inline_used_(0), block_count_(0), next_block_size_(4096) {}
  PassArena(const PassArena &) = delete;
  PassArena & operator=(const PassArena &) = delete;

  ~PassArena()
  {
    for(std::size_t i = 0; i < block_count_; ++i)
      ::operator delete(inline_blocks_[i].storage);
    for(std::size_t i = 0; i < overflow_blocks_.size(); ++i)
      ::operator delete(overflow_blocks_[i].storage);
  }

  void * allocate(std::size_t bytes, std::size_t alignment)
  {
    void * result = allocate_from(inline_storage_, sizeof(inline_storage_),
                                  &inline_used_, bytes, alignment);
    if(result) return result;
    if(block_count_ != 0 || !overflow_blocks_.empty()) {
      Block & block = overflow_blocks_.empty() ?
        inline_blocks_[block_count_ - 1] : overflow_blocks_.back();
      result = allocate_from(block.storage, block.capacity, &block.used,
                             bytes, alignment);
      if(result) return result;
    }
    std::size_t capacity = next_block_size_;
    if(bytes > std::numeric_limits<std::size_t>::max() - alignment + 1)
      throw std::bad_alloc();
    const std::size_t required = bytes + alignment - 1;
    if(capacity < required) capacity = required;
    Block block;
    block.storage = static_cast<unsigned char *>(::operator new(capacity));
    block.capacity = capacity;
    block.used = 0;
    Block * stored;
    if(block_count_ != sizeof(inline_blocks_) / sizeof(inline_blocks_[0])) {
      inline_blocks_[block_count_++] = block;
      stored = &inline_blocks_[block_count_ - 1];
    } else {
      try {
        overflow_blocks_.push_back(block);
      } catch(...) {
        ::operator delete(block.storage);
        throw;
      }
      stored = &overflow_blocks_.back();
    }
    if(next_block_size_ < 1024 * 1024) next_block_size_ *= 2;
    return allocate_from(stored->storage, capacity, &stored->used,
                         bytes, alignment);
  }

private:
  struct Block
  {
    unsigned char * storage;
    std::size_t capacity;
    std::size_t used;
  };

  static void * allocate_from(unsigned char * storage, std::size_t capacity,
                              std::size_t * used, std::size_t bytes,
                              std::size_t alignment)
  {
    const std::uintptr_t address =
      reinterpret_cast<std::uintptr_t>(storage + *used);
    const std::size_t padding =
      (alignment - address % alignment) % alignment;
    if(bytes > capacity - *used || padding > capacity - *used - bytes)
      return 0;
    unsigned char * result = storage + *used + padding;
    *used += padding + bytes;
    return result;
  }

  alignas(std::max_align_t) unsigned char inline_storage_[4096];
  std::size_t inline_used_;
  Block inline_blocks_[8];
  std::size_t block_count_;
  std::size_t next_block_size_;
  std::vector<Block> overflow_blocks_;
};

template <typename T>
class PassAllocator
{
public:
  typedef T value_type;

  explicit PassAllocator(PassArena * arena = 0) : arena_(arena) {}

  template <typename U>
  PassAllocator(const PassAllocator<U> & other) : arena_(other.arena()) {}

  T * allocate(std::size_t count)
  {
    if(!arena_ || count > std::numeric_limits<std::size_t>::max() / sizeof(T))
      throw std::bad_alloc();
    return static_cast<T *>(arena_->allocate(count * sizeof(T), alignof(T)));
  }

  void deallocate(T *, std::size_t) {}
  PassArena * arena() const { return arena_; }

  template <typename U> struct rebind { typedef PassAllocator<U> other; };

private:
  PassArena * arena_;
};

template <typename T, typename U>
bool operator==(const PassAllocator<T> & left,
                const PassAllocator<U> & right)
{
  return left.arena() == right.arena();
}

template <typename T, typename U>
bool operator!=(const PassAllocator<T> & left,
                const PassAllocator<U> & right)
{
  return !(left == right);
}

bool same_operand(const Operand & a, const Operand & b)
{
  return a.kind == b.kind && a.text == b.text;
}

Operand integer_operand(long long value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_INTEGER;
  result.has_int_value = true;
  result.int_value = value;
  result.text = std::to_string(value);
  return result;
}

Operand floating_operand(long double value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_FLOAT;
  result.float_value = value;
  if(std::isinf(value)) result.text = value < 0 ? "-inf" : "inf";
  else if(std::isnan(value)) result.text = "nan";
  else {
    std::ostringstream text;
    text.precision(20);
    text << value;
    result.text = text.str();
    if(type.kind == lowir_model::LTK_F32) result.text += 'f';
    else if(type.kind == lowir_model::LTK_F80) result.text += 'L';
  }
  return result;
}

bool is_integer_type(const LowType & type)
{
  return type.kind == lowir_model::LTK_I1 ||
    (type.kind >= lowir_model::LTK_I8 && type.kind <= lowir_model::LTK_I64) ||
    type.kind == lowir_model::LTK_I128;
}

bool is_float_type(const LowType & type)
{
  return type.kind >= lowir_model::LTK_F32 && type.kind <= lowir_model::LTK_F80;
}

typedef __int128 WideSigned;
typedef unsigned __int128 WideUnsigned;

WideUnsigned wide_mask(const LowType & type)
{
  return type.bit_width >= 128 ? ~static_cast<WideUnsigned>(0) :
    (static_cast<WideUnsigned>(1) << type.bit_width) - 1;
}

WideUnsigned wide_integer(long long value)
{
  return static_cast<WideUnsigned>(static_cast<WideSigned>(value));
}

bool representable_wide_integer(WideUnsigned value, const LowType & type,
                                Operand * result)
{
  const WideSigned signed_value = static_cast<WideSigned>(value);
  if(signed_value < static_cast<WideSigned>(std::numeric_limits<long long>::min()) ||
     signed_value > static_cast<WideSigned>(std::numeric_limits<long long>::max()))
    return false;
  *result = integer_operand(static_cast<long long>(signed_value), type);
  return true;
}

std::uint64_t width_mask(const LowType & type)
{
  return type.bit_width >= 64 ? ~UINT64_C(0) :
    (UINT64_C(1) << type.bit_width) - 1;
}

long long normalize_integer(std::uint64_t value, const LowType & type)
{
  value &= width_mask(type);
  if(type.kind == lowir_model::LTK_U8 || type.kind == lowir_model::LTK_U16 ||
     type.kind == lowir_model::LTK_U32 || type.kind == lowir_model::LTK_PTR)
    return static_cast<long long>(value);
  if(type.bit_width && type.bit_width < 64 &&
     (value & (UINT64_C(1) << (type.bit_width - 1))))
    value |= ~width_mask(type);
  return static_cast<long long>(value);
}

bool is_zero(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == 0;
}

bool is_one(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == 1;
}

bool is_minus_one(const Operand & value)
{
  return value.kind == Operand::OP_INTEGER && value.has_int_value &&
    value.int_value == -1;
}

bool is_eh_instruction(Instruction::Kind kind)
{
  return kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_EH_END;
}

bool is_pure(Instruction::Kind kind)
{
  return kind == Instruction::IK_CONST || kind == Instruction::IK_COPY ||
    kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

bool cse_eligible(Instruction::Kind kind)
{
  return kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

bool commutative(const std::string & op)
{
  return op == "add" || op == "mul" || op == "and" || op == "or" ||
    op == "xor";
}

std::string reverse_compare(const std::string & op)
{
  if(op == "lt") return "gt";
  if(op == "le") return "ge";
  if(op == "gt") return "lt";
  if(op == "ge") return "le";
  if(op == "ult") return "ugt";
  if(op == "ule") return "uge";
  if(op == "ugt") return "ult";
  if(op == "uge") return "ule";
  return op;
}

bool operand_less(const Operand & a, const Operand & b)
{
  if(a.kind != b.kind) return a.kind < b.kind;
  return a.text < b.text;
}

struct ExpressionKey
{
  Instruction::Kind kind;
  std::string op;
  LowTypeKind type_kind;
  std::size_t type_size;
  std::size_t type_alignment;
  LowTypeKind source_type_kind;
  std::size_t source_type_size;
  std::size_t source_type_alignment;
  lowir_model::IndexProjectionKind index_projection;
  Operand::Kind first_kind;
  std::string first;
  Operand::Kind second_kind;
  std::string second;

  bool operator==(const ExpressionKey & other) const
  {
    return kind == other.kind && op == other.op &&
      type_kind == other.type_kind && type_size == other.type_size &&
      type_alignment == other.type_alignment &&
      source_type_kind == other.source_type_kind &&
      source_type_size == other.source_type_size &&
      source_type_alignment == other.source_type_alignment &&
      index_projection == other.index_projection &&
      first_kind == other.first_kind && first == other.first &&
      second_kind == other.second_kind && second == other.second;
  }
};

void combine_hash(std::size_t * seed, std::size_t value)
{
  *seed ^= value + static_cast<std::size_t>(0x9e3779b9U) +
    (*seed << 6) + (*seed >> 2);
}

struct ExpressionKeyHash
{
  std::size_t operator()(const ExpressionKey & key) const
  {
    std::size_t result = static_cast<std::size_t>(key.kind);
    combine_hash(&result, std::hash<std::string>()(key.op));
    combine_hash(&result, static_cast<std::size_t>(key.type_kind));
    combine_hash(&result, key.type_size);
    combine_hash(&result, key.type_alignment);
    combine_hash(&result, static_cast<std::size_t>(key.source_type_kind));
    combine_hash(&result, key.source_type_size);
    combine_hash(&result, key.source_type_alignment);
    combine_hash(&result, static_cast<std::size_t>(key.index_projection));
    combine_hash(&result, static_cast<std::size_t>(key.first_kind));
    combine_hash(&result, std::hash<std::string>()(key.first));
    combine_hash(&result, static_cast<std::size_t>(key.second_kind));
    combine_hash(&result, std::hash<std::string>()(key.second));
    return result;
  }
};

ExpressionKey expression_key(const Instruction & ins)
{
  const Operand * first = &ins.first;
  const Operand * second = &ins.second;
  std::string op = ins.op;
  if((ins.kind == Instruction::IK_BINARY && commutative(op)) ||
     (ins.kind == Instruction::IK_CMP && (op == "eq" || op == "ne"))) {
    if(operand_less(*second, *first)) std::swap(first, second);
  } else if(ins.kind == Instruction::IK_CMP && operand_less(*second, *first)) {
    std::swap(first, second);
    op = reverse_compare(op);
  }
  ExpressionKey key;
  key.kind = ins.kind;
  key.op = std::move(op);
  key.type_kind = ins.type.kind;
  key.type_size = ins.type.storage_size;
  key.type_alignment = ins.type.alignment;
  key.source_type_kind = ins.source_type.kind;
  key.source_type_size = ins.source_type.storage_size;
  key.source_type_alignment = ins.source_type.alignment;
  key.index_projection = ins.index_projection;
  key.first_kind = first->kind;
  key.first = first->text;
  key.second_kind = second->kind;
  key.second = second->text;
  return key;
}

bool fold_unary(const Instruction & ins, Operand * result)
{
  if(ins.op == "decay" && ins.type.kind == lowir_model::LTK_PTR) {
    *result = ins.first;
    return true;
  }
  if(ins.first.kind != Operand::OP_INTEGER || !ins.first.has_int_value ||
     !is_integer_type(ins.type)) return false;
  if(ins.type.bit_width > 64) {
    const WideUnsigned value = wide_integer(ins.first.int_value);
    WideUnsigned folded = 0;
    if(ins.op == "neg") folded = -value;
    else if(ins.op == "bitnot") folded = ~value;
    else if(ins.op == "not")
      return (*result = integer_operand(value == 0, ins.type), true);
    else return false;
    return representable_wide_integer(folded, ins.type, result);
  }
  const std::uint64_t value = static_cast<std::uint64_t>(ins.first.int_value);
  if(ins.op == "neg")
    *result = integer_operand(normalize_integer(UINT64_C(0) - value, ins.type), ins.type);
  else if(ins.op == "bitnot")
    *result = integer_operand(normalize_integer(~value, ins.type), ins.type);
  else if(ins.op == "not")
    *result = integer_operand(value == 0, ins.type);
  else return false;
  return true;
}

bool fold_binary(const Instruction & ins, Operand * result)
{
  if(ins.first.kind != Operand::OP_INTEGER || !ins.first.has_int_value ||
     ins.second.kind != Operand::OP_INTEGER || !ins.second.has_int_value ||
     !is_integer_type(ins.type)) return false;
  if(ins.type.bit_width > 64) {
    const WideUnsigned a = wide_integer(ins.first.int_value);
    const WideUnsigned b = wide_integer(ins.second.int_value);
    WideUnsigned value = 0;
    if(ins.op == "add") value = a + b;
    else if(ins.op == "sub") value = a - b;
    else if(ins.op == "mul") value = a * b;
    else if(ins.op == "and") value = a & b;
    else if(ins.op == "or") value = a | b;
    else if(ins.op == "xor") value = a ^ b;
    else if(ins.op == "shl" && b < 128)
      value = a << static_cast<unsigned>(b);
    else if(ins.op == "ushr" && b < 128)
      value = a >> static_cast<unsigned>(b);
    else if(ins.op == "shr" && b < 128)
      value = static_cast<WideUnsigned>(static_cast<WideSigned>(a) >>
                                       static_cast<unsigned>(b));
    else if((ins.op == "udiv" || ins.op == "umod") && b)
      value = ins.op == "udiv" ? a / b : a % b;
    else if((ins.op == "div" || ins.op == "mod") && b) {
      const WideSigned signed_a = static_cast<WideSigned>(a);
      const WideSigned signed_b = static_cast<WideSigned>(b);
      value = static_cast<WideUnsigned>(ins.op == "div" ?
        signed_a / signed_b : signed_a % signed_b);
    } else return false;
    return representable_wide_integer(value, ins.type, result);
  }
  const std::uint64_t a = static_cast<std::uint64_t>(ins.first.int_value);
  const std::uint64_t b = static_cast<std::uint64_t>(ins.second.int_value);
  std::uint64_t value = 0;
  if(ins.op == "add") value = a + b;
  else if(ins.op == "sub") value = a - b;
  else if(ins.op == "mul") value = a * b;
  else if(ins.op == "and") value = a & b;
  else if(ins.op == "or") value = a | b;
  else if(ins.op == "xor") value = a ^ b;
  else if(ins.op == "shl" && b < 64) value = a << b;
  else if(ins.op == "ushr" && b < 64) value = a >> b;
  else if(ins.op == "shr" && b < 64)
    value = static_cast<std::uint64_t>(ins.first.int_value >> b);
  else if((ins.op == "udiv" || ins.op == "umod") && b)
    value = ins.op == "udiv" ? a / b : a % b;
  else if((ins.op == "div" || ins.op == "mod") && ins.second.int_value &&
          !(ins.first.int_value == std::numeric_limits<long long>::min() &&
            ins.second.int_value == -1))
    value = static_cast<std::uint64_t>(ins.op == "div" ?
      ins.first.int_value / ins.second.int_value :
      ins.first.int_value % ins.second.int_value);
  else return false;
  *result = integer_operand(normalize_integer(value, ins.type), ins.type);
  return true;
}

bool fold_compare(const Instruction & ins, Operand * result)
{
  bool value = false;
  if(ins.first.kind == Operand::OP_INTEGER && ins.first.has_int_value &&
     ins.second.kind == Operand::OP_INTEGER && ins.second.has_int_value) {
    const long long a = ins.first.int_value;
    const long long b = ins.second.int_value;
    if(ins.type.bit_width > 64) {
      const WideSigned signed_a = static_cast<WideSigned>(a);
      const WideSigned signed_b = static_cast<WideSigned>(b);
      const WideUnsigned unsigned_a = static_cast<WideUnsigned>(signed_a);
      const WideUnsigned unsigned_b = static_cast<WideUnsigned>(signed_b);
      if(ins.op == "eq") value = unsigned_a == unsigned_b;
      else if(ins.op == "ne") value = unsigned_a != unsigned_b;
      else if(ins.op == "lt") value = signed_a < signed_b;
      else if(ins.op == "le") value = signed_a <= signed_b;
      else if(ins.op == "gt") value = signed_a > signed_b;
      else if(ins.op == "ge") value = signed_a >= signed_b;
      else if(ins.op == "ult") value = unsigned_a < unsigned_b;
      else if(ins.op == "ule") value = unsigned_a <= unsigned_b;
      else if(ins.op == "ugt") value = unsigned_a > unsigned_b;
      else if(ins.op == "uge") value = unsigned_a >= unsigned_b;
      else return false;
      *result = integer_operand(value ? 1 : 0,
        lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
      return true;
    }
    const std::uint64_t ua = static_cast<std::uint64_t>(a) & width_mask(ins.type);
    const std::uint64_t ub = static_cast<std::uint64_t>(b) & width_mask(ins.type);
    if(ins.op == "eq") value = ua == ub;
    else if(ins.op == "ne") value = ua != ub;
    else if(ins.op == "lt") value = a < b;
    else if(ins.op == "le") value = a <= b;
    else if(ins.op == "gt") value = a > b;
    else if(ins.op == "ge") value = a >= b;
    else if(ins.op == "ult") value = ua < ub;
    else if(ins.op == "ule") value = ua <= ub;
    else if(ins.op == "ugt") value = ua > ub;
    else if(ins.op == "uge") value = ua >= ub;
    else return false;
  } else if(ins.first.kind == Operand::OP_FLOAT &&
            ins.second.kind == Operand::OP_FLOAT) {
    long double a = 0.0L, b = 0.0L;
    if(!lowir_model::parse_lowir_floating_literal(ins.first.text, &a) ||
       !lowir_model::parse_lowir_floating_literal(ins.second.text, &b))
      return false;
    if(ins.op == "eq") value = a == b;
    else if(ins.op == "ne") value = a != b;
    else if(ins.op == "lt") value = a < b;
    else if(ins.op == "le") value = a <= b;
    else if(ins.op == "gt") value = a > b;
    else if(ins.op == "ge") value = a >= b;
    else return false;
  } else if(!is_float_type(ins.type) &&
            same_operand(ins.first, ins.second)) {
    if(ins.op == "eq" || ins.op == "le" || ins.op == "ge" ||
       ins.op == "ule" || ins.op == "uge") value = true;
    else if(ins.op == "ne" || ins.op == "lt" || ins.op == "gt" ||
            ins.op == "ult" || ins.op == "ugt") value = false;
    else return false;
  } else return false;
  *result = integer_operand(value ? 1 : 0,
    lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
  return true;
}

bool fold_convert(const Instruction & ins, Operand * result)
{
  if(lowir_model::same_lowir_type(ins.type, ins.source_type)) {
    *result = ins.first;
    return true;
  }
  if(ins.first.kind == Operand::OP_INTEGER && ins.first.has_int_value) {
    if(is_integer_type(ins.type)) {
      if(ins.type.bit_width > 64) {
        WideUnsigned value = wide_integer(ins.first.int_value);
        if(ins.op == "zext") value &= wide_mask(ins.source_type);
        else if(ins.op == "sext" && ins.source_type.bit_width < 128) {
          const WideUnsigned mask = wide_mask(ins.source_type);
          value &= mask;
          if(ins.source_type.bit_width &&
             (value & (static_cast<WideUnsigned>(1) <<
                       (ins.source_type.bit_width - 1))))
            value |= ~mask;
        } else return false;
        return representable_wide_integer(value, ins.type, result);
      }
      std::uint64_t value = static_cast<std::uint64_t>(ins.first.int_value);
      if(ins.op == "zext") value &= width_mask(ins.source_type);
      *result = integer_operand(normalize_integer(value, ins.type), ins.type);
      return true;
    }
    if(is_float_type(ins.type) && ins.source_type.bit_width <= 64 &&
       (ins.op == "sitofp" || ins.op == "uitofp")) {
      const long double value = ins.op == "uitofp" ?
        static_cast<long double>(static_cast<std::uint64_t>(ins.first.int_value) &
                                 width_mask(ins.source_type)) :
        static_cast<long double>(ins.first.int_value);
      *result = floating_operand(value, ins.type);
      return true;
    }
  }
  if(ins.first.kind == Operand::OP_FLOAT && is_float_type(ins.type) &&
     (ins.op == "fpext" || ins.op == "fptrunc")) {
    *result = floating_operand(ins.first.float_value, ins.type);
    return true;
  }
  return false;
}

bool algebraic_identity(const Instruction & ins, Operand * result)
{
  if(ins.kind != Instruction::IK_BINARY) return false;
  if((ins.op == "add" || ins.op == "or" || ins.op == "xor") && is_zero(ins.second))
    *result = ins.first;
  else if(ins.op == "add" && is_zero(ins.first)) *result = ins.second;
  else if(ins.op == "sub" && is_zero(ins.second)) *result = ins.first;
  else if((ins.op == "mul" || ins.op == "div" || ins.op == "udiv") &&
          is_one(ins.second)) *result = ins.first;
  else if(ins.op == "mul" && is_one(ins.first)) *result = ins.second;
  else if(ins.op == "and" && is_minus_one(ins.second)) *result = ins.first;
  else if(ins.op == "and" && is_minus_one(ins.first)) *result = ins.second;
  else return false;
  return true;
}

class EdgeList
{
public:
  EdgeList() : first_(0), second_(0), size_(0) {}

  std::size_t size() const { return size_; }

  std::size_t operator[](std::size_t index) const
  {
    if(index == 0) return first_;
    if(index == 1) return second_;
    return overflow_[index - 2];
  }

  void push_back(std::size_t value)
  {
    if(size_ == 0) first_ = value;
    else if(size_ == 1) second_ = value;
    else overflow_.push_back(value);
    ++size_;
  }

  void insert_sorted_unique(std::size_t value)
  {
    std::size_t position = 0;
    while(position < size_ && (*this)[position] < value) ++position;
    if(position < size_ && (*this)[position] == value) return;
    if(size_ == 0) first_ = value;
    else if(size_ == 1) {
      if(position == 0) { second_ = first_; first_ = value; }
      else second_ = value;
    } else if(position == 0) {
      overflow_.insert(overflow_.begin(), second_);
      second_ = first_;
      first_ = value;
    } else if(position == 1) {
      overflow_.insert(overflow_.begin(), second_);
      second_ = value;
    } else {
      overflow_.insert(overflow_.begin() + (position - 2), value);
    }
    ++size_;
  }

private:
  std::size_t first_, second_, size_;
  std::vector<std::size_t> overflow_;
};

struct Graph
{
  BlockIndex index;
  std::vector<EdgeList> successors;
  std::vector<EdgeList> predecessors;
  std::unordered_set<std::string> eh_targets;
};

void add_edge(Graph * graph, std::size_t from, const Operand & target,
              Stats * stats)
{
  if(target.kind != Operand::OP_LABEL) return;
  const BlockIndex::const_iterator found = graph->index.find(target.text);
  if(found == graph->index.end()) return;
  graph->successors[from].insert_sorted_unique(found->second);
  if(stats) ++stats->cfg_edge_visits;
}

Graph build_graph(const Function & function, Stats * stats)
{
  Graph result;
  result.successors.resize(function.blocks.size());
  result.predecessors.resize(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    result.index[function.blocks[i].label] = i;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const Block & block = function.blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const Instruction & ins = block.instructions[j];
      if(ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) {
        add_edge(&result, i, ins.first, stats);
        result.eh_targets.insert(ins.first.text);
      }
    }
    if(block.instructions.empty()) continue;
    const Instruction & term = block.instructions.back();
    if(term.kind == Instruction::IK_JUMP)
      add_edge(&result, i, term.first, stats);
    else if(term.kind == Instruction::IK_BRANCH) {
      add_edge(&result, i, term.second, stats);
      add_edge(&result, i, term.third, stats);
    } else if(term.kind == Instruction::IK_SWITCH) {
      add_edge(&result, i, term.second, stats);
      for(std::size_t j = 1; j < term.args.size(); j += 2)
        add_edge(&result, i, term.args[j], stats);
    }
  }
  for(std::size_t i = 0; i < result.successors.size(); ++i) {
    const EdgeList & edges = result.successors[i];
    for(std::size_t j = 0; j < edges.size(); ++j)
      result.predecessors[edges[j]].push_back(i);
  }
  return result;
}

struct DominatorTree
{
  std::vector<std::size_t> immediate;
  std::vector<std::size_t> preorder;
  std::vector<std::size_t> postorder;

  bool dominates(std::size_t parent, std::size_t child) const
  {
    if(parent == child) return true;
    return parent < preorder.size() && child < preorder.size() &&
      preorder[parent] != 0 && preorder[child] != 0 &&
      preorder[parent] <= preorder[child] &&
      postorder[child] <= postorder[parent];
  }
};

std::size_t evaluate_dominator(
    std::size_t node, std::vector<std::size_t> * ancestor,
    std::vector<std::size_t> * label,
    const std::vector<std::size_t> & semi)
{
  if((*ancestor)[node] == kNoBlock) return (*label)[node];
  std::vector<std::size_t> path;
  std::size_t cursor = node;
  while((*ancestor)[cursor] != kNoBlock &&
        (*ancestor)[(*ancestor)[cursor]] != kNoBlock) {
    path.push_back(cursor);
    cursor = (*ancestor)[cursor];
  }
  for(std::size_t i = path.size(); i > 0; --i) {
    const std::size_t value = path[i - 1];
    const std::size_t parent = (*ancestor)[value];
    if(semi[(*label)[parent]] < semi[(*label)[value]])
      (*label)[value] = (*label)[parent];
    (*ancestor)[value] = (*ancestor)[parent];
  }
  return (*label)[node];
}

DominatorTree dominators(const Graph & graph, Stats * stats)
{
  const std::size_t count = graph.successors.size();
  DominatorTree result;
  result.immediate.assign(count, kNoBlock);
  result.preorder.assign(count, 0);
  result.postorder.assign(count, 0);
  if(!count) return result;

  std::vector<std::size_t> semi(count, kNoBlock), parent(count, kNoBlock),
    ancestor(count, kNoBlock), label(count, kNoBlock), vertex;
  struct DfsFrame { std::size_t block; std::size_t edge; };
  std::vector<DfsFrame> dfs;
  semi[0] = 0;
  label[0] = 0;
  vertex.push_back(0);
  dfs.push_back(DfsFrame{0, 0});
  while(!dfs.empty()) {
    DfsFrame & frame = dfs.back();
    if(frame.edge == graph.successors[frame.block].size()) {
      dfs.pop_back();
      continue;
    }
    const std::size_t next = graph.successors[frame.block][frame.edge++];
    if(semi[next] != kNoBlock) continue;
    parent[next] = frame.block;
    semi[next] = vertex.size();
    label[next] = next;
    vertex.push_back(next);
    dfs.push_back(DfsFrame{next, 0});
  }

  std::vector<std::vector<std::size_t> > bucket(count);
  for(std::size_t reverse = vertex.size(); reverse > 1; --reverse) {
    const std::size_t block = vertex[reverse - 1];
    for(std::size_t i = 0; i < graph.predecessors[block].size(); ++i) {
      const std::size_t predecessor = graph.predecessors[block][i];
      if(semi[predecessor] == kNoBlock) continue;
      const std::size_t candidate = evaluate_dominator(
        predecessor, &ancestor, &label, semi);
      semi[block] = std::min(semi[block], semi[candidate]);
    }
    bucket[vertex[semi[block]]].push_back(block);
    ancestor[block] = parent[block];
    std::vector<std::size_t> & pending = bucket[parent[block]];
    for(std::size_t i = 0; i < pending.size(); ++i) {
      const std::size_t candidate = evaluate_dominator(
        pending[i], &ancestor, &label, semi);
      result.immediate[pending[i]] = semi[candidate] < semi[pending[i]] ?
        candidate : parent[block];
    }
    pending.clear();
    if(stats) ++stats->block_visits;
  }
  result.immediate[0] = 0;
  for(std::size_t i = 1; i < vertex.size(); ++i) {
    const std::size_t block = vertex[i];
    if(result.immediate[block] != vertex[semi[block]])
      result.immediate[block] = result.immediate[result.immediate[block]];
  }

  std::vector<std::vector<std::size_t> > children(count);
  for(std::size_t i = 1; i < vertex.size(); ++i)
    children[result.immediate[vertex[i]]].push_back(vertex[i]);
  std::size_t ordinal = 0;
  dfs.clear();
  result.preorder[0] = ++ordinal;
  dfs.push_back(DfsFrame{0, 0});
  while(!dfs.empty()) {
    DfsFrame & frame = dfs.back();
    if(frame.edge < children[frame.block].size()) {
      const std::size_t child = children[frame.block][frame.edge++];
      result.preorder[child] = ++ordinal;
      dfs.push_back(DfsFrame{child, 0});
    } else {
      result.postorder[frame.block] = ordinal;
      dfs.pop_back();
    }
  }
  return result;
}

struct Fact
{
  Operand value;
  std::size_t block;
};

template <typename FactMap>
Operand resolve_operand(Operand value,
                        const FactMap & facts,
                        std::size_t block,
                        const DominatorTree & dom)
{
  for(std::size_t step = 0;
      step < facts.size() && value.kind == Operand::OP_TEMP; ++step) {
    const typename FactMap::const_iterator found = facts.find(value.text);
    if(found == facts.end() ||
       !dom.dominates(found->second.block, block)) break;
    value = found->second.value;
  }
  return value;
}

template <typename FactMap>
void resolve_instruction_operands(Instruction * ins,
                                  const FactMap & facts,
                                  std::size_t block,
                                  const DominatorTree & dom)
{
  Operand * values[] = {&ins->first, &ins->second, &ins->third};
  for(std::size_t i = 0; i < 3; ++i) {
    const bool storage_address =
      (i == 0 && (ins->kind == Instruction::IK_LOAD ||
                  ins->kind == Instruction::IK_ATOMIC_LOAD)) ||
      (i == 1 && (ins->kind == Instruction::IK_STORE ||
                  ins->kind == Instruction::IK_ATOMIC_STORE));
    if(values[i]->kind == Operand::OP_TEMP) {
      const Operand resolved = resolve_operand(*values[i], facts, block, dom);
      if(!storage_address || resolved.kind == Operand::OP_TEMP)
        *values[i] = resolved;
    }
  }
  for(std::size_t i = 0; i < ins->args.size(); ++i)
    if(ins->args[i].kind == Operand::OP_TEMP)
      ins->args[i] = resolve_operand(ins->args[i], facts, block, dom);
}

LowType result_type(const Instruction & ins)
{
  if(ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX)
    return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
  if(ins.kind == Instruction::IK_CMP ||
     ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
     ins.kind == Instruction::IK_EXCEPTION_SELECTOR)
    return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  return ins.type;
}

struct DefinitionFact
{
  Instruction::Kind kind;
  std::string op;
  LowTypeKind type_kind;
  std::size_t type_size;
  std::size_t type_alignment;
  Operand first;
  Operand second;
};

bool same_fact_type(const DefinitionFact & fact, const LowType & type)
{
  return fact.type_kind == type.kind &&
    (type.kind != lowir_model::LTK_OBJECT ||
     (fact.type_size == type.storage_size &&
      fact.type_alignment == type.alignment));
}

template <typename DefinitionMap>
bool reassociate(Instruction * ins, const DefinitionMap & definitions)
{
  if(ins->kind != Instruction::IK_BINARY || !commutative(ins->op) ||
     ins->second.kind != Operand::OP_INTEGER || !ins->second.has_int_value ||
     ins->first.kind != Operand::OP_TEMP) return false;
  const typename DefinitionMap::const_iterator found =
    definitions.find(ins->first.text);
  if(found == definitions.end()) return false;
  const DefinitionFact & parent = found->second;
  if(parent.kind != Instruction::IK_BINARY || parent.op != ins->op ||
     parent.second.kind != Operand::OP_INTEGER ||
     !parent.second.has_int_value ||
     !same_fact_type(parent, ins->type)) return false;
  Instruction constants = *ins;
  constants.first = parent.second;
  Operand folded;
  if(!fold_binary(constants, &folded)) return false;
  ins->first = parent.first;
  ins->second = folded;
  return true;
}

bool simplify_values(Function * function, Stats * stats)
{
  if(function->blocks.empty()) return false;
  bool has_candidate = false;
  for(std::size_t i = 0; !has_candidate && i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j)
      if(is_pure(function->blocks[i].instructions[j].kind)) {
        has_candidate = true;
        break;
      }
  if(!has_candidate) {
    if(stats) ++stats->simplify_candidate_skips;
    return false;
  }
  DominatorTree dom;
  if(function->blocks.size() == 1) {
    dom.immediate.assign(1, 0);
    dom.preorder.assign(1, 1);
    dom.postorder.assign(1, 1);
  } else {
    const Graph graph = build_graph(*function, stats);
    dom = dominators(graph, stats);
  }
  bool function_has_eh = false;
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  struct TypeIdentity
  {
    LowTypeKind kind;
    std::size_t size;
    std::size_t alignment;
  };
  const auto type_identity = [](const LowType & type) {
    return TypeIdentity{type.kind, type.storage_size, type.alignment};
  };
  const auto same_type = [](const TypeIdentity & left, const LowType & right) {
    return left.kind == right.kind &&
      (right.kind != lowir_model::LTK_OBJECT ||
       (left.size == right.storage_size && left.alignment == right.alignment));
  };
  PassArena arena;
  typedef PassAllocator<std::pair<const std::string, TypeIdentity> >
    TypeAllocator;
  typedef PassAllocator<std::string> StringAllocator;
  std::unordered_map<std::string, TypeIdentity, std::hash<std::string>,
    std::equal_to<std::string>, TypeAllocator> types(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      TypeAllocator(&arena));
  std::unordered_set<std::string, std::hash<std::string>,
    std::equal_to<std::string>, StringAllocator> storage_temporaries(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      StringAllocator(&arena));
  types.reserve(function->params.size() + instruction_total);
  storage_temporaries.reserve(instruction_total / 4 + 1);
  for(std::size_t i = 0; i < function->params.size(); ++i)
    types[function->params[i].name] = type_identity(function->params[i].type);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      function_has_eh = function_has_eh || is_eh_instruction(ins.kind);
      if(!ins.dest.empty()) types[ins.dest] = type_identity(result_type(ins));
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.first.text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.second.text);
    }

  typedef PassAllocator<std::pair<const std::string, Fact> > FactAllocator;
  typedef PassAllocator<std::pair<const ExpressionKey, Fact> >
    ExpressionAllocator;
  typedef PassAllocator<std::pair<const std::string, DefinitionFact> >
    DefinitionAllocator;
  std::unordered_map<std::string, Fact, std::hash<std::string>,
    std::equal_to<std::string>, FactAllocator> facts(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      FactAllocator(&arena));
  std::unordered_map<ExpressionKey, Fact, ExpressionKeyHash,
    std::equal_to<ExpressionKey>, ExpressionAllocator> expressions(
      0, ExpressionKeyHash(), std::equal_to<ExpressionKey>(),
      ExpressionAllocator(&arena));
  std::unordered_map<std::string, DefinitionFact, std::hash<std::string>,
    std::equal_to<std::string>, DefinitionAllocator> definitions(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      DefinitionAllocator(&arena));
  facts.reserve(instruction_total);
  expressions.reserve(instruction_total / 2 + 1);
  definitions.reserve(instruction_total);
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t index = 0; index < original_size; ++index) {
      Instruction & ins = instructions[index];
      if(stats) ++stats->instruction_visits;
      resolve_instruction_operands(&ins, facts, block, dom);

      Operand replacement;
      bool replace = false;
      if(ins.kind == Instruction::IK_CONST &&
         !storage_temporaries.count(ins.dest)) {
        replacement = ins.first;
        replace = true;
      } else if(ins.kind == Instruction::IK_COPY &&
                ins.dest.compare(0, 5, "%dbg_") != 0 &&
                !storage_temporaries.count(ins.dest)) {
        const auto source = types.find(ins.first.text);
        replace = ins.first.kind != Operand::OP_TEMP || source == types.end() ||
          same_type(source->second, ins.type);
        replacement = ins.first;
      } else if(ins.kind == Instruction::IK_UNARY)
        replace = fold_unary(ins, &replacement);
      else if(ins.kind == Instruction::IK_BINARY) {
        reassociate(&ins, definitions);
        replace = fold_binary(ins, &replacement) ||
          algebraic_identity(ins, &replacement);
      } else if(ins.kind == Instruction::IK_CMP) {
        replace = fold_compare(ins, &replacement);
        if(!replace && (ins.op == "eq" || ins.op == "ne") &&
           ((is_zero(ins.second) && ins.op == "ne") ||
            (is_one(ins.second) && ins.op == "eq"))) {
          const auto boolean = definitions.find(ins.first.text);
          if(boolean != definitions.end() &&
             boolean->second.kind == Instruction::IK_CMP) {
            replacement = ins.first;
            replace = true;
          }
        }
      } else if(ins.kind == Instruction::IK_CONVERT)
        replace = fold_convert(ins, &replacement);

      if(replace && !ins.dest.empty()) {
        facts[ins.dest] = Fact{replacement, block};
        changed = true;
        if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
        continue;
      }

      if(cse_eligible(ins.kind) && !ins.dest.empty()) {
        const ExpressionKey key = expression_key(ins);
        const auto found = expressions.find(key);
        const bool cross_block_guard = function_has_eh &&
          (ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX) &&
          found != expressions.end() && found->second.block != block;
        if(found != expressions.end() && !cross_block_guard &&
           dom.dominates(found->second.block, block)) {
          facts[ins.dest] = Fact{found->second.value, block};
          changed = true;
          if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
          continue;
        }
        Operand produced;
        produced.kind = Operand::OP_TEMP;
        produced.text = ins.dest;
        expressions[key] = Fact{produced, block};
      }
      if(!ins.dest.empty())
        definitions[ins.dest] = DefinitionFact{
          ins.kind, ins.op, ins.type.kind, ins.type.storage_size,
          ins.type.alignment, ins.first, ins.second};
      if(kept != index) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  return changed;
}

bool call_is_removable(const Instruction & ins,
                       const std::unordered_map<std::string,
                         FunctionBoundaryMetadata> & boundaries)
{
  if(ins.kind != Instruction::IK_CALL || ins.dest.empty()) return false;
  FunctionBoundaryMetadata boundary = ins.call_boundary;
  if(ins.first.kind == Operand::OP_GLOBAL) {
    const std::unordered_map<std::string, FunctionBoundaryMetadata>::const_iterator
      found = boundaries.find(ins.first.text);
    if(found != boundaries.end()) boundary = found->second;
  }
  return boundary.effects == lowir_model::CFXM_READNONE &&
    boundary.unwind == lowir_model::CUM_NO &&
    boundary.returns != lowir_model::CRM_NORETURN;
}

bool eliminate_dead_code(Function * function,
                         const std::unordered_map<std::string,
                           FunctionBoundaryMetadata> & boundaries,
                         Stats * stats)
{
  bool has_candidate = false;
  for(std::size_t i = 0; !has_candidate && i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(is_pure(ins.kind) || ins.kind == Instruction::IK_LOAD ||
         call_is_removable(ins, boundaries)) {
        has_candidate = true;
        break;
      }
    }
  if(!has_candidate) {
    if(stats) ++stats->dce_candidate_skips;
    return false;
  }
  typedef std::pair<std::size_t, std::size_t> Location;
  struct ValueLiveness
  {
    Location definition = Location(0, 0);
    std::size_t uses = 0;
    bool defined = false;
  };
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  PassArena arena;
  typedef PassAllocator<std::pair<const std::string, ValueLiveness> >
    ValueAllocator;
  std::unordered_map<std::string, ValueLiveness, std::hash<std::string>,
    std::equal_to<std::string>, ValueAllocator> values(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      ValueAllocator(&arena));
  values.reserve(instruction_total);
  std::vector<std::vector<unsigned char> > dead(function->blocks.size());
  const auto count_use = [&values](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP) ++values[operand.text].uses;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    dead[i].assign(function->blocks[i].instructions.size(), 0);
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(!ins.dest.empty()) {
        ValueLiveness & value = values[ins.dest];
        value.definition = Location(i, j);
        value.defined = true;
      }
      count_use(ins.first);
      count_use(ins.second);
      count_use(ins.third);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        count_use(ins.args[k]);
      if(stats) ++stats->instruction_visits;
    }
  }

  std::deque<Location> work;
  for(std::unordered_map<std::string, ValueLiveness>::const_iterator it =
        values.begin(); it != values.end(); ++it) {
    if(!it->second.defined) continue;
    const Instruction & ins =
      function->blocks[it->second.definition.first].instructions[
        it->second.definition.second];
    if(it->second.uses == 0 &&
       (is_pure(ins.kind) || ins.kind == Instruction::IK_LOAD ||
        call_is_removable(ins, boundaries))) {
      work.push_back(it->second.definition);
      if(stats) ++stats->worklist_pushes;
    }
  }

  const auto release_operand = [&](const Operand & operand) {
    if(operand.kind != Operand::OP_TEMP) return;
    std::unordered_map<std::string, ValueLiveness>::iterator found =
      values.find(operand.text);
    if(found == values.end() || found->second.uses == 0) return;
    --found->second.uses;
    if(found->second.uses != 0 || !found->second.defined) return;
    const Instruction & producer =
      function->blocks[found->second.definition.first].instructions[
        found->second.definition.second];
    if(is_pure(producer.kind) || producer.kind == Instruction::IK_LOAD ||
       call_is_removable(producer, boundaries)) {
      work.push_back(found->second.definition);
      if(stats) ++stats->worklist_pushes;
    }
  };

  std::size_t removed = 0;
  while(!work.empty()) {
    const Location location = work.front();
    work.pop_front();
    if(dead[location.first][location.second]) continue;
    dead[location.first][location.second] = 1;
    ++removed;
    const Instruction & ins =
      function->blocks[location.first].instructions[location.second];
    release_operand(ins.first);
    release_operand(ins.second);
    release_operand(ins.third);
    for(std::size_t i = 0; i < ins.args.size(); ++i)
      release_operand(ins.args[i]);
  }
  if(!removed) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j)
      if(!dead[i][j]) {
        if(kept != j) instructions[kept] = std::move(instructions[j]);
        ++kept;
      }
    instructions.resize(kept);
  }
  if(stats) stats->rewrites += removed;
  return true;
}

std::vector<std::string> bypass_targets(const Function & function,
                                        const Graph & graph)
{
  const std::size_t count = function.blocks.size();
  std::vector<std::size_t> next(count, kNoBlock);
  for(std::size_t i = 0; i < count; ++i) {
    const Block & block = function.blocks[i];
    if(graph.eh_targets.count(block.label) || block.instructions.size() != 1 ||
       block.instructions[0].kind != Instruction::IK_JUMP) continue;
    const BlockIndex::const_iterator found =
      graph.index.find(block.instructions[0].first.text);
    if(found != graph.index.end()) next[i] = found->second;
  }
  std::vector<std::string> result(count);
  std::vector<unsigned char> state(count, 0);
  for(std::size_t start = 0; start < count; ++start) {
    if(state[start] == 2) continue;
    std::vector<std::size_t> path;
    std::size_t cursor = start;
    while(state[cursor] == 0 && next[cursor] != kNoBlock) {
      state[cursor] = 1;
      path.push_back(cursor);
      cursor = next[cursor];
    }
    if(state[cursor] == 0) {
      state[cursor] = 2;
      result[cursor] = function.blocks[cursor].label;
    }
    if(state[cursor] == 1) {
      std::size_t cycle = 0;
      while(cycle < path.size() && path[cycle] != cursor) ++cycle;
      for(std::size_t i = cycle; i < path.size(); ++i) {
        result[path[i]] = function.blocks[path[i]].label;
        state[path[i]] = 2;
      }
      for(std::size_t i = cycle; i > 0; --i) {
        result[path[i - 1]] = function.blocks[cursor].label;
        state[path[i - 1]] = 2;
      }
      continue;
    }
    std::string target = result[cursor];
    for(std::size_t i = path.size(); i > 0; --i) {
      result[path[i - 1]] = target;
      state[path[i - 1]] = 2;
    }
  }
  for(std::size_t i = 0; i < count; ++i)
    if(result[i].empty()) result[i] = function.blocks[i].label;
  return result;
}

bool cleanup_cfg(Function * function, Stats * stats)
{
  if(function->blocks.empty()) return false;
  bool changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    Block & block = function->blocks[i];
    if(block.instructions.empty()) continue;
    Instruction & term = block.instructions.back();
    if(term.kind == Instruction::IK_BRANCH) {
      if(term.first.kind == Operand::OP_INTEGER && term.first.has_int_value) {
        const Operand selected = term.first.int_value ? term.second : term.third;
        const lowir_model::InstructionDebugLocation debug = term.debug_location;
        term = Instruction();
        term.kind = Instruction::IK_JUMP;
        term.first = selected;
        term.debug_location = debug;
        changed = true;
      } else if(term.second.text == term.third.text) {
        term.kind = Instruction::IK_JUMP;
        term.first = term.second;
        term.second = Operand();
        term.third = Operand();
        changed = true;
      }
    } else if(term.kind == Instruction::IK_SWITCH &&
              term.first.kind == Operand::OP_INTEGER && term.first.has_int_value) {
      Operand selected = term.second;
      for(std::size_t j = 0; j + 1 < term.args.size(); j += 2)
        if(term.args[j].kind == Operand::OP_INTEGER &&
           term.args[j].has_int_value &&
           term.args[j].int_value == term.first.int_value) {
          selected = term.args[j + 1];
          break;
        }
      const lowir_model::InstructionDebugLocation debug = term.debug_location;
      term = Instruction();
      term.kind = Instruction::IK_JUMP;
      term.first = selected;
      term.debug_location = debug;
      changed = true;
    }
  }

  // There are no unreachable blocks, bypass chains, or merge candidates in a
  // one-block function.  Terminal folding above is the complete CFG cleanup.
  if(function->blocks.size() == 1) return changed;

  Graph graph = build_graph(*function, stats);
  const std::vector<std::string> bypass = bypass_targets(*function, graph);
  bool graph_targets_changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      Instruction & ins = function->blocks[i].instructions[j];
      Operand * targets[3] = {0, 0, 0};
      std::size_t count = 0;
      if(ins.kind == Instruction::IK_JUMP || ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) targets[count++] = &ins.first;
      else if(ins.kind == Instruction::IK_BRANCH) {
        targets[count++] = &ins.second; targets[count++] = &ins.third;
      } else if(ins.kind == Instruction::IK_SWITCH) targets[count++] = &ins.second;
      for(std::size_t k = 0; k < count; ++k) {
        const BlockIndex::const_iterator found = graph.index.find(targets[k]->text);
        const std::string target = found == graph.index.end() ?
          targets[k]->text : bypass[found->second];
        if(target != targets[k]->text &&
           ins.kind != Instruction::IK_EH_TRY &&
           ins.kind != Instruction::IK_EH_CLEANUP) {
          targets[k]->text = target;
          changed = true;
          graph_targets_changed = true;
        }
      }
      if(ins.kind == Instruction::IK_SWITCH)
        for(std::size_t k = 1; k < ins.args.size(); k += 2) {
          const BlockIndex::const_iterator found = graph.index.find(ins.args[k].text);
          const std::string target = found == graph.index.end() ?
            ins.args[k].text : bypass[found->second];
          if(target != ins.args[k].text) {
            ins.args[k].text = target;
            changed = true;
            graph_targets_changed = true;
          }
        }
      if(ins.kind == Instruction::IK_BRANCH &&
         ins.second.text == ins.third.text) {
        const Operand selected = ins.second;
        const lowir_model::InstructionDebugLocation debug = ins.debug_location;
        ins = Instruction();
        ins.kind = Instruction::IK_JUMP;
        ins.first = selected;
        ins.debug_location = debug;
        changed = true;
      }
    }
  }

  if(graph_targets_changed) graph = build_graph(*function, stats);
  std::vector<unsigned char> reachable(function->blocks.size(), 0);
  std::deque<std::size_t> work;
  reachable[0] = 1;
  work.push_back(0);
  while(!work.empty()) {
    const std::size_t block = work.front(); work.pop_front();
    for(std::size_t i = 0; i < graph.successors[block].size(); ++i) {
      const std::size_t next = graph.successors[block][i];
      if(!reachable[next]) { reachable[next] = 1; work.push_back(next); }
    }
  }

  const bool has_unreachable =
    std::find(reachable.begin(), reachable.end(), 0) != reachable.end();

  // EH cleanup code can intentionally use an address computed on a source
  // edge which constant folding proves untaken.  The address is still part of
  // the cleanup contract, so rematerialize simple dead-edge definitions at
  // the entry before pruning that edge.
  if(has_unreachable) {
    struct Definition { std::size_t block; Instruction instruction; };
    std::unordered_map<std::string, Definition> definitions;
    std::unordered_map<std::string, std::vector<std::string> > dependencies;
    for(std::size_t i = 0; i < function->blocks.size(); ++i)
      for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.dest.empty()) continue;
      definitions[ins.dest] = Definition{i, ins};
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(operands[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(ins.args[k].text);
      }
    std::unordered_set<std::string> available;
    for(std::size_t i = 0; i < function->params.size(); ++i)
      available.insert(function->params[i].name);
    const std::size_t entry_end = function->blocks[0].instructions.empty() ? 0 :
      function->blocks[0].instructions.size() - 1;
    for(std::size_t i = 0; i < entry_end; ++i)
      if(!function->blocks[0].instructions[i].dest.empty())
        available.insert(function->blocks[0].instructions[i].dest);
    std::vector<Instruction> rematerialized;
    const auto eligible_definition = [&](const std::string & name) {
      const std::unordered_map<std::string, Definition>::const_iterator found =
        definitions.find(name);
      return found != definitions.end() && !reachable[found->second.block] &&
        is_pure(found->second.instruction.kind);
    };
    const auto rematerialize = [&](const std::string & name) {
      if(available.count(name)) return true;
      if(!eligible_definition(name)) return false;
      struct Frame { std::string name; std::size_t dependency; };
      std::vector<Frame> stack(1, Frame{name, 0});
      std::unordered_set<std::string> active;
      active.insert(name);
      while(!stack.empty()) {
        Frame & frame = stack.back();
        const std::vector<std::string> & required = dependencies[frame.name];
        while(frame.dependency < required.size() &&
              available.count(required[frame.dependency]))
          ++frame.dependency;
        if(frame.dependency < required.size()) {
          const std::string dependency = required[frame.dependency++];
          if(active.count(dependency) || !eligible_definition(dependency))
            return false;
          active.insert(dependency);
          stack.push_back(Frame{dependency, 0});
          continue;
        }
        rematerialized.push_back(definitions.find(frame.name)->second.instruction);
        available.insert(frame.name);
        active.erase(frame.name);
        stack.pop_back();
      }
      return true;
    };
    for(std::size_t i = 0; i < function->blocks.size(); ++i) if(reachable[i])
      for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
        const Instruction & ins = function->blocks[i].instructions[j];
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t k = 0; k < 3; ++k)
          if(operands[k]->kind == Operand::OP_TEMP &&
             definitions.count(operands[k]->text) &&
             !reachable[definitions.find(operands[k]->text)->second.block])
            rematerialize(operands[k]->text);
        for(std::size_t k = 0; k < ins.args.size(); ++k)
          if(ins.args[k].kind == Operand::OP_TEMP &&
             definitions.count(ins.args[k].text) &&
             !reachable[definitions.find(ins.args[k].text)->second.block])
            rematerialize(ins.args[k].text);
      }
    if(!rematerialized.empty()) {
      function->blocks[0].instructions.insert(
        function->blocks[0].instructions.begin() + entry_end,
        rematerialized.begin(), rematerialized.end());
      changed = true;
      if(stats) stats->rewrites += rematerialized.size();
    }

    std::vector<Block> live;
    live.reserve(function->blocks.size());
    for(std::size_t i = 0; i < function->blocks.size(); ++i) {
      if(reachable[i]) live.push_back(std::move(function->blocks[i]));
      else changed = true;
    }
    function->blocks.swap(live);

    graph = build_graph(*function, stats);
  }
  std::vector<unsigned char> block_has_eh(function->blocks.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j)
      block_has_eh[i] = block_has_eh[i] ||
        is_eh_instruction(block.instructions[j].kind);
  }
  std::vector<std::size_t> merge_next(function->blocks.size(), kNoBlock),
    merge_parent(function->blocks.size(), kNoBlock);
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    const Block & block = function->blocks[i];
    if(block.instructions.empty() ||
       block.instructions.back().kind != Instruction::IK_JUMP) continue;
    const BlockIndex::const_iterator target =
      graph.index.find(block.instructions.back().first.text);
    if(target == graph.index.end() || target->second == i ||
       block_has_eh[i] || block_has_eh[target->second] ||
       graph.eh_targets.count(block.label) ||
       graph.eh_targets.count(block.instructions.back().first.text) ||
       graph.predecessors[target->second].size() != 1) continue;
    merge_next[i] = target->second;
    merge_parent[target->second] = i;
  }
  std::vector<unsigned char> consumed(function->blocks.size(), 0);
  std::vector<Block> merged(function->blocks.size());
  std::size_t merged_edges = 0;
  for(std::size_t head = 0; head < function->blocks.size(); ++head) {
    if(merge_next[head] == kNoBlock || merge_parent[head] != kNoBlock) continue;
    merged[head] = std::move(function->blocks[head]);
    std::size_t cursor = head;
    while(merge_next[cursor] != kNoBlock) {
      const std::size_t target = merge_next[cursor];
      consumed[target] = 1;
      merged[head].instructions.pop_back();
      merged[head].instructions.insert(merged[head].instructions.end(),
        std::make_move_iterator(function->blocks[target].instructions.begin()),
        std::make_move_iterator(function->blocks[target].instructions.end()));
      cursor = target;
      ++merged_edges;
    }
  }
  if(merged_edges) {
    std::vector<Block> compact;
    compact.reserve(function->blocks.size() - merged_edges);
    for(std::size_t i = 0; i < function->blocks.size(); ++i) {
      if(consumed[i]) continue;
      if(merged[i].label.empty())
        compact.push_back(std::move(function->blocks[i]));
      else compact.push_back(std::move(merged[i]));
    }
    function->blocks.swap(compact);
    changed = true;
    if(stats) stats->rewrites += merged_edges;
  }
  return changed;
}

void normal_successors(const Function & function, const Graph & graph,
                       std::size_t block, std::vector<std::size_t> * out)
{
  if(function.blocks[block].instructions.empty()) return;
  const Instruction & term = function.blocks[block].instructions.back();
  const Operand * targets[2] = {0, 0};
  if(term.kind == Instruction::IK_JUMP) targets[0] = &term.first;
  else if(term.kind == Instruction::IK_BRANCH) {
    targets[0] = &term.second; targets[1] = &term.third;
  }
  for(std::size_t i = 0; i < 2; ++i)
    if(targets[i] && graph.index.count(targets[i]->text))
      out->push_back(graph.index.find(targets[i]->text)->second);
  if(term.kind == Instruction::IK_SWITCH) {
    if(graph.index.count(term.second.text))
      out->push_back(graph.index.find(term.second.text)->second);
    for(std::size_t i = 1; i < term.args.size(); i += 2)
      if(graph.index.count(term.args[i].text))
        out->push_back(graph.index.find(term.args[i].text)->second);
  }
}

bool exceeds_state_budget(std::size_t blocks, std::size_t facts,
                          std::size_t instructions)
{
  const std::size_t scale = blocks + facts + instructions + 1;
  const std::size_t budget = scale >
      std::numeric_limits<std::size_t>::max() / 16 ?
    std::numeric_limits<std::size_t>::max() : scale * 16;
  return facts != 0 && blocks > budget / facts;
}

bool eliminate_dead_slot_stores(Function * function, Stats * stats)
{
  if(function->slots.empty() || function->blocks.empty()) return false;
  PassArena arena;
  typedef PassAllocator<std::string> StringAllocator;
  std::unordered_set<std::string, std::hash<std::string>,
    std::equal_to<std::string>, StringAllocator> escaped(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      StringAllocator(&arena));
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          escaped.insert(operands[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT)
          escaped.insert(ins.args[k].text);
    }
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  if(exceeds_state_budget(function->blocks.size(), function->slots.size(),
                          instruction_total)) {
    if(stats) ++stats->budget_skips;
    return false;
  }
  bool linear_single_block = function->blocks.size() == 1;
  if(linear_single_block) {
    const Block & block = function->blocks[0];
    for(std::size_t i = 0; i < block.instructions.size(); ++i)
      if(is_eh_instruction(block.instructions[i].kind)) {
        linear_single_block = false;
        break;
      }
    if(linear_single_block && !block.instructions.empty()) {
      const Instruction & term = block.instructions.back();
      if((term.kind == Instruction::IK_JUMP &&
          term.first.text == block.label) ||
         (term.kind == Instruction::IK_BRANCH &&
          (term.second.text == block.label || term.third.text == block.label)))
        linear_single_block = false;
      if(term.kind == Instruction::IK_SWITCH) {
        linear_single_block = term.second.text != block.label;
        for(std::size_t i = 1;
            linear_single_block && i < term.args.size(); i += 2)
          linear_single_block = term.args[i].text != block.label;
      }
    }
  }
  if(linear_single_block) {
    typedef std::unordered_set<std::string, std::hash<std::string>,
      std::equal_to<std::string>, StringAllocator> LiveSlots;
    LiveSlots live(0, std::hash<std::string>(), std::equal_to<std::string>(),
                   StringAllocator(&arena));
    std::vector<Instruction> & instructions =
      function->blocks[0].instructions;
    std::vector<unsigned char> dead(instructions.size(), 0);
    std::size_t removed = 0;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if(ins.kind == Instruction::IK_LOAD &&
         ins.first.kind == Operand::OP_SLOT)
        live.insert(ins.first.text);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped.count(ins.second.text)) {
        if(!live.count(ins.second.text)) {
          dead[index - 1] = 1;
          ++removed;
          if(stats) ++stats->rewrites;
          continue;
        }
        live.erase(ins.second.text);
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live.insert(operands[i]->text);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live.insert(ins.args[i].text);
      }
    }
    if(!removed) return false;
    std::size_t kept = 0;
    for(std::size_t index = 0; index < instructions.size(); ++index)
      if(!dead[index]) {
        if(kept != index) instructions[kept] = std::move(instructions[index]);
        ++kept;
      }
    instructions.resize(kept);
    return true;
  }
  const Graph graph = build_graph(*function, stats);
  typedef std::unordered_set<std::string> LiveSlots;
  std::vector<LiveSlots> live_in(function->blocks.size());
  const auto transfer = [&](std::size_t block) {
    LiveSlots live;
    std::vector<std::size_t> successors;
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      live.insert(live_in[successors[i]].begin(), live_in[successors[i]].end());
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.index.count(ins.first.text)) {
        const LiveSlots & handler = live_in[graph.index.find(ins.first.text)->second];
        live.insert(handler.begin(), handler.end());
      }
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live.insert(ins.first.text);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped.count(ins.second.text))
        live.erase(ins.second.text);
      else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT) live.insert(operands[i]->text);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT) live.insert(ins.args[i].text);
      }
      if(stats) ++stats->instruction_visits;
    }
    return live;
  };
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 1);
  for(std::size_t reverse = function->blocks.size(); reverse > 0; --reverse)
    work.push_back(reverse - 1);
  while(!work.empty()) {
    const std::size_t block = work.front();
    work.pop_front();
    queued[block] = 0;
    LiveSlots live = transfer(block);
    if(live == live_in[block]) continue;
    live_in[block].swap(live);
    if(stats) ++stats->dataflow_updates;
    for(std::size_t i = 0; i < graph.predecessors[block].size(); ++i) {
      const std::size_t predecessor = graph.predecessors[block][i];
      if(queued[predecessor]) continue;
      queued[predecessor] = 1;
      work.push_back(predecessor);
      if(stats) ++stats->worklist_pushes;
    }
  }

  bool changed = false;
  for(std::size_t reverse = function->blocks.size(); reverse > 0; --reverse) {
    const std::size_t block = reverse - 1;
    LiveSlots live;
    std::vector<std::size_t> successors;
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      live.insert(live_in[successors[i]].begin(), live_in[successors[i]].end());
    std::vector<Instruction> kept_reverse;
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.index.count(ins.first.text)) {
        const LiveSlots & handler = live_in[graph.index.find(ins.first.text)->second];
        live.insert(handler.begin(), handler.end());
      }
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live.insert(ins.first.text);
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped.count(ins.second.text)) {
        if(!live.count(ins.second.text)) {
          changed = true;
          if(stats) ++stats->rewrites;
          continue;
        }
        live.erase(ins.second.text);
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT) live.insert(operands[i]->text);
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT) live.insert(ins.args[i].text);
      }
      kept_reverse.push_back(std::move(ins));
    }
    std::reverse(kept_reverse.begin(), kept_reverse.end());
    function->blocks[block].instructions.swap(kept_reverse);
  }
  return changed;
}

bool remove_dead_slots(Function * function, Stats * stats)
{
  if(function->slots.empty()) return false;
  PassArena arena;
  typedef PassAllocator<std::pair<const std::string, std::size_t> >
    SizeAllocator;
  typedef PassAllocator<std::string> StringAllocator;
  std::unordered_map<std::string, std::size_t, std::hash<std::string>,
    std::equal_to<std::string>, SizeAllocator> loads(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      SizeAllocator(&arena));
  std::unordered_set<std::string, std::hash<std::string>,
    std::equal_to<std::string>, StringAllocator> escaped(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      StringAllocator(&arena));
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        ++loads[ins.first.text];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          escaped.insert(values[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT) escaped.insert(ins.args[k].text);
    }
  std::unordered_set<std::string, std::hash<std::string>,
    std::equal_to<std::string>, StringAllocator> dead(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      StringAllocator(&arena));
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(!loads[function->slots[i].first] && !escaped.count(function->slots[i].first))
      dead.insert(function->slots[i].first);
  if(dead.empty()) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      if((ins.kind == Instruction::IK_LOAD &&
          dead.count(ins.first.text)) ||
         (ins.kind == Instruction::IK_STORE &&
          dead.count(ins.second.text))) {
        if(stats) ++stats->rewrites;
        continue;
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  const std::size_t original_slots = function->slots.size();
  std::size_t kept_slots = 0;
  for(std::size_t i = 0; i < original_slots; ++i)
    if(!dead.count(function->slots[i].first)) {
      if(kept_slots != i)
        function->slots[kept_slots] = std::move(function->slots[i]);
      ++kept_slots;
    }
  function->slots.resize(kept_slots);
  return true;
}

bool local_slot_forward(Function * function, Stats * stats)
{
  if(function->slots.empty()) return false;
  struct UseBlocks
  {
    std::size_t first = kNoBlock;
    bool multiple = false;
  };
  PassArena use_arena;
  typedef PassAllocator<std::pair<const std::string, UseBlocks> >
    UseAllocator;
  std::unordered_map<std::string, UseBlocks, std::hash<std::string>,
    std::equal_to<std::string>, UseAllocator> use_blocks(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      UseAllocator(&use_arena));
  const auto note_use = [&use_blocks](const std::string & name,
                                      std::size_t block) {
    UseBlocks & uses = use_blocks[name];
    if(uses.first == kNoBlock) uses.first = block;
    else if(uses.first != block) uses.multiple = true;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          note_use(operands[k]->text, i);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          note_use(ins.args[k].text, i);
    }
  bool changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    PassArena block_arena;
    typedef PassAllocator<std::pair<const std::string, Operand> >
      OperandAllocator;
    std::unordered_map<std::string, Operand, std::hash<std::string>,
      std::equal_to<std::string>, OperandAllocator> values(
        0, std::hash<std::string>(), std::equal_to<std::string>(),
        OperandAllocator(&block_arena));
    std::unordered_map<std::string, Operand, std::hash<std::string>,
      std::equal_to<std::string>, OperandAllocator> aliases(
        0, std::hash<std::string>(), std::equal_to<std::string>(),
        OperandAllocator(&block_arena));
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP && aliases.count(operands[k]->text))
          *operands[k] = aliases[operands[k]->text];
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP && aliases.count(ins.args[k].text))
          ins.args[k] = aliases[ins.args[k].text];
      // Taking a slot's address or storing through an indirect pointer can
      // change a previously recorded slot value.  Inlining commonly exposes
      // exactly this shape, so retaining the old value here would turn a real
      // load into a stale constant before the escape-aware O2 pass sees it.
      const Operand * slot_operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(slot_operands[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          values.erase(slot_operands[k]->text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind != Operand::OP_SLOT)
        values.clear();
      if(ins.kind == Instruction::IK_STORE && ins.second.kind == Operand::OP_SLOT) {
        values[ins.second.text] = ins.first;
      } else if(ins.kind == Instruction::IK_LOAD &&
                ins.first.kind == Operand::OP_SLOT && values.count(ins.first.text) &&
                (!use_blocks.count(ins.dest) ||
                 (!use_blocks.find(ins.dest)->second.multiple &&
                  use_blocks.find(ins.dest)->second.first == i))) {
        aliases[ins.dest] = values[ins.first.text];
        changed = true;
        if(stats) ++stats->rewrites;
        continue;
      } else {
        if(ins.kind == Instruction::IK_CALL || ins.kind == Instruction::IK_COPYOBJ ||
           ins.kind == Instruction::IK_ZEROINIT || is_eh_instruction(ins.kind))
          values.clear();
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  return changed;
}

bool forward_single_store_slots(Function * function, Stats * stats)
{
  if(function->slots.empty()) return false;
  struct SlotFact
  {
    std::size_t stores = 0;
    std::size_t store_block = 0;
    std::size_t store_instruction = 0;
    Operand value;
    bool escaped = false;
    bool dominates_loads = true;
    std::size_t first_entry_load = kNoBlock;
    bool has_nonentry_load = false;
  };
  struct LoadFact
  {
    std::size_t slot;
    std::string destination;
  };
  PassArena arena;
  typedef PassAllocator<std::pair<const std::string, std::size_t> >
    SizeAllocator;
  typedef PassAllocator<std::pair<const std::string, Operand> >
    OperandAllocator;
  typedef PassAllocator<std::string> StringAllocator;
  std::unordered_map<std::string, std::size_t, std::hash<std::string>,
    std::equal_to<std::string>, SizeAllocator> slot_index(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      SizeAllocator(&arena));
  slot_index.reserve(function->slots.size());
  std::vector<SlotFact> facts(function->slots.size());
  std::vector<unsigned char> eligible(function->slots.size(), 0);
  std::vector<LoadFact> loads;
  std::unordered_set<std::string, std::hash<std::string>,
    std::equal_to<std::string>, StringAllocator> storage_temporaries(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      StringAllocator(&arena));
  std::size_t first_exception_edge = kNoBlock;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(function->slots[i].second.kind != lowir_model::LTK_OBJECT) {
      slot_index[function->slots[i].first] = i;
      eligible[i] = 1;
    }
  const auto find_slot = [&slot_index](const Operand & operand) {
    if(operand.kind != Operand::OP_SLOT) return kNoBlock;
    const std::unordered_map<std::string, std::size_t>::const_iterator found =
      slot_index.find(operand.text);
    return found == slot_index.end() ? kNoBlock : found->second;
  };
  for(std::size_t b = 0; b < function->blocks.size(); ++b)
    for(std::size_t j = 0; j < function->blocks[b].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[b].instructions[j];
      const std::size_t stored_slot = ins.kind == Instruction::IK_STORE ?
        find_slot(ins.second) : kNoBlock;
      if(stored_slot != kNoBlock) {
        SlotFact & fact = facts[stored_slot];
        ++fact.stores;
        fact.store_block = b;
        fact.store_instruction = j;
        fact.value = ins.first;
      }
      const std::size_t loaded_slot = ins.kind == Instruction::IK_LOAD ?
        find_slot(ins.first) : kNoBlock;
      if(loaded_slot != kNoBlock) {
        SlotFact & fact = facts[loaded_slot];
        if(b == 0) fact.first_entry_load =
          std::min(fact.first_entry_load, j);
        else fact.has_nonentry_load = true;
        loads.push_back(LoadFact{loaded_slot, ins.dest});
      }
      if(b == 0 && (ins.kind == Instruction::IK_EH_TRY ||
                    ins.kind == Instruction::IK_EH_CLEANUP))
        first_exception_edge = std::min(first_exception_edge, j);
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.first.text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.second.text);
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k) {
        const std::size_t slot = find_slot(*operands[k]);
        if(slot != kNoBlock &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          facts[slot].escaped = true;
      }
      for(std::size_t k = 0; k < ins.args.size(); ++k) {
        const std::size_t slot = find_slot(ins.args[k]);
        if(slot != kNoBlock) facts[slot].escaped = true;
      }
    }
  for(std::size_t i = 0; i < facts.size(); ++i) {
    if(!eligible[i]) continue;
    SlotFact & fact = facts[i];
    if(fact.stores != 1 || fact.store_block != 0 || fact.escaped) continue;
    if(fact.first_entry_load < fact.store_instruction ||
       (fact.has_nonentry_load &&
        first_exception_edge < fact.store_instruction))
      fact.dominates_loads = false;
  }
  std::vector<unsigned char> forwarded(facts.size(), 0);
  std::size_t forwarded_count = 0;
  for(std::size_t i = 0; i < facts.size(); ++i)
    if(eligible[i] && facts[i].stores == 1 && facts[i].store_block == 0 &&
       !facts[i].escaped && facts[i].dominates_loads) {
      forwarded[i] = 1;
      ++forwarded_count;
    }
  if(!forwarded_count) return false;

  std::unordered_map<std::string, Operand, std::hash<std::string>,
    std::equal_to<std::string>, OperandAllocator> aliases(
      0, std::hash<std::string>(), std::equal_to<std::string>(),
      OperandAllocator(&arena));
  for(std::size_t i = 0; i < loads.size(); ++i)
    if(forwarded[loads[i].slot] &&
       !storage_temporaries.count(loads[i].destination))
      aliases[loads[i].destination] = facts[loads[i].slot].value;
  const auto resolve_alias = [&](Operand value) {
    for(std::size_t step = 0;
        step < aliases.size() && value.kind == Operand::OP_TEMP; ++step) {
      const std::unordered_map<std::string, Operand>::const_iterator found =
        aliases.find(value.text);
      if(found == aliases.end()) break;
      value = found->second;
    }
    return value;
  };
  for(std::size_t b = 0; b < function->blocks.size(); ++b) {
    std::vector<Instruction> & instructions =
      function->blocks[b].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      const std::size_t loaded_slot = ins.kind == Instruction::IK_LOAD ?
        find_slot(ins.first) : kNoBlock;
      const std::size_t stored_slot = ins.kind == Instruction::IK_STORE ?
        find_slot(ins.second) : kNoBlock;
      if(loaded_slot != kNoBlock && forwarded[loaded_slot] &&
         storage_temporaries.count(ins.dest)) {
        ins.first = resolve_alias(facts[loaded_slot].value);
        ins.kind = ins.first.kind == Operand::OP_INTEGER ||
          ins.first.kind == Operand::OP_FLOAT ?
            Instruction::IK_CONST : Instruction::IK_COPY;
        ins.second = Operand();
        ins.third = Operand();
        if(stats) ++stats->rewrites;
      } else if((loaded_slot != kNoBlock && forwarded[loaded_slot]) ||
         (stored_slot != kNoBlock && forwarded[stored_slot])) {
        if(stats) ++stats->rewrites;
        continue;
      } else {
        Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t k = 0; k < 3; ++k)
          *operands[k] = resolve_alias(*operands[k]);
        for(std::size_t k = 0; k < ins.args.size(); ++k)
          ins.args[k] = resolve_alias(ins.args[k]);
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  const std::size_t original_slots = function->slots.size();
  std::size_t kept_slots = 0;
  for(std::size_t i = 0; i < original_slots; ++i)
    if(!forwarded[i]) {
      if(kept_slots != i)
        function->slots[kept_slots] = std::move(function->slots[i]);
      ++kept_slots;
    }
  function->slots.resize(kept_slots);
  return true;
}

struct AbstractState
{
  bool executable = false;
  std::unordered_map<std::string, Operand> values;
};

bool meet_state(AbstractState * target, const AbstractState & incoming)
{
  if(!incoming.executable) return false;
  if(!target->executable) { *target = incoming; return true; }
  bool changed = false;
  for(std::unordered_map<std::string, Operand>::iterator it = target->values.begin();
      it != target->values.end();) {
    const std::unordered_map<std::string, Operand>::const_iterator found =
      incoming.values.find(it->first);
    if(found == incoming.values.end() || !same_operand(it->second, found->second)) {
      it = target->values.erase(it);
      changed = true;
    } else ++it;
  }
  return changed;
}

Operand abstract_resolve(Operand value, const AbstractState & state)
{
  for(std::size_t step = 0; step < state.values.size() &&
      (value.kind == Operand::OP_TEMP || value.kind == Operand::OP_SLOT);
      ++step) {
    const std::unordered_map<std::string, Operand>::const_iterator found =
      state.values.find(value.text);
    if(found == state.values.end()) break;
    value = found->second;
  }
  return value;
}

void strip_local_facts(AbstractState * state,
                       const std::vector<std::string> & local_temporaries)
{
  if(local_temporaries.empty()) return;
  const std::unordered_set<std::string> locals(
    local_temporaries.begin(), local_temporaries.end());
  for(std::unordered_map<std::string, Operand>::iterator it =
        state->values.begin(); it != state->values.end();) {
    if(locals.count(it->first) ||
       (it->second.kind == Operand::OP_TEMP && locals.count(it->second.text)))
      it = state->values.erase(it);
    else ++it;
  }
}

void rewrite_promoted_slots(Function * function,
                            const std::unordered_set<std::string> & promoted,
                            const std::unordered_set<std::string> & storage,
                            const std::unordered_map<std::string, Operand> & loads,
                            Stats * stats)
{
  const auto resolve_load = [&loads](Operand value) {
    for(std::size_t step = 0;
        step < loads.size() && value.kind == Operand::OP_TEMP; ++step) {
      const std::unordered_map<std::string, Operand>::const_iterator found =
        loads.find(value.text);
      if(found == loads.end()) break;
      value = found->second;
    }
    return value;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::unordered_map<std::string, Operand> aliases = loads;
    for(std::unordered_set<std::string>::const_iterator it = storage.begin();
        it != storage.end(); ++it)
      aliases.erase(*it);
    const auto resolve = [&aliases](Operand value) {
      for(std::size_t step = 0;
          step < aliases.size() && value.kind == Operand::OP_TEMP; ++step) {
        const std::unordered_map<std::string, Operand>::const_iterator found =
          aliases.find(value.text);
        if(found == aliases.end()) break;
        value = found->second;
      }
      return value;
    };
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        *operands[k] = resolve(*operands[k]);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        ins.args[k] = resolve(ins.args[k]);
      if(ins.kind == Instruction::IK_LOAD && promoted.count(ins.first.text) &&
         storage.count(ins.dest)) {
        ins.first = resolve_load(loads.find(ins.dest)->second);
        ins.kind = ins.first.kind == Operand::OP_INTEGER ||
          ins.first.kind == Operand::OP_FLOAT ?
            Instruction::IK_CONST : Instruction::IK_COPY;
        ins.second = Operand();
        ins.third = Operand();
        if(stats) ++stats->rewrites;
      } else if((ins.kind == Instruction::IK_LOAD &&
                 promoted.count(ins.first.text)) ||
                (ins.kind == Instruction::IK_STORE &&
                 promoted.count(ins.second.text))) {
        if(stats) ++stats->rewrites;
        continue;
      }
      if(kept != j) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  const std::size_t original_slots = function->slots.size();
  std::size_t kept_slots = 0;
  for(std::size_t i = 0; i < original_slots; ++i)
    if(!promoted.count(function->slots[i].first)) {
      if(kept_slots != i)
        function->slots[kept_slots] = std::move(function->slots[i]);
      ++kept_slots;
    }
  function->slots.resize(kept_slots);
}

bool promote_slots(Function * function, Stats * stats)
{
  if(function->blocks.empty() || function->slots.empty()) return false;
  std::unordered_set<std::string> eligible;
  for(std::size_t i = 0; i < function->slots.size(); ++i)
    if(function->slots[i].second.kind != lowir_model::LTK_OBJECT)
      eligible.insert(function->slots[i].first);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          eligible.erase(values[k]->text);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT) eligible.erase(ins.args[k].text);
  }
  if(eligible.empty()) return false;
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  if(exceeds_state_budget(function->blocks.size(), eligible.size(),
                          instruction_total)) {
    if(stats) ++stats->budget_skips;
    return false;
  }

  std::unordered_set<std::string> storage_temporaries;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.first.text);
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries.insert(ins.second.text);
    }

  const Graph graph = build_graph(*function, stats);
  std::vector<AbstractState> incoming(function->blocks.size());
  incoming[0].executable = true;
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 0);
  work.push_back(0); queued[0] = 1;
  if(stats) ++stats->worklist_pushes;
  std::vector<std::unordered_map<std::string, Operand> > replacements(
    function->blocks.size());
  while(!work.empty()) {
    const std::size_t block_index = work.front(); work.pop_front();
    queued[block_index] = 0;
    AbstractState state = incoming[block_index];
    replacements[block_index].clear();
    const Block & block = function->blocks[block_index];
    std::vector<std::pair<std::size_t, AbstractState> > exceptional;
    std::vector<std::string> local_temporaries;
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const Instruction & source = block.instructions[i];
      Instruction ins;
      ins.kind = source.kind;
      ins.dest = source.dest;
      ins.type = source.type;
      ins.source_type = source.source_type;
      ins.op = source.op;
      ins.first = abstract_resolve(source.first, state);
      ins.second = abstract_resolve(source.second, state);
      ins.third = abstract_resolve(source.third, state);
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.index.count(ins.first.text)) {
        AbstractState handler = state;
        strip_local_facts(&handler, local_temporaries);
        exceptional.push_back(std::make_pair(
          graph.index.find(ins.first.text)->second, handler));
      }
      if(ins.kind == Instruction::IK_STORE &&
         source.second.kind == Operand::OP_SLOT &&
         eligible.count(source.second.text))
        state.values[source.second.text] = ins.first;
      else if(ins.kind == Instruction::IK_LOAD &&
              source.first.kind == Operand::OP_SLOT &&
              eligible.count(source.first.text)) {
        const std::unordered_map<std::string, Operand>::const_iterator value =
          state.values.find(source.first.text);
        if(value != state.values.end()) {
          state.values[ins.dest] = value->second;
          local_temporaries.push_back(ins.dest);
          replacements[block_index][ins.dest] = value->second;
        }
      } else if(!ins.dest.empty()) {
        Operand folded;
        bool known = ins.kind == Instruction::IK_CONST ? (folded = ins.first, true) :
          ins.kind == Instruction::IK_COPY &&
            ins.dest.compare(0, 5, "%dbg_") != 0 ? (folded = ins.first, true) :
          ins.kind == Instruction::IK_UNARY ? fold_unary(ins, &folded) :
          ins.kind == Instruction::IK_BINARY ? fold_binary(ins, &folded) :
          ins.kind == Instruction::IK_CMP ? fold_compare(ins, &folded) :
          ins.kind == Instruction::IK_CONVERT ? fold_convert(ins, &folded) : false;
        if(known) {
          state.values[ins.dest] = folded;
          local_temporaries.push_back(ins.dest);
        }
        else state.values.erase(ins.dest);
      }
      if(stats) ++stats->instruction_visits;
    }
    std::vector<std::size_t> normal;
    if(!block.instructions.empty()) {
      const Instruction & term = block.instructions.back();
      const Operand selector = abstract_resolve(term.first, state);
      if(term.kind == Instruction::IK_JUMP && graph.index.count(selector.text))
        normal.push_back(graph.index.find(selector.text)->second);
      else if(term.kind == Instruction::IK_BRANCH) {
        const Operand & selected = selector.kind == Operand::OP_INTEGER &&
          selector.has_int_value ?
          (selector.int_value ? term.second : term.third) : term.second;
        if(graph.index.count(selected.text))
          normal.push_back(graph.index.find(selected.text)->second);
        if(!(selector.kind == Operand::OP_INTEGER && selector.has_int_value) &&
           graph.index.count(term.third.text))
          normal.push_back(graph.index.find(term.third.text)->second);
      } else if(term.kind == Instruction::IK_SWITCH) {
        Operand selected = term.second;
        if(selector.kind == Operand::OP_INTEGER && selector.has_int_value)
          for(std::size_t i = 0; i + 1 < term.args.size(); i += 2) {
            Operand case_value = abstract_resolve(term.args[i], state);
            if(case_value.kind == Operand::OP_INTEGER && case_value.has_int_value &&
               case_value.int_value == selector.int_value) selected = term.args[i + 1];
          }
        if(graph.index.count(selected.text))
          normal.push_back(graph.index.find(selected.text)->second);
        if(!(selector.kind == Operand::OP_INTEGER && selector.has_int_value))
          for(std::size_t i = 1; i < term.args.size(); i += 2)
            if(graph.index.count(term.args[i].text))
              normal.push_back(graph.index.find(term.args[i].text)->second);
      }
    }
    strip_local_facts(&state, local_temporaries);
    for(std::size_t i = 0; i < exceptional.size(); ++i) {
      if(meet_state(&incoming[exceptional[i].first], exceptional[i].second) &&
         !queued[exceptional[i].first]) {
        work.push_back(exceptional[i].first); queued[exceptional[i].first] = 1;
        if(stats) { ++stats->worklist_pushes; ++stats->dataflow_updates; }
      }
    }
    for(std::size_t i = 0; i < normal.size(); ++i)
      if(meet_state(&incoming[normal[i]], state) && !queued[normal[i]]) {
        work.push_back(normal[i]); queued[normal[i]] = 1;
        if(stats) { ++stats->worklist_pushes; ++stats->dataflow_updates; }
      }
  }

  std::unordered_set<std::string> replacement_temporaries;
  for(std::size_t block = 0; block < replacements.size(); ++block)
    for(std::unordered_map<std::string, Operand>::const_iterator replacement =
          replacements[block].begin(); replacement != replacements[block].end();
        ++replacement)
      if(replacement->second.kind == Operand::OP_TEMP)
        replacement_temporaries.insert(replacement->second.text);
  std::unordered_map<std::string, std::size_t> replacement_definitions;
  std::unordered_set<std::string> parameter_temporaries;
  if(!replacement_temporaries.empty()) {
    for(std::size_t parameter = 0;
        parameter < function->params.size(); ++parameter)
      if(replacement_temporaries.count(function->params[parameter].name))
        parameter_temporaries.insert(function->params[parameter].name);
    for(std::size_t block = 0; block < function->blocks.size(); ++block)
      for(std::size_t instruction = 0;
          instruction < function->blocks[block].instructions.size();
          ++instruction) {
        const std::string & destination =
          function->blocks[block].instructions[instruction].dest;
        if(!destination.empty() && replacement_temporaries.count(destination))
          replacement_definitions[destination] = block;
      }
  }
  std::unordered_set<std::string> slots_with_loads;
  std::unordered_set<std::string> slots_with_unresolved_loads;
  std::unordered_map<std::string, std::string> load_slots;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind != Instruction::IK_LOAD || ins.first.kind != Operand::OP_SLOT ||
         !eligible.count(ins.first.text))
        continue;
      slots_with_loads.insert(ins.first.text);
      load_slots[ins.dest] = ins.first.text;
      const std::unordered_map<std::string, Operand>::const_iterator replacement =
        replacements[i].find(ins.dest);
      bool textually_available = replacement != replacements[i].end();
      if(textually_available && replacement->second.kind == Operand::OP_TEMP &&
         !parameter_temporaries.count(replacement->second.text)) {
        const std::unordered_map<std::string, std::size_t>::const_iterator
          definition = replacement_definitions.find(replacement->second.text);
        textually_available = definition != replacement_definitions.end() &&
          definition->second <= i;
      }
      if(!textually_available)
        slots_with_unresolved_loads.insert(ins.first.text);
    }
  std::unordered_set<std::string> promoted;
  for(std::unordered_set<std::string>::const_iterator slot = eligible.begin();
      slot != eligible.end(); ++slot)
    if(slots_with_loads.count(*slot) && !slots_with_unresolved_loads.count(*slot))
      promoted.insert(*slot);
  if(promoted.empty()) return false;
  std::unordered_map<std::string, Operand> load_aliases;
  for(std::size_t i = 0; i < replacements.size(); ++i)
    for(std::unordered_map<std::string, Operand>::const_iterator it =
          replacements[i].begin(); it != replacements[i].end(); ++it)
      if(load_slots.count(it->first) &&
         promoted.count(load_slots.find(it->first)->second))
        load_aliases[it->first] = it->second;
  rewrite_promoted_slots(function, promoted, storage_temporaries,
    load_aliases, stats);
  return true;
}

std::unordered_map<std::string, FunctionBoundaryMetadata>
function_boundaries(const LowirProgram & program)
{
  std::unordered_map<std::string, FunctionBoundaryMetadata> result;
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i)
    result[program.function_declarations[i].name] =
      program.function_declarations[i].boundary;
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    result[program.functions[i].name] = program.functions[i].boundary;
  return result;
}

std::size_t instruction_count(const LowirProgram & program)
{
  std::size_t result = 0;
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    for(std::size_t j = 0; j < program.functions[i].blocks.size(); ++j)
      result += program.functions[i].blocks[j].instructions.size();
  return result;
}

typedef bool (*FunctionPass)(Function *, Stats *);

bool timed_function_pass(FunctionPass pass, Function * function,
                         Stats * stats, std::size_t Stats::* runs,
                         std::uint64_t Stats::* nanoseconds)
{
  if(!stats) return pass(function, 0);
  ++(stats->*runs);
  std::size_t Stats::* detailed_runs = 0;
  std::size_t Stats::* detailed_changes = 0;
  std::uint64_t Stats::* detailed_nanoseconds = 0;
  if(pass == forward_single_store_slots) {
    detailed_runs = &Stats::forward_slot_runs;
    detailed_changes = &Stats::forward_slot_changes;
    detailed_nanoseconds = &Stats::forward_slot_nanoseconds;
  } else if(pass == local_slot_forward) {
    detailed_runs = &Stats::local_slot_runs;
    detailed_changes = &Stats::local_slot_changes;
    detailed_nanoseconds = &Stats::local_slot_nanoseconds;
  } else if(pass == remove_dead_slots) {
    detailed_runs = &Stats::remove_slot_runs;
    detailed_changes = &Stats::remove_slot_changes;
    detailed_nanoseconds = &Stats::remove_slot_nanoseconds;
  } else if(pass == promote_slots) {
    detailed_runs = &Stats::promote_slot_runs;
    detailed_changes = &Stats::promote_slot_changes;
    detailed_nanoseconds = &Stats::promote_slot_nanoseconds;
  } else if(pass == eliminate_dead_slot_stores) {
    detailed_runs = &Stats::dead_store_runs;
    detailed_changes = &Stats::dead_store_changes;
    detailed_nanoseconds = &Stats::dead_store_nanoseconds;
  }
  if(detailed_runs) ++(stats->*detailed_runs);
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  const bool changed = pass(function, stats);
  const std::uint64_t elapsed = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
  if(changed) {
    if(pass == simplify_values) ++stats->simplify_changes;
    else if(pass == cleanup_cfg) ++stats->cfg_changes;
    else ++stats->slot_changes;
    if(detailed_changes) ++(stats->*detailed_changes);
  }
  stats->*nanoseconds += elapsed;
  if(detailed_nanoseconds) stats->*detailed_nanoseconds += elapsed;
  return changed;
}

bool timed_dce(Function * function,
               const std::unordered_map<std::string,
                 FunctionBoundaryMetadata> & boundaries,
               Stats * stats)
{
  if(!stats) return eliminate_dead_code(function, boundaries, 0);
  ++stats->dce_runs;
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  const bool changed = eliminate_dead_code(function, boundaries, stats);
  if(changed) ++stats->dce_changes;
  stats->dce_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
  return changed;
}

}  // namespace

void optimize(LowirProgram & program, int level, Stats * stats)
{
  if(level < 0 || level > 2) throw std::logic_error("invalid LowIR optimization level");
  std::chrono::steady_clock::time_point started;
  if(stats) {
    started = std::chrono::steady_clock::now();
    *stats = Stats();
    stats->functions = program.functions.size();
    stats->input_instructions = instruction_count(program);
  }
  if(level == 0) {
    if(stats) {
      stats->output_instructions = stats->input_instructions;
      stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started).count());
    }
    return;
  }
  const std::unordered_map<std::string, FunctionBoundaryMetadata> boundaries =
    function_boundaries(program);
  std::unordered_set<std::string> inlined_functions;
  std::chrono::steady_clock::time_point inline_started;
  if(stats) inline_started = std::chrono::steady_clock::now();
  const std::size_t inline_rewrites =
    inline_o1_calls(program, &inlined_functions, stats);
  if(stats) {
    stats->rewrites += inline_rewrites;
    stats->inline_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - inline_started).count());
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    Function & function = program.functions[i];
    // Keep this an explicit bounded schedule.  A stage is revisited only when
    // a preceding transform can have exposed work in that stage; the
    // individual propagation and liveness analyses use their own dirty
    // worklists.
    timed_function_pass(simplify_values, &function, stats,
      &Stats::simplify_runs, &Stats::simplify_nanoseconds);
    timed_dce(&function, boundaries, stats);
    const bool initial_cfg_changed = timed_function_pass(
      cleanup_cfg, &function, stats,
      &Stats::cfg_runs, &Stats::cfg_nanoseconds);
    const std::chrono::steady_clock::time_point cleanup_resume_started =
      stats ? std::chrono::steady_clock::now() :
              std::chrono::steady_clock::time_point();
    share_terminal_resume_blocks(&function, stats);
    if(stats) stats->cleanup_resume_nanoseconds +=
      static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - cleanup_resume_started).count());
    const std::chrono::steady_clock::time_point cleanup_tail_started =
      stats ? std::chrono::steady_clock::now() :
              std::chrono::steady_clock::time_point();
    share_exact_cleanup_tails(&function, stats);
    if(stats) stats->cleanup_tail_nanoseconds +=
      static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - cleanup_tail_started).count());
    bool post_cfg_values_changed = false;
    if(initial_cfg_changed) {
      post_cfg_values_changed = timed_function_pass(
        simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds);
      timed_dce(&function, boundaries, stats);
    }
    bool slot_values_changed = false;
    if(inlined_functions.count(function.name) || level >= 2)
      slot_values_changed = timed_function_pass(
        forward_single_store_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds);
    if(inlined_functions.count(function.name))
      slot_values_changed = timed_function_pass(
        local_slot_forward, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds) || slot_values_changed;
    if(slot_values_changed) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds);
      timed_dce(&function, boundaries, stats);
    }
    if(post_cfg_values_changed || slot_values_changed)
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds);
    if(timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds)) {
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds);
      timed_dce(&function, boundaries, stats);
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds);
    }
    if(level >= 2 && timed_function_pass(promote_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds)) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds);
      timed_dce(&function, boundaries, stats);
      if(timed_function_pass(cleanup_cfg, &function, stats,
          &Stats::cfg_runs, &Stats::cfg_nanoseconds)) {
        timed_function_pass(simplify_values, &function, stats,
          &Stats::simplify_runs, &Stats::simplify_nanoseconds);
        timed_dce(&function, boundaries, stats);
      }
      timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds);
    }
    if(level >= 2 && timed_function_pass(eliminate_dead_slot_stores,
        &function, stats, &Stats::slot_runs, &Stats::slot_nanoseconds)) {
      timed_dce(&function, boundaries, stats);
      timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds);
    }
  }
  if(stats) {
    stats->output_instructions = instruction_count(program);
    stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }
}

}  // namespace lowir_opt
