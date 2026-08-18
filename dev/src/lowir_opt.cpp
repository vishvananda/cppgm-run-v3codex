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
using lowir_model::BlockId;
using lowir_model::Function;
using lowir_model::FunctionBoundaryMetadata;
using lowir_model::Instruction;
using lowir_model::LowOperation;
using lowir_model::LowType;
using lowir_model::LowTypeKind;
using lowir_model::LowirProgram;
using lowir_model::Operand;

const std::size_t kNoBlockIndex = static_cast<std::size_t>(-1);
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
  if(a.kind != b.kind) return false;
  if(a.kind == Operand::OP_LABEL) return a.block == b.block;
  if(a.kind == Operand::OP_SLOT) return a.slot == b.slot;
  if(a.kind == Operand::OP_TEMP) return a.value == b.value;
  if(a.kind == Operand::OP_GLOBAL) return a.symbol == b.symbol;
  if(a.kind == Operand::OP_INTEGER) {
    if(a.has_spelling && b.has_spelling) return a.literal == b.literal;
    return a.has_int_value == b.has_int_value &&
      a.int_value == b.int_value && a.int_high == b.int_high;
  }
  if(a.kind == Operand::OP_FLOAT) {
    if(a.has_spelling && b.has_spelling) return a.literal == b.literal;
    return (std::isnan(a.float_value) && std::isnan(b.float_value)) ||
      a.float_value == b.float_value;
  }
  return true;
}

Operand integer_operand(long long value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_INTEGER;
  result.has_int_value = true;
  result.int_value = value;
  result.int_high = value < 0 ? ~UINT64_C(0) : 0;
  return result;
}

Operand floating_operand(long double value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_FLOAT;
  result.float_value = value;
  result.literal_type = type;
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
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  return width >= 128 ? ~static_cast<WideUnsigned>(0) :
    (static_cast<WideUnsigned>(1) << width) - 1;
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
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  return width >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << width) - 1;
}

