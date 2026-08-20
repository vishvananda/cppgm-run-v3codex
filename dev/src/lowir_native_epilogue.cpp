#include "lowir_native_epilogue.h"

#include "lowir_native_registers.h"

#include <stdexcept>

namespace lowir_native {
namespace epilogue_detail {
namespace {

std::size_t stack_adjust_size(std::size_t bytes)
{
  if(!bytes) return 0;
  return bytes <= 127 ? 4 : 7;
}

std::size_t pop_size(X64Register reg)
{
  return reg >= XR_R8 ? 2 : 1;
}

std::size_t encoded_size(const mir_model::MirFunction & function)
{
  std::size_t result = 0;
  if(function.has_dynamic_stack) {
    result += 3;  // mov rsp, rbp
    result += stack_adjust_size(function.callee_saved_regs.size() * 8);
  } else {
    result += stack_adjust_size(function_stack_adjustment(function));
  }
  for(std::size_t i = 0; i < function.callee_saved_regs.size(); ++i)
    result += pop_size(function.callee_saved_regs[i]);
  return result + 2;  // pop rbp; ret
}

}  // namespace

bool is_return(const mir_model::MirInstruction & instruction)
{
  return instruction.opcode == mir_model::MirInstruction::MI_RET ||
    instruction.opcode == mir_model::MirInstruction::MI_FRET;
}

std::size_t function_stack_adjustment(
    const mir_model::MirFunction & function)
{
  const std::size_t preserved = function.callee_saved_regs.size() * 8;
  if(function.stack_size < preserved)
    throw std::logic_error("MIR stack reservation is smaller than its saves");
  return function.stack_size - preserved;
}

Plan make_plan(const mir_model::MirFunction & function)
{
  Plan result;
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::vector<mir_model::MirInstruction> & instructions =
      function.blocks[i].instructions;
    for(std::size_t j = 0; j < instructions.size(); ++j)
      if(is_return(instructions[j])) ++result.return_count;
  }
  result.physical_epilogue_count = result.return_count;
  if(result.return_count < 2) return result;

  if(!function.blocks.empty() &&
     !function.blocks.back().instructions.empty() &&
     is_return(function.blocks.back().instructions.back())) {
    result.final_return_falls_through = true;
    result.final_block = function.blocks.size() - 1;
    result.final_instruction = function.blocks.back().instructions.size() - 1;
  }

  const std::size_t teardown_bytes = encoded_size(function);
  const std::size_t branch_count = result.return_count -
    (result.final_return_falls_through ? 1 : 0);
  const std::size_t separate_bytes = result.return_count * teardown_bytes;
  const std::size_t shared_bytes = teardown_bytes + branch_count * 5;
  if(shared_bytes >= separate_bytes) return result;

  result.shared = true;
  result.physical_epilogue_count = 1;
  return result;
}

}  // namespace epilogue_detail
}  // namespace lowir_native
