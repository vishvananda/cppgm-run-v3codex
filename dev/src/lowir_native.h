#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "lowir_model.h"
#include "mir_model.h"

namespace lowir_native {

struct Stats
{
  std::size_t functions = 0;
  std::size_t blocks = 0;
  std::size_t lowir_instructions = 0;
  std::size_t mir_instructions = 0;
  std::size_t fixups = 0;
  std::size_t output_bytes = 0;
  std::uint64_t lower_nanoseconds = 0;
  std::uint64_t encode_nanoseconds = 0;
  std::uint64_t write_nanoseconds = 0;
};

mir_model::MirProgram lower_program(const lowir_model::LowirProgram & program,
                                    const std::string & target,
                                    Stats * stats = 0);

void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            Stats * stats = 0);

}  // namespace lowir_native
