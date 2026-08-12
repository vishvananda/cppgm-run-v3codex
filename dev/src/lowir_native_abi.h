#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "lowir_model.h"
#include "x86_register_model.h"

namespace lowir_native {
namespace abi {

struct FunctionSignature
{
  const std::vector<lowir_model::LowirParameter> * params = 0;
  const lowir_model::LowType * return_type = 0;
};

typedef std::unordered_map<std::string, FunctionSignature> FunctionSignatureIndex;

enum PieceLocation
{
  PL_GPR,
  PL_XMM,
  PL_STACK
};

struct Piece
{
  std::size_t parameter_index = 0;
  std::size_t chunk_offset = 0;
  lowir_model::LowType type;
  PieceLocation location = PL_GPR;
  X64Register reg = XR_RDI;
  XmmRegister xmm = XMM_0;
  std::size_t stack_offset = 0;
};

struct Plan
{
  std::vector<Piece> pieces;
  std::size_t stack_bytes = 0;
};

const lowir_model::LowType & object_chunk_type(std::size_t remaining);
std::size_t frame_storage_size(const lowir_model::LowType & type);
X64Register argument_register(std::size_t index);
Plan classify(const std::vector<lowir_model::LowirParameter> & parameters);

}  // namespace abi
}  // namespace lowir_native
