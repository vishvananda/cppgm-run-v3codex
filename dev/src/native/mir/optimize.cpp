#include "native/mir/optimize.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lowir_native {
namespace machine_opt {
namespace {

using mir_model::MirBlock;
using mir_model::MirFunction;
using mir_model::MirInstruction;
using mir_model::MirOperand;

typedef std::uint64_t RegisterMask;

void note_peak_analysis_bytes(Stats * stats, std::size_t bytes)
{
  if(stats) stats->peak_analysis_bytes =
    std::max(stats->peak_analysis_bytes, bytes);
}

std::size_t bit_vector_bytes(const std::vector<bool> & values)
{
  return (values.capacity() + 7) / 8;
}

const RegisterMask kAllGprs = (RegisterMask(1) << 16) - 1;
const RegisterMask kAllXmms = ((RegisterMask(1) << 8) - 1) << 16;
const RegisterMask kCallArguments =
  (RegisterMask(1) << XR_RDI) | (RegisterMask(1) << XR_RSI) |
  (RegisterMask(1) << XR_RDX) | (RegisterMask(1) << XR_RCX) |
  (RegisterMask(1) << XR_R8) | (RegisterMask(1) << XR_R9) | kAllXmms;
const RegisterMask kCallClobbers =
  (RegisterMask(1) << XR_RAX) | (RegisterMask(1) << XR_RCX) |
  (RegisterMask(1) << XR_RDX) | (RegisterMask(1) << XR_RSI) |
  (RegisterMask(1) << XR_RDI) | (RegisterMask(1) << XR_R8) |
  (RegisterMask(1) << XR_R9) | (RegisterMask(1) << XR_R10) |
  (RegisterMask(1) << XR_R11) | kAllXmms;

RegisterMask gpr_bit(X64Register reg)
{
  return RegisterMask(1) << static_cast<unsigned>(reg);
}

RegisterMask xmm_bit(XmmRegister reg)
{
  return RegisterMask(1) << (16 + static_cast<unsigned>(reg));
}

RegisterMask operand_registers(const MirOperand & operand)
{
  if(operand.kind == MirOperand::OP_REG) return gpr_bit(operand.reg);
  if(operand.kind == MirOperand::OP_XMM) return xmm_bit(operand.xmm);
  if(operand.kind == MirOperand::OP_DEREF)
    return gpr_bit(operand.reg) |
      (operand.has_index ? gpr_bit(operand.index) : 0);
  return 0;
}

bool has_all_gpr_use_and_definition(MirInstruction::Opcode opcode)
{
  switch(opcode) {
  case MirInstruction::MI_LOCK_CMPXCHG16B:
  case MirInstruction::MI_I128_SHL:
  case MirInstruction::MI_I128_SHR:
  case MirInstruction::MI_I128_SAR:
  case MirInstruction::MI_I128_UDIV:
  case MirInstruction::MI_I128_UMOD:
  case MirInstruction::MI_I128_SDIV:
  case MirInstruction::MI_I128_SMOD:
    return true;
  default:
    return false;
  }
}

bool is_write_only_destination(MirInstruction::Opcode opcode)
{
  switch(opcode) {
  case MirInstruction::MI_MOV:
  case MirInstruction::MI_LOAD:
  case MirInstruction::MI_LEA:
  case MirInstruction::MI_FMOV:
  case MirInstruction::MI_FNEG:
  case MirInstruction::MI_FADD:
  case MirInstruction::MI_FSUB:
  case MirInstruction::MI_FMUL:
  case MirInstruction::MI_FDIV:
  case MirInstruction::MI_FEQ:
  case MirInstruction::MI_FNE:
  case MirInstruction::MI_FLT:
  case MirInstruction::MI_FGT:
  case MirInstruction::MI_FLE:
  case MirInstruction::MI_FGE:
  case MirInstruction::MI_SITOFP:
  case MirInstruction::MI_UITOFP:
  case MirInstruction::MI_FPTOSI:
  case MirInstruction::MI_FPTOUI:
  case MirInstruction::MI_FPEXT:
  case MirInstruction::MI_FPTRUNC:
  case MirInstruction::MI_SETCC:
  case MirInstruction::MI_MOVZX:
  case MirInstruction::MI_TLS_ADDR:
  case MirInstruction::MI_LOAD_EXCEPTION:
  case MirInstruction::MI_LOAD_EXCEPTION_SELECTOR:
    return true;
  default:
    return false;
  }
}

bool is_read_write_destination(MirInstruction::Opcode opcode)
{
  switch(opcode) {
  case MirInstruction::MI_LOCK_XADD:
  case MirInstruction::MI_XCHG:
  case MirInstruction::MI_LOCK_CMPXCHG:
  case MirInstruction::MI_ADD:
  case MirInstruction::MI_SUB:
  case MirInstruction::MI_IMUL:
  case MirInstruction::MI_AND:
  case MirInstruction::MI_OR:
  case MirInstruction::MI_XOR:
  case MirInstruction::MI_NEG:
  case MirInstruction::MI_NOT:
  case MirInstruction::MI_BSWAP:
  case MirInstruction::MI_SEXT:
  case MirInstruction::MI_ZEXT:
  case MirInstruction::MI_SHL_CL:
  case MirInstruction::MI_SHR_CL:
  case MirInstruction::MI_SAR_CL:
  case MirInstruction::MI_SHL_IMM:
  case MirInstruction::MI_SHR_IMM:
  case MirInstruction::MI_SAR_IMM:
    return true;
  default:
    return false;
  }
}

RegisterMask instruction_defs(const MirInstruction & instruction)
{
  RegisterMask defs = has_all_gpr_use_and_definition(instruction.opcode) ?
    kAllGprs : 0;
  if(!instruction.operands.empty() &&
     (is_write_only_destination(instruction.opcode) ||
      is_read_write_destination(instruction.opcode))) {
    const MirOperand & destination = instruction.operands[0];
    if(destination.kind == MirOperand::OP_REG) defs |= gpr_bit(destination.reg);
    if(destination.kind == MirOperand::OP_XMM) defs |= xmm_bit(destination.xmm);
  }
  switch(instruction.opcode) {
  case MirInstruction::MI_LOCK_XADD:
  case MirInstruction::MI_XCHG:
    // xadd and xchg replace their explicit register operand with the prior
    // memory value.  The address is operand zero and the exchanged register is
    // operand one.
    if(instruction.operands.size() > 1)
      defs |= operand_registers(instruction.operands[1]) & kAllGprs;
    break;
  case MirInstruction::MI_LOCK_CMPXCHG:
    // cmpxchg replaces rax with the observed memory value on failure.
    defs |= gpr_bit(XR_RAX);
    break;
  case MirInstruction::MI_CQO:
    defs |= gpr_bit(XR_RDX);
    break;
  case MirInstruction::MI_MUL:
  case MirInstruction::MI_IDIV:
  case MirInstruction::MI_DIV:
    defs |= gpr_bit(XR_RAX) | gpr_bit(XR_RDX);
    break;
  case MirInstruction::MI_CALL:
  case MirInstruction::MI_CALL_INDIRECT:
  case MirInstruction::MI_THROW:
    defs |= kCallClobbers;
    break;
  case MirInstruction::MI_EH_PUSH:
    // The compact EH runtime materializes its handler record with rax/r11.
    // Host-EH emission treats this as a marker, so the conservative clobber is
    // harmless there and keeps the MIR fact valid for both encoders.
    defs |= gpr_bit(XR_RAX) | gpr_bit(XR_R11);
    break;
  case MirInstruction::MI_EH_POP:
    defs |= gpr_bit(XR_RAX) | gpr_bit(XR_RCX) | gpr_bit(XR_R11);
    break;
  case MirInstruction::MI_EH_CATCH:
    // Catch selection may call the runtime dynamic-cast helper.
    defs |= kCallClobbers;
    break;
  case MirInstruction::MI_LOAD_EXCEPTION:
  case MirInstruction::MI_LOAD_EXCEPTION_SELECTOR:
    defs |= gpr_bit(XR_RAX) | gpr_bit(XR_RCX) | gpr_bit(XR_R11);
    break;
  case MirInstruction::MI_COPY_BYTES:
    if(instruction.copy_preserves_pointers)
      defs |= instruction.byte_count ?
        gpr_bit(XR_RAX) | gpr_bit(XR_R10) | gpr_bit(XR_R11) :
        gpr_bit(XR_RAX) | gpr_bit(XR_RCX) |
          gpr_bit(XR_R10) | gpr_bit(XR_R11);
    else
      defs |= gpr_bit(XR_RDI) | gpr_bit(XR_RSI) | gpr_bit(XR_RCX);
    break;
  case MirInstruction::MI_ZERO_BYTES:
    defs |= gpr_bit(XR_RAX) | gpr_bit(XR_RDI) | gpr_bit(XR_RCX);
    break;
  default:
    break;
  }
  return defs;
}

RegisterMask instruction_uses(
    const MirInstruction & instruction, bool exact_call_arguments = false)
{
  RegisterMask uses = has_all_gpr_use_and_definition(instruction.opcode) ?
    kAllGprs : 0;
  for(std::size_t i = 0; i < instruction.operands.size(); ++i) {
    if(i == 0 && is_write_only_destination(instruction.opcode)) {
      if(instruction.operands[i].kind == MirOperand::OP_DEREF)
        uses |= operand_registers(instruction.operands[i]);
      continue;
    }
    uses |= operand_registers(instruction.operands[i]);
  }
  switch(instruction.opcode) {
  case MirInstruction::MI_LOCK_CMPXCHG:
    uses |= gpr_bit(XR_RAX);
    break;
  case MirInstruction::MI_CQO:
    uses |= gpr_bit(XR_RAX);
    break;
  case MirInstruction::MI_MUL:
  case MirInstruction::MI_IDIV:
  case MirInstruction::MI_DIV:
    uses |= gpr_bit(XR_RAX) | gpr_bit(XR_RDX);
    break;
  case MirInstruction::MI_SHL_CL:
  case MirInstruction::MI_SHR_CL:
  case MirInstruction::MI_SAR_CL:
    uses |= gpr_bit(XR_RCX);
    break;
  case MirInstruction::MI_CALL:
  case MirInstruction::MI_CALL_INDIRECT:
    // The MIR call target is explicit, while SysV register arguments are
    // physical live-ins to the call instruction.
    uses |= exact_call_arguments && instruction.call_argument_registers_known ?
      instruction.call_argument_register_mask : kCallArguments;
    break;
  case MirInstruction::MI_COPY_BYTES:
    if(!instruction.byte_count && instruction.operands.empty())
      uses |= gpr_bit(XR_RDI) | gpr_bit(XR_RSI) | gpr_bit(XR_RDX);
    break;
  case MirInstruction::MI_EH_PUSH:
    // The compact handler record snapshots the callee-saved machine state.
    uses |= gpr_bit(XR_RBX) | gpr_bit(XR_RBP) | gpr_bit(XR_R12) |
      gpr_bit(XR_R13) | gpr_bit(XR_R14) | gpr_bit(XR_R15);
    break;
  case MirInstruction::MI_RET:
    if(instruction.operands.empty())
      uses |= gpr_bit(XR_RAX) | gpr_bit(XR_RDX);
    break;
  case MirInstruction::MI_EXIT:
    uses |= gpr_bit(XR_RDI);
    break;
  default:
    break;
  }
  return uses;
}

bool is_control_barrier(MirInstruction::Opcode opcode)
{
  return opcode == MirInstruction::MI_JMP ||
    opcode == MirInstruction::MI_JMP_INDIRECT ||
    opcode == MirInstruction::MI_RET || opcode == MirInstruction::MI_FRET ||
    opcode == MirInstruction::MI_EXIT || opcode == MirInstruction::MI_THROW ||
    opcode == MirInstruction::MI_RESUME;
}

struct ValueFact
{
  bool valid = false;
  MirOperand value;
  std::size_t definition = 0;
};

struct LocalFacts
{
  std::array<ValueFact, 16> gprs;
  std::array<ValueFact, 8> xmms;
  std::array<std::size_t, 16> active_gprs;
  std::array<std::size_t, 8> active_xmms;
  std::size_t active_gpr_count = 0;
  std::size_t active_xmm_count = 0;