long long normalize_integer(std::uint64_t value, const LowType & type)
{
  value &= width_mask(type);
  if(type.kind == lowir_model::LTK_U8 || type.kind == lowir_model::LTK_U16 ||
     type.kind == lowir_model::LTK_U32 || type.kind == lowir_model::LTK_PTR)
    return static_cast<long long>(value);
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  if(width && width < 64 && (value & (UINT64_C(1) << (width - 1))))
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

bool commutative(LowOperation op)
{
  return op.kind == LowOperation::LOP_ADD || op.kind == LowOperation::LOP_MUL || op.kind == LowOperation::LOP_AND || op.kind == LowOperation::LOP_OR ||
    op.kind == LowOperation::LOP_XOR;
}

LowOperation reverse_compare(LowOperation op)
{
  if(op.kind == LowOperation::LOP_LT) return LowOperation::LOP_GT;
  if(op.kind == LowOperation::LOP_LE) return LowOperation::LOP_GE;
  if(op.kind == LowOperation::LOP_GT) return LowOperation::LOP_LT;
  if(op.kind == LowOperation::LOP_GE) return LowOperation::LOP_LE;
  if(op.kind == LowOperation::LOP_ULT) return LowOperation::LOP_UGT;
  if(op.kind == LowOperation::LOP_ULE) return LowOperation::LOP_UGE;
  if(op.kind == LowOperation::LOP_UGT) return LowOperation::LOP_ULT;
  if(op.kind == LowOperation::LOP_UGE) return LowOperation::LOP_ULE;
  return op;
}

bool operand_less(const Operand & a, const Operand & b)
{
  if(a.kind != b.kind) return a.kind < b.kind;
  if(a.kind == Operand::OP_SLOT) return a.slot < b.slot;
  if(a.kind == Operand::OP_LABEL) return a.block < b.block;
  if(a.kind == Operand::OP_TEMP) return a.value < b.value;
  if(a.kind == Operand::OP_GLOBAL) return a.symbol < b.symbol;
  if((a.kind == Operand::OP_INTEGER || a.kind == Operand::OP_FLOAT) &&
     a.has_spelling && b.has_spelling)
    return a.literal < b.literal;
  if(a.kind == Operand::OP_INTEGER)
    return a.int_high != b.int_high ? a.int_high < b.int_high :
      static_cast<std::uint64_t>(a.int_value) <
        static_cast<std::uint64_t>(b.int_value);
  if(a.kind == Operand::OP_FLOAT) return a.float_value < b.float_value;
  return false;
}

struct ExpressionOperandKey
{
  Operand::Kind kind;
  std::uint32_t identity;
  long long int_value;
  std::uint64_t int_high;
  long double float_value;

  bool operator==(const ExpressionOperandKey & other) const
  {
    if(kind != other.kind || identity != other.identity ||
       int_value != other.int_value || int_high != other.int_high)
      return false;
    if(kind != Operand::OP_FLOAT || identity != lowir_model::kInvalidCompactId)
      return true;
    return (std::isnan(float_value) && std::isnan(other.float_value)) ||
      float_value == other.float_value;
  }
};

ExpressionOperandKey expression_operand_key(const Operand & operand)
{
  ExpressionOperandKey key;
  key.kind = operand.kind;
  key.identity = lowir_model::kInvalidCompactId;
  key.int_value = 0;
  key.int_high = 0;
  key.float_value = 0.0L;
  if(operand.kind == Operand::OP_SLOT) key.identity = operand.slot;
  else if(operand.kind == Operand::OP_LABEL) key.identity = operand.block;
  else if(operand.kind == Operand::OP_TEMP) key.identity = operand.value;
  else if(operand.kind == Operand::OP_GLOBAL) key.identity = operand.symbol;
  else if((operand.kind == Operand::OP_INTEGER ||
           operand.kind == Operand::OP_FLOAT) && operand.has_spelling)
    key.identity = operand.literal;
  else if(operand.kind == Operand::OP_INTEGER) {
    key.int_value = operand.int_value;
    key.int_high = operand.int_high;
  } else if(operand.kind == Operand::OP_FLOAT)
    key.float_value = operand.float_value;
  return key;
}

struct ExpressionKey
{
  Instruction::Kind kind;
  LowOperation op;
  LowTypeKind type_kind;
  std::size_t type_size;
  std::size_t type_alignment;
  LowTypeKind source_type_kind;
  std::size_t source_type_size;
  std::size_t source_type_alignment;
  lowir_model::IndexProjectionKind index_projection;
  ExpressionOperandKey first;
  ExpressionOperandKey second;

  bool operator==(const ExpressionKey & other) const
  {
    return kind == other.kind && op == other.op &&
      type_kind == other.type_kind && type_size == other.type_size &&
      type_alignment == other.type_alignment &&
      source_type_kind == other.source_type_kind &&
      source_type_size == other.source_type_size &&
      source_type_alignment == other.source_type_alignment &&
      index_projection == other.index_projection &&
      first == other.first && second == other.second;
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
    combine_hash(&result, lowir_model::lowir_operation_hash(key.op));
    combine_hash(&result, static_cast<std::size_t>(key.type_kind));
    combine_hash(&result, key.type_size);
    combine_hash(&result, key.type_alignment);
    combine_hash(&result, static_cast<std::size_t>(key.source_type_kind));
    combine_hash(&result, key.source_type_size);
    combine_hash(&result, key.source_type_alignment);
    combine_hash(&result, static_cast<std::size_t>(key.index_projection));
    const ExpressionOperandKey operands[] = {key.first, key.second};
    for(std::size_t i = 0; i < 2; ++i) {
      combine_hash(&result, static_cast<std::size_t>(operands[i].kind));
      combine_hash(&result, operands[i].identity);
      combine_hash(&result, std::hash<long long>()(operands[i].int_value));
      combine_hash(&result, std::hash<std::uint64_t>()(operands[i].int_high));
      const std::size_t floating = std::isnan(operands[i].float_value) ?
        static_cast<std::size_t>(0x7ff80000U) :
        std::hash<long double>()(operands[i].float_value);
      combine_hash(&result, floating);
    }
    return result;
  }
};

ExpressionKey expression_key(const Instruction & ins)
{
  const Operand * first = &ins.first;
  const Operand * second = &ins.second;
  LowOperation op = ins.op;
  if((ins.kind == Instruction::IK_BINARY && commutative(op)) ||
     (ins.kind == Instruction::IK_CMP && (op.kind == LowOperation::LOP_EQ || op.kind == LowOperation::LOP_NE))) {
    if(operand_less(*second, *first)) std::swap(first, second);
  } else if(ins.kind == Instruction::IK_CMP && operand_less(*second, *first)) {
    std::swap(first, second);
    op = reverse_compare(op);
  }
  ExpressionKey key;
  key.kind = ins.kind;
  key.op = op;
  key.type_kind = ins.type.kind;
  key.type_size = ins.type.storage_size;
  key.type_alignment = ins.type.alignment;
  key.source_type_kind = ins.source_type.kind;
  key.source_type_size = ins.source_type.storage_size;
  key.source_type_alignment = ins.source_type.alignment;
  key.index_projection = ins.index_projection;
  key.first = expression_operand_key(*first);
  key.second = expression_operand_key(*second);
  return key;
}

bool fold_unary(const Instruction & ins, Operand * result)
{
  if(ins.op.kind == LowOperation::LOP_DECAY && ins.type.kind == lowir_model::LTK_PTR) {
    *result = ins.first;
    return true;
  }
  if(ins.first.kind != Operand::OP_INTEGER || !ins.first.has_int_value ||
     !is_integer_type(ins.type)) return false;
  if(lowir_model::lowir_type_bit_width(ins.type) > 64) {
    const WideUnsigned value = wide_integer(ins.first.int_value);
    WideUnsigned folded = 0;
    if(ins.op.kind == LowOperation::LOP_NEG) folded = -value;
    else if(ins.op.kind == LowOperation::LOP_BITNOT) folded = ~value;
    else if(ins.op.kind == LowOperation::LOP_NOT)
      return (*result = integer_operand(value == 0, ins.type), true);
    else return false;
    return representable_wide_integer(folded, ins.type, result);
  }
  const std::uint64_t value = static_cast<std::uint64_t>(ins.first.int_value);
  if(ins.op.kind == LowOperation::LOP_NEG)
    *result = integer_operand(normalize_integer(UINT64_C(0) - value, ins.type), ins.type);
  else if(ins.op.kind == LowOperation::LOP_BITNOT)
    *result = integer_operand(normalize_integer(~value, ins.type), ins.type);
  else if(ins.op.kind == LowOperation::LOP_NOT)
    *result = integer_operand(value == 0, ins.type);
  else return false;
  return true;
}

bool fold_binary(const Instruction & ins, Operand * result)
{
  if(ins.first.kind != Operand::OP_INTEGER || !ins.first.has_int_value ||
     ins.second.kind != Operand::OP_INTEGER || !ins.second.has_int_value ||
     !is_integer_type(ins.type)) return false;
  if(lowir_model::lowir_type_bit_width(ins.type) > 64) {
    const WideUnsigned a = wide_integer(ins.first.int_value);
    const WideUnsigned b = wide_integer(ins.second.int_value);
    WideUnsigned value = 0;
    if(ins.op.kind == LowOperation::LOP_ADD) value = a + b;
    else if(ins.op.kind == LowOperation::LOP_SUB) value = a - b;
    else if(ins.op.kind == LowOperation::LOP_MUL) value = a * b;
    else if(ins.op.kind == LowOperation::LOP_AND) value = a & b;
    else if(ins.op.kind == LowOperation::LOP_OR) value = a | b;
    else if(ins.op.kind == LowOperation::LOP_XOR) value = a ^ b;
    else if(ins.op.kind == LowOperation::LOP_SHL && b < 128)
      value = a << static_cast<unsigned>(b);
    else if(ins.op.kind == LowOperation::LOP_USHR && b < 128)
      value = a >> static_cast<unsigned>(b);
    else if(ins.op.kind == LowOperation::LOP_SHR && b < 128)
      value = static_cast<WideUnsigned>(static_cast<WideSigned>(a) >>
                                       static_cast<unsigned>(b));
    else if((ins.op.kind == LowOperation::LOP_UDIV || ins.op.kind == LowOperation::LOP_UMOD) && b)
      value = ins.op.kind == LowOperation::LOP_UDIV ? a / b : a % b;
    else if((ins.op.kind == LowOperation::LOP_DIV || ins.op.kind == LowOperation::LOP_MOD) && b) {
      const WideSigned signed_a = static_cast<WideSigned>(a);
      const WideSigned signed_b = static_cast<WideSigned>(b);
      value = static_cast<WideUnsigned>(ins.op.kind == LowOperation::LOP_DIV ?
        signed_a / signed_b : signed_a % signed_b);
    } else return false;
    return representable_wide_integer(value, ins.type, result);
  }
  const std::uint64_t a = static_cast<std::uint64_t>(ins.first.int_value);
  const std::uint64_t b = static_cast<std::uint64_t>(ins.second.int_value);
  std::uint64_t value = 0;
  if(ins.op.kind == LowOperation::LOP_ADD) value = a + b;
  else if(ins.op.kind == LowOperation::LOP_SUB) value = a - b;
  else if(ins.op.kind == LowOperation::LOP_MUL) value = a * b;
  else if(ins.op.kind == LowOperation::LOP_AND) value = a & b;
  else if(ins.op.kind == LowOperation::LOP_OR) value = a | b;
  else if(ins.op.kind == LowOperation::LOP_XOR) value = a ^ b;
  else if(ins.op.kind == LowOperation::LOP_SHL && b < 64) value = a << b;
  else if(ins.op.kind == LowOperation::LOP_USHR && b < 64) value = a >> b;
  else if(ins.op.kind == LowOperation::LOP_SHR && b < 64)
    value = static_cast<std::uint64_t>(ins.first.int_value >> b);
  else if((ins.op.kind == LowOperation::LOP_UDIV || ins.op.kind == LowOperation::LOP_UMOD) && b)
    value = ins.op.kind == LowOperation::LOP_UDIV ? a / b : a % b;
  else if((ins.op.kind == LowOperation::LOP_DIV || ins.op.kind == LowOperation::LOP_MOD) && ins.second.int_value &&
          !(ins.first.int_value == std::numeric_limits<long long>::min() &&
            ins.second.int_value == -1))
    value = static_cast<std::uint64_t>(ins.op.kind == LowOperation::LOP_DIV ?
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
    if(lowir_model::lowir_type_bit_width(ins.type) > 64) {
      const WideSigned signed_a = static_cast<WideSigned>(a);
      const WideSigned signed_b = static_cast<WideSigned>(b);
      const WideUnsigned unsigned_a = static_cast<WideUnsigned>(signed_a);
      const WideUnsigned unsigned_b = static_cast<WideUnsigned>(signed_b);
      if(ins.op.kind == LowOperation::LOP_EQ) value = unsigned_a == unsigned_b;
      else if(ins.op.kind == LowOperation::LOP_NE) value = unsigned_a != unsigned_b;
      else if(ins.op.kind == LowOperation::LOP_LT) value = signed_a < signed_b;
      else if(ins.op.kind == LowOperation::LOP_LE) value = signed_a <= signed_b;
      else if(ins.op.kind == LowOperation::LOP_GT) value = signed_a > signed_b;
      else if(ins.op.kind == LowOperation::LOP_GE) value = signed_a >= signed_b;
      else if(ins.op.kind == LowOperation::LOP_ULT) value = unsigned_a < unsigned_b;
      else if(ins.op.kind == LowOperation::LOP_ULE) value = unsigned_a <= unsigned_b;
      else if(ins.op.kind == LowOperation::LOP_UGT) value = unsigned_a > unsigned_b;
      else if(ins.op.kind == LowOperation::LOP_UGE) value = unsigned_a >= unsigned_b;
      else return false;
      *result = integer_operand(value ? 1 : 0,
        lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
      return true;
    }
    const std::uint64_t ua = static_cast<std::uint64_t>(a) & width_mask(ins.type);
    const std::uint64_t ub = static_cast<std::uint64_t>(b) & width_mask(ins.type);
    if(ins.op.kind == LowOperation::LOP_EQ) value = ua == ub;
    else if(ins.op.kind == LowOperation::LOP_NE) value = ua != ub;
    else if(ins.op.kind == LowOperation::LOP_LT) value = a < b;
    else if(ins.op.kind == LowOperation::LOP_LE) value = a <= b;
    else if(ins.op.kind == LowOperation::LOP_GT) value = a > b;
    else if(ins.op.kind == LowOperation::LOP_GE) value = a >= b;
    else if(ins.op.kind == LowOperation::LOP_ULT) value = ua < ub;
    else if(ins.op.kind == LowOperation::LOP_ULE) value = ua <= ub;
    else if(ins.op.kind == LowOperation::LOP_UGT) value = ua > ub;
    else if(ins.op.kind == LowOperation::LOP_UGE) value = ua >= ub;
    else return false;
  } else if(ins.first.kind == Operand::OP_FLOAT &&
            ins.second.kind == Operand::OP_FLOAT) {
    const long double a = ins.first.float_value;
    const long double b = ins.second.float_value;
    if(ins.op.kind == LowOperation::LOP_EQ) value = a == b;
    else if(ins.op.kind == LowOperation::LOP_NE) value = a != b;
    else if(ins.op.kind == LowOperation::LOP_LT) value = a < b;
    else if(ins.op.kind == LowOperation::LOP_LE) value = a <= b;
    else if(ins.op.kind == LowOperation::LOP_GT) value = a > b;
    else if(ins.op.kind == LowOperation::LOP_GE) value = a >= b;
    else return false;
  } else if(!is_float_type(ins.type) &&
            same_operand(ins.first, ins.second)) {
    if(ins.op.kind == LowOperation::LOP_EQ || ins.op.kind == LowOperation::LOP_LE || ins.op.kind == LowOperation::LOP_GE ||
       ins.op.kind == LowOperation::LOP_ULE || ins.op.kind == LowOperation::LOP_UGE) value = true;
    else if(ins.op.kind == LowOperation::LOP_NE || ins.op.kind == LowOperation::LOP_LT || ins.op.kind == LowOperation::LOP_GT ||
            ins.op.kind == LowOperation::LOP_ULT || ins.op.kind == LowOperation::LOP_UGT) value = false;
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
      if(lowir_model::lowir_type_bit_width(ins.type) > 64) {
        WideUnsigned value = wide_integer(ins.first.int_value);
        if(ins.op.kind == LowOperation::LOP_ZEXT) value &= wide_mask(ins.source_type);
        else if(ins.op.kind == LowOperation::LOP_SEXT &&
                lowir_model::lowir_type_bit_width(ins.source_type) < 128) {
          const WideUnsigned mask = wide_mask(ins.source_type);
          value &= mask;
          const std::size_t source_width =
            lowir_model::lowir_type_bit_width(ins.source_type);
          if(source_width &&
             (value & (static_cast<WideUnsigned>(1) <<
                       (source_width - 1))))
            value |= ~mask;
        } else return false;
        return representable_wide_integer(value, ins.type, result);
      }
      std::uint64_t value = static_cast<std::uint64_t>(ins.first.int_value);
      if(ins.op.kind == LowOperation::LOP_ZEXT) value &= width_mask(ins.source_type);
      *result = integer_operand(normalize_integer(value, ins.type), ins.type);
      return true;
    }
    if(is_float_type(ins.type) &&
       lowir_model::lowir_type_bit_width(ins.source_type) <= 64 &&
       (ins.op.kind == LowOperation::LOP_SITOFP || ins.op.kind == LowOperation::LOP_UITOFP)) {
      const long double value = ins.op.kind == LowOperation::LOP_UITOFP ?
        static_cast<long double>(static_cast<std::uint64_t>(ins.first.int_value) &
                                 width_mask(ins.source_type)) :
        static_cast<long double>(ins.first.int_value);
      *result = floating_operand(value, ins.type);
      return true;
    }
  }
  if(ins.first.kind == Operand::OP_FLOAT && is_float_type(ins.type) &&
     (ins.op.kind == LowOperation::LOP_FPEXT || ins.op.kind == LowOperation::LOP_FPTRUNC)) {
    *result = floating_operand(ins.first.float_value, ins.type);
    return true;
  }
  return false;
}

bool algebraic_identity(const Instruction & ins, Operand * result)
{
  if(ins.kind != Instruction::IK_BINARY) return false;
  if((ins.op.kind == LowOperation::LOP_ADD || ins.op.kind == LowOperation::LOP_OR || ins.op.kind == LowOperation::LOP_XOR) && is_zero(ins.second))
    *result = ins.first;
  else if(ins.op.kind == LowOperation::LOP_ADD && is_zero(ins.first)) *result = ins.second;
  else if(ins.op.kind == LowOperation::LOP_SUB && is_zero(ins.second)) *result = ins.first;
  else if((ins.op.kind == LowOperation::LOP_MUL || ins.op.kind == LowOperation::LOP_DIV || ins.op.kind == LowOperation::LOP_UDIV) &&
          is_one(ins.second)) *result = ins.first;
  else if(ins.op.kind == LowOperation::LOP_MUL && is_one(ins.first)) *result = ins.second;
  else if(ins.op.kind == LowOperation::LOP_AND && is_minus_one(ins.second)) *result = ins.first;
  else if(ins.op.kind == LowOperation::LOP_AND && is_minus_one(ins.first)) *result = ins.second;
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
  std::vector<std::size_t> index;
  std::vector<EdgeList> successors;
  std::vector<EdgeList> predecessors;
  std::vector<unsigned char> eh_targets;

  std::size_t find(BlockId block) const
  {
    const std::uint32_t id = block;
    return id < index.size() ? index[id] : kNoBlockIndex;
  }
};

void add_edge(Graph * graph, std::size_t from, const Operand & target,
              Stats * stats)
{
  if(target.kind != Operand::OP_LABEL) return;
  const std::size_t found = graph->find(target.block);
  if(found == kNoBlockIndex) return;
  graph->successors[from].insert_sorted_unique(found);
  if(stats) ++stats->cfg_edge_visits;
}

Graph build_graph(const Function & function, Stats * stats)
{
  Graph result;
  result.successors.resize(function.blocks.size());
  result.predecessors.resize(function.blocks.size());
  result.index.assign(function.next_block_id, kNoBlockIndex);
  result.eh_targets.assign(function.next_block_id, 0);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::uint32_t id = function.blocks[i].id;
    if(id >= result.index.size())
      throw std::logic_error("invalid LowIR block identity in CFG");
    result.index[id] = i;
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const Block & block = function.blocks[i];
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const Instruction & ins = block.instructions[j];
      if(ins.kind == Instruction::IK_EH_TRY ||
         ins.kind == Instruction::IK_EH_CLEANUP) {
        add_edge(&result, i, ins.first, stats);
        result.eh_targets[static_cast<std::uint32_t>(ins.first.block)] = 1;
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

Operand resolve_operand(Operand value,
                        const std::vector<Fact> & facts,
                        const std::vector<unsigned char> & known,
                        std::size_t block,
                        const DominatorTree & dom)
{
  for(std::size_t step = 0;
      step < facts.size() && value.kind == Operand::OP_TEMP; ++step) {
    const std::uint32_t id = value.value;
    if(id >= facts.size() || !known[id] ||
       !dom.dominates(facts[id].block, block)) break;
    value = facts[id].value;
  }
  return value;
}

void resolve_instruction_operands(Instruction * ins,
                                  const std::vector<Fact> & facts,
                                  const std::vector<unsigned char> & known,
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
      const Operand resolved = resolve_operand(
        *values[i], facts, known, block, dom);
      if(!storage_address || resolved.kind == Operand::OP_TEMP)
        *values[i] = resolved;
    }
  }
  for(std::size_t i = 0; i < ins->args.size(); ++i)
    if(ins->args[i].kind == Operand::OP_TEMP)
      ins->args[i] = resolve_operand(ins->args[i], facts, known, block, dom);
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
  LowOperation op;
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

bool reassociate(Instruction * ins,
                 const std::vector<DefinitionFact> & definitions,
                 const std::vector<unsigned char> & known)
{
  if(ins->kind != Instruction::IK_BINARY || !commutative(ins->op) ||
     ins->second.kind != Operand::OP_INTEGER || !ins->second.has_int_value ||
     ins->first.kind != Operand::OP_TEMP) return false;
  const std::uint32_t id = ins->first.value;
  if(id >= definitions.size() || !known[id]) return false;
  const DefinitionFact & parent = definitions[id];
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

std::vector<unsigned char> find_storage_temporaries(
    const Function & function, bool * has_eh = 0)
{
  std::vector<unsigned char> storage(function.value_names.size(), 0);
  if(has_eh) *has_eh = false;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function.blocks[i].instructions[j];
      if(has_eh && is_eh_instruction(ins.kind)) *has_eh = true;
      if((ins.kind == Instruction::IK_LOAD ||
          ins.kind == Instruction::IK_ATOMIC_LOAD) &&
         ins.first.kind == Operand::OP_TEMP)
        storage[ins.first.value] = 1;
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage[ins.second.value] = 1;
    }
  return storage;
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
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  PassArena arena;
  bool function_has_eh = false;
  const std::vector<unsigned char> storage_temporaries =
    find_storage_temporaries(*function, &function_has_eh);

  typedef PassAllocator<std::pair<const ExpressionKey, Fact> >
    ExpressionAllocator;
  std::vector<Fact> facts(function->value_names.size());
  std::vector<unsigned char> known_facts(function->value_names.size(), 0);
  std::unordered_map<ExpressionKey, Fact, ExpressionKeyHash,
    std::equal_to<ExpressionKey>, ExpressionAllocator> expressions(
      0, ExpressionKeyHash(), std::equal_to<ExpressionKey>(),
      ExpressionAllocator(&arena));
  std::vector<DefinitionFact> definitions(function->value_names.size());
  std::vector<unsigned char> known_definitions(
    function->value_names.size(), 0);
  expressions.reserve(instruction_total / 2 + 1);
  bool changed = false;
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t index = 0; index < original_size; ++index) {
      Instruction & ins = instructions[index];
      if(stats) ++stats->instruction_visits;
      resolve_instruction_operands(&ins, facts, known_facts, block, dom);

      Operand replacement;
      bool replace = false;
      if(ins.kind == Instruction::IK_CONST &&
         !storage_temporaries[ins.dest]) {
        replacement = ins.first;
        replace = true;
      } else if(ins.kind == Instruction::IK_COPY &&
                !lowir_model::lowir_value_preserves_copy(
                  *function, ins.dest) &&
                !storage_temporaries[ins.dest]) {
        replace = ins.first.kind != Operand::OP_TEMP ||
          lowir_model::same_lowir_type(
            lowir_model::lowir_value_type(*function, ins.first.value), ins.type);
        replacement = ins.first;
      } else if(ins.kind == Instruction::IK_UNARY)
        replace = fold_unary(ins, &replacement);
      else if(ins.kind == Instruction::IK_BINARY) {
        reassociate(&ins, definitions, known_definitions);
        replace = fold_binary(ins, &replacement) ||
          algebraic_identity(ins, &replacement);
      } else if(ins.kind == Instruction::IK_CMP) {
        replace = fold_compare(ins, &replacement);
        if(!replace && (ins.op.kind == LowOperation::LOP_EQ || ins.op.kind == LowOperation::LOP_NE) &&
           ((is_zero(ins.second) && ins.op.kind == LowOperation::LOP_NE) ||
            (is_one(ins.second) && ins.op.kind == LowOperation::LOP_EQ))) {
          const std::uint32_t id = ins.first.value;
          if(ins.first.kind == Operand::OP_TEMP &&
             id < definitions.size() && known_definitions[id] &&
             definitions[id].kind == Instruction::IK_CMP) {
            replacement = ins.first;
            replace = true;
          }
        }
      } else if(ins.kind == Instruction::IK_CONVERT)
        replace = fold_convert(ins, &replacement);

      if(replace && ins.dest.valid()) {
        facts[ins.dest] = Fact{replacement, block};
        known_facts[ins.dest] = 1;
        changed = true;
        if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
        continue;
      }

      if(cse_eligible(ins.kind) && ins.dest.valid()) {
        const ExpressionKey key = expression_key(ins);
        const auto found = expressions.find(key);
        const bool cross_block_guard = function_has_eh &&
          (ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX) &&
          found != expressions.end() && found->second.block != block;
        if(found != expressions.end() && !cross_block_guard &&
           dom.dominates(found->second.block, block)) {
          facts[ins.dest] = Fact{found->second.value, block};
          known_facts[ins.dest] = 1;
          changed = true;
          if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
          continue;
        }
        Operand produced;
        produced.kind = Operand::OP_TEMP;
        produced.value = ins.dest;
        expressions[key] = Fact{produced, block};
      }
      if(ins.dest.valid()) {
        definitions[ins.dest] = DefinitionFact{
          ins.kind, ins.op, ins.type.kind, ins.type.storage_size,
          ins.type.alignment, ins.first, ins.second};
        known_definitions[ins.dest] = 1;
      }
      if(kept != index) instructions[kept] = std::move(ins);
      ++kept;
    }
    instructions.resize(kept);
  }
  return changed;
}

