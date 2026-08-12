#pragma once

#include <cstddef>
#include <string>

#include "lowir_model.h"
#include "x86_register_model.h"

namespace lowir_native {
namespace selection {

long long integer_literal(const std::string & text);
long long atomic_order(const lowir_model::Operand & operand);
bool is_signed_integer(const lowir_model::LowType & type);
bool is_integer_or_pointer(const lowir_model::LowType & type);
bool is_scalar_float(const lowir_model::LowType & type);
bool is_extended_float(const lowir_model::LowType & type);
bool is_floating(const lowir_model::LowType & type);
const lowir_model::LowType & floating_literal_type(const std::string & text);
X86Condition predicate_condition(const std::string & predicate);
std::size_t align_up(std::size_t value, std::size_t alignment);

}  // namespace selection
}  // namespace lowir_native