  void invalidate(RegisterMask defs)
  {
    std::size_t kept = 0;
    for(std::size_t active = 0; active < active_gpr_count; ++active) {
      const std::size_t i = active_gprs[active];
      const bool destination_changed = defs & (RegisterMask(1) << i);
      const bool source_changed =
        (gprs[i].value.kind == MirOperand::OP_REG &&
         (defs & gpr_bit(gprs[i].value.reg))) ||
        (gprs[i].value.kind == MirOperand::OP_DEREF &&
         ((defs & gpr_bit(gprs[i].value.reg)) ||
          (gprs[i].value.has_index &&
           (defs & gpr_bit(gprs[i].value.index)))));
      if(destination_changed || source_changed)
        gprs[i].valid = false;
      else
        active_gprs[kept++] = i;
    }
    active_gpr_count = kept;
    kept = 0;
    for(std::size_t active = 0; active < active_xmm_count; ++active) {
      const std::size_t i = active_xmms[active];
      const bool destination_changed =
        defs & (RegisterMask(1) << (16 + i));
      const bool source_changed =
        xmms[i].value.kind == MirOperand::OP_XMM &&
        (defs & xmm_bit(xmms[i].value.xmm));
      if(destination_changed || source_changed)
        xmms[i].valid = false;
      else
        active_xmms[kept++] = i;
    }
    active_xmm_count = kept;
  }

  void set_gpr(X64Register reg, const MirOperand & value,
               std::size_t definition)
  {
    const std::size_t index = static_cast<std::size_t>(reg);
    ValueFact & fact = gprs[index];
    if(!fact.valid) active_gprs[active_gpr_count++] = index;
    fact.valid = true;
    fact.value = value;
    fact.definition = definition;
  }

  void set_xmm(XmmRegister reg, const MirOperand & value,
               std::size_t definition)
  {
    const std::size_t index = static_cast<std::size_t>(reg);
    ValueFact & fact = xmms[index];
    if(!fact.valid) active_xmms[active_xmm_count++] = index;
    fact.valid = true;
    fact.value = value;
    fact.definition = definition;
  }

  MirOperand canonical(const MirOperand & operand, std::size_t * definition = 0) const
  {
    MirOperand current = operand;
    std::size_t last_definition = 0;
    for(std::size_t depth = 0; depth != 16; ++depth) {
      if(current.kind == MirOperand::OP_REG) {
        const ValueFact & fact = gprs[static_cast<std::size_t>(current.reg)];
        if(!fact.valid) break;
        current = fact.value;
        last_definition = fact.definition;
        continue;
      }
      if(current.kind == MirOperand::OP_XMM) {
        const ValueFact & fact = xmms[static_cast<std::size_t>(current.xmm)];
        if(!fact.valid) break;
        current = fact.value;
        last_definition = fact.definition;
        continue;
      }
      break;
    }
    if(definition) *definition = last_definition;
    return current;
  }

