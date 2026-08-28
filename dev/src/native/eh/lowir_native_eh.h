#pragma once

#include "lowir/model/program.h"
#include "native/mir/model.h"

#include <vector>

namespace lowir_native {
namespace eh {

void plan_program(const lowir_model::LowirProgram & source,
                  mir_model::MirProgram & target);
lowir_model::SymbolId runtime_data_symbol(
  const std::vector<mir_model::MirRuntimeData> & data,
  mir_model::RuntimeData::Kind kind);
bool lower_marker(const lowir_model::Program & program,
                  const lowir_model::Function & function,
                  const lowir_model::Instruction & source,
                  std::vector<mir_model::MirInstruction> & target);

}  // namespace eh
}  // namespace lowir_native
