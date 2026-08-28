#pragma once

#include <cstddef>
#include <string>

#include "native/lowering/abi.h"
#include "native/analysis/function.h"
#include "lowir/model/program.h"
#include "native/mir/model.h"
#include "native/mir/registers.h"

namespace lowir_native {
namespace selection {

long long integer_value(const lowir_model::Operand & operand);
long long canonical_integer_constant(long long value,
                                     const lowir_model::LowType & type);
long long atomic_order(const lowir_model::Operand & operand);
bool is_signed_integer(const lowir_model::LowType & type);
bool is_integer_or_pointer(const lowir_model::LowType & type);
bool is_narrow_integer(const lowir_model::LowType & type);
bool is_scalar_float(const lowir_model::LowType & type);
bool is_extended_float(const lowir_model::LowType & type);
bool is_floating(const lowir_model::LowType & type);
X86Condition predicate_condition(lowir_model::LowOperation predicate);
mir_model::MirInstruction::Opcode float_binary_opcode(
    lowir_model::LowOperation operation);
mir_model::MirInstruction::Opcode float_compare_opcode(
    lowir_model::LowOperation predicate);
X86Condition float_predicate_condition(lowir_model::LowOperation predicate);
std::size_t align_up(std::size_t value, std::size_t alignment);
bool result_is_immediate_return(const lowir_model::LowirBlock & block,
                                std::size_t instruction_index,
                                lowir_model::ValueId destination,
                                const analysis::FunctionFacts & facts);
bool result_is_immediate_unary_not_branch(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);
bool result_is_immediate_branch(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);
bool result_is_immediately_stored(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);
bool result_is_immediate_store_address_with_later_use(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);
bool call_result_needs_normalization(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    const lowir_model::Instruction & call,
    const analysis::FunctionFacts & facts);
bool result_is_next_direct_call_argument(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    const lowir_model::Instruction & producer,
    const analysis::FunctionFacts & facts,
    const abi::FunctionSignatureIndex & signatures);
inline bool address_only_feeds_dead_index(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts)
{
  if(facts.uses[destination] != 1 ||
     instruction_index + 1 >= block.instructions.size()) return false;
  const lowir_model::Instruction & index =
    block.instructions[instruction_index + 1];
  return index.kind == lowir_model::Instruction::IK_INDEX &&
    index.first.kind == lowir_model::Operand::OP_TEMP &&
    index.first.value == destination && facts.uses[index.dest] == 0;
}

}  // namespace selection
}  // namespace lowir_native