  MirOperand canonical_xmm_alias(const MirOperand & operand,
                                 std::size_t * definition = 0) const
  {
    MirOperand current = operand;
    std::size_t last_definition = 0;
    for(std::size_t depth = 0; depth != 8 && current.kind == MirOperand::OP_XMM;
        ++depth) {
      const ValueFact & fact = xmms[static_cast<std::size_t>(current.xmm)];
      if(!fact.valid || fact.value.kind != MirOperand::OP_XMM) break;
      current = fact.value;
      last_definition = fact.definition;
    }
    if(definition) *definition = last_definition;
    return current;
  }
};

bool immediate_operand_supported(const MirInstruction & instruction,
                                 std::size_t index)
{
  if(instruction.opcode == MirInstruction::MI_MOV && index == 1) return true;
  if(index != 1) return false;
  return instruction.opcode == MirInstruction::MI_ADD ||
    instruction.opcode == MirInstruction::MI_SUB ||
    instruction.opcode == MirInstruction::MI_IMUL ||
    instruction.opcode == MirInstruction::MI_AND ||
    instruction.opcode == MirInstruction::MI_OR ||
    instruction.opcode == MirInstruction::MI_XOR;
}

bool operand_is_read_only(const MirInstruction & instruction, std::size_t index)
{
  // The memory address is operand zero for these atomics, while operand one is
  // both an input and the register receiving the old memory value.  Replacing
  // that register with an alias without also rewriting its later users changes
  // the observable result of the atomic operation.
  if(index == 1 &&
     (instruction.opcode == MirInstruction::MI_LOCK_XADD ||
      instruction.opcode == MirInstruction::MI_XCHG)) return false;
  if(index != 0) return true;
  return !is_write_only_destination(instruction.opcode) &&
    !is_read_write_destination(instruction.opcode);
}

bool has_call_before_redefinition(const MirBlock & block, std::size_t start,
                                  X64Register reg)
{
  const RegisterMask bit = gpr_bit(reg);
  for(std::size_t i = start; i < block.instructions.size(); ++i) {
    const MirInstruction & instruction = block.instructions[i];
    if(instruction.opcode == MirInstruction::MI_CALL ||
       instruction.opcode == MirInstruction::MI_CALL_INDIRECT) {
      return true;
    }
    if(instruction_defs(instruction) & bit) return false;
    if(is_control_barrier(instruction.opcode)) return false;
  }
  return false;
}

bool same_complete_operand(const MirOperand & left, const MirOperand & right)
{
  if(left.kind != right.kind) return false;
  if(left.kind == MirOperand::OP_REG) return left.reg == right.reg;
  if(left.kind == MirOperand::OP_XMM) return left.xmm == right.xmm;
  if(left.kind == MirOperand::OP_IMM) return left.imm == right.imm;
  if(left.kind == MirOperand::OP_FRAME) return left.offset == right.offset;
  if(left.kind == MirOperand::OP_LABEL) return left.block == right.block;
  if(left.kind == MirOperand::OP_SYMBOL || left.kind == MirOperand::OP_GLOBAL)
    return left.symbol == right.symbol;
  if(left.kind == MirOperand::OP_FLOAT_IMM)
    return left.literal_low == right.literal_low &&
      left.literal_high == right.literal_high;
  if(left.kind == MirOperand::OP_DEREF)
    return left.reg == right.reg && left.offset == right.offset &&
      left.has_index == right.has_index &&
      (!left.has_index ||
       (left.index == right.index && left.scale == right.scale));
  return false;
}

void rewrite_local_operands(MirBlock & block, std::vector<bool> & preserve,
                            Stats * stats)
{
  LocalFacts facts;
  preserve.assign(block.instructions.size(), false);
  for(std::size_t i = 0; i < block.instructions.size(); ++i) {
    MirInstruction & instruction = block.instructions[i];
    if(stats) ++stats->instruction_visits;
    for(std::size_t operand_index = 0;
        operand_index < instruction.operands.size(); ++operand_index) {
      MirOperand & operand = instruction.operands[operand_index];
      if(!operand_is_read_only(instruction, operand_index)) {
        if(operand.kind != MirOperand::OP_DEREF) continue;
      }
      const MirOperand original = operand;
      std::size_t definition = 0;
      if(operand.kind == MirOperand::OP_DEREF) {
        MirOperand base;
        base.kind = MirOperand::OP_REG;
        base.reg = operand.reg;
        const MirOperand replacement = facts.canonical(base, &definition);
        if(replacement.kind == MirOperand::OP_REG) operand.reg = replacement.reg;
        else if(replacement.kind == MirOperand::OP_FRAME &&
                !operand.has_index) {
          const long long folded_offset = operand.offset + replacement.offset;
          // Negative frame operands name local storage and are adjusted past
          // callee saves by the encoder.  Nonnegative frame operands name the
          // caller's frame and deliberately receive no such adjustment.  A
          // register derived from a local may nevertheless point at abstract
          // offset zero (for example, one past a local array), so do not lose
          // that provenance by folding it into the caller-frame namespace.
          if(folded_offset < 0) {
            operand.kind = MirOperand::OP_FRAME;
            operand.offset = folded_offset;
            operand.frame_binding = replacement.frame_binding;
          }
        }
        if(operand.kind == MirOperand::OP_DEREF && operand.has_index) {
          MirOperand index;
          index.kind = MirOperand::OP_REG;
          index.reg = operand.index;
          const MirOperand replacement_index = facts.canonical(index);
          if(replacement_index.kind == MirOperand::OP_REG)
            operand.index = replacement_index.reg;
        }
      } else if(operand.kind == MirOperand::OP_REG ||
                operand.kind == MirOperand::OP_XMM) {
        const MirOperand replacement = operand.kind == MirOperand::OP_XMM ?
          facts.canonical_xmm_alias(operand, &definition) :
          facts.canonical(operand, &definition);
        bool allowed = replacement.kind == operand.kind;
        if(operand.kind == MirOperand::OP_REG &&
           replacement.kind == MirOperand::OP_IMM)
          allowed = immediate_operand_supported(instruction, operand_index);
        if(operand.kind == MirOperand::OP_REG &&
           (replacement.kind == MirOperand::OP_SYMBOL ||
            replacement.kind == MirOperand::OP_GLOBAL))
          allowed = instruction.opcode == MirInstruction::MI_MOV &&
            operand_index == 1;
        if(allowed) operand = replacement;
      }
      if(!same_complete_operand(original, operand)) {
        if(stats) ++stats->rewrites;
        if(instruction.opcode == MirInstruction::MI_MOV &&
           operand_index == 1 && operand.kind == MirOperand::OP_IMM &&
           !instruction.operands.empty() &&
           instruction.operands[0].kind == MirOperand::OP_REG &&
           has_call_before_redefinition(block, i + 1,
             instruction.operands[0].reg) && definition < preserve.size())
          preserve[definition] = true;
      }
    }

    const RegisterMask defs = instruction_defs(instruction);
    facts.invalidate(defs);
    if(instruction.opcode == MirInstruction::MI_MOV &&
       instruction.operands.size() == 2 &&
       instruction.operands[0].kind == MirOperand::OP_REG) {
      const MirOperand & source = instruction.operands[1];
      if(source.kind == MirOperand::OP_REG || source.kind == MirOperand::OP_IMM ||
         source.kind == MirOperand::OP_SYMBOL || source.kind == MirOperand::OP_GLOBAL) {
        facts.set_gpr(instruction.operands[0].reg, source, i);
      }
    } else if(instruction.opcode == MirInstruction::MI_FMOV &&
              instruction.operands.size() == 2 &&
              instruction.operands[0].kind == MirOperand::OP_XMM) {
      const MirOperand & source = instruction.operands[1];
      if(source.kind == MirOperand::OP_XMM ||
         source.kind == MirOperand::OP_FLOAT_IMM ||
         source.kind == MirOperand::OP_IMM) {
        facts.set_xmm(instruction.operands[0].xmm, source, i);
      }
    } else if(instruction.opcode == MirInstruction::MI_LEA &&
              instruction.operands.size() == 2 &&
              instruction.operands[0].kind == MirOperand::OP_REG &&
              instruction.operands[1].kind == MirOperand::OP_FRAME) {
      facts.set_gpr(instruction.operands[0].reg,
                    instruction.operands[1], i);
    }
  }
}

bool is_label_operand(const MirInstruction & instruction,
                      lowir_model::BlockId * label)
{
  if(instruction.operands.size() != 1 ||
     instruction.operands[0].kind != MirOperand::OP_LABEL) return false;
  *label = instruction.operands[0].block;
  return true;
}

struct ControlFlow
{
  std::vector<std::vector<std::size_t> > successors;
  std::vector<std::vector<std::size_t> > predecessors;
};

std::size_t control_flow_bytes(const ControlFlow & cfg)
{
  std::size_t bytes =
    cfg.successors.capacity() * sizeof(std::vector<std::size_t>) +
    cfg.predecessors.capacity() * sizeof(std::vector<std::size_t>);
  for(std::size_t i = 0; i < cfg.successors.size(); ++i) {
    bytes += cfg.successors[i].capacity() * sizeof(std::size_t);
    bytes += cfg.predecessors[i].capacity() * sizeof(std::size_t);
  }
  return bytes;
}

std::size_t preserve_bytes(const std::vector<std::vector<bool> > & preserve)
{
  std::size_t bytes = preserve.capacity() * sizeof(std::vector<bool>);
  for(std::size_t i = 0; i < preserve.size(); ++i)
    bytes += bit_vector_bytes(preserve[i]);
  return bytes;
}

void append_successor(std::vector<std::size_t> & values,
                      std::vector<std::size_t> & seen_by_block,
                      std::size_t block, std::size_t value)
{
  if(seen_by_block[value] == block) return;
  seen_by_block[value] = block;
  values.push_back(value);
}

ControlFlow build_control_flow(const MirFunction & function, Stats * stats,
                               std::size_t analysis_base_bytes)
{
  ControlFlow cfg;
  cfg.successors.resize(function.blocks.size());
  cfg.predecessors.resize(function.blocks.size());
  // A switch may contribute many outgoing conditional edges from one block.
  // Indexed marks keep duplicate suppression O(1) per edge instead of
  // repeatedly scanning the growing successor vector.
  std::vector<std::size_t> seen_by_block(function.blocks.size(),
                                         function.blocks.size());
  const std::size_t missing = static_cast<std::size_t>(-1);
  std::vector<std::size_t> labels(function.block_labels.size(), missing);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::uint32_t id = function.blocks[i].id;
    if(id < labels.size()) labels[id] = i;
  }
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const MirBlock & block = function.blocks[i];
    bool falls_through = true;
    for(std::size_t j = 0; j < block.instructions.size(); ++j) {
      const MirInstruction & instruction = block.instructions[j];
      if(instruction.opcode == MirInstruction::MI_JCC ||
         instruction.opcode == MirInstruction::MI_JMP ||
         instruction.opcode == MirInstruction::MI_EH_PUSH) {
        lowir_model::BlockId label;
        const bool has_label = instruction.opcode == MirInstruction::MI_EH_PUSH ?
          !instruction.operands.empty() &&
            instruction.operands[0].kind == MirOperand::OP_LABEL :
          is_label_operand(instruction, &label);
        if(instruction.opcode == MirInstruction::MI_EH_PUSH && has_label)
          label = instruction.operands[0].block;
        if(has_label) {
          const std::uint32_t id = label;
          if(label.valid() && id < labels.size() && labels[id] != missing)
            append_successor(cfg.successors[i], seen_by_block, i, labels[id]);
        }
      }
      if(instruction.opcode == MirInstruction::MI_JMP ||
         is_control_barrier(instruction.opcode)) falls_through = false;
    }
    if(falls_through && i + 1 < function.blocks.size())
      append_successor(cfg.successors[i], seen_by_block, i, i + 1);
    for(std::size_t j = 0; j < cfg.successors[i].size(); ++j) {
      cfg.predecessors[cfg.successors[i][j]].push_back(i);
      if(stats) ++stats->cfg_edge_visits;
    }
  }
  if(stats)
    note_peak_analysis_bytes(stats, analysis_base_bytes +
      control_flow_bytes(cfg) +
      seen_by_block.capacity() * sizeof(std::size_t) +
      labels.capacity() * sizeof(std::size_t));
  return cfg;
}

