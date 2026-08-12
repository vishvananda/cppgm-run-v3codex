#include "lowir_native_mir.h"

namespace lowir_native {
namespace build {

mir_model::MirOperand reg_operand(X64Register reg)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_REG;
  out.reg = reg;
  return out;
}

mir_model::MirOperand xmm_operand(XmmRegister xmm)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_XMM;
  out.xmm = xmm;
  return out;
}

mir_model::MirOperand immediate(long long value)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_IMM;
  out.imm = value;
  return out;
}

mir_model::MirOperand float_immediate(const std::string & text)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_FLOAT_IMM;
  out.text = text;
  return out;
}

mir_model::MirOperand named_operand(mir_model::MirOperand::Kind kind,
                                    const std::string & text)
{
  mir_model::MirOperand out;
  out.kind = kind;
  out.text = text;
  return out;
}

mir_model::MirOperand dereference(X64Register reg, long long offset)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_DEREF;
  out.reg = reg;
  out.offset = offset;
  return out;
}

mir_model::MirOperand frame_operand(long long offset)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_FRAME;
  out.offset = offset;
  return out;
}

mir_model::MirInstruction machine_instruction(
    mir_model::MirInstruction::Opcode opcode, const std::string & type)
{
  mir_model::MirInstruction out;
  out.opcode = opcode;
  out.type = type;
  return out;
}

void append_operand(mir_model::MirInstruction & instruction,
                    const mir_model::MirOperand & operand)
{
  instruction.operands.push_back(operand);
}

void append_move(std::vector<mir_model::MirInstruction> & out,
                 const mir_model::MirOperand & destination,
                 const mir_model::MirOperand & source)
{
  if(destination.kind == mir_model::MirOperand::OP_REG &&
     source.kind == mir_model::MirOperand::OP_REG && destination.reg == source.reg)
    return;
  mir_model::MirInstruction instruction =
    machine_instruction(mir_model::MirInstruction::MI_MOV);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_load(std::vector<mir_model::MirInstruction> & out,
                 const mir_model::MirOperand & destination,
                 const mir_model::MirOperand & source,
                 const std::string & type)
{
  mir_model::MirInstruction instruction =
    machine_instruction(mir_model::MirInstruction::MI_LOAD, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_store(std::vector<mir_model::MirInstruction> & out,
                  const mir_model::MirOperand & destination,
                  const mir_model::MirOperand & source,
                  const std::string & type)
{
  mir_model::MirInstruction instruction =
    machine_instruction(mir_model::MirInstruction::MI_STORE, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_float_move(std::vector<mir_model::MirInstruction> & out,
                       const mir_model::MirOperand & destination,
                       const mir_model::MirOperand & source,
                       const std::string & type,
                       bool omit_identity)
{
  if(omit_identity && destination.kind == mir_model::MirOperand::OP_XMM &&
     source.kind == mir_model::MirOperand::OP_XMM && destination.xmm == source.xmm)
    return;
  mir_model::MirInstruction instruction =
    machine_instruction(mir_model::MirInstruction::MI_FMOV, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

}  // namespace build
}  // namespace lowir_native
