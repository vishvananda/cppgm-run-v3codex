#pragma once

#include "lowir_model.h"

#include <vector>

namespace lowir_native {
namespace memcpy_detail {

inline lowir_model::SymbolId builtin_symbol(
    const lowir_model::LowirProgram & program)
{
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & declaration =
      program.function_declarations[i];
    if(declaration.metadata.object_symbol.valid() &&
       program.strings.get(declaration.metadata.object_symbol) ==
         "cppgm_builtin_memcpy")
      return declaration.symbol;
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const lowir_model::LowirFunction & function = program.functions[i];
    if(function.metadata.object_symbol.valid() &&
       program.strings.get(function.metadata.object_symbol) ==
         "cppgm_builtin_memcpy")
      return function.symbol;
  }
  return lowir_model::SymbolId();
}

inline bool is_inline_unused_call(
    const lowir_model::Instruction & instruction,
    const std::vector<std::size_t> & uses,
    lowir_model::SymbolId memcpy_symbol)
{
  return memcpy_symbol.valid() &&
    instruction.kind == lowir_model::Instruction::IK_CALL &&
    instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
    instruction.first.symbol == memcpy_symbol &&
    instruction.args.size() == 3 && instruction.dest.valid() &&
    uses[instruction.dest] == 0;
}

}  // namespace memcpy_detail
}  // namespace lowir_native
