#pragma once

#include <cstddef>
#include <string>

#include "lowir/model/program.h"

namespace lowir_cy86 {

struct Stats
{
  std::size_t functions = 0;
  std::size_t blocks = 0;
  std::size_t instructions = 0;
  std::size_t output_bytes = 0;
};

std::string render_program(const lowir_model::LowirProgram & program,
                           Stats * stats = 0);

}  // namespace lowir_cy86
