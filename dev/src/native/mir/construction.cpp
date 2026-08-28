#include "native/mir/construction.h"

namespace lowir_native {
namespace build {

namespace {

bool immediate_is_normalized(long long value,
                             const lowir_model::LowType & type)
{
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  if(width >= 64 || type.kind == lowir_model::LTK_PTR) return true;
  if(type.kind == lowir_model::LTK_U8 ||
     type.kind == lowir_model::LTK_U16 ||
     type.kind == lowir_model::LTK_U32 ||
     type.kind == lowir_model::LTK_I1) {
    const unsigned long long limit = (1ULL << width) - 1;
    return value >= 0 && static_cast<unsigned long long>(value) <= limit;
  }
  const long long limit = 1LL << (width - 1);
  return value >= -limit && value < limit;
}

bool same_register_destination(
    const mir_model::MirInstruction & instruction,
    const mir_model::MirOperand & destination)
{
  return !instruction.operands.empty() &&
    instruction.operands[0].kind == mir_model::MirOperand::OP_REG &&
    destination.kind == mir_model::MirOperand::OP_REG &&
    instruction.operands[0].reg == destination.reg;
}

}  // namespace

const lowir_model::LowType & integer_machine_type(std::size_t width)
{
  if(width <= 1) return machine_type(lowir_model::LTK_I1);
  if(width <= 8) return machine_type(lowir_model::LTK_I8);
  if(width <= 16) return machine_type(lowir_model::LTK_I16);
  if(width <= 32) return machine_type(lowir_model::LTK_I32);
  if(width <= 64) return machine_type(lowir_model::LTK_I64);
  return machine_type(lowir_model::LTK_I128);
}

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

mir_model::MirOperand float_immediate(std::uint64_t low, std::uint64_t high,
                                      lowir_model::StringId literal)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_FLOAT_IMM;
  out.literal = literal;
  out.literal_low = low;
  out.literal_high = high;
  return out;
}

mir_model::MirOperand symbol_operand(mir_model::MirOperand::Kind kind,
                                     lowir_model::SymbolId symbol)
{
  mir_model::MirOperand out;
  out.kind = kind;
  out.symbol = symbol;
  return out;
}

mir_model::MirOperand global_operand(mir_model::MirOperand::Kind kind,
                                     const lowir_model::Operand & operand)
{
  mir_model::MirOperand out = symbol_operand(kind, operand.symbol);
  out.address_binding = operand.address_binding ==
    lowir_model::Operand::ADDRESS_PREEMPTIBLE ?
      mir_model::MirOperand::ADDRESS_PREEMPTIBLE :
      mir_model::MirOperand::ADDRESS_LOCAL;
  return out;
}

bool operand_uses_register(const mir_model::MirOperand & operand,
                           X64Register reg)
{
  return (operand.kind == mir_model::MirOperand::OP_REG &&
          operand.reg == reg) ||
    (operand.kind == mir_model::MirOperand::OP_DEREF &&
     (operand.reg == reg || (operand.has_index && operand.index == reg)));
}

mir_model::MirOperand dereference(X64Register reg, long long offset)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_DEREF;
  out.reg = reg;
  out.offset = offset;
  return out;
}

mir_model::MirOperand indexed_dereference(X64Register base,
                                          X64Register index,
                                          unsigned scale,
                                          long long offset)
{
  mir_model::MirOperand out = dereference(base, offset);
  out.has_index = true;
  out.index = index;
  out.scale = scale;
  return out;
}

mir_model::MirOperand frame_operand(long long offset,
                                    std::uint32_t frame_binding)
{
  mir_model::MirOperand out;
  out.kind = mir_model::MirOperand::OP_FRAME;
  out.offset = offset;
  out.frame_binding = frame_binding;
  return out;
}

mir_model::MirInstruction machine_instruction(
    mir_model::MirInstruction::Opcode opcode,
    const lowir_model::LowType & type)
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
                 const lowir_model::LowType & type)
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
                  const lowir_model::LowType & type)
{
  mir_model::MirInstruction instruction =
    machine_instruction(mir_model::MirInstruction::MI_STORE, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_integer_extension(
    std::vector<mir_model::MirInstruction> & out,
    const mir_model::MirOperand & destination, unsigned source_width,
    bool sign_extend)
{
  mir_model::MirInstruction instruction = machine_instruction(
    sign_extend ? mir_model::MirInstruction::MI_SEXT :
                  mir_model::MirInstruction::MI_ZEXT,
    source_width == 8 ? lowir_model::builtin_lowir_type(lowir_model::LTK_I8) :
    source_width == 16 ? lowir_model::builtin_lowir_type(lowir_model::LTK_I16) :
    source_width == 32 ? lowir_model::builtin_lowir_type(lowir_model::LTK_I32) :
    lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
  append_operand(instruction, destination);
  out.push_back(instruction);
}

void append_integer_normalization(
    std::vector<mir_model::MirInstruction> & out,
    const lowir_model::LowType & type,
    const mir_model::MirOperand & destination)
{
  const std::size_t width = lowir_model::lowir_type_bit_width(type);
  if(type.kind == lowir_model::LTK_PTR || width >= 64) return;
  if(!out.empty()) {
    const mir_model::MirInstruction & producer = out.back();
    if(producer.opcode == mir_model::MirInstruction::MI_LOAD &&
       producer.type == type && producer.operands.size() == 2 &&
       same_register_destination(producer, destination))
      return;
    if(producer.opcode == mir_model::MirInstruction::MI_MOVZX &&
       same_register_destination(producer, destination))
      return;
    if(producer.opcode == mir_model::MirInstruction::MI_MOV &&
       producer.operands.size() == 2 &&
       producer.operands[1].kind == mir_model::MirOperand::OP_IMM &&
       same_register_destination(producer, destination) &&
       immediate_is_normalized(producer.operands[1].imm, type))
      return;
    const mir_model::MirInstruction::Opcode normalization =
      type.kind == lowir_model::LTK_I1 ||
      type.kind == lowir_model::LTK_I8 ||
      type.kind == lowir_model::LTK_I16 ||
      type.kind == lowir_model::LTK_I32 ||
      type.kind == lowir_model::LTK_I64 ?
      mir_model::MirInstruction::MI_SEXT :
      mir_model::MirInstruction::MI_ZEXT;
    if(producer.opcode == normalization && producer.type ==
         integer_machine_type(width) &&
       same_register_destination(producer, destination)) return;
  }
  const bool sign_extend = type.kind == lowir_model::LTK_I1 ||
    type.kind == lowir_model::LTK_I8 || type.kind == lowir_model::LTK_I16 ||
    type.kind == lowir_model::LTK_I32 || type.kind == lowir_model::LTK_I64;
  mir_model::MirInstruction instruction = machine_instruction(
    sign_extend ? mir_model::MirInstruction::MI_SEXT :
                  mir_model::MirInstruction::MI_ZEXT,
    integer_machine_type(width));
  append_operand(instruction, destination);
  out.push_back(instruction);
}

void append_float_move(std::vector<mir_model::MirInstruction> & out,
                       const mir_model::MirOperand & destination,
                       const mir_model::MirOperand & source,
                       const lowir_model::LowType & type,
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
