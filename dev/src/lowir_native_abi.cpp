#include "lowir_native_abi.h"

#include <algorithm>
#include <stdexcept>

namespace lowir_native {
namespace abi {
namespace {

std::size_t align_up(std::size_t value, std::size_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

bool is_scalar_float(const lowir_model::LowType & type)
{
  return type.kind == lowir_model::LTK_F32 || type.kind == lowir_model::LTK_F64;
}

}  // namespace

const lowir_model::LowType & object_chunk_type(std::size_t remaining)
{
  if(remaining <= 1) return lowir_model::builtin_lowir_type(lowir_model::LTK_I8);
  if(remaining <= 2) return lowir_model::builtin_lowir_type(lowir_model::LTK_I16);
  if(remaining <= 4) return lowir_model::builtin_lowir_type(lowir_model::LTK_I32);
  return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
}

std::size_t frame_storage_size(const lowir_model::LowType & type)
{
  if(type.kind != lowir_model::LTK_OBJECT) return type.storage_size;
  if(type.storage_size <= 1) return 1;
  if(type.storage_size <= 2) return 2;
  if(type.storage_size <= 4) return 4;
  if(type.storage_size <= 8) return 8;
  return align_up(type.storage_size, 8);
}

X64Register argument_register(std::size_t index)
{
  static const X64Register registers[] = {
    XR_RDI, XR_RSI, XR_RDX, XR_RCX, XR_R8, XR_R9
  };
  if(index >= sizeof(registers) / sizeof(registers[0]))
    throw std::runtime_error("SysV ABI has six INTEGER argument registers");
  return registers[index];
}

Plan classify(const std::vector<lowir_model::LowirParameter> & parameters)
{
  Plan plan;
  std::size_t gpr_index = 0;
  std::size_t xmm_index = 0;
  std::size_t stack_offset = 0;
  for(std::size_t i = 0; i < parameters.size(); ++i) {
    const lowir_model::LowType & type = parameters[i].type;
    if(type.kind == lowir_model::LTK_F80) {
      Piece piece;
      piece.parameter_index = i;
      piece.type = type;
      piece.location = PL_STACK;
      stack_offset = align_up(stack_offset, 16);
      piece.stack_offset = stack_offset;
      stack_offset += 16;
      plan.pieces.push_back(piece);
      continue;
    }
    if(type.kind == lowir_model::LTK_OBJECT) {
      const std::size_t chunks = (type.storage_size + 7) / 8;
      const std::size_t available_gprs = gpr_index < 6 ? 6 - gpr_index : 0;
      const bool in_registers = type.storage_size <= 16 && chunks <= available_gprs;
      if(in_registers) {
        for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
          Piece piece;
          piece.parameter_index = i;
          piece.chunk_offset = chunk * 8;
          piece.type = object_chunk_type(type.storage_size - piece.chunk_offset);
          piece.location = PL_GPR;
          piece.reg = argument_register(gpr_index++);
          plan.pieces.push_back(piece);
        }
      } else {
        const std::size_t stack_alignment = std::min<std::size_t>(
          16, std::max<std::size_t>(8, type.alignment));
        stack_offset = align_up(stack_offset, stack_alignment);
        for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
          Piece piece;
          piece.parameter_index = i;
          piece.chunk_offset = chunk * 8;
          piece.type = object_chunk_type(type.storage_size - piece.chunk_offset);
          piece.location = PL_STACK;
          piece.stack_offset = stack_offset + piece.chunk_offset;
          plan.pieces.push_back(piece);
        }
        stack_offset += align_up(type.storage_size, 8);
      }
      continue;
    }
    Piece piece;
    piece.parameter_index = i;
    piece.type = type;
    if(is_scalar_float(type) && xmm_index < 8) {
      piece.location = PL_XMM;
      piece.xmm = static_cast<XmmRegister>(xmm_index++);
    } else if(!is_scalar_float(type) && gpr_index < 6) {
      piece.location = PL_GPR;
      piece.reg = argument_register(gpr_index++);
    } else {
      piece.location = PL_STACK;
      stack_offset = align_up(stack_offset, 8);
      piece.stack_offset = stack_offset;
      stack_offset += 8;
    }
    plan.pieces.push_back(piece);
  }
  plan.stack_bytes = align_up(stack_offset, 16);
  return plan;
}

}  // namespace abi
}  // namespace lowir_native
