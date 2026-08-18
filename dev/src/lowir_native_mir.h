#pragma once

#include <string>
#include <vector>

#include "mir_model.h"

namespace lowir_native {
namespace build {

mir_model::MirOperand reg_operand(X64Register reg);
mir_model::MirOperand xmm_operand(XmmRegister xmm);
mir_model::MirOperand immediate(long long value);
mir_model::MirOperand float_immediate(const std::string & text);
mir_model::MirOperand named_operand(mir_model::MirOperand::Kind kind,
                                    const std::string & text);
mir_model::MirOperand dereference(X64Register reg, long long offset = 0);
mir_model::MirOperand indexed_dereference(X64Register base,
                                          X64Register index,
                                          unsigned scale,
                                          long long offset = 0);
mir_model::MirOperand frame_operand(long long offset,
                                    std::uint32_t frame_binding = 0);
mir_model::MirInstruction machine_instruction(
    mir_model::MirInstruction::Opcode opcode,
    const std::string & type = std::string());
void append_operand(mir_model::MirInstruction & instruction,
                    const mir_model::MirOperand & operand);
void append_move(std::vector<mir_model::MirInstruction> & out,
                 const mir_model::MirOperand & destination,
                 const mir_model::MirOperand & source);
void append_load(std::vector<mir_model::MirInstruction> & out,
                 const mir_model::MirOperand & destination,
                 const mir_model::MirOperand & source,
                 const std::string & type);
void append_store(std::vector<mir_model::MirInstruction> & out,
                  const mir_model::MirOperand & destination,
                  const mir_model::MirOperand & source,
                  const std::string & type);
void append_integer_extension(
    std::vector<mir_model::MirInstruction> & out,
    const mir_model::MirOperand & destination, unsigned source_width,
    bool sign_extend);
void append_float_move(std::vector<mir_model::MirInstruction> & out,
                       const mir_model::MirOperand & destination,
                       const mir_model::MirOperand & source,
                       const std::string & type,
                       bool omit_identity = false);

}  // namespace build
}  // namespace lowir_native
