#include "lowir_opt.h"
#include "lowir_boolean_cfg.h"
#include "lowir_cleanup_o1.h"
#include "lowir_function_analysis.h"
#include "lowir_expression_key.h"
#include "lowir_function_reachability.h"
#include "lowir_full_unroll_o3.h"
#include "lowir_inline_o1.h"
#include "lowir_inline_analysis.h"
#include "lowir_interprocedural_specialization.h"
#include "lowir_loop_opt.h"
#include "lowir_loop_simplify.h"
#include "lowir_memory_gvn.h"
#include "lowir_pre.h"
#include "lowir_slot_forward_o1.h"
#include "lowir_slot_promotion.h"
#include "lowir_small_object_promotion.h"
#include "lowir_unreachable_opt.h"

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
using lowir_analysis::DominatorTree;
using lowir_analysis::EdgeList;
using lowir_analysis::Graph;
using lowir_analysis::build_graph;
using lowir_analysis::dominance_frontiers;
using lowir_analysis::dominators;

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
    return a.has_int_value == b.has_int_value &&
      a.int_value == b.int_value && a.int_high == b.int_high;
  }
  if(a.kind == Operand::OP_FLOAT) {
    return a.has_float_bits == b.has_float_bits &&
      a.literal_low == b.literal_low && a.literal_high == b.literal_high &&
      same_lowir_type(a.literal_type, b.literal_type);
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
  result.literal_type = type;
  return result;
}

Operand floating_operand(long double value, const LowType & type)
{
  Operand result;
  result.kind = Operand::OP_FLOAT;
  result.literal_type = type;
  lowir_model::lowir_floating_value_bits(
    value, type, &result.literal_low, &result.literal_high);
  result.has_float_bits = true;
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
    kind == Instruction::IK_PHI ||
    kind == Instruction::IK_ADDR || kind == Instruction::IK_INDEX ||
    kind == Instruction::IK_UNARY || kind == Instruction::IK_BINARY ||
    kind == Instruction::IK_CMP || kind == Instruction::IK_CONVERT;
}