struct Liveness
{
  std::vector<RegisterMask> in;
  std::vector<RegisterMask> out;
};

Liveness compute_liveness(const MirFunction & function, const ControlFlow & cfg,
                          Stats * stats, std::size_t analysis_base_bytes)
{
  const std::size_t count = function.blocks.size();
  std::vector<RegisterMask> uses(count, 0), defs(count, 0);
  for(std::size_t i = 0; i < count; ++i) {
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const MirInstruction & instruction = function.blocks[i].instructions[j];
      if(stats) ++stats->instruction_visits;
      uses[i] |= instruction_uses(instruction, true) & ~defs[i];
      defs[i] |= instruction_defs(instruction);
    }
  }
  Liveness live;
  live.in.assign(count, 0);
  live.out.assign(count, 0);
  std::deque<std::size_t> worklist;
  std::vector<bool> queued(count, false);
  for(std::size_t i = count; i != 0; --i) {
    worklist.push_back(i - 1);
    queued[i - 1] = true;
    if(stats) ++stats->worklist_pushes;
  }
  if(stats)
    note_peak_analysis_bytes(stats, analysis_base_bytes +
      uses.capacity() * sizeof(RegisterMask) +
      defs.capacity() * sizeof(RegisterMask) +
      live.in.capacity() * sizeof(RegisterMask) +
      live.out.capacity() * sizeof(RegisterMask) +
      bit_vector_bytes(queued) + count * sizeof(std::size_t));
  while(!worklist.empty()) {
    const std::size_t block = worklist.front();
    worklist.pop_front();
    queued[block] = false;
    RegisterMask new_out = 0;
    for(std::size_t i = 0; i < cfg.successors[block].size(); ++i) {
      new_out |= live.in[cfg.successors[block][i]];
      if(stats) ++stats->cfg_edge_visits;
    }
    const RegisterMask new_in = uses[block] | (new_out & ~defs[block]);
    if(new_out == live.out[block] && new_in == live.in[block]) continue;
    live.out[block] = new_out;
    live.in[block] = new_in;
    for(std::size_t i = 0; i < cfg.predecessors[block].size(); ++i) {
      const std::size_t predecessor = cfg.predecessors[block][i];
      if(queued[predecessor]) continue;
      queued[predecessor] = true;
      worklist.push_back(predecessor);
      if(stats) ++stats->worklist_pushes;
    }
  }
  return live;
}

RegisterMask explicit_registers(const MirInstruction & instruction)
{
  RegisterMask result = 0;
  for(std::size_t i = 0; i < instruction.operands.size(); ++i)
    result |= operand_registers(instruction.operands[i]);
  return result;
}

bool debug_ranges_use_register(const MirFunction & function,
                               X64Register reg)
{
  for(std::size_t variable = 0;
      variable < function.debug_variables.size(); ++variable)
    for(std::size_t range = 0;
        range < function.debug_variables[variable].ranges.size(); ++range) {
      const mir_model::MirDebugVariable::Range & location =
        function.debug_variables[variable].ranges[range];
      if(location.location == mir_model::MirDebugVariable::Range::LK_REG &&
         location.reg == reg)
        return true;
    }
  return false;
}

bool can_recolor_block(const MirBlock & block, RegisterMask live,
                       X64Register source, X64Register destination,
                       bool * source_seen)
{
  const RegisterMask source_bit = gpr_bit(source);
  const RegisterMask destination_bit = gpr_bit(destination);
  for(std::size_t i = block.instructions.size(); i != 0; --i) {
    const MirInstruction & instruction = block.instructions[i - 1];
    const RegisterMask defs = instruction_defs(instruction);
    const RegisterMask uses = instruction_uses(instruction, true);
    const RegisterMask explicit_mask = explicit_registers(instruction);
    const RegisterMask before = uses | (live & ~defs);
    const bool mentions_source = ((defs | uses) & source_bit) != 0;
    *source_seen = *source_seen || mentions_source;
    // Every source occurrence must be explicit so the rewrite cannot miss
    // an ABI or instruction-specific implicit register dependency.
    if(mentions_source && !(explicit_mask & source_bit)) return false;
    // The two colors may not hold distinct live values at any boundary.
    if((before & source_bit) && (before & destination_bit)) return false;
    if((live & source_bit) && (live & destination_bit)) return false;
    // A destination clobber cannot be crossed by a value formerly held in
    // the callee-saved source register.
    if((live & source_bit) && (defs & destination_bit) &&
       !(defs & source_bit))
      return false;
    // Keep the first implementation conservative around instructions that
    // mention both colors, even when boundary liveness would permit
    // coalescing them.
    if((explicit_mask & source_bit) &&
       ((defs | uses) & destination_bit))
      return false;
    live = before;
  }
  return true;
}

