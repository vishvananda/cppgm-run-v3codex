#include "native/lowering/selection.h"
#include "native/errors.h"


namespace lowir_native {
namespace selection {

using lowir_model::LowOperation;

long long integer_value(const lowir_model::Operand & operand)
{
  if(operand.kind != lowir_model::Operand::OP_INTEGER)
    native_errors::ThrowLowirInput("integer value requires an integer operand");
  if(!operand.has_int_value)
    native_errors::ThrowLowirInput("integer operand has no decoded value");
  return operand.int_value;
}

long long canonical_integer_constant(long long value,
                                     const lowir_model::LowType & type)
{
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  if(type.kind == lowir_model::LTK_PTR || width >= 64)
    return value;
  const unsigned bits = static_cast<unsigned>(width);
  const unsigned long long mask = (1ULL << bits) - 1;
  const unsigned long long truncated =
    static_cast<unsigned long long>(value) & mask;
  if(!is_signed_integer(type) || !(truncated & (1ULL << (bits - 1))))
    return static_cast<long long>(truncated);
  return -1 - static_cast<long long>((~truncated) & mask);
}

long long atomic_order(const lowir_model::Operand & operand)
{
  if(operand.kind != lowir_model::Operand::OP_INTEGER)
    native_errors::ThrowLowirInput(
      "atomic memory order must be an integer literal");
  return integer_value(operand);
}

bool is_signed_integer(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_I8 || type.kind == lowir_model::LTK_I16 ||
         type.kind == lowir_model::LTK_I32 || type.kind == lowir_model::LTK_I64;
}

bool is_integer_or_pointer(const lowir_model::LowType & type)
{
  return (type.kind >= lowir_model::LTK_I1 && type.kind <= lowir_model::LTK_I64) ||
         type.kind == lowir_model::LTK_PTR;
}

bool is_narrow_integer(const lowir_model::LowType & type)
{
  return is_integer_or_pointer(type) && type.kind != lowir_model::LTK_PTR &&
    lowir_model::lowir_type_bit_width(type) < 64;
}

bool is_scalar_float(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_F32 || type.kind == lowir_model::LTK_F64;
}

bool is_extended_float(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_F80;
}

bool is_floating(const lowir_model::LowType & type)
{
  return is_scalar_float(type) || is_extended_float(type);
}

X86Condition predicate_condition(lowir_model::LowOperation predicate)
{
  if(predicate.kind == LowOperation::LOP_EQ) return XC_E;
  if(predicate.kind == LowOperation::LOP_NE) return XC_NE;
  if(predicate.kind == LowOperation::LOP_LT) return XC_L;
  if(predicate.kind == LowOperation::LOP_LE) return XC_LE;
  if(predicate.kind == LowOperation::LOP_GT) return XC_G;
  if(predicate.kind == LowOperation::LOP_GE) return XC_GE;
  if(predicate.kind == LowOperation::LOP_ULT) return XC_B;
  if(predicate.kind == LowOperation::LOP_ULE) return XC_BE;
  if(predicate.kind == LowOperation::LOP_UGT) return XC_A;
  if(predicate.kind == LowOperation::LOP_UGE) return XC_AE;
  native_errors::ThrowSource(std::string("unsupported integer comparison predicate: ") +
                           lowir_model::lowir_operation_text(predicate));
}

mir_model::MirInstruction::Opcode float_binary_opcode(LowOperation operation)
{
  using mir_model::MirInstruction;
  if(operation.kind == LowOperation::LOP_ADD) return MirInstruction::MI_FADD;
  if(operation.kind == LowOperation::LOP_SUB) return MirInstruction::MI_FSUB;
  if(operation.kind == LowOperation::LOP_MUL) return MirInstruction::MI_FMUL;
  if(operation.kind == LowOperation::LOP_DIV) return MirInstruction::MI_FDIV;
  native_errors::ThrowSource(std::string("floating binary operation is not implemented: ") +
                           lowir_model::lowir_operation_text(operation));
}

mir_model::MirInstruction::Opcode float_compare_opcode(LowOperation predicate)
{
  using mir_model::MirInstruction;
  if(predicate.kind == LowOperation::LOP_EQ) return MirInstruction::MI_FEQ;
  if(predicate.kind == LowOperation::LOP_NE) return MirInstruction::MI_FNE;
  if(predicate.kind == LowOperation::LOP_LT) return MirInstruction::MI_FLT;
  if(predicate.kind == LowOperation::LOP_GT) return MirInstruction::MI_FGT;
  if(predicate.kind == LowOperation::LOP_LE) return MirInstruction::MI_FLE;
  if(predicate.kind == LowOperation::LOP_GE) return MirInstruction::MI_FGE;
  native_errors::ThrowSource(std::string("floating comparison predicate is not implemented: ") +
                           lowir_model::lowir_operation_text(predicate));
}

X86Condition float_predicate_condition(LowOperation predicate)
{
  // FCMP models the right operand compared with the left operand, so the
  // unsigned x86 conditions directly describe ordered floating predicates.
  if(predicate.kind == LowOperation::LOP_EQ) return XC_E;
  if(predicate.kind == LowOperation::LOP_NE) return XC_NE;
  if(predicate.kind == LowOperation::LOP_LT) return XC_A;
  if(predicate.kind == LowOperation::LOP_GT) return XC_B;
  if(predicate.kind == LowOperation::LOP_LE) return XC_AE;
  if(predicate.kind == LowOperation::LOP_GE) return XC_BE;
  native_errors::ThrowSource(std::string("floating branch predicate is not implemented: ") +
                           lowir_model::lowir_operation_text(predicate));
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

bool result_is_immediate_return(const lowir_model::LowirBlock & block,
                                std::size_t instruction_index,
                                lowir_model::ValueId destination,
                                const analysis::FunctionFacts & facts)
{
  if(facts.uses[destination] != 1 ||
     instruction_index + 1 >= block.instructions.size()) return false;
  const lowir_model::Instruction & next = block.instructions[instruction_index + 1];
  return next.kind == lowir_model::Instruction::IK_RETURN &&
    next.first.kind == lowir_model::Operand::OP_TEMP &&
    next.first.value == destination;
}

bool result_is_immediate_unary_not_branch(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts)
{
  if(instruction_index + 2 >= block.instructions.size() ||
     facts.uses[destination] != 1) return false;
  const lowir_model::Instruction & unary = block.instructions[instruction_index + 1];
  const lowir_model::Instruction & branch = block.instructions[instruction_index + 2];
  return unary.kind == lowir_model::Instruction::IK_UNARY && unary.op.kind == LowOperation::LOP_NOT &&
    unary.first.kind == lowir_model::Operand::OP_TEMP &&
    unary.first.value == destination &&
    branch.kind == lowir_model::Instruction::IK_BRANCH &&
    branch.first.kind == lowir_model::Operand::OP_TEMP &&
    branch.first.value == unary.dest;
}

bool result_is_immediate_branch(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts)
{
  if(instruction_index + 1 >= block.instructions.size() ||
     facts.uses[destination] != 1) return false;
  const lowir_model::Instruction & branch =
    block.instructions[instruction_index + 1];
  return branch.kind == lowir_model::Instruction::IK_BRANCH &&
    branch.first.kind == lowir_model::Operand::OP_TEMP &&
    branch.first.value == destination;
}

bool result_is_immediately_stored(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts)
{
  if(instruction_index + 1 >= block.instructions.size() ||
     facts.uses[destination] != 1) return false;
  const lowir_model::Instruction & store =
    block.instructions[instruction_index + 1];
  return store.kind == lowir_model::Instruction::IK_STORE &&
    store.first.kind == lowir_model::Operand::OP_TEMP &&
    store.first.value == destination;
}

bool result_is_immediate_store_address_with_later_use(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts)
{
  if(instruction_index + 1 >= block.instructions.size()) return false;
  if(facts.uses[destination] <= 1) return false;
  const lowir_model::Instruction & store =
    block.instructions[instruction_index + 1];
  return store.kind == lowir_model::Instruction::IK_STORE &&
    store.second.kind == lowir_model::Operand::OP_TEMP &&
    store.second.value == destination;
}

bool call_result_needs_normalization(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    const lowir_model::Instruction & call,
    const analysis::FunctionFacts & facts)
{
  using lowir_model::Instruction;
  using lowir_model::LowOperation;
  using lowir_model::Operand;
  if(result_is_immediate_return(block, instruction_index, call.dest, facts))
    return false;
  if(facts.uses[call.dest] == 1 &&
     instruction_index + 1 < block.instructions.size()) {
    const Instruction & consumer = block.instructions[instruction_index + 1];
    if(consumer.kind == Instruction::IK_CONVERT &&
       consumer.first.kind == Operand::OP_TEMP &&
       consumer.first.value == call.dest &&
       is_integer_or_pointer(consumer.source_type) &&
       is_integer_or_pointer(consumer.type) &&
       (consumer.op.kind == LowOperation::LOP_SEXT ||
        consumer.op.kind == LowOperation::LOP_ZEXT ||
        consumer.op.kind == LowOperation::LOP_TRUNC))
      return false;
  }
  if(!result_is_immediately_stored(
       block, instruction_index, call.dest, facts)) return true;
  return block.instructions[instruction_index + 1].type != call.type;
}

bool result_is_next_direct_call_argument(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    const lowir_model::Instruction & producer,
    const analysis::FunctionFacts & facts,
    const abi::FunctionSignatureIndex & signatures)
{
  using lowir_model::Instruction;
  using lowir_model::Operand;
  if(facts.uses[producer.dest] != 1 ||
     instruction_index + 1 >= block.instructions.size()) return false;
  const Instruction & call = block.instructions[instruction_index + 1];
  if(call.kind != Instruction::IK_CALL) return false;
  const std::vector<lowir_model::LowirParameter> * parameters = 0;
  if(call.has_call_signature) parameters = &call.call_params;
  else if(call.first.kind == Operand::OP_GLOBAL)
    parameters = signatures[call.first.symbol].params;
  for(std::size_t i = 0; i < call.args.size(); ++i) {
    if(call.args[i].kind != Operand::OP_TEMP ||
       call.args[i].value != producer.dest) continue;
    return !parameters || i >= parameters->size() ||
      (*parameters)[i].metadata.passing == lowir_model::PPM_DIRECT ||
      producer.type.kind == lowir_model::LTK_PTR;
  }
  return false;
}

}  // namespace selection
}  // namespace lowir_native