bool commutative(LowOperation op)
{
  return op.kind == LowOperation::LOP_ADD || op.kind == LowOperation::LOP_MUL || op.kind == LowOperation::LOP_AND || op.kind == LowOperation::LOP_OR ||
    op.kind == LowOperation::LOP_XOR;
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
    const long double a = lowir_model::lowir_floating_value(
      ins.first.literal_low, ins.first.literal_high, ins.first.literal_type);
    const long double b = lowir_model::lowir_floating_value(
      ins.second.literal_low, ins.second.literal_high, ins.second.literal_type);
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
    *result = floating_operand(lowir_model::lowir_floating_value(
      ins.first.literal_low, ins.first.literal_high, ins.first.literal_type),
      ins.type);
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
                                  const std::vector<std::size_t> & block_index,
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
    if(ins->args[i].kind == Operand::OP_TEMP) {
      std::size_t use_block = block;
      if(ins->kind == Instruction::IK_PHI && i > 0 && (i & 1) &&
         ins->args[i - 1].kind == Operand::OP_LABEL) {
        const std::uint32_t predecessor = ins->args[i - 1].block;
        if(predecessor < block_index.size() &&
           block_index[predecessor] != kNoBlockIndex)
          use_block = block_index[predecessor];
      }
      ins->args[i] = resolve_operand(
        ins->args[i], facts, known, use_block, dom);
    }
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

bool simplify_values_with_analysis(
    Function * function, Stats * stats,
    lowir_analysis::FunctionAnalysis * analysis)
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
  DominatorTree owned_dom;
  const DominatorTree * dom_view = &owned_dom;
  if(function->blocks.size() == 1) {
    owned_dom.immediate.assign(1, 0);
    owned_dom.preorder.assign(1, 1);
    owned_dom.postorder.assign(1, 1);
  } else if(analysis) {
    dom_view = &analysis->dominator_tree();
  } else {
    const Graph graph = build_graph(*function, stats);
    owned_dom = dominators(graph, stats);
  }
  const DominatorTree & dom = *dom_view;
  std::size_t instruction_total = 0;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    instruction_total += function->blocks[i].instructions.size();
  PassArena arena;
  bool function_has_eh = false;
  const std::vector<unsigned char> storage_temporaries =
    find_storage_temporaries(*function, &function_has_eh);
  std::vector<std::size_t> block_index(
    function->next_block_id, kNoBlockIndex);
  std::vector<unsigned char> phi_used(function->value_names.size(), 0);
  for(std::size_t block = 0; block < function->blocks.size(); ++block) {
    block_index[function->blocks[block].id] = block;
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind != Instruction::IK_PHI) break;
      for(std::size_t incoming = 1; incoming < ins.args.size(); incoming += 2)
        if(ins.args[incoming].kind == Operand::OP_TEMP)
          phi_used[ins.args[incoming].value] = 1;
    }
  }

  typedef PassAllocator<std::pair<const ExpressionKey, std::size_t> >
    ExpressionAllocator;
  struct ScopedExpression
  {
    Fact fact;
    std::size_t key;
    std::size_t previous;
  };
  struct BlockEvent
  {
    std::size_t block;
    bool entering;
  };
  std::vector<Fact> facts(function->value_names.size());
  std::vector<unsigned char> known_facts(function->value_names.size(), 0);
  std::unordered_map<ExpressionKey, std::size_t, ExpressionKeyHash,
    std::equal_to<ExpressionKey>, ExpressionAllocator> expressions(
      0, ExpressionKeyHash(), std::equal_to<ExpressionKey>(),
      ExpressionAllocator(&arena));
  std::vector<std::size_t> expression_heads;
  std::vector<ScopedExpression> scoped_expressions;
  std::vector<DefinitionFact> definitions(function->value_names.size());
  std::vector<unsigned char> known_definitions(
    function->value_names.size(), 0);
  expressions.reserve(instruction_total / 2 + 1);
  expression_heads.reserve(instruction_total / 2 + 1);
  scoped_expressions.reserve(instruction_total / 2 + 1);

  std::vector<lowir_analysis::EdgeList> owned_dom_children;
  const std::vector<lowir_analysis::EdgeList> * dom_children = 0;
  if(analysis && function->blocks.size() != 1)
    dom_children = &analysis->dominator_children();
  else {
    owned_dom_children = lowir_analysis::build_dominator_children(dom);
    dom_children = &owned_dom_children;
  }
  struct TraversalFrame
  {
    std::size_t block;
    std::size_t child;
  };
  std::vector<BlockEvent> traversal;
  std::vector<TraversalFrame> traversal_stack;
  std::vector<unsigned char> scheduled(function->blocks.size(), 0);
  for(std::size_t root = 0; root < function->blocks.size(); ++root) {
    if(scheduled[root] || (root != 0 && dom.preorder[root] != 0)) continue;
    scheduled[root] = 1;
    traversal.push_back(BlockEvent{root, true});
    traversal_stack.push_back(TraversalFrame{root, 0});
    while(!traversal_stack.empty()) {
      TraversalFrame & frame = traversal_stack.back();
      if(frame.child < (*dom_children)[frame.block].size()) {
        const std::size_t child =
          (*dom_children)[frame.block][frame.child++];
        scheduled[child] = 1;
        traversal.push_back(BlockEvent{child, true});
        traversal_stack.push_back(TraversalFrame{child, 0});
      } else {
        traversal.push_back(BlockEvent{frame.block, false});
        traversal_stack.pop_back();
      }
    }
  }
  std::vector<std::size_t> scope_marks(
    function->blocks.size(), 0);
  bool changed = false;
  for(std::size_t event = 0; event < traversal.size(); ++event) {
    const std::size_t block = traversal[event].block;
    if(!traversal[event].entering) {
      while(scoped_expressions.size() > scope_marks[block]) {
        const ScopedExpression & scoped = scoped_expressions.back();
        expression_heads[scoped.key] = scoped.previous;
        scoped_expressions.pop_back();
      }
      continue;
    }
    scope_marks[block] = scoped_expressions.size();
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    const std::size_t original_size = instructions.size();
    std::size_t kept = 0;
    for(std::size_t index = 0; index < original_size; ++index) {
      Instruction & ins = instructions[index];
      if(stats) ++stats->instruction_visits;
      resolve_instruction_operands(
        &ins, facts, known_facts, block, block_index, dom);

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
      } else if(ins.kind == Instruction::IK_INDEX &&
                ins.first.kind == Operand::OP_TEMP &&
                is_zero(ins.second)) {
        // A zero byte displacement preserves the pointer identity.  Restrict
        // the replacement to a typed value so storage-address rewriting keeps
        // the ordinary temporary-address contract.
        replacement = ins.first;
        replace = true;
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
      else if(ins.kind == Instruction::IK_PHI && ins.args.size() >= 2) {
        replacement = ins.args[1];
        replace = true;
        for(std::size_t incoming = 3; incoming < ins.args.size(); incoming += 2)
          if(!same_operand(replacement, ins.args[incoming])) {
            replace = false;
            break;
          }
      }

      if(replace && ins.dest.valid()) {
        facts[ins.dest] = Fact{replacement, block};
        known_facts[ins.dest] = 1;
        changed = true;
        if(stats) { ++stats->rewrites; ++stats->worklist_pushes; }
        if(!phi_used[ins.dest]) continue;
      }

      if(cse_eligible(ins.kind) && ins.dest.valid()) {
        const ExpressionKey key = expression_key(ins);
        if(stats) ++stats->gvn_expression_probes;
        const auto found = expressions.emplace(
          key, expression_heads.size());
        if(found.second) {
          expression_heads.push_back(kNoBlockIndex);
          if(stats) ++stats->gvn_expression_keys;
        }
        const std::size_t key_id = found.first->second;
        const std::size_t head = expression_heads[key_id];
        const bool cross_block_guard = function_has_eh &&
          (ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX) &&
          head != kNoBlockIndex &&
          scoped_expressions[head].fact.block != block;
        if(head != kNoBlockIndex && !cross_block_guard) {
          facts[ins.dest] = Fact{
            scoped_expressions[head].fact.value, block};
          known_facts[ins.dest] = 1;
          changed = true;
          if(stats) {
            ++stats->gvn_expression_hits;
            ++stats->rewrites;
            ++stats->worklist_pushes;
          }
          continue;
        }
        Operand produced;
        produced.kind = Operand::OP_TEMP;
        produced.value = ins.dest;
        scoped_expressions.push_back(ScopedExpression{
          Fact{produced, block}, key_id, head});
        expression_heads[key_id] = scoped_expressions.size() - 1;
        if(stats)
          stats->gvn_expression_peak_scope = std::max(
            stats->gvn_expression_peak_scope,
            scoped_expressions.size());
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
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      Instruction & ins = function->blocks[block].instructions[index];
      if(ins.kind != Instruction::IK_PHI) break;
      resolve_instruction_operands(
        &ins, facts, known_facts, block, block_index, dom);
    }
  return changed;
}

bool simplify_values(Function * function, Stats * stats)
{
  return simplify_values_with_analysis(function, stats, 0);
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
  return (boundary.effects == lowir_model::CFXM_READNONE ||
          boundary.effects == lowir_model::CFXM_READONLY) &&
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
  bool changed = fold_boolean_phi_branch(function, stats);
  // Phi predecessor identities are part of the instruction contract.  Phi
  // construction runs after CFG cleanup; a later optimizer round trip keeps
  // that CFG stable until edge-aware repair is requested by a transform.
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function->blocks[block].instructions.size();
        ++instruction)
      if(function->blocks[block].instructions[instruction].kind ==
         Instruction::IK_PHI) return changed;
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
    // A backward merge would relocate the target after blocks that may use
    // values it defines, violating LowIR's presentation-order requirement.
    if(target == kNoBlockIndex || target <= i ||
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


struct AbstractState
{
  bool executable = false;
  struct SlotBinding
  {
    lowir_model::SlotId slot;
    Operand value;
  };
  std::vector<SlotBinding> slots;
};

bool abstract_slot_value(const AbstractState & state,
                         lowir_model::SlotId slot, Operand * value)
{
  if(!state.executable) return false;
  for(std::size_t i = 0; i < state.slots.size(); ++i)
    if(state.slots[i].slot == slot) {
      *value = state.slots[i].value;
      return true;
    }
  return false;
}

class SparseMeetScratch
{
public:
  explicit SparseMeetScratch(std::size_t slot_count)
    : values_(slot_count), epochs_(slot_count, 0), epoch_(0)
  {}

  bool meet(AbstractState * target, const AbstractState & incoming,
            Stats * stats)
  {
    if(!incoming.executable) return false;
    if(stats) ++stats->promote_sparse_meets;
    if(!target->executable) {
      *target = incoming;
      return true;
    }
    advance_epoch();
    for(std::size_t i = 0; i < incoming.slots.size(); ++i) {
      const std::uint32_t slot = incoming.slots[i].slot;
      epochs_[slot] = epoch_;
      values_[slot] = incoming.slots[i].value;
    }
    std::size_t kept = 0;
    for(std::size_t i = 0; i < target->slots.size(); ++i) {
      const std::uint32_t slot = target->slots[i].slot;
      if(epochs_[slot] != epoch_ ||
         !same_operand(target->slots[i].value, values_[slot]))
        continue;
      if(kept != i) target->slots[kept] = target->slots[i];
      ++kept;
    }
    const bool changed = kept != target->slots.size();
    target->slots.resize(kept);
    return changed;
  }

  std::size_t storage_bytes() const
  {
    return values_.capacity() * sizeof(Operand) +
      epochs_.capacity() * sizeof(std::uint32_t);
  }

private:
  void advance_epoch()
  {
    ++epoch_;
    if(epoch_ != 0) return;
    std::fill(epochs_.begin(), epochs_.end(), 0);
    epoch_ = 1;
  }

  std::vector<Operand> values_;
  std::vector<std::uint32_t> epochs_;
  std::uint32_t epoch_;
};

class SparseTransferState
{
public:
  SparseTransferState(std::size_t slot_count, std::size_t value_count)
    : slot_values_(slot_count), slot_epochs_(slot_count, 0),
      value_values_(value_count), value_epochs_(value_count, 0), epoch_(0)
  {}

  void begin(const AbstractState & incoming)
  {
    advance_epoch();
    active_slots_.clear();
    for(std::size_t i = 0; i < incoming.slots.size(); ++i)
      set_slot(incoming.slots[i].slot, incoming.slots[i].value);
  }

  void set_slot(lowir_model::SlotId slot, const Operand & value)
  {
    const std::uint32_t id = slot;
    if(slot_epochs_[id] != epoch_) {
      slot_epochs_[id] = epoch_;
      active_slots_.push_back(slot);
    }
    slot_values_[id] = value;
  }

  bool slot_value(lowir_model::SlotId slot, Operand * value) const
  {
    const std::uint32_t id = slot;
    if(id >= slot_epochs_.size() || slot_epochs_[id] != epoch_) return false;
    *value = slot_values_[id];
    return true;
  }

  void set_value(lowir_model::ValueId value, const Operand & replacement)
  {
    const std::uint32_t id = value;
    value_epochs_[id] = epoch_;
    value_values_[id] = replacement;
  }

  void clear_value(lowir_model::ValueId value)
  {
    value_epochs_[static_cast<std::uint32_t>(value)] = 0;
  }

  Operand resolve(Operand value) const
  {
    const std::size_t limit = slot_values_.size() + value_values_.size();
    for(std::size_t step = 0; step < limit; ++step) {
      if(value.kind == Operand::OP_SLOT) {
        Operand replacement;
        if(!slot_value(value.slot, &replacement)) break;
        value = replacement;
      } else if(value.kind == Operand::OP_TEMP) {
        const std::uint32_t id = value.value;
        if(id >= value_epochs_.size() || value_epochs_[id] != epoch_) break;
        value = value_values_[id];
      } else break;
    }
    return value;
  }

  AbstractState snapshot() const
  {
    AbstractState result;
    result.executable = true;
    result.slots.reserve(active_slots_.size());
    for(std::size_t i = 0; i < active_slots_.size(); ++i) {
      const lowir_model::SlotId slot = active_slots_[i];
      const Operand & value = slot_values_[static_cast<std::uint32_t>(slot)];
      if(value.kind == Operand::OP_TEMP) {
        const std::uint32_t id = value.value;
        if(id < value_epochs_.size() && value_epochs_[id] == epoch_) continue;
      }
      AbstractState::SlotBinding binding;
      binding.slot = slot;
      binding.value = value;
      result.slots.push_back(binding);
    }
    return result;
  }

  std::size_t storage_bytes() const
  {
    return slot_values_.capacity() * sizeof(Operand) +
      slot_epochs_.capacity() * sizeof(std::uint32_t) +
      value_values_.capacity() * sizeof(Operand) +
      value_epochs_.capacity() * sizeof(std::uint32_t) +
      active_slots_.capacity() * sizeof(lowir_model::SlotId);
  }

private:
  void advance_epoch()
  {
    ++epoch_;
    if(epoch_ != 0) return;
    std::fill(slot_epochs_.begin(), slot_epochs_.end(), 0);
    std::fill(value_epochs_.begin(), value_epochs_.end(), 0);
    epoch_ = 1;
  }

  std::vector<Operand> slot_values_;
  std::vector<std::uint32_t> slot_epochs_;
  std::vector<lowir_model::SlotId> active_slots_;
  std::vector<Operand> value_values_;
  std::vector<std::uint32_t> value_epochs_;
  std::uint32_t epoch_;
};

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

bool promote_slots_with_analysis(
    Function * function, Stats * stats,
    lowir_analysis::FunctionAnalysis * analysis)
{
  if(function->blocks.empty() || function->slots.empty()) return false;
  std::size_t eligible_count = 0;
  const std::vector<unsigned char> eligible =
    find_promotable_slots(*function, &eligible_count);
  if(eligible_count == 0) return false;
  if(stats) stats->promote_eligible_slots += eligible_count;
  const std::vector<unsigned char> storage_temporaries =
    find_storage_temporaries(*function);

  Graph owned_graph;
  DominatorTree owned_dominators;
  std::vector<EdgeList> owned_frontiers;
  const Graph * graph_view;
  const DominatorTree * dominator_view;
  const std::vector<EdgeList> * frontier_view;
  if(analysis) {
    graph_view = &analysis->graph();
    dominator_view = &analysis->dominator_tree();
    frontier_view = &analysis->dominance_frontier();
  } else {
    owned_graph = build_graph(*function, stats);
    owned_dominators = dominators(owned_graph, stats);
    owned_frontiers = dominance_frontiers(owned_graph, owned_dominators);
    graph_view = &owned_graph;
    dominator_view = &owned_dominators;
    frontier_view = &owned_frontiers;
  }
  const Graph & graph = *graph_view;
  const DominatorTree & promotion_dominators = *dominator_view;
  const std::vector<EdgeList> & frontiers = *frontier_view;
  std::vector<unsigned char> slot_has_load(function->slot_names.size(), 0);
  typedef std::pair<std::uint32_t, std::uint32_t> SlotDefinition;
  std::vector<SlotDefinition> slot_definitions;
  std::size_t instruction_count = 0;
  for(std::size_t block = 0; block < function->blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function->blocks[block].instructions.size(); ++index) {
      const Instruction & ins = function->blocks[block].instructions[index];
      ++instruction_count;
      if(ins.kind == Instruction::IK_LOAD &&
         ins.first.kind == Operand::OP_SLOT && eligible[ins.first.slot])
        slot_has_load[ins.first.slot] = 1;
      else if(ins.kind == Instruction::IK_STORE &&
              ins.second.kind == Operand::OP_SLOT && eligible[ins.second.slot])
        slot_definitions.push_back(SlotDefinition(
          static_cast<std::uint32_t>(ins.second.slot),
          static_cast<std::uint32_t>(block)));
    }
  std::sort(slot_definitions.begin(), slot_definitions.end());
  slot_definitions.erase(
    std::unique(slot_definitions.begin(), slot_definitions.end()),
    slot_definitions.end());

  struct PlannedPhi
  {
    lowir_model::SlotId slot;
    lowir_model::ValueId destination;
    std::size_t block;
    std::size_t next;
    Instruction instruction;
    bool complete;
  };
  std::vector<PlannedPhi> planned_phis;
  std::vector<std::size_t> first_phi(function->blocks.size(), kNoBlockIndex);
  std::vector<std::size_t> last_phi(function->blocks.size(), kNoBlockIndex);
  std::vector<std::uint32_t> definition_epochs(
    function->blocks.size(), 0);
  std::vector<std::uint32_t> phi_epochs(function->blocks.size(), 0);
  std::vector<std::size_t> phi_work;
  std::vector<std::size_t> slot_phi_blocks;
  std::uint32_t phi_epoch = 0;
  const std::size_t phi_budget = std::min<std::size_t>(
    65536, std::max<std::size_t>(64, instruction_count / 2 + 1));
  std::size_t definition_cursor = 0;
  for(std::size_t slot_index = 0;
      slot_index < function->slots.size(); ++slot_index) {
    const lowir_model::SlotId slot = function->slots[slot_index];
    const std::uint32_t slot_id = slot;
    while(definition_cursor < slot_definitions.size() &&
          slot_definitions[definition_cursor].first < slot_id)
      ++definition_cursor;
    std::size_t definition_end = definition_cursor;
    while(definition_end < slot_definitions.size() &&
          slot_definitions[definition_end].first == slot_id)
      ++definition_end;
    if(!eligible[slot] || !slot_has_load[slot] ||
       !slot_is_phi_scalar_type(lowir_model::lowir_slot_type(*function, slot)) ||
       definition_end - definition_cursor < 2) {
      definition_cursor = definition_end;
      continue;
    }
    ++phi_epoch;
    if(phi_epoch == 0) {
      std::fill(definition_epochs.begin(), definition_epochs.end(), 0);
      std::fill(phi_epochs.begin(), phi_epochs.end(), 0);
      phi_epoch = 1;
    }
    phi_work.clear();
    slot_phi_blocks.clear();
    for(std::size_t definition = definition_cursor;
        definition < definition_end; ++definition) {
      const std::size_t block = slot_definitions[definition].second;
      if(!promotion_dominators.preorder[block] ||
         definition_epochs[block] == phi_epoch) continue;
      definition_epochs[block] = phi_epoch;
      phi_work.push_back(block);
    }
    bool exhausted = false;
    for(std::size_t work_index = 0;
        work_index < phi_work.size() && !exhausted; ++work_index) {
      const EdgeList & block_frontier = frontiers[phi_work[work_index]];
      for(std::size_t edge = 0; edge < block_frontier.size(); ++edge) {
        const std::size_t join = block_frontier[edge];
        const std::uint32_t block_id = function->blocks[join].id;
        if(!promotion_dominators.preorder[join] ||
           graph.predecessors[join].size() <= 1 ||
           (block_id < graph.eh_targets.size() &&
            graph.eh_targets[block_id]) ||
           phi_epochs[join] == phi_epoch)
          continue;
        if(planned_phis.size() + slot_phi_blocks.size() == phi_budget) {
          exhausted = true;
          break;
        }
        phi_epochs[join] = phi_epoch;
        slot_phi_blocks.push_back(join);
        if(definition_epochs[join] != phi_epoch) {
          definition_epochs[join] = phi_epoch;
          phi_work.push_back(join);
        }
      }
    }
    definition_cursor = definition_end;
    if(exhausted) {
      if(stats) { ++stats->promote_phi_budget_skips; ++stats->budget_skips; }
      continue;
    }
    std::sort(slot_phi_blocks.begin(), slot_phi_blocks.end());
    for(std::size_t index = 0; index < slot_phi_blocks.size(); ++index) {
      PlannedPhi planned;
      planned.slot = slot;
      planned.block = slot_phi_blocks[index];
      planned.next = kNoBlockIndex;
      planned.complete = false;
      planned.instruction.kind = Instruction::IK_PHI;
      planned.instruction.type =
        lowir_model::lowir_slot_type(*function, slot);
      planned.destination = lowir_model::append_lowir_fresh_generated_value(
        *function, planned.instruction.type);
      planned.instruction.dest = planned.destination;
      const std::size_t planned_index = planned_phis.size();
      planned_phis.push_back(std::move(planned));
      if(first_phi[slot_phi_blocks[index]] == kNoBlockIndex)
        first_phi[slot_phi_blocks[index]] = planned_index;
      else
        planned_phis[last_phi[slot_phi_blocks[index]]].next = planned_index;
      last_phi[slot_phi_blocks[index]] = planned_index;
    }
  }

  std::vector<AbstractState> incoming(function->blocks.size());
  incoming[0].executable = true;
  std::deque<std::size_t> work;
  std::vector<unsigned char> queued(function->blocks.size(), 0);
  work.push_back(0); queued[0] = 1;
  if(stats) ++stats->worklist_pushes;
  struct LoadReplacement
  {
    lowir_model::ValueId destination;
    Operand value;
  };
  std::vector<std::vector<LoadReplacement> > replacements(
    function->blocks.size());
  std::vector<AbstractState> outgoing_states(function->blocks.size());
  SparseTransferState state(
    function->slot_names.size(), function->value_names.size());
  SparseMeetScratch meet(function->slot_names.size());
  while(!work.empty()) {
    const std::size_t block_index = work.front(); work.pop_front();
    queued[block_index] = 0;
    state.begin(incoming[block_index]);
    for(std::size_t phi = first_phi[block_index];
        phi != kNoBlockIndex; phi = planned_phis[phi].next) {
      Operand merged;
      merged.kind = Operand::OP_TEMP;
      merged.value = planned_phis[phi].destination;
      state.set_slot(planned_phis[phi].slot, merged);
    }
    replacements[block_index].clear();
    const Block & block = function->blocks[block_index];
    std::vector<std::pair<std::size_t, AbstractState> > exceptional;
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const Instruction & source = block.instructions[i];
      Instruction ins;
      ins.kind = source.kind;
      ins.dest = source.dest;
      ins.type = source.type;
      ins.source_type = source.source_type;
      ins.op = source.op;
      ins.first = state.resolve(source.first);
      ins.second = state.resolve(source.second);
      ins.third = state.resolve(source.third);
      if((ins.kind == Instruction::IK_EH_TRY ||
          ins.kind == Instruction::IK_EH_CLEANUP) &&
         graph.find(ins.first.block) != kNoBlockIndex) {
        exceptional.push_back(std::make_pair(
          graph.find(ins.first.block), state.snapshot()));
      }
      if(ins.kind == Instruction::IK_STORE &&
         source.second.kind == Operand::OP_SLOT &&
         eligible[source.second.slot]) {
        state.set_slot(source.second.slot, ins.first);
      }
      else if(ins.kind == Instruction::IK_LOAD &&
              source.first.kind == Operand::OP_SLOT &&
              eligible[source.first.slot]) {
        Operand value;
        if(state.slot_value(source.first.slot, &value)) {
          state.set_value(ins.dest, value);
          LoadReplacement replacement;
          replacement.destination = ins.dest;
          replacement.value = value;
          replacements[block_index].push_back(replacement);
        }
      } else if(ins.dest.valid()) {
        Operand folded;
        bool known = ins.kind == Instruction::IK_CONST ? (folded = ins.first, true) :
          ins.kind == Instruction::IK_COPY &&
            !lowir_model::lowir_value_preserves_copy(
              *function, ins.dest) &&
            (ins.first.kind != Operand::OP_TEMP ||
             lowir_model::same_lowir_type(
               lowir_model::lowir_value_type(*function, ins.first.value),
               ins.type)) ?
              (folded = ins.first, true) :
          ins.kind == Instruction::IK_UNARY ? fold_unary(ins, &folded) :
          ins.kind == Instruction::IK_BINARY ? fold_binary(ins, &folded) :
          ins.kind == Instruction::IK_CMP ? fold_compare(ins, &folded) :
          ins.kind == Instruction::IK_CONVERT ? fold_convert(ins, &folded) : false;
        if(known) state.set_value(ins.dest, folded);
        else state.clear_value(ins.dest);
      }
      if(stats) ++stats->instruction_visits;
    }
    std::vector<std::size_t> normal;
    if(!block.instructions.empty()) {
      const Instruction & term = block.instructions.back();
      const Operand selector = state.resolve(term.first);
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
            Operand case_value = state.resolve(term.args[i]);
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
    for(std::size_t i = 0; i < exceptional.size(); ++i) {
      if(meet.meet(&incoming[exceptional[i].first], exceptional[i].second,
                   stats) &&
         !queued[exceptional[i].first]) {
        work.push_back(exceptional[i].first); queued[exceptional[i].first] = 1;
        if(stats) { ++stats->worklist_pushes; ++stats->dataflow_updates; }
      }
    }
    const AbstractState outgoing_state = state.snapshot();
    outgoing_states[block_index] = outgoing_state;
    for(std::size_t i = 0; i < normal.size(); ++i)
      if(meet.meet(&incoming[normal[i]], outgoing_state, stats) &&
         !queued[normal[i]]) {
        work.push_back(normal[i]); queued[normal[i]] = 1;
        if(stats) { ++stats->worklist_pushes; ++stats->dataflow_updates; }
      }
  }

  for(std::size_t phi = 0; phi < planned_phis.size(); ++phi) {
    PlannedPhi & planned = planned_phis[phi];
    const EdgeList & predecessors = graph.predecessors[planned.block];
    planned.complete = true;
    for(std::size_t predecessor = 0;
        predecessor < predecessors.size(); ++predecessor) {
      const std::size_t source = predecessors[predecessor];
      Operand value;
      if(!abstract_slot_value(outgoing_states[source], planned.slot, &value)) {
        planned.complete = false;
        break;
      }
      Operand label;
      label.kind = Operand::OP_LABEL;
      label.block = function->blocks[source].id;
      planned.instruction.args.push_back(label);
      planned.instruction.args.push_back(value);
    }
  }

  struct PhiDependency
  {
    std::size_t dependent;
    std::size_t next;
  };
  std::vector<std::size_t> phi_owner(
    function->value_names.size(), kNoBlockIndex);
  std::vector<std::size_t> first_dependents(
    planned_phis.size(), kNoBlockIndex);
  std::vector<PhiDependency> phi_dependencies;
  std::vector<std::size_t> incomplete_phis;
  incomplete_phis.reserve(planned_phis.size());
  for(std::size_t phi = 0; phi < planned_phis.size(); ++phi)
    phi_owner[planned_phis[phi].destination] = phi;
  for(std::size_t phi = 0; phi < planned_phis.size(); ++phi) {
    if(!planned_phis[phi].complete) incomplete_phis.push_back(phi);
    const std::vector<Operand> & args = planned_phis[phi].instruction.args;
    for(std::size_t incoming = 1; incoming < args.size(); incoming += 2) {
      if(args[incoming].kind != Operand::OP_TEMP) continue;
      const std::size_t source = phi_owner[args[incoming].value];
      if(source == kNoBlockIndex) continue;
      PhiDependency dependency = {phi, first_dependents[source]};
      first_dependents[source] = phi_dependencies.size();
      phi_dependencies.push_back(dependency);
    }
  }
  for(std::size_t cursor = 0; cursor < incomplete_phis.size(); ++cursor)
    for(std::size_t edge = first_dependents[incomplete_phis[cursor]];
        edge != kNoBlockIndex; edge = phi_dependencies[edge].next) {
      PlannedPhi & dependent =
        planned_phis[phi_dependencies[edge].dependent];
      if(!dependent.complete) continue;
      dependent.complete = false;
      incomplete_phis.push_back(phi_dependencies[edge].dependent);
    }

  if(stats) {
    std::size_t transient_bytes = state.storage_bytes() +
      meet.storage_bytes() +
      incoming.capacity() * sizeof(AbstractState) +
      outgoing_states.capacity() * sizeof(AbstractState) +
      replacements.capacity() * sizeof(std::vector<LoadReplacement>) +
      planned_phis.capacity() * sizeof(PlannedPhi) +
      first_phi.capacity() * sizeof(std::size_t) +
      last_phi.capacity() * sizeof(std::size_t) +
      definition_epochs.capacity() * sizeof(std::uint32_t) +
      phi_epochs.capacity() * sizeof(std::uint32_t) +
      phi_work.capacity() * sizeof(std::size_t) +
      slot_phi_blocks.capacity() * sizeof(std::size_t) +
      slot_definitions.capacity() * sizeof(SlotDefinition) +
      phi_owner.capacity() * sizeof(std::size_t) +
      first_dependents.capacity() * sizeof(std::size_t) +
      phi_dependencies.capacity() * sizeof(PhiDependency) +
      incomplete_phis.capacity() * sizeof(std::size_t);
    for(std::size_t block = 0; block < incoming.size(); ++block) {
      stats->promote_sparse_state_entries += incoming[block].slots.size();
      transient_bytes += incoming[block].slots.capacity() *
        sizeof(AbstractState::SlotBinding);
      transient_bytes += outgoing_states[block].slots.capacity() *
        sizeof(AbstractState::SlotBinding);
      transient_bytes += replacements[block].capacity() *
        sizeof(LoadReplacement);
      if(graph.predecessors[block].size() > 1)
        stats->promote_sparse_merge_facts += incoming[block].slots.size();
    }
    stats->promote_peak_transient_bytes = std::max(
      stats->promote_peak_transient_bytes, transient_bytes);
  }

  std::vector<Operand> replacement_values(function->value_names.size());
  std::vector<unsigned char> has_replacement(
    function->value_names.size(), 0);
  for(std::size_t block = 0; block < replacements.size(); ++block)
    for(std::size_t i = 0; i < replacements[block].size(); ++i) {
      const lowir_model::ValueId destination =
        replacements[block][i].destination;
      replacement_values[destination] = replacements[block][i].value;
      has_replacement[destination] = 1;
    }
  std::vector<unsigned char> replacement_temporaries(
    function->value_names.size(), 0);
  bool has_replacement_temporary = false;
  for(std::size_t value = 0; value < has_replacement.size(); ++value)
      if(has_replacement[value] &&
         replacement_values[value].kind == Operand::OP_TEMP) {
        replacement_temporaries[replacement_values[value].value] = 1;
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
    for(std::size_t phi = 0; phi < planned_phis.size(); ++phi)
      if(planned_phis[phi].complete &&
         replacement_temporaries[planned_phis[phi].destination])
        replacement_definitions[planned_phis[phi].destination] =
          planned_phis[phi].block;
  }
  std::vector<unsigned char> slots_with_loads(function->slot_names.size(), 0);
  std::vector<unsigned char> slots_with_unresolved_loads(
    function->slot_names.size(), 0);
  std::vector<lowir_model::SlotId> load_slots(function->value_names.size());
  std::vector<unsigned char> has_load_slot(function->value_names.size(), 0);
  std::vector<unsigned char> blocked_join_slots;
  if(stats) {
    blocked_join_slots.assign(function->slot_names.size(), 0);
  }
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const Instruction & ins = function->blocks[i].instructions[j];
      if(ins.kind != Instruction::IK_LOAD || ins.first.kind != Operand::OP_SLOT ||
         !eligible[ins.first.slot])
        continue;
      slots_with_loads[ins.first.slot] = 1;
      load_slots[ins.dest] = ins.first.slot;
      has_load_slot[ins.dest] = 1;
      bool textually_available = has_replacement[ins.dest];
      const Operand & replacement = replacement_values[ins.dest];
      if(textually_available && replacement.kind == Operand::OP_TEMP &&
         !parameter_temporaries[replacement.value]) {
        textually_available =
          replacement_definitions[replacement.value] != kNoBlockIndex &&
          replacement_definitions[replacement.value] <= i &&
          promotion_dominators.dominates(
            replacement_definitions[replacement.value], i);
      }
      if(!textually_available) {
        slots_with_unresolved_loads[ins.first.slot] = 1;
        if(stats && graph.predecessors[i].size() > 1) {
          ++stats->promote_blocked_join_loads;
          blocked_join_slots[ins.first.slot] = 1;
          const std::uint32_t block_id = function->blocks[i].id;
          if(block_id < graph.eh_targets.size() &&
             graph.eh_targets[block_id]) {
            ++stats->promote_blocked_eh_loads;
          } else {
            bool loop_header = false;
            for(std::size_t predecessor = 0;
                predecessor < graph.predecessors[i].size(); ++predecessor)
              loop_header = loop_header || promotion_dominators.dominates(
                i, graph.predecessors[i][predecessor]);
            if(loop_header) ++stats->promote_blocked_loop_loads;
            else ++stats->promote_blocked_ordinary_loads;
          }
        }
      }
    }
  if(stats) {
    const std::size_t blocked_slots =
      std::count(blocked_join_slots.begin(), blocked_join_slots.end(), 1);
    stats->promote_blocked_join_slots += blocked_slots;
    if(blocked_slots) ++stats->promote_blocked_join_functions;
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
  std::vector<std::vector<Instruction> > inserted_phis(
    function->blocks.size());
  std::vector<std::vector<Instruction> > inserted_phi_edge_copies(
    function->blocks.size());
  std::vector<unsigned char> phi_values(function->value_names.size(), 0);
  for(std::size_t phi = 0; phi < planned_phis.size(); ++phi)
    if(planned_phis[phi].complete && promoted[planned_phis[phi].slot]) {
      Instruction & instruction = planned_phis[phi].instruction;
      for(std::size_t incoming = 1;
          incoming < instruction.args.size(); incoming += 2) {
        Operand & value = instruction.args[incoming];
        if(value.kind != Operand::OP_TEMP ||
           lowir_model::same_lowir_type(
             lowir_model::lowir_value_type(*function, value.value),
             instruction.type))
          continue;
        const std::size_t predecessor =
          graph.find(instruction.args[incoming - 1].block);
        if(predecessor == kNoBlockIndex)
          throw std::logic_error("invalid promoted phi predecessor");
        Instruction copy;
        copy.kind = Instruction::IK_COPY;
        copy.type = instruction.type;
        copy.first = value;
        copy.dest = lowir_model::append_lowir_fresh_generated_value(
          *function, copy.type);
        value.kind = Operand::OP_TEMP;
        value.value = copy.dest;
        inserted_phi_edge_copies[predecessor].push_back(std::move(copy));
        if(stats) ++stats->rewrites;
      }
      phi_values[planned_phis[phi].destination] = 1;
      if(stats) {
        ++stats->promote_phi_instructions;
        stats->promote_phi_incoming_edges +=
          planned_phis[phi].instruction.args.size() / 2;
      }
      inserted_phis[planned_phis[phi].block].push_back(
        std::move(instruction));
    }
  for(std::size_t block = 0;
      block < inserted_phi_edge_copies.size(); ++block) {
    std::vector<Instruction> & copies = inserted_phi_edge_copies[block];
    if(copies.empty()) continue;
    std::vector<Instruction> & instructions =
      function->blocks[block].instructions;
    std::size_t position = instructions.size();
    while(position &&
          instructions[position - 1].kind == Instruction::IK_EH_END)
      --position;
    if(position) --position;
    instructions.insert(instructions.begin() + position,
      std::make_move_iterator(copies.begin()),
      std::make_move_iterator(copies.end()));
  }
  for(std::size_t block = 0; block < inserted_phis.size(); ++block)
    if(!inserted_phis[block].empty())
      function->blocks[block].instructions.insert(
        function->blocks[block].instructions.begin(),
        std::make_move_iterator(inserted_phis[block].begin()),
        std::make_move_iterator(inserted_phis[block].end()));
  std::vector<Operand> load_aliases(function->value_names.size());
  std::vector<unsigned char> has_load_alias(function->value_names.size(), 0);
  for(std::size_t value = 0; value < has_replacement.size(); ++value)
      if(has_replacement[value] && has_load_slot[value] &&
         promoted[load_slots[value]]) {
        load_aliases[value] = replacement_values[value];
        has_load_alias[value] = 1;
        if(stats && replacement_values[value].kind == Operand::OP_TEMP &&
           phi_values[replacement_values[value].value])
          ++stats->promote_phi_loads;
      }
  rewrite_promoted_slots(function, promoted, storage_temporaries,
    load_aliases, has_load_alias, stats);
  return true;
}

bool promote_slots(Function * function, Stats * stats)
{
  return promote_slots_with_analysis(function, stats, 0);
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
                         std::uint64_t Stats::* nanoseconds,
                         lowir_analysis::FunctionAnalysis * analysis = 0)
{
  const auto run = [pass, function, analysis](Stats * pass_stats) {
    if(analysis && pass == simplify_values)
      return simplify_values_with_analysis(function, pass_stats, analysis);
    if(analysis && pass == promote_slots)
      return promote_slots_with_analysis(function, pass_stats, analysis);
    return pass(function, pass_stats);
  };
  if(!stats) {
    const bool changed = run(0);
    if(changed && analysis && pass == cleanup_cfg) analysis->invalidate_cfg();
    return changed;
  }
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
  } else if(pass == promote_small_objects) {
    detailed_runs = &Stats::small_object_runs;
    detailed_changes = &Stats::small_object_changes;
    detailed_nanoseconds = &Stats::small_object_nanoseconds;
  } else if(pass == eliminate_dead_slot_stores) {
    detailed_runs = &Stats::dead_store_runs;
    detailed_changes = &Stats::dead_store_changes;
    detailed_nanoseconds = &Stats::dead_store_nanoseconds;
  }
  if(detailed_runs) ++(stats->*detailed_runs);
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  const bool changed = run(stats);
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
  if(changed && analysis && pass == cleanup_cfg) analysis->invalidate_cfg();
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

struct LateInlineCleanupContext
{
  const FunctionBoundaries * boundaries;
};

void cleanup_late_inline_body(Function * function, Stats * stats,
                              void * opaque)
{
  const LateInlineCleanupContext & context =
    *static_cast<const LateInlineCleanupContext *>(opaque);
  prepare_for_inlining(function, *context.boundaries, stats);
}

}  // namespace

void optimize(LowirProgram & program, int level, Stats * stats)
{
  if(level < 0 || level > 3)
    throw std::logic_error("invalid LowIR optimization level");
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
  FunctionBoundaries boundaries =
    function_boundaries(program);
  const std::chrono::steady_clock::time_point unreachable_started =
    stats ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point();
  const UnreachableRoleIndex unreachable_roles(program);
  if(stats) stats->unreachable_role_symbols = unreachable_roles.symbol_count();
  std::vector<unsigned char> unreachable_changed(program.functions.size(), 0);
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    unreachable_changed[i] = unreachable_roles.eliminate_conditional_edges(
      &program.functions[i], stats) ? 1 : 0;
  if(stats) stats->unreachable_nanoseconds = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - unreachable_started).count());
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
      (prepare_for_inlining(&program.functions[i], boundaries, stats) ||
       unreachable_changed[i]) ? 1 : 0;
  }
  std::vector<unsigned char> inlined_symbols(program.symbol_names.size(), 0);
  const InlineCallGraph call_graph = analyze_inline_call_graph(program, stats);
  std::chrono::steady_clock::time_point inline_started;
  if(stats) inline_started = std::chrono::steady_clock::now();
  const std::size_t inline_rewrites =
    inline_o1_calls(program, call_graph, prepared_oversized_symbols,
      original_instruction_counts, &inlined_symbols, stats);
  if(stats) {
    stats->inline_changed_callers =
      std::count(inlined_symbols.begin(), inlined_symbols.end(), 1);
    stats->rewrites += inline_rewrites;
    stats->inline_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - inline_started).count());
  }
  if(level >= 2) {
    std::vector<unsigned char> ipa_rewritten_symbols(
      program.symbol_names.size(), 0);
    const std::chrono::steady_clock::time_point ipa_started =
      stats ? std::chrono::steady_clock::now() :
              std::chrono::steady_clock::time_point();
    const std::size_t ipa_rewrites = specialize_interprocedural_arguments(
      program, call_graph, &ipa_rewritten_symbols, stats);
    if(stats) {
      stats->rewrites += ipa_rewrites;
      stats->ipa_nanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - ipa_started).count());
    }
    post_cfg_values_changed.resize(program.functions.size(), 0);
    inlined_symbols.resize(program.symbol_names.size(), 0);
    boundaries = function_boundaries(program);
    for(std::size_t i = 0; i < program.functions.size(); ++i)
      if(ipa_rewritten_symbols[program.functions[i].symbol])
        post_cfg_values_changed[i] =
          prepare_for_inlining(&program.functions[i], boundaries, stats) ||
          post_cfg_values_changed[i];
  }
  MemoryGVNSession memory_gvn(program);
  O3UnrollBudget o3_unroll_budget;
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
    lowir_analysis::FunctionAnalysis analysis(function, stats);
    bool slot_values_changed = false;
    if(inlined_symbols[function.symbol] || level >= 2)
      slot_values_changed = timed_function_pass(
        forward_single_store_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis);
    if(inlined_symbols[function.symbol])
      slot_values_changed = timed_function_pass(
        local_slot_forward, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis) ||
          slot_values_changed;
    if(slot_values_changed) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds, &analysis);
      timed_dce(&function, boundaries, stats);
    }
    if(post_cfg_values_changed[i] || slot_values_changed)
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds, &analysis);
    if(timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis)) {
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds, &analysis);
      timed_dce(&function, boundaries, stats);
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds, &analysis);
    }
    if(level >= 2 && timed_function_pass(promote_small_objects, &function,
        stats, &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis)) {
      timed_dce(&function, boundaries, stats);
      timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis);
    }
    if(level >= 2 && timed_function_pass(promote_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis)) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds, &analysis);
      timed_dce(&function, boundaries, stats);
      if(timed_function_pass(cleanup_cfg, &function, stats,
          &Stats::cfg_runs, &Stats::cfg_nanoseconds, &analysis)) {
        timed_function_pass(simplify_values, &function, stats,
          &Stats::simplify_runs, &Stats::simplify_nanoseconds, &analysis);
        timed_dce(&function, boundaries, stats);
      }
      timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis);
    }
    if(level >= 2 && timed_function_pass(eliminate_dead_slot_stores,
        &function, stats, &Stats::slot_runs, &Stats::slot_nanoseconds,
        &analysis)) {
      timed_dce(&function, boundaries, stats);
      timed_function_pass(remove_dead_slots, &function, stats,
        &Stats::slot_runs, &Stats::slot_nanoseconds, &analysis);
    }
    if(level >= 2 && simplify_counted_loops(&function, &analysis, stats)) {
      timed_dce(&function, boundaries, stats);
      timed_function_pass(cleanup_cfg, &function, stats,
        &Stats::cfg_runs, &Stats::cfg_nanoseconds, &analysis);
    }
    if(level >= 3) {
      const std::chrono::steady_clock::time_point unroll_started =
        stats ? std::chrono::steady_clock::now() :
                std::chrono::steady_clock::time_point();
      const bool unrolled = fully_unroll_small_loop(
        &function, &analysis, &o3_unroll_budget, stats);
      if(stats) stats->o3_unroll_nanoseconds +=
        static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - unroll_started).count());
      if(unrolled) {
        timed_function_pass(simplify_values, &function, stats,
          &Stats::simplify_runs, &Stats::simplify_nanoseconds, &analysis);
        timed_dce(&function, boundaries, stats);
        timed_function_pass(cleanup_cfg, &function, stats,
          &Stats::cfg_runs, &Stats::cfg_nanoseconds, &analysis);
      }
    }
    hoist_loop_invariants(&program, &function, &analysis, level, stats);
    if(level >= 2 &&
       memory_gvn.eliminate_redundant_loads(&function, &analysis, stats)) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds, &analysis);
      timed_dce(&function, boundaries, stats);
    }
    if(level >= 2 &&
       eliminate_partial_redundancies(&function, &analysis, stats)) {
      timed_function_pass(simplify_values, &function, stats,
        &Stats::simplify_runs, &Stats::simplify_nanoseconds, &analysis);
      timed_dce(&function, boundaries, stats);
    }
  }
  if(level >= 1) {
    const std::chrono::steady_clock::time_point late_inline_started =
      stats ? std::chrono::steady_clock::now() :
              std::chrono::steady_clock::time_point();
    const InlineCallGraph late_call_graph =
      analyze_inline_call_graph(program, stats);
    if(stats)
      stats->late_inline_direct_edges = late_call_graph.edges.size();
    std::vector<unsigned char> late_rewritten_symbols(
      program.symbol_names.size(), 0);
    LateInlineCleanupContext cleanup_context = {&boundaries};
    InlineCleanup cleanup;
    cleanup.run = cleanup_late_inline_body;
    cleanup.context = &cleanup_context;
    const std::size_t late_rewrites = inline_optimized_calls(
      program, late_call_graph, &late_rewritten_symbols, stats, &cleanup);
    if(stats) {
      stats->rewrites += late_rewrites;
      stats->late_inline_changed_callers = std::count(
        late_rewritten_symbols.begin(), late_rewritten_symbols.end(), 1);
    }
    if(stats) stats->late_inline_nanoseconds =
      static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - late_inline_started).count());
  }
  lowir_model::FunctionPruningSummary pruning =
    lowir_model::prune_unreachable_weak_functions(program);
  if(level >= 1) {
    const std::chrono::steady_clock::time_point post_prune_inline_started =
      stats ? std::chrono::steady_clock::now() :
              std::chrono::steady_clock::time_point();
    const InlineCallGraph post_prune_call_graph =
      analyze_inline_call_graph(program, 0);
    if(stats)
      stats->post_prune_inline_direct_edges = post_prune_call_graph.edges.size();
    std::vector<unsigned char> post_prune_rewritten_symbols(
      program.symbol_names.size(), 0);
    const std::size_t post_prune_rewrites = inline_post_prune_single_calls(
      program, post_prune_call_graph, &post_prune_rewritten_symbols, stats);
    if(stats) {
      stats->rewrites += post_prune_rewrites;
      stats->post_prune_inline_changed_callers = std::count(
        post_prune_rewritten_symbols.begin(),
        post_prune_rewritten_symbols.end(), 1);
    }
    if(post_prune_rewrites) {
      boundaries = function_boundaries(program);
      for(std::size_t i = 0; i < program.functions.size(); ++i)
        if(post_prune_rewritten_symbols[program.functions[i].symbol])
          prepare_for_inlining(&program.functions[i], boundaries, stats);
      const std::size_t prior_pruned = pruning.pruned_functions;
      const std::size_t prior_unreachable_weak =
        pruning.unreachable_weak_functions;
      const std::size_t prior_unreachable_internal =
        pruning.unreachable_internal_functions;
      pruning = lowir_model::prune_unreachable_weak_functions(program);
      pruning.pruned_functions += prior_pruned;
      pruning.unreachable_weak_functions += prior_unreachable_weak;
      pruning.unreachable_internal_functions += prior_unreachable_internal;
    }
    if(stats) stats->post_prune_inline_nanoseconds =
      static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - post_prune_inline_started).count());
  }
  if(stats) {
    collect_retained_inline_census(program, stats);
    stats->inline_reachable_functions = pruning.reachable_functions;
    stats->inline_pruned_functions = pruning.pruned_functions;
    stats->inline_unreachable_weak_functions =
      pruning.unreachable_weak_functions;
    stats->inline_unreachable_internal_functions =
      pruning.unreachable_internal_functions;
    stats->inline_retained_external_strong =
      pruning.retained_external_strong;
    stats->inline_retained_address_or_relocation =
      pruning.retained_address_or_relocation;
    stats->inline_retained_direct_call = pruning.retained_direct_call;
    stats->inline_retained_lifecycle = pruning.retained_lifecycle;
    stats->inline_retained_object_output_root =
      pruning.retained_object_output_root;
    stats->inline_retained_object_output_root_weak =
      pruning.retained_object_output_root_weak;
    stats->inline_retained_object_output_root_internal =
      pruning.retained_object_output_root_internal;
    stats->output_instructions = instruction_count(program);
    stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }
}

}  // namespace lowir_opt