bool can_recolor_register(const MirFunction & function,
                          const Liveness & liveness,
                          X64Register source, X64Register destination)
{
  if(debug_ranges_use_register(function, source)) return false;
  bool source_seen = false;
  for(std::size_t block_index = 0;
      block_index < function.blocks.size(); ++block_index) {
    const MirBlock & block = function.blocks[block_index];
    if(!can_recolor_block(
         block, liveness.out[block_index], source, destination,
         &source_seen)) return false;
  }
  return source_seen;
}

bool block_mentions_register(const MirBlock & block, X64Register reg)
{
  const RegisterMask bit = gpr_bit(reg);
  for(std::size_t instruction = 0;
      instruction < block.instructions.size(); ++instruction)
    if((instruction_defs(block.instructions[instruction]) |
        instruction_uses(block.instructions[instruction], true)) & bit)
      return true;
  return false;
}

bool function_mentions_register(const MirFunction & function,
                                X64Register reg)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    if(block_mentions_register(function.blocks[block], reg)) return true;
  return false;
}

void rewrite_register(MirOperand * operand, X64Register source,
                      X64Register destination)
{
  if(operand->kind == MirOperand::OP_REG && operand->reg == source)
    operand->reg = destination;
  else if(operand->kind == MirOperand::OP_DEREF) {
    if(operand->reg == source) operand->reg = destination;
    if(operand->has_index && operand->index == source)
      operand->index = destination;
  }
}

// Lowering allocates physical registers online.  A short call-free range can
// therefore occupy a callee-saved register merely because the caller-saved
// choices were busy at its definition, even when a caller-saved register is
// free for that range in the completed CFG.  Recolor one whole physical color
// when exact MIR liveness proves that all of its ranges avoid the replacement
// and every replacement clobber.  Whole-color rewriting keeps the proof small
// and lets frame finalization remove the now-unused eager save/restore.
bool recolor_call_free_callee_saved(MirFunction & function,
                                    const Liveness & liveness,
                                    Stats * stats)
{
  bool has_call = false;
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function.blocks[block].instructions.size();
        ++instruction) {
      const MirInstruction::Opcode opcode =
        function.blocks[block].instructions[instruction].opcode;
      has_call = has_call || opcode == MirInstruction::MI_CALL ||
        opcode == MirInstruction::MI_CALL_INDIRECT;
    }
  // In a frameless SysV call function, an even save count also needs one
  // padding word.  Recoloring one save then removes both the memory operation
  // and the padding adjustment in each prologue/epilogue.  At an odd save
  // count, require two independently safe colors so the result also removes
  // one save/alignment pair rather than merely trading a save for padding.
  // Leaf functions have no call-alignment tax at all.
  if(!has_call) return false;
  static const X64Register destinations[] = {
    XR_R8, XR_R9, XR_RDI, XR_RSI
  };
  std::vector<std::pair<X64Register, X64Register> > rewrites;
  bool destination_used[sizeof(destinations) / sizeof(destinations[0])] =
    {false, false, false, false};
  const std::size_t required =
    function.callee_saved_regs.size() % 2 == 0 ? 1 : 2;
  for(std::size_t saved = function.callee_saved_regs.size();
      saved != 0 && rewrites.size() < required; --saved) {
    const X64Register source = function.callee_saved_regs[saved - 1];
    for(std::size_t candidate = 0;
        candidate < sizeof(destinations) / sizeof(destinations[0]);
        ++candidate) {
      if(destination_used[candidate]) continue;
      const X64Register destination = destinations[candidate];
      if(!can_recolor_register(
           function, liveness, source, destination)) continue;
      rewrites.push_back(std::make_pair(source, destination));
      destination_used[candidate] = true;
      break;
    }
  }
  if(rewrites.size() < required) return false;
  for(std::size_t rewrite = 0; rewrite < rewrites.size(); ++rewrite)
    for(std::size_t block = 0; block < function.blocks.size(); ++block)
      for(std::size_t instruction = 0;
          instruction < function.blocks[block].instructions.size();
          ++instruction)
        for(std::size_t operand = 0;
            operand < function.blocks[block].instructions[instruction].
              operands.size(); ++operand)
          rewrite_register(
            &function.blocks[block].instructions[instruction].operands[operand],
            rewrites[rewrite].first, rewrites[rewrite].second);
  if(stats) stats->rewrites += rewrites.size();
  return true;
}

// A completed CFG can expose several disjoint live ranges that happened to
// receive the same callee-saved color during online lowering.  If none of the
// ranges reaches a block edge, choose a conflict-free destination separately
// in each block and eliminate the source color only when every range succeeds.
// Caller-saved destinations are free.  A surviving callee-saved color is also
// profitable because it adds no new save/restore.  The caller prevents an
// eliminated source from becoming a destination again and prevents an anchor
// destination from becoming a later source, so at most five colors can be
// eliminated and the iterative liveness proof must converge.
bool recolor_one_block_local_callee_saved(
    MirFunction & function, const Liveness & liveness,
    RegisterMask unavailable_sources,
    RegisterMask unavailable_destinations,
    RegisterMask * eliminated_source,
    RegisterMask * anchored_destinations,
    std::size_t persistent_analysis_storage,
    Stats * stats)
{
  static const X64Register caller_saved[] = {
    XR_RAX, XR_RCX, XR_RDX, XR_RDI, XR_RSI, XR_R8, XR_R9
  };
  for(std::size_t saved = function.callee_saved_regs.size();
      saved != 0; --saved) {
    const X64Register source = function.callee_saved_regs[saved - 1];
    const RegisterMask source_bit = gpr_bit(source);
    if(unavailable_sources & source_bit) continue;
    if(debug_ranges_use_register(function, source)) continue;
    if(!function_mentions_register(function, source)) continue;
    if(stats) ++stats->block_recolor_candidates;
    std::vector<X64Register> destinations(
      function.blocks.size(), XR_RSP);
    if(stats)
      note_peak_analysis_bytes(stats, persistent_analysis_storage +
        liveness.in.capacity() * sizeof(RegisterMask) +
        liveness.out.capacity() * sizeof(RegisterMask) +
        destinations.capacity() * sizeof(X64Register));
    bool failed = false;
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      if((liveness.in[block] | liveness.out[block]) & source_bit) {
        failed = true;
        break;
      }
      if(!block_mentions_register(function.blocks[block], source)) continue;
      bool destination_found = false;
      for(std::size_t candidate = 0;
          candidate < sizeof(caller_saved) / sizeof(caller_saved[0]);
          ++candidate) {
        bool candidate_seen = false;
        if(!can_recolor_block(
             function.blocks[block], liveness.out[block], source,
             caller_saved[candidate], &candidate_seen)) continue;
        if(!candidate_seen) continue;
        destination_found = true;
        destinations[block] = caller_saved[candidate];
        break;
      }
      if(!destination_found) {
        for(std::size_t candidate = 0;
            candidate < function.callee_saved_regs.size(); ++candidate) {
          const X64Register destination =
            function.callee_saved_regs[candidate];
          if(destination == source ||
             (unavailable_destinations & gpr_bit(destination)) ||
             !function_mentions_register(function, destination)) continue;
          bool candidate_seen = false;
          if(!can_recolor_block(
               function.blocks[block], liveness.out[block], source,
               destination, &candidate_seen)) continue;
          if(!candidate_seen) continue;
          destination_found = true;
          destinations[block] = destination;
          break;
        }
      }
      if(!destination_found) {
        failed = true;
        break;
      }
    }
    if(failed) continue;
    std::size_t changed_blocks = 0;
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      if(destinations[block] == XR_RSP) continue;
      ++changed_blocks;
      for(std::size_t instruction = 0;
          instruction < function.blocks[block].instructions.size();
          ++instruction)
        for(std::size_t operand = 0;
            operand < function.blocks[block].instructions[instruction].
              operands.size(); ++operand)
          rewrite_register(
            &function.blocks[block].instructions[instruction].operands[operand],
            source, destinations[block]);
    }
    if(stats) {
      stats->rewrites += changed_blocks;
      ++stats->block_recolor_registers;
      stats->block_recolor_blocks += changed_blocks;
    }
    *eliminated_source = source_bit;
    *anchored_destinations = 0;
    for(std::size_t block = 0; block < destinations.size(); ++block)
      if(destinations[block] != XR_RSP &&
         (destinations[block] == XR_RBX ||
          destinations[block] == XR_R12 ||
          destinations[block] == XR_R13 ||
          destinations[block] == XR_R14 ||
          destinations[block] == XR_R15))
        *anchored_destinations |= gpr_bit(destinations[block]);
    return true;
  }
  return false;
}

