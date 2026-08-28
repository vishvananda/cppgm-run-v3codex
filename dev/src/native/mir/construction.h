#pragma once

#include <string>
#include <vector>

#include "native/mir/model.h"

namespace lowir_native {
namespace build {

inline const lowir_model::LowType & machine_type(lowir_model::LowTypeKind kind)
{
  return lowir_model::builtin_lowir_type(kind);
}
const lowir_model::LowType & integer_machine_type(std::size_t width);

mir_model::MirOperand reg_operand(X64Register reg);
mir_model::MirOperand xmm_operand(XmmRegister xmm);
mir_model::MirOperand immediate(long long value);
mir_model::MirOperand float_immediate(
    std::uint64_t low, std::uint64_t high,
    lowir_model::StringId literal = lowir_model::StringId());
mir_model::MirOperand symbol_operand(mir_model::MirOperand::Kind kind,
                                     lowir_model::SymbolId symbol);
mir_model::MirOperand global_operand(mir_model::MirOperand::Kind kind,
                                     const lowir_model::Operand & operand);
bool operand_uses_register(const mir_model::MirOperand & operand,
                           X64Register reg);
mir_model::MirOperand dereference(X64Register reg, long long offset = 0);
mir_model::MirOperand indexed_dereference(X64Register base,
                                          X64Register index,
                                          unsigned scale,
                                          long long offset = 0);
mir_model::MirOperand frame_operand(long long offset,
                                    std::uint32_t frame_binding = 0);
mir_model::MirInstruction machine_instruction(
    mir_model::MirInstruction::Opcode opcode,
    const lowir_model::LowType & type = lowir_model::LowType());
void append_operand(mir_model::MirInstruction & instruction,
                    const mir_model::MirOperand & operand);
void append_move(std::vector<mir_model::MirInstruction> & out,
                 const mir_model::MirOperand & destination,
                 const mir_model::MirOperand & source);
void append_load(std::vector<mir_model::MirInstruction> & out,
                 const mir_model::MirOperand & destination,
                 const mir_model::MirOperand & source,
                 const lowir_model::LowType & type);
void append_store(std::vector<mir_model::MirInstruction> & out,
                  const mir_model::MirOperand & destination,
                  const mir_model::MirOperand & source,
                  const lowir_model::LowType & type);
void append_integer_extension(
    std::vector<mir_model::MirInstruction> & out,
    const mir_model::MirOperand & destination, unsigned source_width,
    bool sign_extend);
void append_integer_normalization(
    std::vector<mir_model::MirInstruction> & out,
    const lowir_model::LowType & type,
    const mir_model::MirOperand & destination);
void append_float_move(std::vector<mir_model::MirInstruction> & out,
                       const mir_model::MirOperand & destination,
                       const mir_model::MirOperand & source,
                       const lowir_model::LowType & type,
                       bool omit_identity = false);

}  // namespace build
}  // namespace lowir_native
