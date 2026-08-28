#pragma once

#include "native/mir/model.h"

#include <cstddef>

namespace lowir_native {
namespace epilogue_detail {

struct Plan
{
  std::size_t return_count = 0;
  std::size_t physical_epilogue_count = 0;
  std::size_t final_block = 0;
  std::size_t final_instruction = 0;
  bool shared = false;
  bool final_return_falls_through = false;
};

bool is_return(const mir_model::MirInstruction & instruction);
std::size_t function_stack_adjustment(
    const mir_model::MirFunction & function);
Plan make_plan(const mir_model::MirFunction & function);

}  // namespace epilogue_detail
}  // namespace lowir_native