struct FunctionBoundaries
{
  std::vector<FunctionBoundaryMetadata> values;
  std::vector<unsigned char> known;
};

bool call_is_removable(const Instruction & ins,
                       const FunctionBoundaries & boundaries)
{
  if(ins.kind != Instruction::IK_CALL || !ins.dest.valid()) return false;
  FunctionBoundaryMetadata boundary = ins.call_boundary;
  if(ins.first.kind == Operand::OP_GLOBAL && boundaries.known[ins.first.symbol])
    boundary = boundaries.values[ins.first.symbol];
  return boundary.effects == lowir_model::CFXM_READNONE &&
    boundary.unwind == lowir_model::CUM_NO &&
    boundary.returns != lowir_model::CRM_NORETURN;
}

bool eliminate_dead_code(Function * function,
                         const FunctionBoundaries & boundaries,
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
  std::vector<ValueLiveness> values(function->value_names.size());
  std::vector<std::vector<unsigned char> > dead(function->blocks.size());
  const auto count_use = [&values](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP) ++values[operand.value].uses;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    dead[i].assign(function->blocks[i].instructions.size(), 0);
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.dest.valid()) {
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
  for(std::size_t i = 0; i < values.size(); ++i) {
    const ValueLiveness & value = values[i];
    if(!value.defined) continue;
    const Instruction & ins =
      function->blocks[value.definition.first].instructions[
        value.definition.second];
    if(value.uses == 0 &&
       (is_pure(ins.kind) || ins.kind == Instruction::IK_LOAD ||
        call_is_removable(ins, boundaries))) {
      work.push_back(value.definition);
      if(stats) ++stats->worklist_pushes;
    }
  }

  const auto release_operand = [&](const Operand & operand) {
    if(operand.kind != Operand::OP_TEMP) return;
    ValueLiveness & value = values[operand.value];
    if(value.uses == 0) return;
    --value.uses;
    if(value.uses != 0 || !value.defined) return;
    const Instruction & producer =
      function->blocks[value.definition.first].instructions[
        value.definition.second];
    if(is_pure(producer.kind) || producer.kind == Instruction::IK_LOAD ||
       call_is_removable(producer, boundaries)) {
      work.push_back(value.definition);
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

std::vector<BlockId> bypass_targets(const Function & function,
                                    const Graph & graph)
{
  const std::size_t count = function.blocks.size();
  std::vector<std::size_t> next(count, kNoBlock);
  for(std::size_t i = 0; i < count; ++i) {
    const Block & block = function.blocks[i];
    if(graph.eh_targets[static_cast<std::uint32_t>(block.id)] ||
       block.instructions.size() != 1 ||
       block.instructions[0].kind != Instruction::IK_JUMP) continue;
    const std::size_t found = graph.find(block.instructions[0].first.block);
    if(found != kNoBlockIndex) next[i] = found;
  }
  std::vector<BlockId> result(count);
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
      result[cursor] = function.blocks[cursor].id;
    }
    if(state[cursor] == 1) {
      std::size_t cycle = 0;
      while(cycle < path.size() && path[cycle] != cursor) ++cycle;
      for(std::size_t i = cycle; i < path.size(); ++i) {
        result[path[i]] = function.blocks[path[i]].id;
        state[path[i]] = 2;
      }
      for(std::size_t i = cycle; i > 0; --i) {
        result[path[i - 1]] = function.blocks[cursor].id;
        state[path[i - 1]] = 2;
      }
      continue;
    }
    const BlockId target = result[cursor];
    for(std::size_t i = path.size(); i > 0; --i) {
      result[path[i - 1]] = target;
      state[path[i - 1]] = 2;
    }
  }
  for(std::size_t i = 0; i < count; ++i)
    if(!result[i].valid()) result[i] = function.blocks[i].id;
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
      } else if(term.second.block == term.third.block) {
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
  const std::vector<BlockId> bypass = bypass_targets(*function, graph);
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
        const std::size_t found = graph.find(targets[k]->block);
        const BlockId target = found == kNoBlockIndex ?
          targets[k]->block : bypass[found];
        if(target != targets[k]->block &&
           ins.kind != Instruction::IK_EH_TRY &&
           ins.kind != Instruction::IK_EH_CLEANUP) {
          targets[k]->block = target;
          changed = true;
          graph_targets_changed = true;
        }
      }
      if(ins.kind == Instruction::IK_SWITCH)
        for(std::size_t k = 1; k < ins.args.size(); k += 2) {
          const std::size_t found = graph.find(ins.args[k].block);
          const BlockId target = found == kNoBlockIndex ?
            ins.args[k].block : bypass[found];
          if(target != ins.args[k].block) {
            ins.args[k].block = target;
            changed = true;
            graph_targets_changed = true;
          }
        }
      if(ins.kind == Instruction::IK_BRANCH &&
         ins.second.block == ins.third.block) {
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
    std::vector<Definition> definitions(function->value_names.size());
    std::vector<unsigned char> defined(function->value_names.size(), 0);
    std::vector<std::vector<lowir_model::ValueId> > dependencies(
      function->value_names.size());
    for(std::size_t i = 0; i < function->blocks.size(); ++i)
      for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(!ins.dest.valid()) continue;
      definitions[ins.dest] = Definition{i, ins};
      defined[ins.dest] = 1;
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(operands[k]->value);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          dependencies[ins.dest].push_back(ins.args[k].value);
      }
    std::vector<unsigned char> available(function->value_names.size(), 0);
    for(std::size_t i = 0; i < function->params.size(); ++i)
      available[function->params[i].value] = 1;
    const std::size_t entry_end = function->blocks[0].instructions.empty() ? 0 :
      function->blocks[0].instructions.size() - 1;
    for(std::size_t i = 0; i < entry_end; ++i)
      if(function->blocks[0].instructions[i].dest.valid())
        available[function->blocks[0].instructions[i].dest] = 1;
    std::vector<Instruction> rematerialized;
    const auto eligible_definition = [&](lowir_model::ValueId value) {
      return defined[value] && !reachable[definitions[value].block] &&
        is_pure(definitions[value].instruction.kind);
    };
    const auto rematerialize = [&](lowir_model::ValueId value) {
      if(available[value]) return true;
      if(!eligible_definition(value)) return false;
      struct Frame { lowir_model::ValueId value; std::size_t dependency; };
      std::vector<Frame> stack(1, Frame{value, 0});
      std::vector<unsigned char> active(function->value_names.size(), 0);
      active[value] = 1;
      while(!stack.empty()) {
        Frame & frame = stack.back();
        const std::vector<lowir_model::ValueId> & required =
          dependencies[frame.value];
        while(frame.dependency < required.size() &&
              available[required[frame.dependency]])
          ++frame.dependency;
        if(frame.dependency < required.size()) {
          const lowir_model::ValueId dependency = required[frame.dependency++];
          if(active[dependency] || !eligible_definition(dependency))
            return false;
          active[dependency] = 1;
          stack.push_back(Frame{dependency, 0});
          continue;
        }
        rematerialized.push_back(definitions[frame.value].instruction);
        available[frame.value] = 1;
        active[frame.value] = 0;
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
             defined[operands[k]->value] &&
             !reachable[definitions[operands[k]->value].block])
            rematerialize(operands[k]->value);
        for(std::size_t k = 0; k < ins.args.size(); ++k)
          if(ins.args[k].kind == Operand::OP_TEMP &&
             defined[ins.args[k].value] &&
             !reachable[definitions[ins.args[k].value].block])
            rematerialize(ins.args[k].value);
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
    const std::size_t target =
      graph.find(block.instructions.back().first.block);
    if(target == kNoBlockIndex || target == i ||
       block_has_eh[i] || block_has_eh[target] ||
       graph.eh_targets[static_cast<std::uint32_t>(block.id)] ||
       graph.eh_targets[static_cast<std::uint32_t>(
         block.instructions.back().first.block)] ||
       graph.predecessors[target].size() != 1) continue;
    merge_next[i] = target;
    merge_parent[target] = i;
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
      if(!merged[i].id.valid())
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
    if(targets[i] && graph.find(targets[i]->block) != kNoBlockIndex)
      out->push_back(graph.find(targets[i]->block));
  if(term.kind == Instruction::IK_SWITCH) {
    if(graph.find(term.second.block) != kNoBlockIndex)
      out->push_back(graph.find(term.second.block));
    for(std::size_t i = 1; i < term.args.size(); i += 2)
      if(graph.find(term.args[i].block) != kNoBlockIndex)
        out->push_back(graph.find(term.args[i].block));
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
  const std::size_t slot_count = function->slot_names.size();
  std::vector<unsigned char> escaped(slot_count, 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          escaped[operands[k]->slot] = 1;
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT)
          escaped[ins.args[k].slot] = 1;
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
          term.first.block == block.id) ||
         (term.kind == Instruction::IK_BRANCH &&
          (term.second.block == block.id || term.third.block == block.id)))
        linear_single_block = false;
      if(term.kind == Instruction::IK_SWITCH) {
        linear_single_block = term.second.block != block.id;
        for(std::size_t i = 1;
            linear_single_block && i < term.args.size(); i += 2)
          linear_single_block = term.args[i].block != block.id;
      }
    }
  }
  if(linear_single_block) {
    std::vector<unsigned char> live(slot_count, 0);
    std::vector<Instruction> & instructions =
      function->blocks[0].instructions;
    std::vector<unsigned char> dead(instructions.size(), 0);
    std::size_t removed = 0;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if(ins.kind == Instruction::IK_LOAD &&
         ins.first.kind == Operand::OP_SLOT)
        live[ins.first.slot] = 1;
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped[ins.second.slot]) {
        if(!live[ins.second.slot]) {
          dead[index - 1] = 1;
          ++removed;
          if(stats) ++stats->rewrites;
          continue;
        }
        live[ins.second.slot] = 0;
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live[operands[i]->slot] = 1;
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live[ins.args[i].slot] = 1;
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
  typedef std::vector<unsigned char> LiveSlots;
  std::vector<LiveSlots> live_in(
    function->blocks.size(), LiveSlots(slot_count, 0));
  const auto transfer = [&](std::size_t block) {
    LiveSlots live(slot_count, 0);
    std::vector<std::size_t> successors;
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      for(std::size_t slot = 0; slot < slot_count; ++slot)
        live[slot] |= live_in[successors[i]][slot];
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.find(ins.first.block) != kNoBlockIndex) {
        const LiveSlots & handler = live_in[graph.find(ins.first.block)];
        for(std::size_t slot = 0; slot < slot_count; ++slot)
          live[slot] |= handler[slot];
      }
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live[ins.first.slot] = 1;
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped[ins.second.slot])
        live[ins.second.slot] = 0;
      else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live[operands[i]->slot] = 1;
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live[ins.args[i].slot] = 1;
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
    LiveSlots live(slot_count, 0);
    std::vector<std::size_t> successors;
    normal_successors(*function, graph, block, &successors);
    for(std::size_t i = 0; i < successors.size(); ++i)
      for(std::size_t slot = 0; slot < slot_count; ++slot)
        live[slot] |= live_in[successors[i]][slot];
    std::vector<Instruction> kept_reverse;
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    for(std::size_t index = instructions.size(); index > 0; --index) {
      Instruction & ins = instructions[index - 1];
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.find(ins.first.block) != kNoBlockIndex) {
        const LiveSlots & handler = live_in[graph.find(ins.first.block)];
        for(std::size_t slot = 0; slot < slot_count; ++slot)
          live[slot] |= handler[slot];
      }
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        live[ins.first.slot] = 1;
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT &&
              !escaped[ins.second.slot]) {
        if(!live[ins.second.slot]) {
          changed = true;
          if(stats) ++stats->rewrites;
          continue;
        }
        live[ins.second.slot] = 0;
      } else {
        const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
        for(std::size_t i = 0; i < 3; ++i)
          if(operands[i]->kind == Operand::OP_SLOT)
            live[operands[i]->slot] = 1;
        for(std::size_t i = 0; i < ins.args.size(); ++i)
          if(ins.args[i].kind == Operand::OP_SLOT)
            live[ins.args[i].slot] = 1;
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
  std::vector<std::size_t> loads(function->slot_names.size(), 0);
  std::vector<unsigned char> escaped(function->slot_names.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind == Instruction::IK_LOAD && ins.first.kind == Operand::OP_SLOT)
        ++loads[ins.first.slot];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          escaped[values[k]->slot] = 1;
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT)
          escaped[ins.args[k].slot] = 1;
    }
  std::vector<unsigned char> dead(function->slot_names.size(), 0);
  std::size_t dead_count = 0;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const lowir_model::SlotId slot = function->slots[i];
    if(!loads[slot] && !escaped[slot]) { dead[slot] = 1; ++dead_count; }
  }
  if(dead_count == 0) return false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      if((ins.kind == Instruction::IK_LOAD &&
          ins.first.kind == Operand::OP_SLOT &&
          dead[ins.first.slot]) ||
         (ins.kind == Instruction::IK_STORE &&
          ins.second.kind == Operand::OP_SLOT &&
          dead[ins.second.slot])) {
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
    if(!dead[function->slots[i]]) {
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
  std::vector<UseBlocks> use_blocks(function->value_names.size());
  const auto note_use = [&use_blocks](lowir_model::ValueId value,
                                      std::size_t block) {
    UseBlocks & uses = use_blocks[value];
    if(uses.first == kNoBlock) uses.first = block;
    else if(uses.first != block) uses.multiple = true;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      const Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP)
          note_use(operands[k]->value, i);
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP)
          note_use(ins.args[k].value, i);
    }
  bool changed = false;
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<Operand> values(function->slot_names.size());
    std::vector<unsigned char> has_value(function->slot_names.size(), 0);
    std::vector<Operand> aliases(function->value_names.size());
    std::vector<unsigned char> has_alias(function->value_names.size(), 0);
    std::vector<Instruction> & instructions =
      function->blocks[i].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t j = 0; j < original_size; ++j) {
      Instruction & ins = instructions[j];
      Operand * operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(operands[k]->kind == Operand::OP_TEMP &&
           has_alias[operands[k]->value])
          *operands[k] = aliases[operands[k]->value];
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_TEMP &&
           has_alias[ins.args[k].value])
          ins.args[k] = aliases[ins.args[k].value];
      // Taking a slot's address or storing through an indirect pointer can
      // change a previously recorded slot value.  Inlining commonly exposes
      // exactly this shape, so retaining the old value here would turn a real
      // load into a stale constant before the escape-aware O2 pass sees it.
      const Operand * slot_operands[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(slot_operands[k]->kind == Operand::OP_SLOT &&
           !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)))
          has_value[slot_operands[k]->slot] = 0;
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind != Operand::OP_SLOT)
        std::fill(has_value.begin(), has_value.end(), 0);
      if(ins.kind == Instruction::IK_STORE && ins.second.kind == Operand::OP_SLOT) {
        values[ins.second.slot] = ins.first;
        has_value[ins.second.slot] = 1;
      } else if(ins.kind == Instruction::IK_LOAD &&
                ins.first.kind == Operand::OP_SLOT && has_value[ins.first.slot] &&
                (!use_blocks[ins.dest].multiple &&
                 (use_blocks[ins.dest].first == kNoBlock ||
                  use_blocks[ins.dest].first == i))) {
        aliases[ins.dest] = values[ins.first.slot];
        has_alias[ins.dest] = 1;
        changed = true;
        if(stats) ++stats->rewrites;
        continue;
      } else {
        if(ins.kind == Instruction::IK_CALL || ins.kind == Instruction::IK_COPYOBJ ||
           ins.kind == Instruction::IK_ZEROINIT || is_eh_instruction(ins.kind))
          std::fill(has_value.begin(), has_value.end(), 0);
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
    lowir_model::ValueId destination;
  };
  std::vector<SlotFact> facts(function->slot_names.size());
  std::vector<unsigned char> eligible(function->slot_names.size(), 0);
  std::vector<LoadFact> loads;
  std::vector<unsigned char> storage_temporaries(
    function->value_names.size(), 0);
  std::size_t first_exception_edge = kNoBlock;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const lowir_model::SlotId slot = function->slots[i];
    if(lowir_model::lowir_slot_type(*function, slot).kind !=
       lowir_model::LTK_OBJECT)
      eligible[slot] = 1;
  }
  const auto find_slot = [&eligible](const Operand & operand) {
    if(operand.kind != Operand::OP_SLOT) return kNoBlock;
    const std::uint32_t slot = operand.slot;
    return slot < eligible.size() && eligible[slot] ? slot : kNoBlock;
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
        storage_temporaries[ins.first.value] = 1;
      if((ins.kind == Instruction::IK_STORE ||
          ins.kind == Instruction::IK_ATOMIC_STORE) &&
         ins.second.kind == Operand::OP_TEMP)
        storage_temporaries[ins.second.value] = 1;
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

  std::vector<Operand> aliases(function->value_names.size());
  std::vector<unsigned char> has_alias(function->value_names.size(), 0);
  for(std::size_t i = 0; i < loads.size(); ++i)
    if(forwarded[loads[i].slot] &&
       !storage_temporaries[loads[i].destination]) {
      aliases[loads[i].destination] = facts[loads[i].slot].value;
      has_alias[loads[i].destination] = 1;
    }
  const auto resolve_alias = [&aliases, &has_alias](Operand value) {
    for(std::size_t step = 0;
        step < aliases.size() && value.kind == Operand::OP_TEMP; ++step) {
      const std::uint32_t id = value.value;
      if(id >= aliases.size() || !has_alias[id]) break;
      value = aliases[id];
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
         storage_temporaries[ins.dest]) {
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
    if(!forwarded[function->slots[i]]) {
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
  std::vector<Operand> values;
  std::vector<unsigned char> known_values;
  std::vector<Operand> slots;
  std::vector<unsigned char> known_slots;
};

bool meet_state(AbstractState * target, const AbstractState & incoming)
{
  if(!incoming.executable) return false;
  if(!target->executable) { *target = incoming; return true; }
  bool changed = false;
  for(std::size_t value = 0; value < target->known_values.size(); ++value)
    if(target->known_values[value] &&
       (!incoming.known_values[value] ||
        !same_operand(target->values[value], incoming.values[value]))) {
      target->known_values[value] = 0;
      changed = true;
    }
  for(std::size_t slot = 0; slot < target->known_slots.size(); ++slot)
    if(target->known_slots[slot] &&
       (!incoming.known_slots[slot] ||
        !same_operand(target->slots[slot], incoming.slots[slot]))) {
      target->known_slots[slot] = 0;
      changed = true;
    }
  return changed;
}

Operand abstract_resolve(Operand value, const AbstractState & state)
{
  for(std::size_t step = 0;
      step < state.values.size() + state.slots.size() &&
      (value.kind == Operand::OP_TEMP || value.kind == Operand::OP_SLOT);
      ++step) {
    if(value.kind == Operand::OP_SLOT) {
      const std::uint32_t slot = value.slot;
      if(slot >= state.known_slots.size() || !state.known_slots[slot]) break;
      value = state.slots[slot];
      continue;
    }
    const std::uint32_t id = value.value;
    if(id >= state.values.size() || !state.known_values[id]) break;
    value = state.values[id];
  }
  return value;
}

void strip_local_facts(AbstractState * state,
                       const std::vector<lowir_model::ValueId> & local_values)
{
  if(local_values.empty()) return;
  std::vector<unsigned char> locals(state->values.size(), 0);
  for(std::size_t i = 0; i < local_values.size(); ++i)
    locals[local_values[i]] = 1;
  for(std::size_t value = 0; value < state->known_values.size(); ++value)
    if(state->known_values[value] &&
       (locals[value] ||
        (state->values[value].kind == Operand::OP_TEMP &&
         locals[state->values[value].value])))
      state->known_values[value] = 0;
  for(std::size_t slot = 0; slot < state->known_slots.size(); ++slot)
    if(state->known_slots[slot] &&
       state->slots[slot].kind == Operand::OP_TEMP &&
       locals[state->slots[slot].value])
      state->known_slots[slot] = 0;
}

void rewrite_promoted_slots(Function * function,
                            const std::vector<unsigned char> & promoted,
                            const std::vector<unsigned char> & storage,
                            const std::vector<Operand> & loads,
                            const std::vector<unsigned char> & has_load,
                            Stats * stats)
{
  const auto resolve_load = [&loads, &has_load](Operand value) {
    for(std::size_t step = 0;
        step < loads.size() && value.kind == Operand::OP_TEMP; ++step) {
      const std::uint32_t id = value.value;
      if(id >= loads.size() || !has_load[id]) break;
      value = loads[id];
    }
    return value;
  };
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<unsigned char> has_alias = has_load;
    for(std::size_t value = 0; value < storage.size(); ++value)
      if(storage[value]) has_alias[value] = 0;
    const auto resolve = [&loads, &has_alias](Operand value) {
      for(std::size_t step = 0;
          step < loads.size() && value.kind == Operand::OP_TEMP; ++step) {
        const std::uint32_t id = value.value;
        if(id >= loads.size() || !has_alias[id]) break;
        value = loads[id];
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
      if(ins.kind == Instruction::IK_LOAD &&
         ins.first.kind == Operand::OP_SLOT && promoted[ins.first.slot] &&
         storage[ins.dest]) {
        ins.first = resolve_load(loads[ins.dest]);
        ins.kind = ins.first.kind == Operand::OP_INTEGER ||
          ins.first.kind == Operand::OP_FLOAT ?
            Instruction::IK_CONST : Instruction::IK_COPY;
        ins.second = Operand();
        ins.third = Operand();
        if(stats) ++stats->rewrites;
      } else if((ins.kind == Instruction::IK_LOAD &&
                 ins.first.kind == Operand::OP_SLOT &&
                 promoted[ins.first.slot]) ||
                (ins.kind == Instruction::IK_STORE &&
                 ins.second.kind == Operand::OP_SLOT &&
                 promoted[ins.second.slot])) {
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
    if(!promoted[function->slots[i]]) {
      if(kept_slots != i)
        function->slots[kept_slots] = std::move(function->slots[i]);
      ++kept_slots;
    }
  function->slots.resize(kept_slots);
}

std::vector<unsigned char> find_promotable_slots(
    const Function & function, std::size_t * count)
{
  std::vector<unsigned char> eligible(function.slot_names.size(), 0);
  *count = 0;
  for(std::size_t i = 0; i < function.slots.size(); ++i) {
    const lowir_model::SlotId slot = function.slots[i];
    if(lowir_model::lowir_slot_type(function, slot).kind !=
       lowir_model::LTK_OBJECT) {
      eligible[slot] = 1;
      ++*count;
    }
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function.blocks[i].instructions[j];
      const Operand * values[] = {&ins.first, &ins.second, &ins.third};
      for(std::size_t k = 0; k < 3; ++k)
        if(values[k]->kind == Operand::OP_SLOT &&
          !((ins.kind == Instruction::IK_LOAD && k == 0) ||
             (ins.kind == Instruction::IK_STORE && k == 1)) &&
           eligible[values[k]->slot]) {
          eligible[values[k]->slot] = 0;
          --*count;
        }
      for(std::size_t k = 0; k < ins.args.size(); ++k)
        if(ins.args[k].kind == Operand::OP_SLOT && eligible[ins.args[k].slot]) {
          eligible[ins.args[k].slot] = 0;
          --*count;
        }
    }
  return eligible;
}

bool promote_slots(Function * function, Stats * stats)
{
  if(function->blocks.empty() || function->slots.empty()) return false;
  std::size_t eligible_count = 0;
  const std::vector<unsigned char> eligible =
    find_promotable_slots(*function, &eligible_count);
  if(eligible_count == 0) return false;
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  if(exceeds_state_budget(function->blocks.size(), eligible_count,
                          instruction_total)) {
    if(stats) ++stats->budget_skips;
    return false;
  }

  const std::vector<unsigned char> storage_temporaries =
    find_storage_temporaries(*function);

  const Graph graph = build_graph(*function, stats);
  std::vector<AbstractState> incoming(function->blocks.size());
  for(std::size_t i = 0; i < incoming.size(); ++i) {
    incoming[i].values.resize(function->value_names.size());
    incoming[i].known_values.assign(function->value_names.size(), 0);
    incoming[i].slots.resize(function->slot_names.size());
    incoming[i].known_slots.assign(function->slot_names.size(), 0);
  }
  incoming[0].executable = true;
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 0);
  work.push_back(0); queued[0] = 1;
  if(stats) ++stats->worklist_pushes;
  struct BlockReplacements
  {
    std::vector<Operand> values;
    std::vector<unsigned char> known;
  };
  std::vector<BlockReplacements> replacements(function->blocks.size());
  for(std::size_t i = 0; i < replacements.size(); ++i) {
    replacements[i].values.resize(function->value_names.size());
    replacements[i].known.assign(function->value_names.size(), 0);
  }
  while(!work.empty()) {
    const std::size_t block_index = work.front(); work.pop_front();
    queued[block_index] = 0;
    AbstractState state = incoming[block_index];
    std::fill(replacements[block_index].known.begin(),
              replacements[block_index].known.end(), 0);
    const Block & block = function->blocks[block_index];
    std::vector<std::pair<std::size_t, AbstractState> > exceptional;
    std::vector<lowir_model::ValueId> local_temporaries;
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
         graph.find(ins.first.block) != kNoBlockIndex) {
        AbstractState handler = state;
        strip_local_facts(&handler, local_temporaries);
        exceptional.push_back(std::make_pair(
          graph.find(ins.first.block), handler));
      }
      if(ins.kind == Instruction::IK_STORE &&
         source.second.kind == Operand::OP_SLOT &&
         eligible[source.second.slot]) {
        state.slots[source.second.slot] = ins.first;
        state.known_slots[source.second.slot] = 1;
      }
      else if(ins.kind == Instruction::IK_LOAD &&
              source.first.kind == Operand::OP_SLOT &&
              eligible[source.first.slot]) {
        if(state.known_slots[source.first.slot]) {
          state.values[ins.dest] = state.slots[source.first.slot];
          state.known_values[ins.dest] = 1;
          local_temporaries.push_back(ins.dest);
          replacements[block_index].values[ins.dest] =
            state.slots[source.first.slot];
          replacements[block_index].known[ins.dest] = 1;
        }
      } else if(ins.dest.valid()) {
        Operand folded;
        bool known = ins.kind == Instruction::IK_CONST ? (folded = ins.first, true) :
          ins.kind == Instruction::IK_COPY &&
            !lowir_model::lowir_value_preserves_copy(
              *function, ins.dest) ?
              (folded = ins.first, true) :
          ins.kind == Instruction::IK_UNARY ? fold_unary(ins, &folded) :
          ins.kind == Instruction::IK_BINARY ? fold_binary(ins, &folded) :
          ins.kind == Instruction::IK_CMP ? fold_compare(ins, &folded) :
          ins.kind == Instruction::IK_CONVERT ? fold_convert(ins, &folded) : false;
        if(known) {
          state.values[ins.dest] = folded;
          state.known_values[ins.dest] = 1;
          local_temporaries.push_back(ins.dest);
        }
        else state.known_values[ins.dest] = 0;
      }
      if(stats) ++stats->instruction_visits;
    }
    std::vector<std::size_t> normal;
    if(!block.instructions.empty()) {
      const Instruction & term = block.instructions.back();
      const Operand selector = abstract_resolve(term.first, state);
      if(term.kind == Instruction::IK_JUMP &&
         graph.find(selector.block) != kNoBlockIndex)
        normal.push_back(graph.find(selector.block));
      else if(term.kind == Instruction::IK_BRANCH) {
        const Operand & selected = selector.kind == Operand::OP_INTEGER &&
          selector.has_int_value ?
          (selector.int_value ? term.second : term.third) : term.second;
        if(graph.find(selected.block) != kNoBlockIndex)
          normal.push_back(graph.find(selected.block));
        if(!(selector.kind == Operand::OP_INTEGER && selector.has_int_value) &&
           graph.find(term.third.block) != kNoBlockIndex)
          normal.push_back(graph.find(term.third.block));
      } else if(term.kind == Instruction::IK_SWITCH) {
        Operand selected = term.second;
        if(selector.kind == Operand::OP_INTEGER && selector.has_int_value)
          for(std::size_t i = 0; i + 1 < term.args.size(); i += 2) {
            Operand case_value = abstract_resolve(term.args[i], state);
            if(case_value.kind == Operand::OP_INTEGER && case_value.has_int_value &&
               case_value.int_value == selector.int_value) selected = term.args[i + 1];
          }
        if(graph.find(selected.block) != kNoBlockIndex)
          normal.push_back(graph.find(selected.block));
        if(!(selector.kind == Operand::OP_INTEGER && selector.has_int_value))
          for(std::size_t i = 1; i < term.args.size(); i += 2)
            if(graph.find(term.args[i].block) != kNoBlockIndex)
              normal.push_back(graph.find(term.args[i].block));
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

  std::vector<unsigned char> replacement_temporaries(
    function->value_names.size(), 0);
  bool has_replacement_temporary = false;
  for(std::size_t block = 0; block < replacements.size(); ++block)
    for(std::size_t value = 0; value < replacements[block].known.size(); ++value)
      if(replacements[block].known[value] &&
         replacements[block].values[value].kind == Operand::OP_TEMP) {
        replacement_temporaries[replacements[block].values[value].value] = 1;
        has_replacement_temporary = true;
      }
  std::vector<std::size_t> replacement_definitions(
    function->value_names.size(), kNoBlockIndex);
  std::vector<unsigned char> parameter_temporaries(
    function->value_names.size(), 0);
  if(has_replacement_temporary) {
    for(std::size_t parameter = 0;
        parameter < function->params.size(); ++parameter)
      if(replacement_temporaries[function->params[parameter].value])
        parameter_temporaries[function->params[parameter].value] = 1;
    for(std::size_t block = 0; block < function->blocks.size(); ++block)
      for(std::size_t instruction = 0;
          instruction < function->blocks[block].instructions.size();
          ++instruction) {
        const lowir_model::ValueId destination =
          function->blocks[block].instructions[instruction].dest;
        if(destination.valid() && replacement_temporaries[destination])
          replacement_definitions[destination] = block;
      }
  }
  std::vector<unsigned char> slots_with_loads(function->slot_names.size(), 0);
  std::vector<unsigned char> slots_with_unresolved_loads(
    function->slot_names.size(), 0);
  std::vector<lowir_model::SlotId> load_slots(function->value_names.size());
  std::vector<unsigned char> has_load_slot(function->value_names.size(), 0);
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind != Instruction::IK_LOAD || ins.first.kind != Operand::OP_SLOT ||
         !eligible[ins.first.slot])
        continue;
      slots_with_loads[ins.first.slot] = 1;
      load_slots[ins.dest] = ins.first.slot;
      has_load_slot[ins.dest] = 1;
      bool textually_available = replacements[i].known[ins.dest];
      const Operand & replacement = replacements[i].values[ins.dest];
      if(textually_available && replacement.kind == Operand::OP_TEMP &&
         !parameter_temporaries[replacement.value]) {
        textually_available =
          replacement_definitions[replacement.value] != kNoBlockIndex &&
          replacement_definitions[replacement.value] <= i;
      }
      if(!textually_available)
        slots_with_unresolved_loads[ins.first.slot] = 1;
    }
  std::vector<unsigned char> promoted(function->slot_names.size(), 0);
  std::size_t promoted_count = 0;
  for(std::size_t i = 0; i < function->slots.size(); ++i) {
    const lowir_model::SlotId slot = function->slots[i];
    if(eligible[slot] && slots_with_loads[slot] &&
       !slots_with_unresolved_loads[slot]) {
      promoted[slot] = 1;
      ++promoted_count;
    }
  }
  if(promoted_count == 0) return false;
  std::vector<Operand> load_aliases(function->value_names.size());
  std::vector<unsigned char> has_load_alias(function->value_names.size(), 0);
  for(std::size_t i = 0; i < replacements.size(); ++i)
    for(std::size_t value = 0; value < replacements[i].known.size(); ++value)
      if(replacements[i].known[value] && has_load_slot[value] &&
         promoted[load_slots[value]]) {
        load_aliases[value] = replacements[i].values[value];
        has_load_alias[value] = 1;
      }
  rewrite_promoted_slots(function, promoted, storage_temporaries,
    load_aliases, has_load_alias, stats);
  return true;
}

FunctionBoundaries
function_boundaries(const LowirProgram & program)
{
  FunctionBoundaries result;
  result.values.resize(program.symbol_names.size());
  result.known.assign(program.symbol_names.size(), 0);
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const lowir_model::SymbolId symbol =
      program.function_declarations[i].symbol;
    result.values[symbol] = program.function_declarations[i].boundary;
    result.known[symbol] = 1;
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const lowir_model::SymbolId symbol = program.functions[i].symbol;
    result.values[symbol] = program.functions[i].boundary;
    result.known[symbol] = 1;
  }
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
               const FunctionBoundaries & boundaries,
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

bool prepare_for_inlining(Function * function,
                          const FunctionBoundaries & boundaries,
                          Stats * stats)
{
  timed_function_pass(simplify_values, function, stats,
    &Stats::simplify_runs, &Stats::simplify_nanoseconds);
  timed_dce(function, boundaries, stats);
  if(!timed_function_pass(cleanup_cfg, function, stats,
       &Stats::cfg_runs, &Stats::cfg_nanoseconds))
    return false;
  const bool values_changed = timed_function_pass(
    simplify_values, function, stats,
    &Stats::simplify_runs, &Stats::simplify_nanoseconds);
  timed_dce(function, boundaries, stats);
  return values_changed;
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
  const FunctionBoundaries boundaries =
    function_boundaries(program);
  std::vector<unsigned char> prepared_oversized_symbols(
    program.symbol_names.size(), 0);
  std::vector<std::size_t> original_instruction_counts(
    program.functions.size(), 0);
  std::vector<unsigned char> post_cfg_values_changed(
    program.functions.size(), 0);
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    std::size_t original_instructions = 0;
    for(std::size_t b = 0; b < program.functions[i].blocks.size(); ++b)
      original_instructions +=
        program.functions[i].blocks[b].instructions.size();
    original_instruction_counts[i] = original_instructions;
    if(original_instructions > 40 &&
       !program.functions[i].metadata.prefer_local_object_binding)
      prepared_oversized_symbols[program.functions[i].symbol] = 1;
    post_cfg_values_changed[i] =
      prepare_for_inlining(&program.functions[i], boundaries, stats) ? 1 : 0;
  }
  std::vector<unsigned char> inlined_symbols(program.symbol_names.size(), 0);
  std::chrono::steady_clock::time_point inline_started;
  if(stats) inline_started = std::chrono::steady_clock::now();
  const std::size_t inline_rewrites =
    inline_o1_calls(program, prepared_oversized_symbols,
      original_instruction_counts, &inlined_symbols, stats);
  if(stats) {
    stats->inline_changed_callers =
      std::count(inlined_symbols.begin(), inlined_symbols.end(), 1);
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
    if(inlined_symbols[function.symbol])
      post_cfg_values_changed[i] =
        prepare_for_inlining(&function, boundaries, stats) ? 1 : 0;
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
    bool slot_values_changed = false;
    if(inlined_symbols[function.symbol] || level >= 2)
      slot_values_changed = timed_function_pass(
        forward_single_store_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds);
    if(inlined_symbols[function.symbol])
      slot_values_changed = timed_function_pass(
        local_slot_forward, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds) || slot_values_changed;
    if(slot_values_changed) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds);
      timed_dce(&function, boundaries, stats);
    }
    if(post_cfg_values_changed[i] || slot_values_changed)
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
  lowir_model::intern_lowir_program_literals(program);
}

}  // namespace lowir_opt