bool is_removable_definition(const MirInstruction & instruction,
                              RegisterMask live)
{
  if(instruction.opcode != MirInstruction::MI_MOV &&
     instruction.opcode != MirInstruction::MI_FMOV &&
     instruction.opcode != MirInstruction::MI_LEA) return false;
  const RegisterMask defs = instruction_defs(instruction);
  return defs != 0 && (defs & live) == 0;
}

void retain_debug(const MirInstruction & removed, MirInstruction * survivor)
{
  if(!survivor) return;
  if(!survivor->debug_location.present() && removed.debug_location.present())
    survivor->debug_location = removed.debug_location;
  if(!survivor->has_source_position && removed.has_source_position) {
    survivor->has_source_position = true;
    survivor->source_position = removed.source_position;
  }
}

const lowir_model::LowType * normalized_load_type(
    const MirInstruction & normalization)
{
  if(normalization.opcode != MirInstruction::MI_SEXT &&
     normalization.opcode != MirInstruction::MI_ZEXT)
    return 0;
  const bool sign = normalization.opcode == MirInstruction::MI_SEXT;
  switch(lowir_model::lowir_type_bit_width(normalization.type)) {
  case 8:
    return &lowir_model::builtin_lowir_type(
      sign ? lowir_model::LTK_I8 : lowir_model::LTK_U8);
  case 16:
    return &lowir_model::builtin_lowir_type(
      sign ? lowir_model::LTK_I16 : lowir_model::LTK_U16);
  case 32:
    return &lowir_model::builtin_lowir_type(
      sign ? lowir_model::LTK_I32 : lowir_model::LTK_U32);
  default:
    return 0;
  }
}

bool same_normalized_register(const MirInstruction & producer,
                              const MirInstruction & normalization)
{
  return !producer.operands.empty() &&
    producer.operands[0].kind == MirOperand::OP_REG &&
    normalization.operands.size() == 1 &&
    normalization.operands[0].kind == MirOperand::OP_REG &&
    producer.operands[0].reg == normalization.operands[0].reg;
}

// A narrow load and its immediately following extension are one x86 load.
// Likewise, zero-extending from a narrower integer range already proves that
// a subsequent wider signed normalization cannot change the value.  Keep the
// proof block-local and adjacent so no register-liveness reasoning is needed.
void combine_adjacent_integer_normalizations(MirFunction & function,
                                             Stats * stats)
{
  for(std::size_t block_index = 0;
      block_index < function.blocks.size(); ++block_index) {
    std::vector<MirInstruction> & instructions =
      function.blocks[block_index].instructions;
    std::size_t kept = 0;
    for(std::size_t index = 0; index < instructions.size(); ++index) {
      MirInstruction instruction = std::move(instructions[index]);
      if(kept != 0 && same_normalized_register(
           instructions[kept - 1], instruction)) {
        MirInstruction & producer = instructions[kept - 1];
        const lowir_model::LowType * load_type =
          normalized_load_type(instruction);
        if(producer.opcode == MirInstruction::MI_LOAD && load_type &&
           lowir_model::lowir_type_bit_width(producer.type) ==
             lowir_model::lowir_type_bit_width(instruction.type)) {
          producer.type = *load_type;
          retain_debug(instruction, &producer);
          if(stats) ++stats->rewrites;
          continue;
        }
        const bool producer_normalizes =
          producer.opcode == MirInstruction::MI_SEXT ||
          producer.opcode == MirInstruction::MI_ZEXT;
        if(producer_normalizes &&
           (instruction.opcode == MirInstruction::MI_SEXT ||
            instruction.opcode == MirInstruction::MI_ZEXT)) {
          const std::size_t producer_width =
            lowir_model::lowir_type_bit_width(producer.type);
          const std::size_t result_width =
            lowir_model::lowir_type_bit_width(instruction.type);
          if(result_width <= producer_width) {
            retain_debug(producer, &instruction);
            producer = std::move(instruction);
            if(stats) ++stats->rewrites;
            continue;
          }
          if(producer.opcode == instruction.opcode ||
             producer.opcode == MirInstruction::MI_ZEXT) {
            retain_debug(instruction, &producer);
            if(stats) ++stats->rewrites;
            continue;
          }
        }
      }
      instructions[kept++] = std::move(instruction);
    }
    instructions.resize(kept);
  }
}

bool debug_ranges_use_frame_offset(const MirFunction & function,
                                   long long offset)
{
  for(std::size_t variable = 0;
      variable < function.debug_variables.size(); ++variable)
    for(std::size_t range = 0;
        range < function.debug_variables[variable].ranges.size(); ++range) {
      const mir_model::MirDebugVariable::Range & location =
        function.debug_variables[variable].ranges[range];
      if(location.location == mir_model::MirDebugVariable::Range::LK_FRAME &&
         location.frame_offset == offset)
        return true;
    }
  return false;
}

// A value selected into a temporary frame home can still be sitting intact in
// its defining register when its sole consumer immediately follows the store.
// Forward the register into an integer compare before liveness is built.  The
// binding identity proves that no other logical use needs the stored value;
// limiting the first rule to an adjacent consumer avoids any intervening
// clobber proof and leaves the broader delayed-reload oracle undisturbed.
void forward_adjacent_single_use_frame_compares(MirFunction & function,
                                                Stats * stats)
{
  if(function.frame_bindings.empty()) return;
  std::vector<std::size_t> uses(function.frame_bindings.size() + 1, 0);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t instruction = 0;
        instruction < function.blocks[block].instructions.size();
        ++instruction)
      for(std::size_t operand = 0;
          operand < function.blocks[block].instructions[instruction].
            operands.size(); ++operand) {
        const MirOperand & value =
          function.blocks[block].instructions[instruction].operands[operand];
        if(value.kind == MirOperand::OP_FRAME &&
           value.frame_binding != 0 && value.frame_binding < uses.size())
          ++uses[value.frame_binding];
      }

  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    std::vector<MirInstruction> & instructions =
      function.blocks[block].instructions;
    for(std::size_t index = 0; index + 1 < instructions.size();) {
      MirInstruction & store = instructions[index];
      MirInstruction & compare = instructions[index + 1];
      if(store.opcode != MirInstruction::MI_STORE || store.volatile_access ||
         store.operands.size() != 2 ||
         store.operands[0].kind != MirOperand::OP_FRAME ||
         store.operands[0].frame_binding == 0 ||
         store.operands[1].kind != MirOperand::OP_REG ||
         compare.opcode != MirInstruction::MI_CMP ||
         compare.operands.size() != 2 || compare.type != store.type) {
        ++index;
        continue;
      }
      const std::uint32_t binding = store.operands[0].frame_binding;
      if(binding > function.frame_bindings.size() || uses[binding] != 2 ||
         function.frame_bindings[binding - 1].kind !=
           mir_model::MirFrameBinding::FB_TEMP ||
         function.frame_bindings[binding - 1].type != store.type ||
         debug_ranges_use_frame_offset(
           function, function.frame_bindings[binding - 1].offset)) {
        ++index;
        continue;
      }
      std::size_t frame_operand = compare.operands.size();
      for(std::size_t operand = 0; operand < compare.operands.size(); ++operand)
        if(compare.operands[operand].kind == MirOperand::OP_FRAME &&
           compare.operands[operand].frame_binding == binding) {
          if(frame_operand != compare.operands.size()) {
            frame_operand = compare.operands.size();
            break;
          }
          frame_operand = operand;
        }
      if(frame_operand == compare.operands.size() ||
         (frame_operand == 0 &&
          compare.operands[1].kind != MirOperand::OP_REG &&
          compare.operands[1].kind != MirOperand::OP_IMM) ||
         (frame_operand == 1 &&
          compare.operands[0].kind != MirOperand::OP_REG)) {
        ++index;
        continue;
      }
      MirOperand replacement;
      replacement.kind = MirOperand::OP_REG;
      replacement.reg = store.operands[1].reg;
      compare.operands[frame_operand] = replacement;
      retain_debug(store, &compare);
      instructions.erase(instructions.begin() + index);
      if(stats) ++stats->rewrites;
      // The compare now occupies the erased store's index.  Continue after it.
      ++index;
    }
  }
}

