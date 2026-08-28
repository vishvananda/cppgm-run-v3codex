#pragma once

#include <vector>

#include "lowir/model/program.h"
#include "native/mir/model.h"

namespace lowir_native {
namespace wide {

bool is_integer(const lowir_model::LowType & type);

struct Words
{
  unsigned long long low = 0;
  unsigned long long high = 0;
};

struct Value
{
  bool immediate = false;
  Words words;
  mir_model::MirOperand storage;
};

Value literal_value(long long low, std::uint64_t high);
Value literal_value(const lowir_model::Operand & operand);
Value storage_value(const mir_model::MirOperand & storage);

void append_word_to_register(const Value & value, std::size_t chunk,
                             X64Register destination, X64Register scratch,
                             std::vector<mir_model::MirInstruction> & out);
void append_word_store(const mir_model::MirOperand & destination,
                       const Value & value, std::size_t chunk,
                       X64Register value_register, X64Register scratch,
                       std::vector<mir_model::MirInstruction> & out);
void append_copy(const mir_model::MirOperand & destination, const Value & source,
                 std::vector<mir_model::MirInstruction> & out);
void append_compare(const Value & left, const Value & right,
                    lowir_model::LowOperation operation,
                    std::vector<mir_model::MirInstruction> & out);
void append_compare_branch(const Value & left, const Value & right,
                           lowir_model::LowOperation operation,
                           const mir_model::MirOperand & true_target,
                           const mir_model::MirOperand & false_target,
                           std::vector<mir_model::MirInstruction> & out);
void append_binary(const mir_model::MirOperand & destination,
                   const Value & left, const Value & right,
                   lowir_model::LowOperation operation,
                   std::vector<mir_model::MirInstruction> & out);
void append_unary(const mir_model::MirOperand & destination,
                  const Value & source, lowir_model::LowOperation operation,
                  std::vector<mir_model::MirInstruction> & out);
void append_atomic_load(const mir_model::MirOperand & object,
                        const mir_model::MirOperand & destination,
                        std::vector<mir_model::MirInstruction> & out);
void append_atomic_compare_exchange(
    const mir_model::MirOperand & object,
    const mir_model::MirOperand & expected,
    const Value & desired,
    std::vector<mir_model::MirInstruction> & out);

}  // namespace wide
}  // namespace lowir_native
