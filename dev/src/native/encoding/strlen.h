#pragma once

#include "lowir/model/program.h"
#include "native/mir/model.h"

#include <vector>

namespace lowir_native
{
namespace elf_detail { class CodeBuffer; }
namespace strlen_detail
{

inline lowir_model::SymbolId builtin_symbol(
    const lowir_model::LowirProgram & program)
{
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & declaration =
      program.function_declarations[i];
    if(declaration.metadata.object_symbol.valid() &&
       program.strings.get(declaration.metadata.object_symbol) ==
         "cppgm_builtin_strlen")
      return declaration.symbol;
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    const lowir_model::LowirFunction & function = program.functions[i];
    if(function.metadata.object_symbol.valid() &&
       program.strings.get(function.metadata.object_symbol) ==
         "cppgm_builtin_strlen")
      return function.symbol;
  }
  return lowir_model::SymbolId();
}

inline mir_model::MirInstruction::CallEncoding call_encoding(
    int optimization_level,
    const lowir_model::LowirInstruction & instruction,
    const std::vector<lowir_model::LowirParameter> & parameters,
    bool variadic,
    lowir_model::SymbolId builtin_strlen)
{
  return optimization_level >= 1 &&
    instruction.first.symbol == builtin_strlen &&
    instruction.args.size() == 1 && parameters.size() == 1 &&
    parameters[0].type.kind == lowir_model::LTK_PTR && !variadic &&
    !instruction.call_returns_void &&
    instruction.type.kind == lowir_model::LTK_I64 ?
    mir_model::MirInstruction::MCE_STRLEN_PREFIX16 :
    mir_model::MirInstruction::MCE_DEFAULT;
}

bool emit_prefix16_call(
    elf_detail::CodeBuffer & out,
    const mir_model::MirInstruction & instruction);

}
}
