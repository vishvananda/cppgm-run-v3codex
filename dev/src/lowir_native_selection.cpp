#include "lowir_native_selection.h"

#include <cerrno>
#include <cstdlib>
#include <stdexcept>

namespace lowir_native {
namespace selection {

long long integer_literal(const std::string & text)
{
  errno = 0;
  char * end = 0;
  const long long value = std::strtoll(text.c_str(), &end, 0);
  if(errno || !end || *end)
    throw std::runtime_error("invalid integer literal: " + text);
  return value;
}

long long atomic_order(const lowir_model::Operand & operand)
{
  if(operand.kind != lowir_model::Operand::OP_INTEGER)
    throw std::runtime_error("atomic memory order must be an integer literal");
  return integer_literal(operand.text);
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

const lowir_model::LowType & floating_literal_type(const std::string & text)
{
  if(!text.empty() && (text.back() == 'f' || text.back() == 'F'))
    return lowir_model::builtin_lowir_type(lowir_model::LTK_F32);
  if(!text.empty() && (text.back() == 'l' || text.back() == 'L'))
    return lowir_model::builtin_lowir_type(lowir_model::LTK_F80);
  return lowir_model::builtin_lowir_type(lowir_model::LTK_F64);
}

X86Condition predicate_condition(const std::string & predicate)
{
  if(predicate == "eq") return XC_E;
  if(predicate == "ne") return XC_NE;
  if(predicate == "lt") return XC_L;
  if(predicate == "le") return XC_LE;
  if(predicate == "gt") return XC_G;
  if(predicate == "ge") return XC_GE;
  if(predicate == "ult") return XC_B;
  if(predicate == "ule") return XC_BE;
  if(predicate == "ugt") return XC_A;
  if(predicate == "uge") return XC_AE;
  throw std::runtime_error("unsupported integer comparison predicate: " + predicate);
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

bool result_is_immediate_return(const lowir_model::LowirBlock & block,
                                std::size_t instruction_index,
                                const std::string & destination,
                                const analysis::FunctionFacts & facts)
{
  if(facts.uses.find(destination) == facts.uses.end() ||
     facts.uses.find(destination)->second != 1 ||
     instruction_index + 1 >= block.instructions.size()) return false;
  const lowir_model::Instruction & next = block.instructions[instruction_index + 1];
  return next.kind == lowir_model::Instruction::IK_RETURN &&
    next.first.text == destination;
}

bool result_is_immediate_unary_not_branch(
    const lowir_model::LowirBlock & block, std::size_t instruction_index,
    const std::string & destination, const analysis::FunctionFacts & facts)
{
  if(instruction_index + 2 >= block.instructions.size() ||
     facts.uses.find(destination) == facts.uses.end() ||
     facts.uses.find(destination)->second != 1) return false;
  const lowir_model::Instruction & unary = block.instructions[instruction_index + 1];
  const lowir_model::Instruction & branch = block.instructions[instruction_index + 2];
  return unary.kind == lowir_model::Instruction::IK_UNARY && unary.op == "not" &&
    unary.first.text == destination &&
    branch.kind == lowir_model::Instruction::IK_BRANCH &&
    branch.first.text == unary.dest;
}

}  // namespace selection
}  // namespace lowir_native
