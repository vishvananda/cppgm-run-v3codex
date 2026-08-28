#pragma once

#include "lowir/model/program.h"
#include "native/lowering/abi.h"
#include "native/mir/model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace lowir_native {
struct Stats;

class ProgramLoweringSession
{
public:
  ProgramLoweringSession(const lowir_model::LowirProgram & program,
                         const std::string & target, int optimization_level = 0,
                         Stats * stats = 0);
  ~ProgramLoweringSession();

  std::size_t function_count() const;
  const lowir_model::LowirProgram & prepared_program() const;
  mir_model::MirFunction lower_function(std::size_t index);
  mir_model::MirProgram take_program_shell();

private:
  struct Impl;
  Impl * impl_;

  ProgramLoweringSession(const ProgramLoweringSession &);
  ProgramLoweringSession & operator=(const ProgramLoweringSession &);
};

mir_model::MirProgram lower_program(const lowir_model::LowirProgram & program,
                                    const std::string & target,
                                    int optimization_level = 0,
                                    Stats * stats = 0);

namespace allocation { class AllocationDecisionLog; }
namespace session_detail {

mir_model::MirFunction lower_native_function(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    const std::vector<unsigned char> & pointer_globals,
    const std::vector<lowir_model::SymbolId> & tls_wrappers,
    const abi::FunctionSignatureIndex & signatures,
    int optimization_level,
    Stats * stats,
    allocation::AllocationDecisionLog * decisions);

}  // namespace session_detail
}  // namespace lowir_native