bool is_identity_move(const MirInstruction & instruction)
{
  if((instruction.opcode != MirInstruction::MI_MOV &&
      instruction.opcode != MirInstruction::MI_FMOV) ||
     instruction.operands.size() != 2)
    return false;
  const MirOperand & destination = instruction.operands[0];
  const MirOperand & source = instruction.operands[1];
  if(destination.kind != source.kind) return false;
  if(destination.kind == MirOperand::OP_REG)
    return destination.reg == source.reg;
  if(destination.kind == MirOperand::OP_XMM)
    return destination.xmm == source.xmm;
  return false;
}

void remove_dead_definitions(MirFunction & function,
                             const std::vector<std::vector<bool> > & preserve,
                             const Liveness & liveness, Stats * stats)
{
  for(std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
    MirBlock & block = function.blocks[block_index];
    RegisterMask live = liveness.out[block_index];
    std::vector<MirInstruction> reverse;
    reverse.reserve(block.instructions.size());
    for(std::size_t i = block.instructions.size(); i != 0; --i) {
      const std::size_t index = i - 1;
      const MirInstruction & instruction = block.instructions[index];
      if(stats) ++stats->instruction_visits;
      const bool identity = is_identity_move(instruction);
      const bool unpreserved = index < preserve[block_index].size() &&
        !preserve[block_index][index];
      if(identity || (unpreserved &&
         is_removable_definition(instruction, live))) {
        retain_debug(instruction, reverse.empty() ? 0 : &reverse.back());
        if(stats) {
          ++stats->rewrites;
          if(identity) ++stats->identity_moves;
        }
        continue;
      }
      live &= ~instruction_defs(instruction);
      live |= instruction_uses(instruction);
      reverse.push_back(instruction);
    }
    std::reverse(reverse.begin(), reverse.end());
    block.instructions.swap(reverse);
  }
}

X86Condition inverse_condition(X86Condition condition)
{
  return static_cast<X86Condition>(static_cast<unsigned>(condition) ^ 1U);
}

bool jump_targets(const MirInstruction & instruction,
                  lowir_model::BlockId label)
{
  return instruction.operands.size() == 1 &&
    instruction.operands[0].kind == MirOperand::OP_LABEL &&
    instruction.operands[0].block == label;
}

void clean_branches(MirFunction & function, Stats * stats)
{
  for(std::size_t i = 0; i + 1 < function.blocks.size(); ++i) {
    MirBlock & block = function.blocks[i];
    const lowir_model::BlockId fallthrough = function.blocks[i + 1].id;
    if(block.instructions.empty()) continue;
    MirInstruction & last = block.instructions.back();
    if(last.opcode == MirInstruction::MI_JMP && jump_targets(last, fallthrough)) {
      if(block.instructions.size() > 1)
        retain_debug(last, &block.instructions[block.instructions.size() - 2]);
      block.instructions.pop_back();
      if(stats) ++stats->rewrites;
      continue;
    }
    if(last.opcode != MirInstruction::MI_JMP || block.instructions.size() < 2)
      continue;
    MirInstruction & branch = block.instructions[block.instructions.size() - 2];
    if(branch.opcode != MirInstruction::MI_JCC ||
       !jump_targets(branch, fallthrough)) continue;
    branch.condition = inverse_condition(branch.condition);
    branch.operands[0] = last.operands[0];
    retain_debug(last, &branch);
    block.instructions.pop_back();
    if(stats) ++stats->rewrites;
  }
}

void form_zero_tests(MirFunction & function, Stats * stats)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      MirInstruction & instruction = function.blocks[i].instructions[j];
      if(instruction.opcode != MirInstruction::MI_CMP ||
         instruction.operands.size() != 2 ||
         instruction.operands[0].kind != MirOperand::OP_REG ||
         instruction.operands[1].kind != MirOperand::OP_IMM ||
         instruction.operands[1].imm != 0) continue;
      instruction.opcode = MirInstruction::MI_TEST;
      instruction.operands[1] = instruction.operands[0];
      if(stats) ++stats->rewrites;
    }
}

void trace_layout(MirFunction & function, Stats * stats)
{
  if(function.blocks.size() < 2) return;
  // An unconditional trace can move a successor that is already the natural
  // fallthrough of a conditional block.  The later branch cleanup then has no
  // adjacent successor and must retain both its conditional and unconditional
  // jumps.  Until layout accounts for both successors together, keep the
  // established order for every function containing a conditional branch.
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      if(function.blocks[i].instructions[j].opcode ==
         MirInstruction::MI_JCC) return;
  const std::size_t missing = static_cast<std::size_t>(-1);
  std::vector<std::size_t> labels(function.block_labels.size(), missing);
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    const std::uint32_t id = function.blocks[i].id;
    if(id < labels.size()) labels[id] = i;
  }
  std::vector<bool> placed(function.blocks.size(), false);
  std::vector<std::size_t> order;
  order.reserve(function.blocks.size());
  for(std::size_t seed = 0; seed < function.blocks.size(); ++seed) {
    std::size_t current = seed;
    while(!placed[current]) {
      placed[current] = true;
      order.push_back(current);
      const MirBlock & block = function.blocks[current];
      if(block.instructions.empty() ||
         block.instructions.back().opcode != MirInstruction::MI_JMP) break;
      bool conditional_tail = false;
      for(std::size_t i = block.instructions.size() - 1; i != 0; --i) {
        const MirInstruction::Opcode opcode = block.instructions[i - 1].opcode;
        if(opcode == MirInstruction::MI_JCC) conditional_tail = true;
        if(opcode != MirInstruction::MI_JCC) break;
      }
      if(conditional_tail) break;
      lowir_model::BlockId target;
      if(!is_label_operand(block.instructions.back(), &target)) break;
      const std::uint32_t id = target;
      if(!target.valid() || id >= labels.size() || labels[id] == missing ||
         placed[labels[id]]) break;
      current = labels[id];
    }
  }
  bool changed = false;
  for(std::size_t i = 0; i < order.size(); ++i) changed = changed || order[i] != i;
  if(stats)
    note_peak_analysis_bytes(stats, labels.capacity() * sizeof(std::size_t) +
      bit_vector_bytes(placed) + order.capacity() * sizeof(std::size_t) +
      (changed ? function.blocks.size() * sizeof(MirBlock) : 0));
  if(!changed) return;
  std::vector<MirBlock> blocks;
  blocks.reserve(function.blocks.size());
  for(std::size_t i = 0; i < order.size(); ++i)
    blocks.push_back(std::move(function.blocks[order[i]]));
  function.blocks.swap(blocks);
  if(stats) ++stats->rewrites;
}

