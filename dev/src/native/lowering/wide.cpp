#include "native/lowering/wide.h"

#include "native/errors.h"
#include "native/mir/construction.h"

#include <cstdint>

namespace lowir_native {
namespace wide {
namespace {

using namespace build;
using lowir_model::LowOperation;
using mir_model::MirInstruction;
using mir_model::MirOperand;

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
  } else native_errors::ThrowInternal("i128 value is not addressable (MIR operand kind " +
    std::to_string(static_cast<unsigned>(storage.kind)) + ")");
}

unsigned long long word(const Words & words, std::size_t chunk)
{
  if(chunk > 1) native_errors::ThrowInternal("invalid i128 chunk");
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
  append_load(out, reg_operand(low), dereference(scratch), machine_type(lowir_model::LTK_I64));
  append_load(out, reg_operand(high), dereference(scratch, 8), machine_type(lowir_model::LTK_I64));
}

void append_pair_store(const MirOperand & destination,
                       X64Register low, X64Register high,
                       X64Register scratch,
                       std::vector<MirInstruction> & out)
{
  append_address(out, scratch, destination);
  append_store(out, dereference(scratch), reg_operand(low), machine_type(lowir_model::LTK_I64));
  append_store(out, dereference(scratch, 8), reg_operand(high), machine_type(lowir_model::LTK_I64));
}

void append_equality_part(X64Register left, X64Register right,
                          X64Register destination,
                          std::vector<MirInstruction> & out)
{
  MirInstruction compare = machine_instruction(MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
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

void append_condition(X64Register destination, X86Condition condition,
                      std::vector<MirInstruction> & out)
{
  MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
  set.condition = condition;
  append_operand(set, reg_operand(destination));
  out.push_back(set);
  MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
  append_operand(extend, reg_operand(destination));
  append_operand(extend, reg_operand(destination));
  out.push_back(extend);
}

void append_register_binary(MirInstruction::Opcode opcode,
                            X64Register destination, X64Register source,
                            std::vector<MirInstruction> & out)
{
  MirInstruction instruction = machine_instruction(opcode, machine_type(lowir_model::LTK_I64));
  append_operand(instruction, reg_operand(destination));
  append_operand(instruction, reg_operand(source));
  out.push_back(instruction);
}

void append_register_immediate(MirInstruction::Opcode opcode,
                               X64Register destination, long long value,
                               std::vector<MirInstruction> & out)
{
  MirInstruction instruction = machine_instruction(opcode, machine_type(lowir_model::LTK_I64));
  append_operand(instruction, reg_operand(destination));
  append_operand(instruction, immediate(value));
  out.push_back(instruction);
}

}  // namespace

bool is_integer(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_I128;
}

Value literal_value(long long low, std::uint64_t high)
{
  Value result;
  result.immediate = true;
  result.words.low = static_cast<std::uint64_t>(low);
  result.words.high = high;
  return result;
}

Value literal_value(const lowir_model::Operand & operand)
{
  if(operand.kind != lowir_model::Operand::OP_INTEGER ||
     !operand.has_int_value)
    native_errors::ThrowLowirInput(
      "wide literal requires a decoded integer operand");
  return literal_value(operand.int_value, operand.int_high);
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
  append_load(out, reg_operand(destination), dereference(scratch, chunk * 8), machine_type(lowir_model::LTK_I64));
}

void append_word_store(const MirOperand & destination,
                       const Value & value, std::size_t chunk,
                       X64Register value_register, X64Register scratch,
                       std::vector<MirInstruction> & out)
{
  append_word_to_register(value, chunk, value_register, scratch, out);
  append_store(out, destination, reg_operand(value_register), machine_type(lowir_model::LTK_I64));
}

void append_copy(const MirOperand & destination, const Value & source,
                 std::vector<MirInstruction> & out)
{
  append_pair_to_registers(source, XR_RAX, XR_RDX, XR_R11, out);
  append_pair_store(destination, XR_RAX, XR_RDX, XR_R11, out);
}

void append_compare(const Value & left, const Value & right,
                    lowir_model::LowOperation operation,
                    std::vector<MirInstruction> & out)
{
  append_pair_to_registers(left, XR_RAX, XR_RDX, XR_R11, out);
  append_pair_to_registers(right, XR_RCX, XR_RSI, XR_R11, out);
  if(operation.kind == LowOperation::LOP_EQ || operation.kind == LowOperation::LOP_NE) {
    append_equality_part(XR_RDX, XR_RSI, XR_R10, out);
    append_equality_part(XR_RAX, XR_RCX, XR_R11, out);
    append_register_binary(MirInstruction::MI_AND, XR_R10, XR_R11, out);
    if(operation.kind == LowOperation::LOP_NE)
      append_register_immediate(MirInstruction::MI_XOR, XR_R10, 1, out);
    return;
  }
  const bool less = operation.kind == LowOperation::LOP_LT || operation.kind == LowOperation::LOP_LE ||
                    operation.kind == LowOperation::LOP_ULT || operation.kind == LowOperation::LOP_ULE;
  const bool inclusive = operation.kind == LowOperation::LOP_LE || operation.kind == LowOperation::LOP_GE ||
                         operation.kind == LowOperation::LOP_ULE || operation.kind == LowOperation::LOP_UGE;
  const bool signed_high = operation.kind == LowOperation::LOP_LT || operation.kind == LowOperation::LOP_LE ||
                           operation.kind == LowOperation::LOP_GT || operation.kind == LowOperation::LOP_GE;
  if(!less && operation.kind != LowOperation::LOP_GT && operation.kind != LowOperation::LOP_GE &&
     operation.kind != LowOperation::LOP_UGT && operation.kind != LowOperation::LOP_UGE)
    native_errors::ThrowSource(std::string("unsupported i128 comparison: ") +
                             lowir_model::lowir_operation_text(operation));

  MirInstruction low_compare = machine_instruction(MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
  append_operand(low_compare, reg_operand(XR_RAX));
  append_operand(low_compare, reg_operand(XR_RCX));
  out.push_back(low_compare);
  append_condition(XR_R10, less ? (inclusive ? XC_BE : XC_B) :
    (inclusive ? XC_AE : XC_A), out);

  MirInstruction high_compare = machine_instruction(MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
  append_operand(high_compare, reg_operand(XR_RDX));
  append_operand(high_compare, reg_operand(XR_RSI));
  out.push_back(high_compare);
  append_condition(XR_R11, XC_E, out);
  append_register_binary(MirInstruction::MI_AND, XR_R10, XR_R11, out);

  out.push_back(high_compare);
  X86Condition high_condition;
  if(less) high_condition = signed_high ? XC_L : XC_B;
  else high_condition = signed_high ? XC_G : XC_A;
  append_condition(XR_R11, high_condition, out);
  append_register_binary(MirInstruction::MI_OR, XR_R10, XR_R11, out);
}

void append_compare_branch(const Value & left, const Value & right,
                           lowir_model::LowOperation operation,
                           const MirOperand & true_target,
                           const MirOperand & false_target,
                           std::vector<MirInstruction> & out)
{
  append_pair_to_registers(left, XR_RAX, XR_RDX, XR_R11, out);
  append_pair_to_registers(right, XR_RCX, XR_RSI, XR_R11, out);

  MirInstruction high_compare = machine_instruction(
    MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
  append_operand(high_compare, reg_operand(XR_RDX));
  append_operand(high_compare, reg_operand(XR_RSI));
  out.push_back(high_compare);

  if(operation.kind == LowOperation::LOP_EQ ||
     operation.kind == LowOperation::LOP_NE) {
    MirInstruction high_decision = machine_instruction(MirInstruction::MI_JCC);
    high_decision.condition = XC_NE;
    append_operand(high_decision,
      operation.kind == LowOperation::LOP_EQ ? false_target : true_target);
    out.push_back(high_decision);

    MirInstruction low_compare = machine_instruction(
      MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
    append_operand(low_compare, reg_operand(XR_RAX));
    append_operand(low_compare, reg_operand(XR_RCX));
    out.push_back(low_compare);
    MirInstruction low_decision = machine_instruction(MirInstruction::MI_JCC);
    low_decision.condition = operation.kind == LowOperation::LOP_EQ ? XC_E : XC_NE;
    append_operand(low_decision, true_target);
    out.push_back(low_decision);
  } else {
    const bool less = operation.kind == LowOperation::LOP_LT ||
      operation.kind == LowOperation::LOP_LE ||
      operation.kind == LowOperation::LOP_ULT ||
      operation.kind == LowOperation::LOP_ULE;
    const bool greater = operation.kind == LowOperation::LOP_GT ||
      operation.kind == LowOperation::LOP_GE ||
      operation.kind == LowOperation::LOP_UGT ||
      operation.kind == LowOperation::LOP_UGE;
    if(!less && !greater)
      native_errors::ThrowSource(std::string("unsupported i128 comparison: ") +
        lowir_model::lowir_operation_text(operation));
    const bool signed_high = operation.kind == LowOperation::LOP_LT ||
      operation.kind == LowOperation::LOP_LE ||
      operation.kind == LowOperation::LOP_GT ||
      operation.kind == LowOperation::LOP_GE;
    const bool inclusive = operation.kind == LowOperation::LOP_LE ||
      operation.kind == LowOperation::LOP_GE ||
      operation.kind == LowOperation::LOP_ULE ||
      operation.kind == LowOperation::LOP_UGE;

    MirInstruction high_true = machine_instruction(MirInstruction::MI_JCC);
    high_true.condition = less ? (signed_high ? XC_L : XC_B) :
      (signed_high ? XC_G : XC_A);
    append_operand(high_true, true_target);
    out.push_back(high_true);
    MirInstruction high_false = machine_instruction(MirInstruction::MI_JCC);
    high_false.condition = less ? (signed_high ? XC_G : XC_A) :
      (signed_high ? XC_L : XC_B);
    append_operand(high_false, false_target);
    out.push_back(high_false);

    MirInstruction low_compare = machine_instruction(
      MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
    append_operand(low_compare, reg_operand(XR_RAX));
    append_operand(low_compare, reg_operand(XR_RCX));
    out.push_back(low_compare);
    MirInstruction low_decision = machine_instruction(MirInstruction::MI_JCC);
    low_decision.condition = less ? (inclusive ? XC_BE : XC_B) :
      (inclusive ? XC_AE : XC_A);
    append_operand(low_decision, true_target);
    out.push_back(low_decision);
  }

  MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
  append_operand(jump_false, false_target);
  out.push_back(jump_false);
}

void append_binary(const MirOperand & destination,
                   const Value & left, const Value & right,
                   lowir_model::LowOperation operation,
                   std::vector<MirInstruction> & out)
{
  append_pair_to_registers(left, XR_RAX, XR_RDX, XR_R11, out);
  append_pair_to_registers(right, XR_RCX, XR_RSI, XR_R11, out);
  if(operation.kind == LowOperation::LOP_ADD || operation.kind == LowOperation::LOP_SUB) {
    append_register_binary(operation.kind == LowOperation::LOP_ADD ? MirInstruction::MI_ADD :
      MirInstruction::MI_SUB, XR_RAX, XR_RCX, out);
    append_condition(XR_R10, XC_B, out);
    append_register_binary(operation.kind == LowOperation::LOP_ADD ? MirInstruction::MI_ADD :
      MirInstruction::MI_SUB, XR_RDX, XR_RSI, out);
    append_register_binary(operation.kind == LowOperation::LOP_ADD ? MirInstruction::MI_ADD :
      MirInstruction::MI_SUB, XR_RDX, XR_R10, out);
  } else if(operation.kind == LowOperation::LOP_MUL) {
    append_move(out, reg_operand(XR_R10), reg_operand(XR_RAX));
    append_move(out, reg_operand(XR_RDI), reg_operand(XR_RDX));
    MirInstruction multiply = machine_instruction(MirInstruction::MI_MUL, machine_type(lowir_model::LTK_I64));
    append_operand(multiply, reg_operand(XR_RCX));
    out.push_back(multiply);
    append_register_binary(MirInstruction::MI_IMUL, XR_R10, XR_RSI, out);
    append_register_binary(MirInstruction::MI_ADD, XR_RDX, XR_R10, out);
    append_register_binary(MirInstruction::MI_IMUL, XR_RDI, XR_RCX, out);
    append_register_binary(MirInstruction::MI_ADD, XR_RDX, XR_RDI, out);
  } else if(operation.kind == LowOperation::LOP_AND || operation.kind == LowOperation::LOP_OR || operation.kind == LowOperation::LOP_XOR) {
    MirInstruction::Opcode opcode = operation.kind == LowOperation::LOP_AND ? MirInstruction::MI_AND :
      operation.kind == LowOperation::LOP_OR ? MirInstruction::MI_OR : MirInstruction::MI_XOR;
    append_register_binary(opcode, XR_RAX, XR_RCX, out);
    append_register_binary(opcode, XR_RDX, XR_RSI, out);
  } else if(operation.kind == LowOperation::LOP_SHL || operation.kind == LowOperation::LOP_SHR || operation.kind == LowOperation::LOP_USHR) {
    append_word_to_register(right, 0, XR_RCX, XR_R11, out);
    MirInstruction::Opcode opcode = operation.kind == LowOperation::LOP_SHL ?
      MirInstruction::MI_I128_SHL : operation.kind == LowOperation::LOP_SHR ?
      MirInstruction::MI_I128_SAR : MirInstruction::MI_I128_SHR;
    out.push_back(machine_instruction(opcode));
  } else if(operation.kind == LowOperation::LOP_DIV || operation.kind == LowOperation::LOP_MOD ||
            operation.kind == LowOperation::LOP_UDIV || operation.kind == LowOperation::LOP_UMOD) {
    MirInstruction::Opcode opcode = operation.kind == LowOperation::LOP_DIV ?
      MirInstruction::MI_I128_SDIV : operation.kind == LowOperation::LOP_MOD ?
      MirInstruction::MI_I128_SMOD : operation.kind == LowOperation::LOP_UDIV ?
      MirInstruction::MI_I128_UDIV : MirInstruction::MI_I128_UMOD;
    out.push_back(machine_instruction(opcode));
  } else {
    native_errors::ThrowSource(std::string("unsupported i128 binary operation: ") +
                             lowir_model::lowir_operation_text(operation));
  }
  append_pair_store(destination, XR_RAX, XR_RDX, XR_R11, out);
}

void append_unary(const MirOperand & destination,
                  const Value & source, lowir_model::LowOperation operation,
                  std::vector<MirInstruction> & out)
{
  append_pair_to_registers(source, XR_RAX, XR_RDX, XR_R11, out);
  if(operation.kind == LowOperation::LOP_BITNOT || operation.kind == LowOperation::LOP_NEG) {
    MirInstruction low = machine_instruction(MirInstruction::MI_NOT, machine_type(lowir_model::LTK_I64));
    append_operand(low, reg_operand(XR_RAX));
    out.push_back(low);
    MirInstruction high = machine_instruction(MirInstruction::MI_NOT, machine_type(lowir_model::LTK_I64));
    append_operand(high, reg_operand(XR_RDX));
    out.push_back(high);
    if(operation.kind == LowOperation::LOP_NEG) {
      append_register_immediate(MirInstruction::MI_ADD, XR_RAX, 1, out);
      append_condition(XR_R10, XC_B, out);
      append_register_binary(MirInstruction::MI_ADD, XR_RDX, XR_R10, out);
    }
  } else native_errors::ThrowSource(std::string("unsupported i128 unary operation: ") +
                                  lowir_model::lowir_operation_text(operation));
  append_pair_store(destination, XR_RAX, XR_RDX, XR_R11, out);
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
  append_load(out, reg_operand(XR_RAX), expected, machine_type(lowir_model::LTK_I64));
  MirOperand expected_high = expected;
  expected_high.offset += 8;
  append_load(out, reg_operand(XR_RDX), expected_high, machine_type(lowir_model::LTK_I64));
  append_pair_to_registers(desired, XR_RBX, XR_RCX, XR_R11, out);
  append_address(out, XR_R11, object);
  MirInstruction exchange = machine_instruction(MirInstruction::MI_LOCK_CMPXCHG16B);
  append_operand(exchange, dereference(XR_R11));
  out.push_back(exchange);
  append_store(out, expected, reg_operand(XR_RAX), machine_type(lowir_model::LTK_I64));
  append_store(out, expected_high, reg_operand(XR_RDX), machine_type(lowir_model::LTK_I64));
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
