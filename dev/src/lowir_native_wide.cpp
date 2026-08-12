#include "lowir_native_wide.h"

#include "lowir_native_mir.h"

#include <stdexcept>

namespace lowir_native {
namespace wide {
namespace {

using namespace build;
using mir_model::MirInstruction;
using mir_model::MirOperand;

Words add(Words left, Words right)
{
  Words result;
  result.low = left.low + right.low;
  result.high = left.high + right.high + (result.low < left.low ? 1 : 0);
  return result;
}

Words twice(Words value)
{
  Words result;
  result.high = (value.high << 1) | (value.low >> 63);
  result.low = value.low << 1;
  return result;
}

Words multiply_add(Words value, unsigned multiplier, unsigned digit)
{
  Words result;
  Words addend = value;
  while(multiplier) {
    if(multiplier & 1) result = add(result, addend);
    addend = twice(addend);
    multiplier >>= 1;
  }
  Words tail;
  tail.low = digit;
  return add(result, tail);
}

unsigned digit_value(char c)
{
  if(c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
  if(c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
  if(c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
  return 255;
}

Words parse_words(const std::string & text)
{
  if(text.empty()) throw std::runtime_error("empty i128 literal");
  std::size_t at = 0;
  bool negative = false;
  if(text[at] == '+' || text[at] == '-') {
    negative = text[at] == '-';
    if(++at == text.size()) throw std::runtime_error("invalid i128 literal: " + text);
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
  if(at == text.size()) return Words();
  Words result;
  for(; at < text.size(); ++at) {
    const unsigned digit = digit_value(text[at]);
    if(digit >= base) throw std::runtime_error("invalid i128 literal: " + text);
    result = multiply_add(result, base, digit);
  }
  if(negative) {
    result.low = ~result.low + 1;
    result.high = ~result.high + (result.low == 0 ? 1 : 0);
  }
  return result;
}

void append_address(std::vector<MirInstruction> & out, X64Register destination,
                    const MirOperand & storage)
{
  if(storage.kind == MirOperand::OP_FRAME) {
    MirInstruction lea = machine_instruction(MirInstruction::MI_LEA);
    append_operand(lea, reg_operand(destination));
    append_operand(lea, storage);
    out.push_back(lea);
  } else if(storage.kind == MirOperand::OP_GLOBAL ||
            storage.kind == MirOperand::OP_SYMBOL) {
    append_move(out, reg_operand(destination), storage);
  } else if(storage.kind == MirOperand::OP_DEREF) {
    if(storage.offset == 0)
      append_move(out, reg_operand(destination), reg_operand(storage.reg));
    else {
      MirInstruction lea = machine_instruction(MirInstruction::MI_LEA);
      append_operand(lea, reg_operand(destination));
      append_operand(lea, storage);
      out.push_back(lea);
    }
  } else throw std::runtime_error("i128 value is not addressable");
}

unsigned long long word(const Words & words, std::size_t chunk)
{
  if(chunk > 1) throw std::logic_error("invalid i128 chunk");
  return chunk ? words.high : words.low;
}

void append_pair_to_registers(const Value & value,
                              X64Register low, X64Register high,
                              X64Register scratch,
                              std::vector<MirInstruction> & out)
{
  if(value.immediate) {
    append_move(out, reg_operand(low),
                immediate(static_cast<long long>(value.words.low)));
    append_move(out, reg_operand(high),
                immediate(static_cast<long long>(value.words.high)));
    return;
  }
  append_address(out, scratch, value.storage);
  append_load(out, reg_operand(low), dereference(scratch), "i64");
  append_load(out, reg_operand(high), dereference(scratch, 8), "i64");
}

void append_pair_store(const MirOperand & destination,
                       X64Register low, X64Register high,
                       X64Register scratch,
                       std::vector<MirInstruction> & out)
{
  append_address(out, scratch, destination);
  append_store(out, dereference(scratch), reg_operand(low), "i64");
  append_store(out, dereference(scratch, 8), reg_operand(high), "i64");
}

void append_equality_part(X64Register left, X64Register right,
                          X64Register destination,
                          std::vector<MirInstruction> & out)
{
  MirInstruction compare = machine_instruction(MirInstruction::MI_CMP, "i64");
  append_operand(compare, reg_operand(left));
  append_operand(compare, reg_operand(right));
  out.push_back(compare);
  MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
  set.condition = XC_E;
  append_operand(set, reg_operand(destination));
  out.push_back(set);
  MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
  append_operand(extend, reg_operand(destination));
  append_operand(extend, reg_operand(destination));
  out.push_back(extend);
}

}  // namespace

bool is_integer(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_I128;
}

Value literal_value(const std::string & text)
{
  Value result;
  result.immediate = true;
  result.words = parse_words(text);
  return result;
}

Value storage_value(const MirOperand & storage)
{
  Value result;
  result.storage = storage;
  return result;
}

void append_word_to_register(const Value & value, std::size_t chunk,
                             X64Register destination, X64Register scratch,
                             std::vector<MirInstruction> & out)
{
  if(value.immediate) {
    append_move(out, reg_operand(destination),
                immediate(static_cast<long long>(word(value.words, chunk))));
    return;
  }
  append_address(out, scratch, value.storage);
  append_load(out, reg_operand(destination), dereference(scratch, chunk * 8), "i64");
}

void append_word_store(const MirOperand & destination,
                       const Value & value, std::size_t chunk,
                       X64Register value_register, X64Register scratch,
                       std::vector<MirInstruction> & out)
{
  append_word_to_register(value, chunk, value_register, scratch, out);
  append_store(out, destination, reg_operand(value_register), "i64");
}

void append_copy(const MirOperand & destination, const Value & source,
                 std::vector<MirInstruction> & out)
{
  append_pair_to_registers(source, XR_RAX, XR_RDX, XR_R11, out);
  append_pair_store(destination, XR_RAX, XR_RDX, XR_R11, out);
}

void append_compare(const Value & left, const Value & right, bool equal,
                    std::vector<MirInstruction> & out)
{
  append_pair_to_registers(left, XR_RAX, XR_RDX, XR_R11, out);
  append_pair_to_registers(right, XR_RCX, XR_RSI, XR_R11, out);
  append_equality_part(XR_RDX, XR_RSI, XR_R10, out);
  append_equality_part(XR_RAX, XR_RCX, XR_R11, out);
  MirInstruction combine = machine_instruction(MirInstruction::MI_AND);
  append_operand(combine, reg_operand(XR_R10));
  append_operand(combine, reg_operand(XR_R11));
  out.push_back(combine);
  if(!equal) {
    MirInstruction invert = machine_instruction(MirInstruction::MI_XOR);
    append_operand(invert, reg_operand(XR_R10));
    append_operand(invert, immediate(1));
    out.push_back(invert);
  }
}

void append_atomic_load(const MirOperand & object,
                        const MirOperand & destination,
                        std::vector<MirInstruction> & out)
{
  append_move(out, reg_operand(XR_RAX), immediate(0));
  append_move(out, reg_operand(XR_RDX), immediate(0));
  append_move(out, reg_operand(XR_RBX), immediate(0));
  append_move(out, reg_operand(XR_RCX), immediate(0));
  append_address(out, XR_R11, object);
  MirInstruction exchange = machine_instruction(MirInstruction::MI_LOCK_CMPXCHG16B);
  append_operand(exchange, dereference(XR_R11));
  out.push_back(exchange);
  append_pair_store(destination, XR_RAX, XR_RDX, XR_R11, out);
}

void append_atomic_compare_exchange(const MirOperand & object,
                                    const MirOperand & expected,
                                    const Value & desired,
                                    std::vector<MirInstruction> & out)
{
  append_load(out, reg_operand(XR_RAX), expected, "i64");
  MirOperand expected_high = expected;
  expected_high.offset += 8;
  append_load(out, reg_operand(XR_RDX), expected_high, "i64");
  append_pair_to_registers(desired, XR_RBX, XR_RCX, XR_R11, out);
  append_address(out, XR_R11, object);
  MirInstruction exchange = machine_instruction(MirInstruction::MI_LOCK_CMPXCHG16B);
  append_operand(exchange, dereference(XR_R11));
  out.push_back(exchange);
  append_store(out, expected, reg_operand(XR_RAX), "i64");
  append_store(out, expected_high, reg_operand(XR_RDX), "i64");
  MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
  set.condition = XC_E;
  append_operand(set, reg_operand(XR_RAX));
  out.push_back(set);
  MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
  append_operand(extend, reg_operand(XR_RAX));
  append_operand(extend, reg_operand(XR_RAX));
  out.push_back(extend);
}

}  // namespace wide
}  // namespace lowir_native