bool has_implicit_callee_saved_use(const MirFunction & function)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      const MirInstruction::Opcode opcode =
        function.blocks[i].instructions[j].opcode;
      if(opcode == MirInstruction::MI_LOCK_CMPXCHG16B ||
         opcode == MirInstruction::MI_I128_SHL ||
         opcode == MirInstruction::MI_I128_SHR ||
         opcode == MirInstruction::MI_I128_SAR ||
         opcode == MirInstruction::MI_I128_UDIV ||
         opcode == MirInstruction::MI_I128_UMOD ||
         opcode == MirInstruction::MI_I128_SDIV ||
         opcode == MirInstruction::MI_I128_SMOD) return true;
    }
  return false;
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

void finalize_frame(MirFunction & function, Stats * stats)
{
  if(has_implicit_callee_saved_use(function)) return;
  RegisterMask referenced = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      for(std::size_t k = 0;
          k < function.blocks[i].instructions[j].operands.size(); ++k)
        referenced |= operand_registers(
          function.blocks[i].instructions[j].operands[k]);
  const std::size_t old_saved = function.callee_saved_regs.size();
  function.callee_saved_regs.erase(std::remove_if(
    function.callee_saved_regs.begin(), function.callee_saved_regs.end(),
    [referenced](X64Register reg) { return (referenced & gpr_bit(reg)) == 0; }),
    function.callee_saved_regs.end());
  if(function.callee_saved_regs.size() == old_saved) return;
  // Lowering records the local-frame requirement separately from an
  // ABI-driven total-stack floor.  Recombine those facts with the surviving
  // saves, then publish one final stack_size for both MIR and the encoder.
  function.stack_size = align_up(std::max(function.stack_floor_bytes,
    function.stack_frame_bytes + function.callee_saved_regs.size() * 8), 16);
  if(stats) ++stats->rewrites;
}

void select_frame_pointer_policy(MirFunction & function, Stats * stats)
{
  function.omit_frame_pointer = false;
  if(function.has_dynamic_stack || function.host_eh_enabled ||
     function.scratch_bytes != 0 || !function.debug_variables.empty()) return;

  bool has_call = false;
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const MirInstruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.opcode == MirInstruction::MI_EH_PUSH ||
         instruction.opcode == MirInstruction::MI_EH_POP ||
         instruction.opcode == MirInstruction::MI_EH_CATCH ||
         instruction.opcode == MirInstruction::MI_EH_FILTER ||
         instruction.opcode == MirInstruction::MI_EH_CLEANUP_CLAUSE ||
         instruction.opcode == MirInstruction::MI_LOAD_EXCEPTION ||
         instruction.opcode == MirInstruction::MI_LOAD_EXCEPTION_SELECTOR ||
         instruction.opcode == MirInstruction::MI_RESUME) return;
      has_call = has_call || instruction.opcode == MirInstruction::MI_CALL ||
        instruction.opcode == MirInstruction::MI_CALL_INDIRECT;
      for(std::size_t operand = 0;
          operand < instruction.operands.size(); ++operand)
        if(instruction.operands[operand].kind == MirOperand::OP_FRAME) return;
    }

  function.omit_frame_pointer = true;
  if(stats) {
    ++stats->frameless_functions;
    if(has_call) ++stats->frameless_call_functions;
    stats->frameless_saved_registers += function.callee_saved_regs.size();
  }
}

std::size_t instruction_count(const MirFunction & function)
{
  std::size_t count = 0;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    count += function.blocks[i].instructions.size();
  return count;
}

void make_scalar_float_returns_explicit(MirFunction & function, Stats * stats)
{
  if(function.return_type.kind != lowir_model::LTK_F32 &&
     function.return_type.kind != lowir_model::LTK_F64) return;
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      MirInstruction & instruction = function.blocks[i].instructions[j];
      if(instruction.opcode != MirInstruction::MI_RET ||
         !instruction.operands.empty()) continue;
      MirOperand result;
      result.kind = MirOperand::OP_XMM;
      result.xmm = XMM_0;
      instruction.operands.push_back(result);
      if(stats) ++stats->rewrites;
    }
}

void select_medium_copy_chunks(MirFunction & function, Stats * stats)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      MirInstruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.opcode != MirInstruction::MI_COPY_BYTES ||
         instruction.byte_count <= 32 || instruction.byte_count > 64 ||
         instruction.byte_alignment >= 8 ||
         instruction.copy_encoding != MirInstruction::MBC_DEFAULT)
        continue;
      instruction.copy_encoding = MirInstruction::MBC_DIRECT_CHUNKS;
      if(stats) {
        ++stats->medium_copy_chunks;
        ++stats->rewrites;
      }
    }
}

}  // namespace

std::uint64_t instruction_definition_mask(
    const mir_model::MirInstruction & instruction)
{
  return instruction_defs(instruction);
}

void optimize_function(MirFunction & function, int level, Stats * stats)
{
  if(level < 0 || level > 3)
    throw std::logic_error("unsupported machine optimization level");
  if(level == 0) return;
  function.share_epilogues = false;
  const std::chrono::steady_clock::time_point started =
    std::chrono::steady_clock::now();
  if(stats) {
    ++stats->functions;
    stats->input_instructions += instruction_count(function);
  }
  // PA29 MIR encoded scalar floating returns only through the ABI metadata and
  // an implicit xmm0 convention.  Make that dependency explicit at the PA38
  // optimization boundary so liveness, copy propagation, MIR serialization,
  // and native encoding all consume the same return fact.  O0 remains the
  // preserved PA29 representation.
  make_scalar_float_returns_explicit(function, stats);
  if(level >= 2) {
    select_medium_copy_chunks(function, stats);
    combine_adjacent_integer_normalizations(function, stats);
  }
  if(level >= 2) trace_layout(function, stats);
  forward_adjacent_single_use_frame_compares(function, stats);
  std::vector<std::vector<bool> > preserve(function.blocks.size());
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    rewrite_local_operands(function.blocks[i], preserve[i], stats);
  form_zero_tests(function, stats);
  clean_branches(function, stats);
  const std::size_t local_storage = stats ? preserve_bytes(preserve) : 0;
  const ControlFlow cfg = build_control_flow(function, stats, local_storage);
  const std::size_t persistent_analysis_storage = stats ?
    local_storage + control_flow_bytes(cfg) : 0;
  const Liveness liveness = compute_liveness(
    function, cfg, stats, persistent_analysis_storage);
  remove_dead_definitions(function, preserve, liveness, stats);
  // Removing value shuffles can expose one more natural branch tail.
  clean_branches(function, stats);
  const bool recolored =
    recolor_call_free_callee_saved(function, liveness, stats);
  bool block_recolored = false;
  if(level >= 3 && !function.callee_saved_regs.empty()) {
    RegisterMask eliminated_sources = 0;
    RegisterMask anchored_destinations = 0;
    Liveness local_liveness = compute_liveness(
      function, cfg, stats, persistent_analysis_storage);
    while(true) {
      RegisterMask eliminated_source = 0;
      RegisterMask newly_anchored = 0;
      if(!recolor_one_block_local_callee_saved(
           function, local_liveness,
           eliminated_sources | anchored_destinations,
           eliminated_sources, &eliminated_source, &newly_anchored,
           persistent_analysis_storage, stats)) break;
      block_recolored = true;
      eliminated_sources |= eliminated_source;
      anchored_destinations |= newly_anchored;
      local_liveness = compute_liveness(
        function, cfg, stats, persistent_analysis_storage);
    }
  }
  if(level >= 2 || recolored || block_recolored)
    finalize_frame(function, stats);
  select_frame_pointer_policy(function, stats);
  if(level >= 3 && instruction_count(function) >= 64)
    function.code_alignment = 16;
  if(stats) {
    stats->output_instructions += instruction_count(function);
    stats->elapsed_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }
}

void optimize(mir_model::MirProgram & program, int level, Stats * stats)
{
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    optimize_function(program.functions[i], level, stats);
}

}  // namespace machine_opt
}  // namespace lowir_native
