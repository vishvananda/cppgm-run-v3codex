#pragma once

#include <cstddef>
#include <string>

#include "lowir_native_analysis.h"
#include "lowir_model.h"
#include "x86_register_model.h"

namespace lowir_native {
namespace selection {

long long integer_value(const lowir_model::Operand & operand);
long long canonical_integer_constant(long long value,
                                     const lowir_model::LowType & type);
long long atomic_order(const lowir_model::Operand & operand);
bool is_signed_integer(const lowir_model::LowType & type);
bool is_integer_or_pointer(const lowir_model::LowType & type);
bool is_scalar_float(const lowir_model::LowType & type);
bool is_extended_float(const lowir_model::LowType & type);
bool is_floating(const lowir_model::LowType & type);
const lowir_model::LowType & floating_literal_type(const std::string & text);
X86Condition predicate_condition(lowir_model::LowOperation predicate);
std::size_t align_up(std::size_t value, std::size_t alignment);
bool result_is_immediate_return(const lowir_model::LowirBlock & block,
                                std::size_t instruction_index,
                                lowir_model::ValueId destination,
                                const analysis::FunctionFacts & facts);
bool result_is_immediate_unary_not_branch(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);
bool result_is_immediately_stored(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);
bool result_is_immediate_store_address_with_later_use(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    lowir_model::ValueId destination, const analysis::FunctionFacts & facts);

}  // namespace selection
}  // namespace lowir_native
