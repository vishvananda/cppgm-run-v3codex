#pragma once

#include <cstddef>
#include <vector>

#include "lowir/model/program.h"
#include "native/mir/model.h"
#include "native/mir/registers.h"

namespace lowir_native {
namespace abi {

struct FunctionSignature
{
  const std::vector<lowir_model::LowirParameter> * params = 0;
  const lowir_model::LowType * return_type = 0;
  const lowir_model::FunctionBoundaryMetadata * boundary = 0;
};

typedef std::vector<FunctionSignature> FunctionSignatureIndex;

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
  std::size_t stack_argument_bytes = 0;
  std::size_t stack_bytes = 0;
};

struct VariadicState
{
  std::size_t gp_offset = 0;
  std::size_t fp_offset = 48;
  std::size_t overflow_arg_offset = 16;
};

const lowir_model::LowType & object_chunk_type(std::size_t remaining);
std::size_t frame_storage_size(const lowir_model::LowType & type);
std::size_t direct_parameter_bytes(
    const std::vector<lowir_model::LowirParameter> & parameters);
X64Register argument_register(std::size_t index);
Plan classify(const std::vector<lowir_model::LowirParameter> & parameters);
void record_argument_registers(mir_model::MirInstruction & call,
                               const Plan & plan);
void record_argument_registers(
    mir_model::MirInstruction & call,
    const std::vector<lowir_model::LowirParameter> & parameters);
std::size_t xmm_register_count(const Plan & plan);
VariadicState variadic_state(
    const std::vector<lowir_model::LowirParameter> & named_parameters);

}  // namespace abi
}  // namespace lowir_native
