#include "lowir_native.h"
#include "lowir_native_analysis.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lowir_native {
namespace {

using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using mir_model::MirInstruction;
using mir_model::MirOperand;
using analysis::FunctionFacts;
using analysis::analyze_function;
using analysis::register_mask;

long long integer_literal(const std::string & text)
{
  errno = 0;
  char * end = 0;
  const long long value = std::strtoll(text.c_str(), &end, 0);
  if(errno || !end || *end) throw std::runtime_error("invalid integer literal: " + text);
  return value;
}

MirOperand reg_operand(X64Register reg)
{
  MirOperand out;
  out.kind = MirOperand::OP_REG;
  out.reg = reg;
  return out;
}

MirOperand xmm_operand(XmmRegister xmm)
{
  MirOperand out;
  out.kind = MirOperand::OP_XMM;
  out.xmm = xmm;
  return out;
}

MirOperand immediate(long long value)
{
  MirOperand out;
  out.kind = MirOperand::OP_IMM;
  out.imm = value;
  return out;
}

MirOperand float_immediate(const std::string & text)
{
  MirOperand out;
  out.kind = MirOperand::OP_FLOAT_IMM;
  out.text = text;
  return out;
}

MirOperand named_operand(MirOperand::Kind kind, const std::string & text)
{
  MirOperand out;
  out.kind = kind;
  out.text = text;
  return out;
}

MirOperand dereference(X64Register reg, long long offset = 0)
{
  MirOperand out;
  out.kind = MirOperand::OP_DEREF;
  out.reg = reg;
  out.offset = offset;
  return out;
}

MirOperand frame_operand(long long offset)
{
  MirOperand out;
  out.kind = MirOperand::OP_FRAME;
  out.offset = offset;
  return out;
}

MirInstruction machine_instruction(MirInstruction::Opcode opcode,
                                   const std::string & type = std::string())
{
  MirInstruction out;
  out.opcode = opcode;
  out.type = type;
  return out;
}

void append_operand(MirInstruction & instruction, const MirOperand & operand)
{
  instruction.operands.push_back(operand);
}

void append_move(std::vector<MirInstruction> & out,
                 const MirOperand & destination,
                 const MirOperand & source)
{
  if(destination.kind == MirOperand::OP_REG && source.kind == MirOperand::OP_REG &&
     destination.reg == source.reg) return;
  MirInstruction instruction = machine_instruction(MirInstruction::MI_MOV);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_load(std::vector<MirInstruction> & out,
                 const MirOperand & destination,
                 const MirOperand & source,
                 const std::string & type)
{
  MirInstruction instruction = machine_instruction(MirInstruction::MI_LOAD, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_store(std::vector<MirInstruction> & out,
                  const MirOperand & destination,
                  const MirOperand & source,
                  const std::string & type)
{
  MirInstruction instruction = machine_instruction(MirInstruction::MI_STORE, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

void append_float_move(std::vector<MirInstruction> & out,
                       const MirOperand & destination,
                       const MirOperand & source,
                       const std::string & type,
                       bool omit_identity = false)
{
  if(omit_identity && destination.kind == MirOperand::OP_XMM &&
     source.kind == MirOperand::OP_XMM && destination.xmm == source.xmm) return;
  MirInstruction instruction = machine_instruction(MirInstruction::MI_FMOV, type);
  append_operand(instruction, destination);
  append_operand(instruction, source);
  out.push_back(instruction);
}

bool is_callee_saved(X64Register reg)
{
  return reg == XR_RBX || reg == XR_R12 || reg == XR_R13 ||
         reg == XR_R14 || reg == XR_R15;
}

bool is_signed_integer(const LowType & type)
{
  return type.kind == lowir_model::LTK_I8 || type.kind == lowir_model::LTK_I16 ||
         type.kind == lowir_model::LTK_I32 || type.kind == lowir_model::LTK_I64;
}

bool is_integer_or_pointer(const LowType & type)
{
  return (type.kind >= lowir_model::LTK_I1 && type.kind <= lowir_model::LTK_I64) ||
         type.kind == lowir_model::LTK_PTR;
}

bool is_scalar_float(const LowType & type)
{
  return type.kind == lowir_model::LTK_F32 || type.kind == lowir_model::LTK_F64;
}

const LowType & floating_literal_type(const std::string & text)
{
  if(!text.empty() && (text.back() == 'f' || text.back() == 'F'))
    return lowir_model::builtin_lowir_type(lowir_model::LTK_F32);
  if(!text.empty() && (text.back() == 'l' || text.back() == 'L'))
    return lowir_model::builtin_lowir_type(lowir_model::LTK_F80);
  return lowir_model::builtin_lowir_type(lowir_model::LTK_F64);
}

X86Condition predicate_condition(const std::string & predicate)
{
  if(predicate == "eq") return XC_E;
  if(predicate == "ne") return XC_NE;
  if(predicate == "lt") return XC_L;
  if(predicate == "le") return XC_LE;
  if(predicate == "gt") return XC_G;
  if(predicate == "ge") return XC_GE;
  if(predicate == "ult") return XC_B;
  if(predicate == "ule") return XC_BE;
  if(predicate == "ugt") return XC_A;
  if(predicate == "uge") return XC_AE;
  throw std::runtime_error("unsupported integer comparison predicate: " + predicate);
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

struct ValueFact
{
  MirOperand location;
  LowType type;
  bool parameter = false;
  bool frame_address = false;
  bool has_frame_provenance = false;
  long long frame_provenance = 0;
  std::string pointer_global_cell;
};

struct FunctionSignature
{
  const std::vector<lowir_model::LowirParameter> * params = 0;
  const LowType * return_type = 0;
};

typedef std::unordered_map<std::string, FunctionSignature> FunctionSignatureIndex;

enum AbiPieceLocation
{
  APL_GPR,
  APL_XMM,
  APL_STACK
};

struct AbiPiece
{
  std::size_t parameter_index = 0;
  std::size_t chunk_offset = 0;
  LowType type;
  AbiPieceLocation location = APL_GPR;
  X64Register reg = XR_RDI;
  XmmRegister xmm = XMM_0;
  std::size_t stack_offset = 0;
};

struct AbiPlan
{
  std::vector<AbiPiece> pieces;
  std::size_t stack_bytes = 0;
};

const LowType & object_chunk_type(std::size_t remaining)
{
  if(remaining <= 1) return lowir_model::builtin_lowir_type(lowir_model::LTK_I8);
  if(remaining <= 2) return lowir_model::builtin_lowir_type(lowir_model::LTK_I16);
  if(remaining <= 4) return lowir_model::builtin_lowir_type(lowir_model::LTK_I32);
  return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
}

std::size_t frame_storage_size(const LowType & type)
{
  if(type.kind != lowir_model::LTK_OBJECT) return type.storage_size;
  if(type.storage_size <= 1) return 1;
  if(type.storage_size <= 2) return 2;
  if(type.storage_size <= 4) return 4;
  if(type.storage_size <= 8) return 8;
  return align_up(type.storage_size, 8);
}

X64Register abi_argument_register(std::size_t index)
{
  static const X64Register registers[] = {
    XR_RDI, XR_RSI, XR_RDX, XR_RCX, XR_R8, XR_R9
  };
  if(index >= sizeof(registers) / sizeof(registers[0]))
    throw std::runtime_error("SysV ABI has six INTEGER argument registers");
  return registers[index];
}

AbiPlan classify_abi(const std::vector<lowir_model::LowirParameter> & parameters)
{
  AbiPlan plan;
  std::size_t gpr_index = 0;
  std::size_t xmm_index = 0;
  std::size_t stack_offset = 0;
  for(std::size_t i = 0; i < parameters.size(); ++i) {
    const LowType & type = parameters[i].type;
    if(type.kind == lowir_model::LTK_OBJECT) {
      const std::size_t chunks = (type.storage_size + 7) / 8;
      const std::size_t available_gprs = gpr_index < 6 ? 6 - gpr_index : 0;
      const bool in_registers = type.storage_size <= 16 && chunks <= available_gprs;
      if(in_registers) {
        for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
          AbiPiece piece;
          piece.parameter_index = i;
          piece.chunk_offset = chunk * 8;
          piece.type = object_chunk_type(type.storage_size - piece.chunk_offset);
          piece.location = APL_GPR;
          piece.reg = abi_argument_register(gpr_index++);
          plan.pieces.push_back(piece);
        }
      } else {
        const std::size_t stack_alignment = std::min<std::size_t>(
          16, std::max<std::size_t>(8, type.alignment));
        stack_offset = align_up(stack_offset, stack_alignment);
        for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
          AbiPiece piece;
          piece.parameter_index = i;
          piece.chunk_offset = chunk * 8;
          piece.type = object_chunk_type(type.storage_size - piece.chunk_offset);
          piece.location = APL_STACK;
          piece.stack_offset = stack_offset + piece.chunk_offset;
          plan.pieces.push_back(piece);
        }
        stack_offset += align_up(type.storage_size, 8);
      }
      continue;
    }
    AbiPiece piece;
    piece.parameter_index = i;
    piece.type = type;
    if(is_scalar_float(type) && xmm_index < 8) {
      piece.location = APL_XMM;
      piece.xmm = static_cast<XmmRegister>(xmm_index++);
    } else if(!is_scalar_float(type) && gpr_index < 6) {
      piece.location = APL_GPR;
      piece.reg = abi_argument_register(gpr_index++);
    } else {
      piece.location = APL_STACK;
      stack_offset = align_up(stack_offset, 8);
      piece.stack_offset = stack_offset;
      stack_offset += 8;
    }
    plan.pieces.push_back(piece);
  }
  plan.stack_bytes = align_up(stack_offset, 16);
  return plan;
}

class RegisterPool
{
public:
  RegisterPool()
  {
    std::fill(used_, used_ + 16, false);
  }

  void reserve(X64Register reg)
  {
    if(used_[static_cast<unsigned>(reg)])
      throw std::runtime_error("MIR register allocation conflict");
    used_[static_cast<unsigned>(reg)] = true;
    remember_preserve(reg);
  }

  bool is_used(X64Register reg) const
  {
    return used_[static_cast<unsigned>(reg)];
  }

  X64Register allocate(bool across_call)
  {
    X64Register result = XR_RSP;
    if(try_allocate(across_call, result)) return result;
    throw std::runtime_error("foundation register pool exhausted");
  }

  bool try_allocate(bool across_call, X64Register & result)
  {
    static const X64Register ordinary[] = {
      XR_R8, XR_R9, XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
    };
    static const X64Register preserved[] = {
      XR_RBX, XR_R12, XR_R13, XR_R14, XR_R15
    };
    const X64Register * choices = across_call ? preserved : ordinary;
    const std::size_t count = across_call ?
      sizeof(preserved) / sizeof(preserved[0]) :
      sizeof(ordinary) / sizeof(ordinary[0]);
    for(std::size_t i = 0; i < count; ++i) {
      const unsigned index = static_cast<unsigned>(choices[i]);
      if(!used_[index]) {
        used_[index] = true;
        remember_preserve(choices[i]);
        result = choices[i];
        return true;
      }
    }
    return false;
  }

  void release(X64Register reg)
  {
    const unsigned index = static_cast<unsigned>(reg);
    if(reg == XR_R8 || reg == XR_R9 || is_callee_saved(reg)) used_[index] = false;
  }

  const std::vector<X64Register> & preserves() const { return preserves_; }

private:
  bool used_[16];
  std::vector<X64Register> preserves_;

  void remember_preserve(X64Register reg)
  {
    if(is_callee_saved(reg) &&
       std::find(preserves_.begin(), preserves_.end(), reg) == preserves_.end())
      preserves_.push_back(reg);
  }
};

class XmmPool
{
public:
  XmmPool() { std::fill(used_, used_ + 8, false); }

  XmmRegister allocate()
  {
    XmmRegister result = XMM_0;
    if(try_allocate(result)) return result;
    throw std::runtime_error("scalar XMM register pool exhausted");
  }

  bool try_allocate(XmmRegister & result)
  {
    // xmm6/xmm7 remain available to the encoder for immediate and memory forms.
    for(unsigned i = 0; i != 6; ++i) {
      if(used_[i]) continue;
      used_[i] = true;
      result = static_cast<XmmRegister>(i);
      return true;
    }
    return false;
  }

  void release(XmmRegister xmm)
  {
    const unsigned index = static_cast<unsigned>(xmm);
    if(index < 6) used_[index] = false;
  }

private:
  bool used_[8];
};

class FunctionLowerer
{
public:
  FunctionLowerer(const lowir_model::LowirFunction & source,
                  const std::unordered_set<std::string> & pointer_globals,
                  const FunctionSignatureIndex & signatures)
    : source_(source), pointer_globals_(pointer_globals), signatures_(signatures),
      facts_(analyze_function(source)), position_(0)
  {
    target_.name = source.name;
    target_.return_type = source.return_type.text;
    discover_parameter_slot_aliases();
    plan_slots();
    bind_parameters();
  }

  mir_model::MirFunction lower()
  {
    for(std::size_t i = 0; i < source_.blocks.size(); ++i) {
      mir_model::MirBlock block;
      block.label = source_.blocks[i].label;
      if(i == 0) block.instructions.insert(block.instructions.end(),
                                           parameter_moves_.begin(), parameter_moves_.end());
      for(std::size_t j = 0; j < source_.blocks[i].instructions.size(); ++j, ++position_)
        lower_instruction(source_.blocks[i], j, block.instructions);
      target_.blocks.push_back(block);
    }
    target_.callee_saved_regs = registers_.preserves();
    target_.scratch_bytes = uses_scalar_float_ ? 48 : 0;
    std::size_t direct_parameter_bytes = 0;
    for(std::size_t i = 0; i < source_.params.size(); ++i)
      if(source_.params[i].metadata.passing == lowir_model::PPM_DIRECT)
        direct_parameter_bytes += source_.params[i].type.kind == lowir_model::LTK_OBJECT ?
          align_up(source_.params[i].type.storage_size, 8) : 8;
    if(uses_scalar_float_) {
      const std::size_t float_frame_bytes = source_.params.empty() ? frame_bytes_ :
        std::max<std::size_t>(frame_bytes_, 16);
      target_.stack_size = align_up(float_frame_bytes + target_.callee_saved_regs.size() * 8 +
                                    target_.scratch_bytes, 16);
    } else
      target_.stack_size = align_up(
        std::max(std::max(target_.callee_saved_regs.size() * 8,
                          direct_parameter_bytes), frame_bytes_), 16);
    return target_;
  }

private:
  const lowir_model::LowirFunction & source_;
  const std::unordered_set<std::string> & pointer_globals_;
  const FunctionSignatureIndex & signatures_;
  FunctionFacts facts_;
  mir_model::MirFunction target_;
  RegisterPool registers_;
  XmmPool xmms_;
  std::unordered_map<std::string, ValueFact> values_;
  std::unordered_map<std::string, long long> slot_offsets_;
  std::unordered_map<std::string, LowType> slot_types_;
  std::unordered_map<std::string, std::string> parameter_slot_aliases_;
  std::unordered_set<std::string> discarded_slots_;
  std::unordered_map<std::string, long long> spill_offsets_;
  std::vector<MirInstruction> parameter_moves_;
  const Instruction * active_instruction_ = 0;
  std::size_t position_;
  std::size_t skipped_position_ = static_cast<std::size_t>(-1);
  std::size_t frame_bytes_ = 0;
  bool uses_scalar_float_ = false;
  bool has_xmm_call_scratch_ = false;
  long long xmm_call_scratch_ = 0;

  long long allocate_frame_binding(mir_model::MirFrameBinding::Kind kind,
                                   const std::string & name,
                                   const LowType & type)
  {
    frame_bytes_ = align_up(frame_bytes_, type.alignment);
    frame_bytes_ += frame_storage_size(type);
    mir_model::MirFrameBinding binding;
    binding.kind = kind;
    binding.name = name;
    binding.offset = -static_cast<long long>(frame_bytes_);
    binding.type = type.text;
    target_.frame_bindings.push_back(binding);
    return binding.offset;
  }

  void discover_parameter_slot_aliases()
  {
    for(std::size_t p = 0; p < source_.params.size(); ++p) {
      const lowir_model::LowirParameter & parameter = source_.params[p];
      if(parameter.type.kind != lowir_model::LTK_OBJECT) continue;
      for(std::size_t s = 0; s < source_.slots.size(); ++s) {
        const std::string & slot_name = source_.slots[s].first;
        if(!lowir_model::same_lowir_type(parameter.type, source_.slots[s].second) ||
           parameter.name.size() < 2 || slot_name.size() < 2 ||
           parameter.name.substr(1) != slot_name.substr(1)) continue;
        bool found_copy = false;
        bool slot_seen = false;
        for(std::size_t b = 0; b < source_.blocks.size() && !found_copy; ++b) {
          const std::vector<Instruction> & instructions = source_.blocks[b].instructions;
          for(std::size_t i = 0; i < instructions.size(); ++i) {
            const Instruction & current = instructions[i];
            const bool mentions_slot =
              (current.first.kind == Operand::OP_SLOT && current.first.text == slot_name) ||
              (current.second.kind == Operand::OP_SLOT && current.second.text == slot_name) ||
              (current.third.kind == Operand::OP_SLOT && current.third.text == slot_name);
            if(!mentions_slot) continue;
            if(slot_seen || current.kind != Instruction::IK_ADDR ||
               i + 1 >= instructions.size()) break;
            slot_seen = true;
            const Instruction & copy = instructions[i + 1];
            found_copy = copy.kind == Instruction::IK_COPYOBJ &&
              copy.first.kind == Operand::OP_TEMP && copy.first.text == parameter.name &&
              copy.second.kind == Operand::OP_TEMP && copy.second.text == current.dest &&
              copy.byte_count == parameter.type.storage_size;
            break;
          }
        }
        if(found_copy) parameter_slot_aliases_[slot_name] = parameter.name;
      }
    }
  }

  void plan_slots()
  {
    for(std::size_t i = 0; i < source_.slots.size(); ++i) {
      const std::string & name = source_.slots[i].first;
      const LowType & type = source_.slots[i].second;
      slot_types_[name] = type;
      if(parameter_slot_aliases_.count(name)) continue;
      bool written = false;
      bool observed = false;
      for(std::size_t b = 0; b < source_.blocks.size(); ++b)
        for(std::size_t j = 0; j < source_.blocks[b].instructions.size(); ++j) {
          const Instruction & instruction = source_.blocks[b].instructions[j];
          const bool first = instruction.first.kind == Operand::OP_SLOT &&
            instruction.first.text == name;
          const bool second = instruction.second.kind == Operand::OP_SLOT &&
            instruction.second.text == name;
          const bool third = instruction.third.kind == Operand::OP_SLOT &&
            instruction.third.text == name;
          bool argument = false;
          for(std::size_t a = 0; a < instruction.args.size(); ++a)
            if(instruction.args[a].kind == Operand::OP_SLOT &&
               instruction.args[a].text == name) argument = true;
          if(instruction.kind == Instruction::IK_STORE && second && !first && !third)
            written = true;
          else if(first || second || third || argument)
            observed = true;
        }
      if(written && !observed) {
        discarded_slots_.insert(name);
        frame_bytes_ = align_up(frame_bytes_, type.alignment);
        frame_bytes_ += frame_storage_size(type);
        continue;
      }
      slot_offsets_[name] = allocate_frame_binding(
        mir_model::MirFrameBinding::FB_SLOT, name, type);
    }
  }

  static X64Register argument_register(std::size_t index)
  {
    return abi_argument_register(index);
  }

  bool crosses_call(const std::string & name) const
  {
    return facts_.live_across_call.count(name) != 0;
  }

  bool crosses_register_clobber(const std::string & name, X64Register reg) const
  {
    const std::unordered_map<std::string, unsigned>::const_iterator found =
      facts_.live_across_clobbers.find(name);
    return found != facts_.live_across_clobbers.end() &&
      (found->second & register_mask(reg)) != 0;
  }

  void bind_mixed_parameters()
  {
    uses_scalar_float_ = true;
    std::size_t gpr_parameters = 0;
    std::size_t float_parameters = 0;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(is_scalar_float(source_.params[i].type)) ++float_parameters;
      else ++gpr_parameters;
    }
    if(gpr_parameters >= 2 && float_parameters)
      registers_.reserve(XR_R8); // fixed conversion/setup scratch at mixed boundaries
    std::size_t gpr_index = 0;
    std::size_t xmm_index = 0;
    std::size_t stack_index = 0;
    std::vector<MirInstruction> gpr_parameter_moves;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name;
      binding.type = parameter.type.text;
      ValueFact value;
      value.type = parameter.type;
      value.parameter = true;
      if(is_scalar_float(parameter.type) && xmm_index < 8) {
        binding.location = mir_model::MirParamBinding::PL_XMM;
        binding.xmm = static_cast<XmmRegister>(xmm_index++);
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_float_move(parameter_moves_, frame_operand(home),
                          xmm_operand(binding.xmm), parameter.type.text);
        value.location = frame_operand(home);
      } else if(!is_scalar_float(parameter.type) && gpr_index < 6) {
        binding.location = mir_model::MirParamBinding::PL_REG;
        const std::size_t parameter_gpr_index = gpr_index++;
        binding.reg = argument_register(parameter_gpr_index);
        value.location = reg_operand(binding.reg);
        const std::size_t uses = facts_.uses[parameter.name];
        if(crosses_register_clobber(parameter.name, binding.reg) ||
           (parameter_gpr_index != 0 && uses) ||
           (parameter.type.kind == lowir_model::LTK_PTR && uses > 1)) {
          const X64Register destination = registers_.allocate(true);
          value.location = reg_operand(destination);
          append_move(gpr_parameter_moves, value.location, reg_operand(binding.reg));
        } else if(uses && crosses_call(parameter.name)) {
          const X64Register destination = registers_.allocate(true);
          value.location = reg_operand(destination);
          append_move(gpr_parameter_moves, value.location, reg_operand(binding.reg));
        }
      } else {
        binding.location = mir_model::MirParamBinding::PL_STACK;
        binding.stack_offset = 16 + static_cast<long long>(stack_index++ * 8);
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        if(is_scalar_float(parameter.type))
          append_float_move(parameter_moves_, frame_operand(home),
                            frame_operand(binding.stack_offset), parameter.type.text);
        else {
          append_load(parameter_moves_, reg_operand(XR_RAX),
                      frame_operand(binding.stack_offset), parameter.type.text);
          append_store(parameter_moves_, frame_operand(home), reg_operand(XR_RAX),
                       parameter.type.text);
        }
        value.location = frame_operand(home);
      }
      target_.params.push_back(binding);
      values_[parameter.name] = value;
    }
    parameter_moves_.insert(parameter_moves_.end(),
                            gpr_parameter_moves.begin(), gpr_parameter_moves.end());
  }

  void bind_aggregate_parameters()
  {
    const AbiPlan plan = classify_abi(source_.params);
    std::vector<long long> homes(source_.params.size(), 0);
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      homes[i] = allocate_frame_binding(
        mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
      ValueFact value;
      value.type = parameter.type;
      value.parameter = true;
      value.location = frame_operand(homes[i]);
      if(parameter.type.kind == lowir_model::LTK_OBJECT) {
        value.frame_address = true;
        value.has_frame_provenance = true;
        value.frame_provenance = homes[i];
      }
      values_[parameter.name] = value;
      for(std::unordered_map<std::string, std::string>::const_iterator alias =
            parameter_slot_aliases_.begin(); alias != parameter_slot_aliases_.end(); ++alias)
        if(alias->second == parameter.name) slot_offsets_[alias->first] = homes[i];
    }
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const AbiPiece & piece = plan.pieces[i];
      const lowir_model::LowirParameter & parameter =
        source_.params[piece.parameter_index];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name;
      binding.type = parameter.type.kind == lowir_model::LTK_OBJECT ?
        piece.type.text : parameter.type.text;
      binding.chunk_offset = static_cast<long long>(piece.chunk_offset);
      const MirOperand home = frame_operand(
        homes[piece.parameter_index] + static_cast<long long>(piece.chunk_offset));
      if(piece.location == APL_GPR) {
        binding.location = mir_model::MirParamBinding::PL_REG;
        binding.reg = piece.reg;
        append_store(parameter_moves_, home, reg_operand(piece.reg), piece.type.text);
      } else if(piece.location == APL_XMM) {
        binding.location = mir_model::MirParamBinding::PL_XMM;
        binding.xmm = piece.xmm;
        uses_scalar_float_ = true;
        append_float_move(parameter_moves_, home, xmm_operand(piece.xmm), piece.type.text);
      } else {
        binding.location = mir_model::MirParamBinding::PL_STACK;
        binding.stack_offset = 16 + static_cast<long long>(piece.stack_offset);
        if(is_scalar_float(piece.type)) {
          uses_scalar_float_ = true;
          append_float_move(parameter_moves_, home,
                            frame_operand(binding.stack_offset), piece.type.text);
        } else {
          append_load(parameter_moves_, reg_operand(XR_RAX),
                      frame_operand(binding.stack_offset), piece.type.text);
          append_store(parameter_moves_, home, reg_operand(XR_RAX), piece.type.text);
        }
      }
      target_.params.push_back(binding);
    }
  }

  void bind_parameters()
  {
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(source_.params[i].type.kind != lowir_model::LTK_OBJECT) continue;
      bind_aggregate_parameters();
      return;
    }
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(!is_scalar_float(source_.params[i].type)) continue;
      bind_mixed_parameters();
      return;
    }
    const bool wide_gpr_boundary = source_.params.size() >= 6;
    bool incoming_pool_reserved[6] = {false, false, false, false, false, false};
    if(!wide_gpr_boundary) {
      for(std::size_t i = 4; i < source_.params.size() && i < 6; ++i) {
        if(facts_.uses[source_.params[i].name] == 0) continue;
        registers_.reserve(argument_register(i));
        incoming_pool_reserved[i] = true;
      }
    }
    bool home_unused_register_parameters = wide_gpr_boundary;
    for(std::size_t i = 0; i < source_.params.size() && i < 6; ++i)
      if(facts_.uses[source_.params[i].name] != 0)
        home_unused_register_parameters = false;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name;
      binding.type = parameter.type.text;
      ValueFact value;
      value.type = parameter.type;
      value.parameter = true;
      if(i >= 6) {
        binding.location = mir_model::MirParamBinding::PL_STACK;
        binding.stack_offset = 16 + static_cast<long long>((i - 6) * 8);
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_load(parameter_moves_, reg_operand(XR_RAX),
                    frame_operand(binding.stack_offset), parameter.type.text);
        append_store(parameter_moves_, frame_operand(home), reg_operand(XR_RAX),
                     parameter.type.text);
        value.location = frame_operand(home);
        target_.params.push_back(binding);
        values_[parameter.name] = value;
        continue;
      }
      binding.location = mir_model::MirParamBinding::PL_REG;
      binding.reg = argument_register(i);
      target_.params.push_back(binding);
      value.location = reg_operand(binding.reg);
      const std::size_t uses = facts_.uses[parameter.name];
      if(home_unused_register_parameters) {
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(binding.reg),
                     parameter.type.text);
        value.location = frame_operand(home);
      } else if(wide_gpr_boundary && crosses_call(parameter.name)) {
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(binding.reg),
                     parameter.type.text);
        value.location = frame_operand(home);
      } else if(!wide_gpr_boundary &&
                parameter.metadata.passing != lowir_model::PPM_DIRECT && uses) {
        const X64Register destination = registers_.allocate(crosses_call(parameter.name));
        value.location = reg_operand(destination);
        append_move(parameter_moves_, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary &&
                crosses_register_clobber(parameter.name, binding.reg)) {
        const X64Register destination = registers_.allocate(crosses_call(parameter.name));
        value.location = reg_operand(destination);
        append_move(parameter_moves_, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && parameter.type.kind == lowir_model::LTK_PTR && uses > 1) {
        const X64Register destination = registers_.allocate(true);
        value.location = reg_operand(destination);
        append_move(parameter_moves_, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && uses > 1) {
        const X64Register destination = registers_.allocate(crosses_call(parameter.name));
        value.location = reg_operand(destination);
        append_move(parameter_moves_, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && binding.reg == XR_RDX && uses &&
                facts_.first_use[parameter.name] > 0) {
        const X64Register destination = registers_.is_used(XR_R9) ?
          registers_.allocate(crosses_call(parameter.name)) : XR_R9;
        if(destination == XR_R9) registers_.reserve(XR_R9);
        value.location = reg_operand(destination);
        append_move(parameter_moves_, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && uses && crosses_call(parameter.name)) {
        const X64Register destination = registers_.allocate(true);
        value.location = reg_operand(destination);
        append_move(parameter_moves_, value.location, reg_operand(binding.reg));
      }
      if(incoming_pool_reserved[i] &&
         (value.location.kind != MirOperand::OP_REG || value.location.reg != binding.reg))
        registers_.release(binding.reg);
      values_[parameter.name] = value;
    }
    if(!wide_gpr_boundary || home_unused_register_parameters) return;
    static const std::size_t order[] = {2, 3, 4, 5, 1, 0};
    static const X64Register destinations[] = {
      XR_R15, XR_R9, XR_RBX, XR_R12, XR_R13, XR_R14
    };
    for(std::size_t step = 0; step < sizeof(order) / sizeof(order[0]); ++step) {
      const std::size_t index = order[step];
      if(index >= source_.params.size()) continue;
      const std::string & name = source_.params[index].name;
      if(facts_.uses[name] == 0 || values_[name].location.kind == MirOperand::OP_FRAME)
        continue;
      const X64Register destination = destinations[index];
      registers_.reserve(destination);
      append_move(parameter_moves_, reg_operand(destination),
                  reg_operand(argument_register(index)));
      values_[name].location = reg_operand(destination);
      values_[name].parameter = false;
    }
  }

  bool result_is_immediate_return(const lowir_model::LowirBlock & block,
                                  std::size_t instruction_index,
                                  const std::string & destination) const
  {
    if(facts_.uses.find(destination) == facts_.uses.end() ||
       facts_.uses.find(destination)->second != 1 ||
       instruction_index + 1 >= block.instructions.size()) return false;
    const Instruction & next = block.instructions[instruction_index + 1];
    return next.kind == Instruction::IK_RETURN && next.first.text == destination;
  }

  bool result_is_immediate_unary_not_branch(const lowir_model::LowirBlock & block,
                                            std::size_t instruction_index,
                                            const std::string & destination) const
  {
    if(instruction_index + 2 >= block.instructions.size() ||
       facts_.uses.find(destination) == facts_.uses.end() ||
       facts_.uses.find(destination)->second != 1) return false;
    const Instruction & unary = block.instructions[instruction_index + 1];
    const Instruction & branch = block.instructions[instruction_index + 2];
    return unary.kind == Instruction::IK_UNARY && unary.op == "not" &&
      unary.first.text == destination && branch.kind == Instruction::IK_BRANCH &&
      branch.first.text == unary.dest;
  }

  bool result_is_next_call_address_argument(const lowir_model::LowirBlock & block,
                                            std::size_t instruction_index,
                                            const Instruction & producer) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       producer.type.kind == lowir_model::LTK_PTR) return false;
    const Instruction & call = block.instructions[instruction_index + 1];
    if(call.kind != Instruction::IK_CALL) return false;
    std::vector<lowir_model::LowirParameter> parameters;
    if(call.has_call_signature) parameters = call.call_params;
    else if(call.first.kind == Operand::OP_GLOBAL) {
      const FunctionSignatureIndex::const_iterator found = signatures_.find(call.first.text);
      if(found != signatures_.end() && found->second.params)
        parameters = *found->second.params;
    }
    for(std::size_t i = 0; i < call.args.size() && i < parameters.size(); ++i)
      if(call.args[i].kind == Operand::OP_TEMP &&
         call.args[i].text == producer.dest &&
         parameters[i].metadata.passing != lowir_model::PPM_DIRECT)
        return true;
    return false;
  }

  bool result_is_immediately_stored(const lowir_model::LowirBlock & block,
                                    std::size_t instruction_index,
                                    const std::string & destination) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       facts_.uses.find(destination) == facts_.uses.end() ||
       facts_.uses.find(destination)->second != 1) return false;
    const Instruction & store = block.instructions[instruction_index + 1];
    return store.kind == Instruction::IK_STORE &&
      store.first.kind == Operand::OP_TEMP && store.first.text == destination;
  }

  bool address_is_immediately_loaded(const lowir_model::LowirBlock & block,
                                     std::size_t instruction_index,
                                     const std::string & destination) const
  {
    if(instruction_index + 1 >= block.instructions.size()) return false;
    const Instruction & next = block.instructions[instruction_index + 1];
    return next.kind == Instruction::IK_LOAD && next.first.text == destination &&
           facts_.uses.find(destination) != facts_.uses.end() &&
           facts_.uses.find(destination)->second == 1;
  }

  bool address_is_immediately_stored(const lowir_model::LowirBlock & block,
                                     std::size_t instruction_index,
                                     const std::string & destination) const
  {
    if(instruction_index + 1 >= block.instructions.size()) return false;
    const Instruction & next = block.instructions[instruction_index + 1];
    return next.kind == Instruction::IK_STORE && next.second.text == destination &&
      facts_.uses.find(destination) != facts_.uses.end() &&
      facts_.uses.find(destination)->second == 1;
  }

  bool address_is_next_bulk_operand(const lowir_model::LowirBlock & block,
                                    std::size_t instruction_index,
                                    const std::string & destination) const
  {
    if(instruction_index + 1 >= block.instructions.size()) return false;
    const Instruction & next = block.instructions[instruction_index + 1];
    if(next.kind == Instruction::IK_ZEROINIT)
      return next.first.kind == Operand::OP_TEMP && next.first.text == destination;
    if(next.kind != Instruction::IK_COPYOBJ) return false;
    return (next.first.kind == Operand::OP_TEMP && next.first.text == destination) ||
      (next.second.kind == Operand::OP_TEMP && next.second.text == destination);
  }

  bool address_precedes_elided_copy(const lowir_model::LowirBlock & block,
                                    std::size_t instruction_index,
                                    const std::string & destination) const
  {
    if(instruction_index + 1 >= block.instructions.size()) return false;
    const Instruction & next = block.instructions[instruction_index + 1];
    if(next.kind != Instruction::IK_COPYOBJ ||
       next.second.kind != Operand::OP_TEMP || next.second.text != destination)
      return false;
    long long source_offset = 0;
    long long destination_offset = 0;
    return frame_provenance(next.first, source_offset) &&
      frame_provenance(block.instructions[instruction_index].first,
                       destination_offset) &&
      source_offset == destination_offset;
  }

  bool address_is_next_call_argument(const lowir_model::LowirBlock & block,
                                     std::size_t instruction_index,
                                     const std::string & destination) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       facts_.uses.find(destination) == facts_.uses.end() ||
       facts_.uses.find(destination)->second != 1) return false;
    const Instruction & next = block.instructions[instruction_index + 1];
    if(next.kind != Instruction::IK_CALL) return false;
    for(std::size_t i = 0; i < next.args.size(); ++i)
      if(next.args[i].kind == Operand::OP_TEMP && next.args[i].text == destination)
        return true;
    return false;
  }

  bool address_is_object_result_destination(const lowir_model::LowirBlock & block,
                                            std::size_t instruction_index,
                                            const std::string & destination) const
  {
    if(instruction_index + 2 >= block.instructions.size()) return false;
    const Instruction & call = block.instructions[instruction_index + 1];
    const Instruction & copy = block.instructions[instruction_index + 2];
    return call.kind == Instruction::IK_CALL &&
      !call.call_returns_void && call.type.kind == lowir_model::LTK_OBJECT &&
      copy.kind == Instruction::IK_COPYOBJ &&
      copy.first.kind == Operand::OP_TEMP && copy.first.text == call.dest &&
      copy.second.kind == Operand::OP_TEMP && copy.second.text == destination;
  }

  X64Register direct_slot_address_register(const lowir_model::LowirBlock & block,
                                           std::size_t instruction_index,
                                           const std::string & destination) const
  {
    for(std::size_t i = instruction_index + 1;
        i < block.instructions.size() && i <= instruction_index + 2; ++i) {
      const Instruction & comparison = block.instructions[i];
      if(comparison.kind != Instruction::IK_CMP) continue;
      if(!comparison_feeds_branch(block, i, comparison)) break;
      if(comparison.first.text == destination) return XR_RAX;
      if(comparison.second.text == destination) return XR_RDX;
    }
    return XR_RSP;
  }

  bool slot_load_feeds_direct_compare(const lowir_model::LowirBlock & block,
                                      std::size_t instruction_index,
                                      const Instruction & load) const
  {
    if(load.first.kind != Operand::OP_SLOT ||
       instruction_index + 2 >= block.instructions.size()) return false;
    const Instruction & comparison = block.instructions[instruction_index + 1];
    return comparison.kind == Instruction::IK_CMP &&
      comparison.first.text == load.dest &&
      comparison_feeds_branch(block, instruction_index + 1, comparison) &&
      facts_.uses.find(load.dest) != facts_.uses.end() &&
      facts_.uses.find(load.dest)->second == 1;
  }

  MirOperand resolve(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_TEMP) {
      const std::unordered_map<std::string, ValueFact>::const_iterator found =
        values_.find(operand.text);
      if(found == values_.end()) throw std::runtime_error("missing lowered temporary");
      return found->second.location;
    }
    if(operand.kind == Operand::OP_SLOT) {
      const std::unordered_map<std::string, long long>::const_iterator found =
        slot_offsets_.find(operand.text);
      if(found == slot_offsets_.end()) throw std::runtime_error("missing frame slot");
      return frame_operand(found->second);
    }
    if(operand.kind == Operand::OP_INTEGER) return immediate(integer_literal(operand.text));
    if(operand.kind == Operand::OP_FLOAT) return float_immediate(operand.text);
    if(operand.kind == Operand::OP_GLOBAL)
      return named_operand(MirOperand::OP_SYMBOL, operand.text);
    if(operand.kind == Operand::OP_LABEL)
      return named_operand(MirOperand::OP_LABEL, operand.text);
    throw std::runtime_error("foundation operand is not implemented: " + operand.text);
  }

  const LowType & operand_type(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_TEMP) {
      const std::unordered_map<std::string, ValueFact>::const_iterator found =
        values_.find(operand.text);
      if(found == values_.end()) throw std::runtime_error("missing operand type");
      return found->second.type;
    }
    if(operand.kind == Operand::OP_SLOT) {
      const std::unordered_map<std::string, LowType>::const_iterator found =
        slot_types_.find(operand.text);
      if(found == slot_types_.end()) throw std::runtime_error("missing slot type");
      return found->second;
    }
    if(operand.kind == Operand::OP_GLOBAL)
      return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
    if(operand.kind == Operand::OP_FLOAT) {
      return floating_literal_type(operand.text);
    }
    return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  }

  void move_value_to_register(std::vector<MirInstruction> & out,
                              X64Register destination,
                              const MirOperand & source,
                              const LowType & type)
  {
    const MirOperand target = reg_operand(destination);
    if(source.kind == MirOperand::OP_FRAME || source.kind == MirOperand::OP_GLOBAL ||
       source.kind == MirOperand::OP_DEREF)
      append_load(out, target, source, type.text);
    else append_move(out, target, source);
  }

  MirOperand storage(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_GLOBAL)
      return named_operand(MirOperand::OP_GLOBAL, operand.text);
    if(operand.kind == Operand::OP_SLOT) {
      const std::unordered_map<std::string, long long>::const_iterator found =
        slot_offsets_.find(operand.text);
      if(found == slot_offsets_.end()) throw std::runtime_error("missing frame slot");
      return frame_operand(found->second);
    }
    const MirOperand address = resolve(operand);
    if(address.kind != MirOperand::OP_REG)
      throw std::runtime_error("storage address is not register-resident");
    return dereference(address.reg);
  }

  MirOperand materialized_storage(const Operand & operand,
                                  std::vector<MirInstruction> & out)
  {
    if(operand.kind == Operand::OP_GLOBAL || operand.kind == Operand::OP_SLOT)
      return storage(operand);
    if(is_frame_address(operand)) {
      append_address(out, XR_RCX, resolve(operand));
      return dereference(XR_RCX);
    }
    const MirOperand address = resolve(operand);
    if(address.kind == MirOperand::OP_REG) return dereference(address.reg);
    move_value_to_register(out, XR_RCX, address, operand_type(operand));
    return dereference(XR_RCX);
  }

  bool can_reuse(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP) return false;
    const std::unordered_map<std::string, std::size_t>::const_iterator use =
      facts_.uses.find(operand.text);
    const std::unordered_map<std::string, ValueFact>::const_iterator value =
      values_.find(operand.text);
    return use != facts_.uses.end() && use->second == 1 &&
           value != values_.end() && !value->second.parameter &&
           !facts_.edge_live.count(operand.text) &&
           value->second.location.kind == MirOperand::OP_REG;
  }

  void consume(const Operand & operand, X64Register retained = XR_RSP)
  {
    if(operand.kind != Operand::OP_TEMP) return;
    std::unordered_map<std::string, std::size_t>::iterator found = facts_.uses.find(operand.text);
    if(found == facts_.uses.end() || found->second == 0)
      throw std::runtime_error("invalid temporary use count");
    --found->second;
    if(found->second == 0) {
      const ValueFact & value = values_.find(operand.text)->second;
      if(value.location.kind == MirOperand::OP_REG &&
         value.location.reg != retained && value.location.reg != XR_RAX)
        registers_.release(value.location.reg);
      if(!value.parameter && value.location.kind == MirOperand::OP_XMM)
        xmms_.release(value.location.xmm);
    }
  }

  bool current_instruction_uses(const std::string & name) const
  {
    if(!active_instruction_) return false;
    const Operand * fixed[] = {
      &active_instruction_->first, &active_instruction_->second,
      &active_instruction_->third
    };
    for(std::size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
      if(fixed[i]->kind == Operand::OP_TEMP && fixed[i]->text == name) return true;
    for(std::size_t i = 0; i < active_instruction_->args.size(); ++i)
      if(active_instruction_->args[i].kind == Operand::OP_TEMP &&
         active_instruction_->args[i].text == name) return true;
    return false;
  }

  static bool managed_register(X64Register reg)
  {
    return reg == XR_R8 || reg == XR_R9 || is_callee_saved(reg);
  }

  bool spill_one(bool needs_callee_saved, std::vector<MirInstruction> & out)
  {
    std::unordered_map<std::string, ValueFact>::iterator victim = values_.end();
    std::size_t farthest_use = 0;
    for(std::unordered_map<std::string, ValueFact>::iterator value = values_.begin();
        value != values_.end(); ++value) {
      if(value->second.location.kind != MirOperand::OP_REG ||
         !managed_register(value->second.location.reg) ||
         (needs_callee_saved && !is_callee_saved(value->second.location.reg)) ||
         current_instruction_uses(value->first)) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator uses =
        facts_.uses.find(value->first);
      if(uses == facts_.uses.end() || uses->second == 0) continue;
      const std::size_t last = facts_.last_use.count(value->first) ?
        facts_.last_use.find(value->first)->second : 0;
      if(victim == values_.end() || last >= farthest_use) {
        victim = value;
        farthest_use = last;
      }
    }
    if(victim == values_.end()) return false;
    long long home = 0;
    const std::unordered_map<std::string, long long>::const_iterator existing =
      spill_offsets_.find(victim->first);
    if(existing != spill_offsets_.end()) home = existing->second;
    else {
      home = allocate_frame_binding(victim->second.parameter ?
        mir_model::MirFrameBinding::FB_PARAM_SLOT :
        mir_model::MirFrameBinding::FB_TEMP, victim->first, victim->second.type);
      spill_offsets_[victim->first] = home;
    }
    append_store(out, frame_operand(home), victim->second.location,
                 victim->second.type.text);
    const X64Register released = victim->second.location.reg;
    victim->second.location = frame_operand(home);
    registers_.release(released);
    return true;
  }

  X64Register allocate_result(const std::string & name,
                              std::vector<MirInstruction> & out,
                              bool force_preserved = false)
  {
    const bool across = force_preserved ||
      (facts_.last_use.count(name) && crosses_call(name));
    X64Register result = XR_RSP;
    if(registers_.try_allocate(across, result)) return result;
    if(spill_one(across, out) && registers_.try_allocate(across, result)) return result;
    throw std::runtime_error("reactive GPR allocation exhausted");
  }

  MirOperand allocate_temp_home(const std::string & name, const LowType & type)
  {
    return frame_operand(allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, name, type));
  }

  MirOperand allocate_float_result(const std::string & name, const LowType & type)
  {
    uses_scalar_float_ = true;
    if(result_crosses_call(name)) return allocate_temp_home(name, type);
    XmmRegister result = XMM_0;
    if(xmms_.try_allocate(result)) return xmm_operand(result);
    return allocate_temp_home(name, type);
  }

  void define(const std::string & name, const LowType & type, const MirOperand & location)
  {
    ValueFact value;
    value.location = location;
    value.type = type;
    values_[name] = value;
  }

  bool is_frame_address(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP) return false;
    const std::unordered_map<std::string, ValueFact>::const_iterator found =
      values_.find(operand.text);
    return found != values_.end() && found->second.frame_address;
  }

  void append_address(std::vector<MirInstruction> & out,
                      X64Register destination,
                      const MirOperand & source)
  {
    MirInstruction lea = machine_instruction(MirInstruction::MI_LEA);
    append_operand(lea, reg_operand(destination));
    append_operand(lea, source);
    out.push_back(lea);
  }

  void emit_operand_address(std::vector<MirInstruction> & out,
                            X64Register destination,
                            const Operand & operand)
  {
    if(operand.kind == Operand::OP_SLOT) {
      append_address(out, destination, storage(operand));
      return;
    }
    if(operand.kind == Operand::OP_GLOBAL) {
      append_move(out, reg_operand(destination),
                  named_operand(MirOperand::OP_SYMBOL, operand.text));
      return;
    }
    if(operand.kind != Operand::OP_TEMP)
      throw std::runtime_error("bulk object operand is not addressable");
    const std::unordered_map<std::string, ValueFact>::const_iterator found =
      values_.find(operand.text);
    if(found == values_.end()) throw std::runtime_error("missing address value");
    const MirOperand & location = found->second.location;
    if(found->second.frame_address || found->second.type.kind == lowir_model::LTK_OBJECT) {
      if(location.kind == MirOperand::OP_FRAME || location.kind == MirOperand::OP_DEREF) {
        append_address(out, destination, location);
        return;
      }
      if(location.kind == MirOperand::OP_GLOBAL || location.kind == MirOperand::OP_SYMBOL) {
        append_move(out, reg_operand(destination),
                    named_operand(MirOperand::OP_SYMBOL, location.text));
        return;
      }
    }
    move_value_to_register(out, destination, location,
                           lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
  }

  MirOperand make_addressable(const Operand & operand,
                              std::vector<MirInstruction> & out)
  {
    if(operand.kind == Operand::OP_SLOT) return storage(operand);
    if(operand.kind == Operand::OP_GLOBAL)
      return named_operand(MirOperand::OP_SYMBOL, operand.text);
    if(operand.kind != Operand::OP_TEMP)
      throw std::runtime_error("call argument cannot be passed by address");
    std::unordered_map<std::string, ValueFact>::iterator found = values_.find(operand.text);
    if(found == values_.end()) throw std::runtime_error("missing addressable temporary");
    if(found->second.frame_address || found->second.location.kind == MirOperand::OP_FRAME)
      return found->second.location;
    const MirOperand home = allocate_temp_home(operand.text, found->second.type);
    if(found->second.location.kind == MirOperand::OP_XMM)
      append_float_move(out, home, found->second.location, found->second.type.text);
    else
      append_store(out, home, found->second.location, found->second.type.text);
    if(found->second.location.kind == MirOperand::OP_REG)
      registers_.release(found->second.location.reg);
    else if(found->second.location.kind == MirOperand::OP_XMM)
      xmms_.release(found->second.location.xmm);
    found->second.location = home;
    spill_offsets_[operand.text] = home.offset;
    return home;
  }

  bool frame_provenance(const Operand & operand, long long & offset) const
  {
    if(operand.kind == Operand::OP_SLOT) {
      const std::unordered_map<std::string, long long>::const_iterator found =
        slot_offsets_.find(operand.text);
      if(found == slot_offsets_.end()) return false;
      offset = found->second;
      return true;
    }
    if(operand.kind != Operand::OP_TEMP) return false;
    const std::unordered_map<std::string, ValueFact>::const_iterator found =
      values_.find(operand.text);
    if(found == values_.end() || !found->second.has_frame_provenance) return false;
    offset = found->second.frame_provenance;
    return true;
  }

  bool aliases_same_object(const Operand & source, const Operand & destination) const
  {
    long long source_offset = 0;
    long long destination_offset = 0;
    return frame_provenance(source, source_offset) &&
      frame_provenance(destination, destination_offset) &&
      source_offset == destination_offset;
  }

  bool result_crosses_call(const std::string & name) const
  {
    return facts_.last_use.count(name) && crosses_call(name);
  }

  void normalize_integer(const LowType & type, const MirOperand & destination,
                         std::vector<MirInstruction> & out)
  {
    if(type.kind == lowir_model::LTK_PTR || type.bit_width >= 64) return;
    const std::string width_type = "i" + std::to_string(type.bit_width);
    MirInstruction normalize = machine_instruction(is_signed_integer(type) ?
      MirInstruction::MI_SEXT : MirInstruction::MI_ZEXT, width_type);
    append_operand(normalize, destination);
    out.push_back(normalize);
  }

  MirOperand binary_destination(const Instruction & instruction,
                                const MirOperand & left,
                                std::vector<MirInstruction> & out,
                                bool allow_same_instruction_duplicate = false)
  {
    const bool duplicate_last_use = allow_same_instruction_duplicate &&
      instruction.first.kind == Operand::OP_TEMP &&
      instruction.first.text == instruction.second.text &&
      facts_.uses.find(instruction.first.text) != facts_.uses.end() &&
      facts_.uses.find(instruction.first.text)->second == 2 &&
      !values_.find(instruction.first.text)->second.parameter;
    const bool safe_reuse = (can_reuse(instruction.first) || duplicate_last_use) &&
      (!result_crosses_call(instruction.dest) || is_callee_saved(left.reg));
    if(safe_reuse) return left;
    const MirOperand destination = reg_operand(allocate_result(instruction.dest, out));
    move_value_to_register(out, destination.reg, left, operand_type(instruction.first));
    if(left.kind == MirOperand::OP_FRAME || left.kind == MirOperand::OP_GLOBAL ||
       left.kind == MirOperand::OP_DEREF)
      normalize_integer(operand_type(instruction.first), destination, out);
    return destination;
  }

  MirInstruction::Opcode float_binary_opcode(const std::string & operation) const
  {
    if(operation == "add") return MirInstruction::MI_FADD;
    if(operation == "sub") return MirInstruction::MI_FSUB;
    if(operation == "mul") return MirInstruction::MI_FMUL;
    if(operation == "div") return MirInstruction::MI_FDIV;
    throw std::runtime_error("floating binary operation is not implemented: " + operation);
  }

  MirInstruction::Opcode float_compare_opcode(const std::string & predicate) const
  {
    if(predicate == "eq") return MirInstruction::MI_FEQ;
    if(predicate == "ne") return MirInstruction::MI_FNE;
    if(predicate == "lt") return MirInstruction::MI_FLT;
    if(predicate == "gt") return MirInstruction::MI_FGT;
    if(predicate == "le") return MirInstruction::MI_FLE;
    if(predicate == "ge") return MirInstruction::MI_FGE;
    throw std::runtime_error("floating comparison predicate is not implemented: " + predicate);
  }

  X86Condition float_predicate_condition(const std::string & predicate) const
  {
    // FCMP models the right operand compared with the left operand so the
    // unsigned x86 conditions directly describe ordered floating predicates.
    if(predicate == "eq") return XC_E;
    if(predicate == "ne") return XC_NE;
    if(predicate == "lt") return XC_A;
    if(predicate == "gt") return XC_B;
    if(predicate == "le") return XC_AE;
    if(predicate == "ge") return XC_BE;
    throw std::runtime_error("floating branch predicate is not implemented: " + predicate);
  }

  void emit_float_const(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination, float_immediate(instruction.first.text),
                      instruction.type.text);
    define(instruction.dest, instruction.type, destination);
  }

  void emit_float_copy(const Instruction & instruction,
                       std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination, resolve(instruction.first), instruction.type.text);
    consume(instruction.first);
    define(instruction.dest, instruction.type, destination);
  }

  void emit_float_binary(const Instruction & instruction,
                         std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    MirInstruction operation = machine_instruction(
      float_binary_opcode(instruction.op), instruction.type.text);
    append_operand(operation, destination);
    append_operand(operation, resolve(instruction.first));
    append_operand(operation, resolve(instruction.second));
    out.push_back(operation);
    consume(instruction.first);
    consume(instruction.second);
    define(instruction.dest, instruction.type, destination);
  }

  void emit_float_direct_compare_branch(const Instruction & comparison,
                                        const Instruction & branch,
                                        std::vector<MirInstruction> & out)
  {
    uses_scalar_float_ = true;
    MirInstruction compare = machine_instruction(MirInstruction::MI_FCMP,
                                                 comparison.type.text);
    append_operand(compare, resolve(comparison.first));
    append_operand(compare, resolve(comparison.second));
    out.push_back(compare);

    MirInstruction unordered = machine_instruction(MirInstruction::MI_JCC);
    unordered.condition = XC_P;
    append_operand(unordered, named_operand(MirOperand::OP_LABEL,
      comparison.op == "ne" ? branch.second.text : branch.third.text));
    out.push_back(unordered);

    MirInstruction jump_true = machine_instruction(MirInstruction::MI_JCC);
    jump_true.condition = float_predicate_condition(comparison.op);
    append_operand(jump_true, named_operand(MirOperand::OP_LABEL, branch.second.text));
    out.push_back(jump_true);

    MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump_false, named_operand(MirOperand::OP_LABEL, branch.third.text));
    out.push_back(jump_false);
    consume(comparison.first);
    consume(comparison.second);
    skipped_position_ = position_ + 1;
  }

  void emit_float_compare_value(const Instruction & instruction,
                                const lowir_model::LowirBlock & block,
                                std::size_t instruction_index,
                                std::vector<MirInstruction> & out)
  {
    uses_scalar_float_ = true;
    MirInstruction compare = machine_instruction(float_compare_opcode(instruction.op),
                                                 instruction.type.text);
    append_operand(compare, reg_operand(XR_RAX));
    append_operand(compare, resolve(instruction.first));
    append_operand(compare, resolve(instruction.second));
    out.push_back(compare);
    MirOperand destination = reg_operand(XR_RAX);
    if(!result_is_immediate_return(block, instruction_index, instruction.dest)) {
      destination = reg_operand(allocate_result(instruction.dest, out));
      append_move(out, destination, reg_operand(XR_RAX));
    }
    consume(instruction.first);
    consume(instruction.second);
    define(instruction.dest, lowir_model::builtin_lowir_type(lowir_model::LTK_I64),
           destination);
  }

  void emit_float_load(const Instruction & instruction,
                       std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination, materialized_storage(instruction.first, out),
                      instruction.type.text);
    consume(instruction.first);
    define(instruction.dest, instruction.type, destination);
  }

  void emit_float_store(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    uses_scalar_float_ = true;
    const MirOperand destination = materialized_storage(instruction.second, out);
    append_float_move(out, destination, resolve(instruction.first), instruction.type.text);
    consume(instruction.first);
    consume(instruction.second);
  }

  void emit_float_unary(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    if(instruction.op != "neg")
      throw std::runtime_error("floating unary operation is not implemented: " + instruction.op);
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    MirInstruction negate = machine_instruction(MirInstruction::MI_FNEG,
                                                instruction.type.text);
    append_operand(negate, destination);
    append_operand(negate, resolve(instruction.first));
    out.push_back(negate);
    consume(instruction.first);
    define(instruction.dest, instruction.type, destination);
  }

  void emit_convert(const Instruction & instruction,
                    std::vector<MirInstruction> & out)
  {
    const bool source_float = is_scalar_float(instruction.source_type);
    const bool destination_float = is_scalar_float(instruction.type);
    if(source_float || destination_float) {
      uses_scalar_float_ = true;
      MirInstruction::Opcode opcode = MirInstruction::MI_SITOFP;
      if(instruction.op == "uitofp") opcode = MirInstruction::MI_UITOFP;
      else if(instruction.op == "fptosi") opcode = MirInstruction::MI_FPTOSI;
      else if(instruction.op == "fptoui") opcode = MirInstruction::MI_FPTOUI;
      else if(instruction.op == "fpext") opcode = MirInstruction::MI_FPEXT;
      else if(instruction.op == "fptrunc") opcode = MirInstruction::MI_FPTRUNC;
      else if(instruction.op != "sitofp")
        throw std::runtime_error("floating conversion is not implemented: " + instruction.op);
      const MirOperand destination = destination_float ?
        allocate_float_result(instruction.dest, instruction.type) :
        reg_operand(allocate_result(instruction.dest, out));
      const std::string source_name = is_integer_or_pointer(instruction.source_type) ?
        "i" + std::to_string(instruction.source_type.bit_width) : instruction.source_type.text;
      const std::string destination_name = is_integer_or_pointer(instruction.type) ?
        "i" + std::to_string(instruction.type.bit_width) : instruction.type.text;
      MirInstruction conversion = machine_instruction(
        opcode, source_name + "." + destination_name);
      append_operand(conversion, destination);
      append_operand(conversion, resolve(instruction.first));
      out.push_back(conversion);
      consume(instruction.first);
      define(instruction.dest, instruction.type, destination);
      return;
    }
    if(is_integer_or_pointer(instruction.source_type) &&
       is_integer_or_pointer(instruction.type)) {
      const MirOperand destination = reg_operand(allocate_result(instruction.dest, out));
      move_value_to_register(out, destination.reg, resolve(instruction.first),
                             instruction.source_type);
      normalize_integer(instruction.type, destination, out);
      consume(instruction.first, destination.reg);
      define(instruction.dest, instruction.type, destination);
      return;
    }
    throw std::runtime_error("conversion categories are not implemented");
  }

  void emit_division(const Instruction & instruction, const MirOperand & destination,
                     const MirOperand & right, std::vector<MirInstruction> & out)
  {
    append_move(out, reg_operand(XR_RDX), right);
    append_move(out, reg_operand(XR_RCX), reg_operand(XR_RDX));
    append_move(out, reg_operand(XR_RAX), destination);
    const bool unsigned_operation = instruction.op == "udiv" || instruction.op == "umod";
    if(unsigned_operation) append_move(out, reg_operand(XR_RDX), immediate(0));
    else out.push_back(machine_instruction(MirInstruction::MI_CQO));
    MirInstruction divide = machine_instruction(unsigned_operation ?
      MirInstruction::MI_DIV : MirInstruction::MI_IDIV);
    append_operand(divide, reg_operand(XR_RCX));
    out.push_back(divide);
    const bool remainder = instruction.op == "mod" || instruction.op == "umod";
    append_move(out, destination, reg_operand(remainder ? XR_RDX : XR_RAX));
  }

  void emit_shift(const Instruction & instruction, const MirOperand & destination,
                  const MirOperand & right, std::vector<MirInstruction> & out)
  {
    append_move(out, reg_operand(XR_RDX), right);
    append_move(out, reg_operand(XR_RCX), reg_operand(XR_RDX));
    MirInstruction::Opcode opcode = MirInstruction::MI_SHL_CL;
    if(instruction.op == "shr") opcode = MirInstruction::MI_SAR_CL;
    else if(instruction.op == "ushr") opcode = MirInstruction::MI_SHR_CL;
    MirInstruction shift = machine_instruction(opcode);
    append_operand(shift, destination);
    out.push_back(shift);
  }

  void emit_binary(const Instruction & instruction,
                   std::vector<MirInstruction> & out)
  {
    if(is_scalar_float(instruction.type)) {
      emit_float_binary(instruction, out);
      return;
    }
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer binary operation");
    const MirOperand left = resolve(instruction.first);
    MirOperand right = resolve(instruction.second);
    const MirOperand destination = binary_destination(instruction, left, out, true);
    const bool bitwise = instruction.op == "and" || instruction.op == "or" ||
                         instruction.op == "xor";
    if(right.kind == MirOperand::OP_FRAME || right.kind == MirOperand::OP_GLOBAL ||
       right.kind == MirOperand::OP_DEREF) {
      move_value_to_register(out, XR_RDX, right, operand_type(instruction.second));
      right = reg_operand(XR_RDX);
      normalize_integer(operand_type(instruction.second), right, out);
    }
    if(right.kind == MirOperand::OP_IMM &&
       (bitwise || right.imm < INT32_MIN || right.imm > INT32_MAX)) {
      append_move(out, reg_operand(XR_RDX), right);
      right = reg_operand(XR_RDX);
    }
    MirInstruction::Opcode opcode = MirInstruction::MI_ADD;
    if(instruction.op == "sub") opcode = MirInstruction::MI_SUB;
    else if(instruction.op == "mul") opcode = MirInstruction::MI_IMUL;
    else if(instruction.op == "and") opcode = MirInstruction::MI_AND;
    else if(instruction.op == "or") opcode = MirInstruction::MI_OR;
    else if(instruction.op == "xor") opcode = MirInstruction::MI_XOR;
    else if(instruction.op == "div" || instruction.op == "mod" ||
            instruction.op == "udiv" || instruction.op == "umod") {
      emit_division(instruction, destination, right, out);
      normalize_integer(instruction.type, destination, out);
      consume(instruction.first, destination.reg);
      consume(instruction.second, destination.reg);
      define(instruction.dest, instruction.type, destination);
      return;
    } else if(instruction.op == "shl" || instruction.op == "shr" ||
              instruction.op == "ushr") {
      emit_shift(instruction, destination, right, out);
      normalize_integer(instruction.type, destination, out);
      consume(instruction.first, destination.reg);
      consume(instruction.second, destination.reg);
      define(instruction.dest, instruction.type, destination);
      return;
    } else if(instruction.op != "add") {
      throw std::runtime_error("integer binary operation is not implemented: " + instruction.op);
    }
    MirInstruction operation = machine_instruction(opcode, instruction.type.text);
    append_operand(operation, destination);
    append_operand(operation, right);
    out.push_back(operation);
    normalize_integer(instruction.type, destination, out);
    consume(instruction.first, destination.reg);
    consume(instruction.second, destination.reg);
    define(instruction.dest, instruction.type, destination);
  }

  bool comparison_feeds_branch(const lowir_model::LowirBlock & block,
                               std::size_t instruction_index,
                               const Instruction & comparison) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       facts_.uses.find(comparison.dest) == facts_.uses.end() ||
       facts_.uses.find(comparison.dest)->second != 1) return false;
    const Instruction & branch = block.instructions[instruction_index + 1];
    return branch.kind == Instruction::IK_BRANCH && branch.first.text == comparison.dest;
  }

  MirOperand direct_compare_left(const Operand & operand,
                                 std::vector<MirInstruction> & out)
  {
    const MirOperand source = resolve(operand);
    if(source.kind == MirOperand::OP_REG || source.kind == MirOperand::OP_FRAME ||
       source.kind == MirOperand::OP_GLOBAL || source.kind == MirOperand::OP_DEREF)
      return source;
    append_move(out, reg_operand(XR_RAX), source);
    return reg_operand(XR_RAX);
  }

  MirOperand direct_compare_right(const Operand & operand, const LowType & type,
                                  std::vector<MirInstruction> & out)
  {
    const MirOperand source = resolve(operand);
    if(source.kind == MirOperand::OP_IMM && type.bit_width >= 32 &&
       (type.bit_width < 64 || (source.imm >= INT32_MIN && source.imm <= INT32_MAX)))
      return source;
    if(source.kind == MirOperand::OP_REG || source.kind == MirOperand::OP_FRAME ||
       source.kind == MirOperand::OP_GLOBAL || source.kind == MirOperand::OP_DEREF)
      return source;
    move_value_to_register(out, XR_RDX, source, operand_type(operand));
    return reg_operand(XR_RDX);
  }

  void emit_direct_compare_branch(const Instruction & comparison,
                                  const Instruction & branch,
                                  std::vector<MirInstruction> & out)
  {
    MirOperand left = direct_compare_left(comparison.first, out);
    const MirOperand unresolved_right = resolve(comparison.second);
    if(unresolved_right.kind == MirOperand::OP_IMM && comparison.type.bit_width == 64 &&
       (unresolved_right.imm < INT32_MIN || unresolved_right.imm > INT32_MAX) &&
       (left.kind != MirOperand::OP_REG || left.reg != XR_RAX)) {
      append_move(out, reg_operand(XR_RAX), left);
      left = reg_operand(XR_RAX);
    }
    const MirOperand right = direct_compare_right(comparison.second, comparison.type, out);
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 comparison.type.text);
    append_operand(compare, left);
    append_operand(compare, right);
    out.push_back(compare);
    MirInstruction jump_true = machine_instruction(MirInstruction::MI_JCC);
    jump_true.condition = predicate_condition(comparison.op);
    append_operand(jump_true, named_operand(MirOperand::OP_LABEL, branch.second.text));
    out.push_back(jump_true);
    MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump_false, named_operand(MirOperand::OP_LABEL, branch.third.text));
    out.push_back(jump_false);
    consume(comparison.first);
    consume(comparison.second);
    skipped_position_ = position_ + 1;
  }

  void emit_compare_value(const Instruction & instruction,
                          std::vector<MirInstruction> & out)
  {
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer comparison");
    const MirOperand left = resolve(instruction.first);
    const MirOperand destination = binary_destination(instruction, left, out);
    MirOperand right = resolve(instruction.second);
    if(right.kind != MirOperand::OP_REG) {
      move_value_to_register(out, XR_RDX, right, operand_type(instruction.second));
      right = reg_operand(XR_RDX);
    }
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 instruction.type.text);
    append_operand(compare, destination);
    append_operand(compare, right);
    out.push_back(compare);
    MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
    set.condition = predicate_condition(instruction.op);
    append_operand(set, destination);
    out.push_back(set);
    MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
    append_operand(extend, destination);
    append_operand(extend, destination);
    out.push_back(extend);
    consume(instruction.first, destination.reg);
    consume(instruction.second, destination.reg);
    define(instruction.dest, lowir_model::builtin_lowir_type(lowir_model::LTK_I64), destination);
  }

  void emit_copy(const Instruction & instruction, std::vector<MirInstruction> & out)
  {
    if(is_scalar_float(instruction.type)) {
      emit_float_copy(instruction, out);
      return;
    }
    const MirOperand source = resolve(instruction.first);
    MirOperand destination;
    if(can_reuse(instruction.first)) destination = source;
    else {
      destination = reg_operand(allocate_result(instruction.dest, out));
      move_value_to_register(out, destination.reg, source, instruction.type);
    }
    consume(instruction.first, destination.reg);
    define(instruction.dest, instruction.type, destination);
  }

  void emit_unary_value(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    if(is_scalar_float(instruction.type)) {
      emit_float_unary(instruction, out);
      return;
    }
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer unary operation");
    if(instruction.op == "decay") {
      emit_copy(instruction, out);
      return;
    }
    const MirOperand source = resolve(instruction.first);
    const MirOperand destination = binary_destination(instruction, source, out);
    if(instruction.op == "not") {
      MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                   instruction.type.text);
      append_operand(compare, destination);
      append_operand(compare, immediate(0));
      out.push_back(compare);
      MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
      set.condition = XC_E;
      append_operand(set, destination);
      out.push_back(set);
      MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
      append_operand(extend, destination);
      append_operand(extend, destination);
      out.push_back(extend);
    } else {
      MirInstruction::Opcode opcode = MirInstruction::MI_NEG;
      if(instruction.op == "bitnot") opcode = MirInstruction::MI_NOT;
      else if(instruction.op == "bswap") opcode = MirInstruction::MI_BSWAP;
      else if(instruction.op != "neg")
        throw std::runtime_error("integer unary operation is not implemented: " + instruction.op);
      MirInstruction operation = machine_instruction(opcode, instruction.type.text);
      append_operand(operation, destination);
      out.push_back(operation);
      normalize_integer(instruction.type, destination, out);
    }
    consume(instruction.first, destination.reg);
    define(instruction.dest, instruction.op == "not" ?
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64) : instruction.type,
      destination);
  }

  bool unary_not_feeds_branch(const lowir_model::LowirBlock & block,
                              std::size_t instruction_index,
                              const Instruction & instruction) const
  {
    return instruction.op == "not" &&
      comparison_feeds_branch(block, instruction_index, instruction);
  }

  void emit_direct_unary_not_branch(const Instruction & instruction,
                                    const Instruction & branch,
                                    std::vector<MirInstruction> & out)
  {
    const MirOperand value = direct_compare_left(instruction.first, out);
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 instruction.type.text);
    append_operand(compare, value);
    append_operand(compare, immediate(0));
    out.push_back(compare);
    MirInstruction jump_true = machine_instruction(MirInstruction::MI_JCC);
    jump_true.condition = XC_E;
    append_operand(jump_true, named_operand(MirOperand::OP_LABEL, branch.second.text));
    out.push_back(jump_true);
    MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump_false, named_operand(MirOperand::OP_LABEL, branch.third.text));
    out.push_back(jump_false);
    consume(instruction.first);
    skipped_position_ = position_ + 1;
  }

  void emit_index(const Instruction & instruction,
                  std::vector<MirInstruction> & out)
  {
    const MirOperand base = resolve(instruction.first);
    const bool constant_index = instruction.second.kind == Operand::OP_INTEGER;
    const long long offset = constant_index ?
      integer_literal(instruction.second.text) *
        static_cast<long long>(instruction.type.storage_size) : 0;
    MirOperand destination;
    const bool safe_reuse = base.kind == MirOperand::OP_REG &&
      constant_index && can_reuse(instruction.first) &&
      (!result_crosses_call(instruction.dest) || is_callee_saved(base.reg));
    if(safe_reuse) destination = base;
    else destination = reg_operand(allocate_result(
      instruction.dest, out,
      instruction.first.kind == Operand::OP_TEMP &&
      values_.find(instruction.first.text)->second.parameter));
    if(base.kind != MirOperand::OP_REG || destination.reg != base.reg) {
      if(is_frame_address(instruction.first))
        append_address(out, destination.reg, base);
      else
        move_value_to_register(out, destination.reg, base, operand_type(instruction.first));
    }
    if(constant_index) {
      MirInstruction lea = machine_instruction(MirInstruction::MI_LEA);
      append_operand(lea, destination);
      append_operand(lea, dereference(destination.reg, offset));
      out.push_back(lea);
    } else {
      MirOperand index = resolve(instruction.second);
      if(index.kind != MirOperand::OP_REG) {
        move_value_to_register(out, XR_RDX, index, operand_type(instruction.second));
        index = reg_operand(XR_RDX);
      }
      if(instruction.type.storage_size != 1) {
        if(index.reg != XR_RDX) append_move(out, reg_operand(XR_RDX), index);
        MirInstruction scale = machine_instruction(MirInstruction::MI_IMUL, "i64");
        append_operand(scale, reg_operand(XR_RDX));
        append_operand(scale, immediate(
          static_cast<long long>(instruction.type.storage_size)));
        out.push_back(scale);
        index = reg_operand(XR_RDX);
      }
      MirInstruction add = machine_instruction(MirInstruction::MI_ADD, "ptr");
      append_operand(add, destination);
      append_operand(add, index);
      out.push_back(add);
    }
    consume(instruction.first, destination.reg);
    consume(instruction.second, destination.reg);
    define(instruction.dest, lowir_model::builtin_lowir_type(lowir_model::LTK_PTR), destination);
  }

  struct GprMove
  {
    X64Register destination = XR_RDI;
    MirOperand source;
    LowType type;
    bool source_is_address = false;
    bool object_chunk = false;
    Operand object_source;
    std::size_t chunk_offset = 0;
    bool pending = true;
  };

  bool move_destination_is_safe(const std::vector<GprMove> & moves,
                                std::size_t candidate) const
  {
    for(std::size_t i = 0; i < moves.size(); ++i) {
      if(i == candidate || !moves[i].pending ||
         moves[i].source.kind != MirOperand::OP_REG) continue;
      if(moves[i].source.reg == moves[candidate].destination) return false;
    }
    return true;
  }

  void emit_gpr_move(const GprMove & move,
                     std::vector<MirInstruction> & out)
  {
    if(move.object_chunk) {
      emit_operand_address(out, XR_R11, move.object_source);
      append_load(out, reg_operand(move.destination),
                  dereference(XR_R11, static_cast<long long>(move.chunk_offset)),
                  move.type.text);
    } else if(move.source_is_address) {
      if(move.source.kind == MirOperand::OP_FRAME ||
         move.source.kind == MirOperand::OP_DEREF)
        append_address(out, move.destination, move.source);
      else
        move_value_to_register(out, move.destination, move.source,
          lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
    } else {
      move_value_to_register(out, move.destination, move.source, move.type);
    }
  }

  void emit_parallel_gpr_moves(const Instruction & instruction,
                               std::vector<MirInstruction> & out)
  {
    std::vector<GprMove> moves;
    std::size_t gpr_index = 0;
    for(std::size_t i = 0; i < instruction.args.size() && gpr_index < 6; ++i) {
      if(is_scalar_float(operand_type(instruction.args[i]))) continue;
      GprMove move;
      move.destination = argument_register(gpr_index++);
      move.source = resolve(instruction.args[i]);
      move.type = operand_type(instruction.args[i]);
      move.source_is_address = is_frame_address(instruction.args[i]);
      moves.push_back(move);
    }
    std::size_t remaining = moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < moves.size(); ++i) {
        if(!moves[i].pending || !move_destination_is_safe(moves, i)) continue;
        emit_gpr_move(moves[i], out);
        moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < moves.size() && !moves[cycle].pending) ++cycle;
      if(cycle == moves.size() || moves[cycle].source.kind != MirOperand::OP_REG)
        throw std::logic_error("invalid parallel GPR move cycle");
      const X64Register saved = moves[cycle].source.reg;
      append_move(out, reg_operand(XR_R11), reg_operand(saved));
      for(std::size_t i = 0; i < moves.size(); ++i)
        if(moves[i].pending && moves[i].source.kind == MirOperand::OP_REG &&
           moves[i].source.reg == saved)
          moves[i].source = reg_operand(XR_R11);
    }
  }

  bool call_has_scalar_float(const Instruction & instruction) const
  {
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      if(is_scalar_float(operand_type(instruction.args[i]))) return true;
    return false;
  }

  struct XmmMove
  {
    XmmRegister destination = XMM_0;
    MirOperand source;
    LowType type;
    bool pending = true;
  };

  bool xmm_destination_is_safe(const std::vector<XmmMove> & moves,
                               std::size_t candidate) const
  {
    for(std::size_t i = 0; i < moves.size(); ++i) {
      if(i == candidate || !moves[i].pending ||
         moves[i].source.kind != MirOperand::OP_XMM) continue;
      if(moves[i].source.xmm == moves[candidate].destination) return false;
    }
    return true;
  }

  long long xmm_call_scratch()
  {
    if(has_xmm_call_scratch_) return xmm_call_scratch_;
    xmm_call_scratch_ = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, "%xmm-call-scratch",
      lowir_model::builtin_lowir_type(lowir_model::LTK_F64));
    has_xmm_call_scratch_ = true;
    return xmm_call_scratch_;
  }

  void emit_parallel_xmm_moves(const Instruction & instruction,
                               std::vector<MirInstruction> & out)
  {
    std::size_t xmm_index = 0;
    std::vector<XmmMove> moves;
    for(std::size_t i = 0; i < instruction.args.size() && xmm_index < 8; ++i) {
      const LowType & type = operand_type(instruction.args[i]);
      if(!is_scalar_float(type)) continue;
      XmmMove move;
      move.destination = static_cast<XmmRegister>(xmm_index++);
      move.source = resolve(instruction.args[i]);
      move.type = type;
      moves.push_back(move);
    }
    std::size_t remaining = moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < moves.size(); ++i) {
        if(!moves[i].pending || !xmm_destination_is_safe(moves, i)) continue;
        append_float_move(out, xmm_operand(moves[i].destination), moves[i].source,
                          moves[i].type.text, true);
        moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < moves.size() &&
            (!moves[cycle].pending || moves[cycle].source.kind != MirOperand::OP_XMM))
        ++cycle;
      if(cycle == moves.size())
        throw std::logic_error("invalid parallel XMM move cycle");
      const XmmRegister saved = moves[cycle].source.xmm;
      const MirOperand scratch = frame_operand(xmm_call_scratch());
      append_float_move(out, scratch, xmm_operand(saved), moves[cycle].type.text);
      for(std::size_t i = 0; i < moves.size(); ++i)
        if(moves[i].pending && moves[i].source.kind == MirOperand::OP_XMM &&
           moves[i].source.xmm == saved)
          moves[i].source = scratch;
    }
  }

  std::size_t emit_mixed_stack_arguments(const Instruction & instruction,
                                         std::vector<MirInstruction> & out)
  {
    std::size_t gpr_index = 0;
    std::size_t xmm_index = 0;
    std::size_t stack_count = 0;
    for(std::size_t i = 0; i < instruction.args.size(); ++i) {
      const bool floating = is_scalar_float(operand_type(instruction.args[i]));
      if(floating ? xmm_index++ >= 8 : gpr_index++ >= 6) ++stack_count;
    }
    if(!stack_count) return 0;
    const std::size_t bytes = align_up(stack_count * 8, 16);
    MirInstruction subtract = machine_instruction(MirInstruction::MI_SUB);
    append_operand(subtract, reg_operand(XR_RSP));
    append_operand(subtract, immediate(static_cast<long long>(bytes)));
    out.push_back(subtract);
    gpr_index = xmm_index = stack_count = 0;
    for(std::size_t i = 0; i < instruction.args.size(); ++i) {
      const LowType & type = operand_type(instruction.args[i]);
      const bool floating = is_scalar_float(type);
      const bool on_stack = floating ? xmm_index++ >= 8 : gpr_index++ >= 6;
      if(!on_stack) continue;
      const MirOperand destination = dereference(
        XR_RSP, static_cast<long long>(stack_count++ * 8));
      if(floating)
        append_float_move(out, destination, resolve(instruction.args[i]), type.text);
      else {
        MirOperand value = resolve(instruction.args[i]);
        if(value.kind != MirOperand::OP_REG) {
          move_value_to_register(out, XR_R11, value, type);
          value = reg_operand(XR_R11);
        }
        append_store(out, destination, value, type.text);
      }
    }
    return bytes;
  }

  std::size_t emit_stack_arguments(const Instruction & instruction,
                                   std::vector<MirInstruction> & out)
  {
    if(instruction.args.size() <= 6) return 0;
    const std::size_t bytes = align_up((instruction.args.size() - 6) * 8, 16);
    MirInstruction subtract = machine_instruction(MirInstruction::MI_SUB);
    append_operand(subtract, reg_operand(XR_RSP));
    append_operand(subtract, immediate(static_cast<long long>(bytes)));
    out.push_back(subtract);
    for(std::size_t i = 6; i < instruction.args.size(); ++i) {
      if(is_frame_address(instruction.args[i])) continue;
      MirOperand value = resolve(instruction.args[i]);
      const LowType & type = operand_type(instruction.args[i]);
      if(value.kind != MirOperand::OP_REG) {
        move_value_to_register(out, XR_R11, value, type);
        value = reg_operand(XR_R11);
      }
      append_store(out, dereference(XR_RSP, static_cast<long long>((i - 6) * 8)),
                   value, type.text);
    }
    return bytes;
  }

  void emit_deferred_stack_addresses(const Instruction & instruction,
                                     std::vector<MirInstruction> & out)
  {
    for(std::size_t i = 6; i < instruction.args.size(); ++i) {
      if(!is_frame_address(instruction.args[i])) continue;
      append_address(out, XR_R11, resolve(instruction.args[i]));
      append_store(out, dereference(XR_RSP, static_cast<long long>((i - 6) * 8)),
                   reg_operand(XR_R11), operand_type(instruction.args[i]).text);
    }
  }

  void emit_bulk(const Instruction & instruction,
                 std::vector<MirInstruction> & out)
  {
    const bool copying = instruction.kind == Instruction::IK_COPYOBJ;
    if(copying && aliases_same_object(instruction.first, instruction.second)) {
      consume(instruction.first);
      consume(instruction.second);
      return;
    }
    X64Register saved_source = XR_RSP;
    if(copying && instruction.first.kind == Operand::OP_TEMP) {
      const MirOperand source = resolve(instruction.first);
      if(source.kind == MirOperand::OP_REG && source.reg == XR_RDI) {
        if(!registers_.try_allocate(false, saved_source)) saved_source = XR_R11;
        append_move(out, reg_operand(saved_source), source);
      }
    }
    const Operand & destination = copying ? instruction.second : instruction.first;
    emit_operand_address(out, XR_RDI, destination);
    if(copying) {
      if(saved_source != XR_RSP)
        append_move(out, reg_operand(XR_RSI), reg_operand(saved_source));
      else
        emit_operand_address(out, XR_RSI, instruction.first);
    }
    MirInstruction bulk = machine_instruction(copying ?
      MirInstruction::MI_COPY_BYTES : MirInstruction::MI_ZERO_BYTES);
    bulk.byte_count = instruction.byte_count;
    bulk.byte_alignment = instruction.byte_alignment;
    append_operand(bulk, reg_operand(XR_RDI));
    if(copying) append_operand(bulk, reg_operand(XR_RSI));
    out.push_back(bulk);
    consume(instruction.first);
    if(copying) consume(instruction.second);
  }

  std::vector<lowir_model::LowirParameter> call_parameters(
      const Instruction & instruction) const
  {
    std::vector<lowir_model::LowirParameter> parameters;
    if(instruction.has_call_signature) parameters = instruction.call_params;
    else if(instruction.first.kind == Operand::OP_GLOBAL) {
      const FunctionSignatureIndex::const_iterator found =
        signatures_.find(instruction.first.text);
      if(found != signatures_.end() && found->second.params)
        parameters = *found->second.params;
    }
    if(parameters.size() > instruction.args.size())
      parameters.resize(instruction.args.size());
    while(parameters.size() < instruction.args.size()) {
      lowir_model::LowirParameter parameter;
      parameter.name = "%arg" + std::to_string(parameters.size());
      parameter.type = operand_type(instruction.args[parameters.size()]);
      parameters.push_back(parameter);
    }
    return parameters;
  }

  bool argument_needs_address(const lowir_model::LowirParameter & parameter,
                              const Operand & argument) const
  {
    return parameter.metadata.passing != lowir_model::PPM_DIRECT &&
      operand_type(argument).kind != lowir_model::LTK_PTR;
  }

  bool requires_extended_call(const Instruction & instruction,
                              const std::vector<lowir_model::LowirParameter> & parameters) const
  {
    if(!instruction.call_returns_void &&
       instruction.type.kind == lowir_model::LTK_OBJECT) return true;
    for(std::size_t i = 0; i < parameters.size(); ++i)
      if(parameters[i].type.kind == lowir_model::LTK_OBJECT ||
         parameters[i].metadata.passing != lowir_model::PPM_DIRECT)
        return true;
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      if(operand_type(instruction.args[i]).kind == lowir_model::LTK_OBJECT) return true;
    return false;
  }

  void emit_stack_adjust(std::vector<MirInstruction> & out,
                         MirInstruction::Opcode opcode,
                         std::size_t bytes)
  {
    if(!bytes) return;
    MirInstruction adjust = machine_instruction(opcode);
    append_operand(adjust, reg_operand(XR_RSP));
    append_operand(adjust, immediate(static_cast<long long>(bytes)));
    out.push_back(adjust);
  }

  void emit_extended_stack_arguments(
      const Instruction & instruction,
      const std::vector<lowir_model::LowirParameter> & parameters,
      const AbiPlan & plan,
      const std::vector<MirOperand> & addressable,
      const std::vector<bool> & needs_address,
      std::vector<MirInstruction> & out)
  {
    emit_stack_adjust(out, MirInstruction::MI_SUB, plan.stack_bytes);
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const AbiPiece & piece = plan.pieces[i];
      if(piece.location != APL_STACK) continue;
      const Operand & argument = instruction.args[piece.parameter_index];
      if(parameters[piece.parameter_index].type.kind == lowir_model::LTK_OBJECT) {
        if(piece.chunk_offset) continue;
        append_address(out, XR_RDI,
                       dereference(XR_RSP, static_cast<long long>(piece.stack_offset)));
        emit_operand_address(out, XR_RSI, argument);
        MirInstruction copy = machine_instruction(MirInstruction::MI_COPY_BYTES);
        copy.byte_count = parameters[piece.parameter_index].type.storage_size;
        copy.byte_alignment = parameters[piece.parameter_index].type.alignment;
        append_operand(copy, reg_operand(XR_RDI));
        append_operand(copy, reg_operand(XR_RSI));
        out.push_back(copy);
        continue;
      }
      const MirOperand destination = dereference(
        XR_RSP, static_cast<long long>(piece.stack_offset));
      if(needs_address[piece.parameter_index] || is_frame_address(argument)) {
        GprMove move;
        move.destination = XR_R11;
        move.source = needs_address[piece.parameter_index] ?
          addressable[piece.parameter_index] : resolve(argument);
        move.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
        move.source_is_address = true;
        emit_gpr_move(move, out);
        append_store(out, destination, reg_operand(XR_R11), "ptr");
      } else if(is_scalar_float(piece.type)) {
        uses_scalar_float_ = true;
        append_float_move(out, destination, resolve(argument), piece.type.text);
      } else {
        move_value_to_register(out, XR_R11, resolve(argument),
                               operand_type(argument));
        append_store(out, destination, reg_operand(XR_R11), piece.type.text);
      }
    }
  }

  void emit_extended_register_arguments(
      const Instruction & instruction,
      const std::vector<lowir_model::LowirParameter> & parameters,
      const AbiPlan & plan,
      const std::vector<MirOperand> & addressable,
      const std::vector<bool> & needs_address,
      std::vector<MirInstruction> & out)
  {
    std::vector<GprMove> gpr_moves;
    std::vector<XmmMove> xmm_moves;
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const AbiPiece & piece = plan.pieces[i];
      const Operand & argument = instruction.args[piece.parameter_index];
      if(piece.location == APL_GPR) {
        GprMove move;
        move.destination = piece.reg;
        move.type = piece.type;
        if(parameters[piece.parameter_index].type.kind == lowir_model::LTK_OBJECT) {
          move.object_chunk = true;
          move.object_source = argument;
          move.chunk_offset = piece.chunk_offset;
        } else {
          move.source = needs_address[piece.parameter_index] ?
            addressable[piece.parameter_index] : resolve(argument);
          move.source_is_address = needs_address[piece.parameter_index] ||
            is_frame_address(argument);
        }
        gpr_moves.push_back(move);
      } else if(piece.location == APL_XMM) {
        XmmMove move;
        move.destination = piece.xmm;
        move.source = resolve(argument);
        move.type = piece.type;
        xmm_moves.push_back(move);
      }
    }
    std::size_t remaining = gpr_moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < gpr_moves.size(); ++i) {
        if(!gpr_moves[i].pending || !move_destination_is_safe(gpr_moves, i)) continue;
        emit_gpr_move(gpr_moves[i], out);
        gpr_moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < gpr_moves.size() && !gpr_moves[cycle].pending) ++cycle;
      if(cycle == gpr_moves.size() ||
         gpr_moves[cycle].source.kind != MirOperand::OP_REG)
        throw std::logic_error("invalid extended GPR move cycle");
      const X64Register saved = gpr_moves[cycle].source.reg;
      append_move(out, reg_operand(XR_R11), reg_operand(saved));
      for(std::size_t i = 0; i < gpr_moves.size(); ++i)
        if(gpr_moves[i].pending && gpr_moves[i].source.kind == MirOperand::OP_REG &&
           gpr_moves[i].source.reg == saved)
          gpr_moves[i].source = reg_operand(XR_R11);
    }
    remaining = xmm_moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < xmm_moves.size(); ++i) {
        if(!xmm_moves[i].pending || !xmm_destination_is_safe(xmm_moves, i)) continue;
        append_float_move(out, xmm_operand(xmm_moves[i].destination),
                          xmm_moves[i].source, xmm_moves[i].type.text, true);
        xmm_moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < xmm_moves.size() &&
            (!xmm_moves[cycle].pending ||
             xmm_moves[cycle].source.kind != MirOperand::OP_XMM)) ++cycle;
      if(cycle == xmm_moves.size())
        throw std::logic_error("invalid extended XMM move cycle");
      const XmmRegister saved = xmm_moves[cycle].source.xmm;
      const MirOperand scratch = frame_operand(xmm_call_scratch());
      append_float_move(out, scratch, xmm_operand(saved), xmm_moves[cycle].type.text);
      for(std::size_t i = 0; i < xmm_moves.size(); ++i)
        if(xmm_moves[i].pending && xmm_moves[i].source.kind == MirOperand::OP_XMM &&
           xmm_moves[i].source.xmm == saved)
          xmm_moves[i].source = scratch;
    }
  }

  bool object_result_alias(const Instruction & instruction,
                           const lowir_model::LowirBlock & block,
                           std::size_t instruction_index,
                           Operand & destination,
                           std::size_t & skip_delta) const
  {
    if(instruction_index + 1 < block.instructions.size()) {
      const Instruction & copy = block.instructions[instruction_index + 1];
      if(copy.kind == Instruction::IK_COPYOBJ &&
         copy.first.kind == Operand::OP_TEMP && copy.first.text == instruction.dest) {
        destination = copy.second;
        skip_delta = 1;
        return true;
      }
    }
    if(instruction_index + 2 < block.instructions.size()) {
      const Instruction & address = block.instructions[instruction_index + 1];
      const Instruction & copy = block.instructions[instruction_index + 2];
      if(address.kind == Instruction::IK_ADDR &&
         copy.kind == Instruction::IK_COPYOBJ &&
         copy.first.kind == Operand::OP_TEMP && copy.first.text == instruction.dest &&
         copy.second.kind == Operand::OP_TEMP && copy.second.text == address.dest) {
        destination = address.first;
        skip_delta = 2;
        return true;
      }
    }
    return false;
  }

  void store_object_return_registers(const LowType & type,
                                     const Operand * destination,
                                     const MirOperand * home,
                                     std::vector<MirInstruction> & out)
  {
    if(type.storage_size > 16)
      throw std::runtime_error("direct object return exceeds two SysV eightbytes");
    if(destination) emit_operand_address(out, XR_R11, *destination);
    else append_address(out, XR_R11, *home);
    const std::size_t chunks = (type.storage_size + 7) / 8;
    for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
      const LowType & chunk_type = object_chunk_type(type.storage_size - chunk * 8);
      append_store(out, dereference(XR_R11, static_cast<long long>(chunk * 8)),
                   reg_operand(chunk ? XR_RDX : XR_RAX), chunk_type.text);
    }
  }

  void define_object_result(const std::string & name,
                            const LowType & type,
                            const Operand * destination,
                            const MirOperand & home)
  {
    ValueFact value;
    value.type = type;
    if(destination) {
      long long frame = 0;
      if(frame_provenance(*destination, frame)) {
        value.location = frame_operand(frame);
        value.frame_address = true;
        value.has_frame_provenance = true;
        value.frame_provenance = frame;
      } else if(destination->kind == Operand::OP_TEMP) {
        const ValueFact & address = values_.find(destination->text)->second;
        value.location = address.location;
        value.frame_address = address.frame_address;
      } else {
        value.location = named_operand(MirOperand::OP_SYMBOL, destination->text);
      }
    } else {
      value.location = home;
      value.frame_address = true;
      value.has_frame_provenance = true;
      value.frame_provenance = home.offset;
    }
    values_[name] = value;
  }

  void emit_extended_call(
      const Instruction & instruction,
      const lowir_model::LowirBlock & block,
      std::size_t instruction_index,
      const std::vector<lowir_model::LowirParameter> & parameters,
      std::vector<MirInstruction> & out)
  {
    const bool direct = instruction.first.kind == Operand::OP_GLOBAL;
    if(!direct)
      move_value_to_register(out, XR_R10, resolve(instruction.first),
                             operand_type(instruction.first));
    const AbiPlan plan = classify_abi(parameters);
    std::vector<bool> needs_address(parameters.size(), false);
    std::vector<MirOperand> addressable(parameters.size());
    for(std::size_t i = 0; i < parameters.size(); ++i) {
      needs_address[i] = argument_needs_address(parameters[i], instruction.args[i]);
      if(needs_address[i]) addressable[i] = make_addressable(instruction.args[i], out);
      if(is_scalar_float(parameters[i].type)) uses_scalar_float_ = true;
    }
    emit_extended_stack_arguments(instruction, parameters, plan,
                                  addressable, needs_address, out);
    emit_extended_register_arguments(instruction, parameters, plan,
                                     addressable, needs_address, out);
    MirInstruction call = machine_instruction(direct ?
      MirInstruction::MI_CALL : MirInstruction::MI_CALL_INDIRECT);
    append_operand(call, direct ?
      named_operand(MirOperand::OP_SYMBOL, instruction.first.text) :
      reg_operand(XR_R10));
    out.push_back(call);
    emit_stack_adjust(out, MirInstruction::MI_ADD, plan.stack_bytes);

    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      consume(instruction.args[i]);
    active_instruction_ = 0;
    if(!instruction.call_returns_void &&
       instruction.type.kind == lowir_model::LTK_OBJECT) {
      Operand alias_destination;
      std::size_t skip_delta = 0;
      const bool alias = object_result_alias(instruction, block, instruction_index,
                                             alias_destination, skip_delta);
      MirOperand home;
      if(alias) skipped_position_ = position_ + skip_delta;
      else home = allocate_temp_home(instruction.dest, instruction.type);
      store_object_return_registers(instruction.type,
                                    alias ? &alias_destination : 0,
                                    alias ? 0 : &home, out);
      define_object_result(instruction.dest, instruction.type,
                           alias ? &alias_destination : 0, home);
    } else if(!instruction.call_returns_void && is_scalar_float(instruction.type)) {
      const MirOperand location = allocate_float_result(instruction.dest, instruction.type);
      append_float_move(out, location, xmm_operand(XMM_0), instruction.type.text);
      define(instruction.dest, instruction.type, location);
    } else if(!instruction.call_returns_void) {
      MirOperand location = reg_operand(XR_RAX);
      if(!result_is_immediate_return(block, instruction_index, instruction.dest) &&
         !result_is_immediate_unary_not_branch(block, instruction_index,
                                               instruction.dest)) {
        location = reg_operand(allocate_result(instruction.dest, out));
        append_move(out, location, reg_operand(XR_RAX));
      }
      if(is_integer_or_pointer(instruction.type))
        normalize_integer(instruction.type, location, out);
      define(instruction.dest, instruction.type, location);
    }
    consume(instruction.first);
  }

  void emit_call(const Instruction & instruction,
                 const lowir_model::LowirBlock & block,
                 std::size_t instruction_index,
                 std::vector<MirInstruction> & out)
  {
    const std::vector<lowir_model::LowirParameter> parameters =
      call_parameters(instruction);
    if(requires_extended_call(instruction, parameters)) {
      emit_extended_call(instruction, block, instruction_index, parameters, out);
      return;
    }
    const bool direct = instruction.first.kind == Operand::OP_GLOBAL;
    if(!direct) {
      std::string pointer_cell;
      if(instruction.first.kind == Operand::OP_TEMP) {
        const std::unordered_map<std::string, ValueFact>::const_iterator value =
          values_.find(instruction.first.text);
        if(value != values_.end()) pointer_cell = value->second.pointer_global_cell;
      }
      if(!pointer_cell.empty()) {
        MirInstruction load = machine_instruction(MirInstruction::MI_LOAD, "ptr");
        append_operand(load, reg_operand(XR_R10));
        append_operand(load, named_operand(MirOperand::OP_GLOBAL, pointer_cell));
        out.push_back(load);
      } else {
        move_value_to_register(out, XR_R10, resolve(instruction.first),
                               operand_type(instruction.first));
      }
    }
    const bool mixed_float = call_has_scalar_float(instruction);
    if(mixed_float) uses_scalar_float_ = true;
    const std::size_t stack_bytes = mixed_float ?
      emit_mixed_stack_arguments(instruction, out) : emit_stack_arguments(instruction, out);
    emit_parallel_gpr_moves(instruction, out);
    if(mixed_float) emit_parallel_xmm_moves(instruction, out);
    else emit_deferred_stack_addresses(instruction, out);

    if(direct) {
      MirInstruction call = machine_instruction(MirInstruction::MI_CALL);
      append_operand(call, named_operand(MirOperand::OP_SYMBOL, instruction.first.text));
      out.push_back(call);
    } else {
      MirInstruction call = machine_instruction(MirInstruction::MI_CALL_INDIRECT);
      append_operand(call, reg_operand(XR_R10));
      out.push_back(call);
    }
    if(stack_bytes) {
      MirInstruction add = machine_instruction(MirInstruction::MI_ADD);
      append_operand(add, reg_operand(XR_RSP));
      append_operand(add, immediate(static_cast<long long>(stack_bytes)));
      out.push_back(add);
    }
    bool arguments_consumed = false;
    if(!instruction.call_returns_void && !is_scalar_float(instruction.type)) {
      for(std::size_t i = 0; i < instruction.args.size(); ++i)
        consume(instruction.args[i]);
      arguments_consumed = true;
      active_instruction_ = 0; // Call operands are dead or safely spillable after the call.
    }
    if(!instruction.call_returns_void) {
      if(is_scalar_float(instruction.type)) {
        const MirOperand location = allocate_float_result(instruction.dest, instruction.type);
        append_float_move(out, location, xmm_operand(XMM_0), instruction.type.text);
        define(instruction.dest, instruction.type, location);
      } else {
        if(result_is_next_call_address_argument(block, instruction_index, instruction)) {
          const MirOperand home = allocate_temp_home(instruction.dest, instruction.type);
          append_store(out, home, reg_operand(XR_RAX), instruction.type.text);
          define(instruction.dest, instruction.type, home);
          if(!arguments_consumed)
            for(std::size_t i = 0; i < instruction.args.size(); ++i)
              consume(instruction.args[i]);
          consume(instruction.first);
          return;
        }
        MirOperand location = reg_operand(XR_RAX);
        if(!result_is_immediate_return(block, instruction_index, instruction.dest) &&
           !result_is_immediate_unary_not_branch(block, instruction_index,
                                                 instruction.dest)) {
          location = reg_operand(allocate_result(instruction.dest, out));
          append_move(out, location, reg_operand(XR_RAX));
        }
        if(is_integer_or_pointer(instruction.type))
          normalize_integer(instruction.type, location, out);
        define(instruction.dest, instruction.type, location);
      }
    }
    if(!arguments_consumed)
      for(std::size_t i = 0; i < instruction.args.size(); ++i)
        consume(instruction.args[i]);
    consume(instruction.first);
  }

  void emit_branch(const Instruction & instruction, std::vector<MirInstruction> & out)
  {
    move_value_to_register(out, XR_RAX, resolve(instruction.first),
                           operand_type(instruction.first));
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP, "i64");
    append_operand(compare, reg_operand(XR_RAX));
    append_operand(compare, immediate(0));
    out.push_back(compare);
    MirInstruction branch = machine_instruction(MirInstruction::MI_JCC);
    branch.condition = XC_NE;
    append_operand(branch, named_operand(MirOperand::OP_LABEL, instruction.second.text));
    out.push_back(branch);
    MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump, named_operand(MirOperand::OP_LABEL, instruction.third.text));
    out.push_back(jump);
    consume(instruction.first);
  }

  void emit_switch(const Instruction & instruction, std::vector<MirInstruction> & out)
  {
    move_value_to_register(out, XR_RAX, resolve(instruction.first),
                           operand_type(instruction.first));
    for(std::size_t i = 0; i < instruction.args.size(); i += 2) {
      move_value_to_register(out, XR_RCX, resolve(instruction.args[i]),
                             operand_type(instruction.args[i]));
      MirInstruction compare = machine_instruction(MirInstruction::MI_CMP, "i64");
      append_operand(compare, reg_operand(XR_RAX));
      append_operand(compare, reg_operand(XR_RCX));
      out.push_back(compare);
      MirInstruction branch = machine_instruction(MirInstruction::MI_JCC);
      branch.condition = XC_E;
      append_operand(branch, named_operand(MirOperand::OP_LABEL,
                                           instruction.args[i + 1].text));
      out.push_back(branch);
      consume(instruction.args[i]);
    }
    MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump, named_operand(MirOperand::OP_LABEL, instruction.second.text));
    out.push_back(jump);
    consume(instruction.first);
  }

  void emit_return(const Instruction & instruction, std::vector<MirInstruction> & out)
  {
    if(instruction.type.kind == lowir_model::LTK_OBJECT) {
      if(instruction.type.storage_size > 16)
        throw std::runtime_error("direct object return exceeds two SysV eightbytes");
      emit_operand_address(out, XR_R11, instruction.first);
      const std::size_t chunks = (instruction.type.storage_size + 7) / 8;
      for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
        const LowType & chunk_type = object_chunk_type(
          instruction.type.storage_size - chunk * 8);
        append_load(out, reg_operand(chunk ? XR_RDX : XR_RAX),
                    dereference(XR_R11, static_cast<long long>(chunk * 8)),
                    chunk_type.text);
      }
    } else if(is_scalar_float(instruction.type)) {
      uses_scalar_float_ = true;
      append_float_move(out, xmm_operand(XMM_0), resolve(instruction.first),
                        instruction.type.text);
    } else if(instruction.type.kind != lowir_model::LTK_VOID)
      move_value_to_register(out, XR_RAX, resolve(instruction.first), instruction.type);
    MirInstruction ret = machine_instruction(MirInstruction::MI_RET);
    if(instruction.type.kind != lowir_model::LTK_VOID &&
       !is_scalar_float(instruction.type) &&
       instruction.type.kind != lowir_model::LTK_OBJECT)
      append_operand(ret, reg_operand(XR_RAX));
    out.push_back(ret);
    consume(instruction.first);
  }

  void lower_instruction(const lowir_model::LowirBlock & block,
                         std::size_t instruction_index,
                         std::vector<MirInstruction> & out)
  {
    const Instruction & instruction = block.instructions[instruction_index];
    active_instruction_ = &instruction;
    if(position_ == skipped_position_) return;
    if(instruction.kind == Instruction::IK_CONST) {
      if(is_scalar_float(instruction.type)) {
        emit_float_const(instruction, out);
        return;
      }
      MirOperand destination;
      X64Register reg = XR_RSP;
      if(registers_.try_allocate(result_crosses_call(instruction.dest), reg)) {
        destination = reg_operand(reg);
        append_move(out, destination, immediate(integer_literal(instruction.first.text)));
      } else {
        destination = allocate_temp_home(instruction.dest, instruction.type);
        append_move(out, reg_operand(XR_RAX),
                    immediate(integer_literal(instruction.first.text)));
        append_store(out, destination, reg_operand(XR_RAX), instruction.type.text);
      }
      define(instruction.dest, instruction.type, destination);
    } else if(instruction.kind == Instruction::IK_COPY) {
      emit_copy(instruction, out);
    } else if(instruction.kind == Instruction::IK_ADDR) {
      MirOperand destination;
      if(instruction.first.kind == Operand::OP_SLOT) {
        if(address_is_next_call_argument(block, instruction_index, instruction.dest) ||
           address_is_next_bulk_operand(block, instruction_index, instruction.dest) ||
           address_is_immediately_stored(block, instruction_index,
                                         instruction.dest) ||
           address_is_object_result_destination(block, instruction_index,
                                                instruction.dest)) {
          define(instruction.dest, lowir_model::builtin_lowir_type(lowir_model::LTK_PTR),
                 storage(instruction.first));
          values_[instruction.dest].frame_address = true;
          values_[instruction.dest].has_frame_provenance = true;
          values_[instruction.dest].frame_provenance = storage(instruction.first).offset;
          return;
        }
        const X64Register compare_register = direct_slot_address_register(
          block, instruction_index, instruction.dest);
        if(compare_register != XR_RSP) destination = reg_operand(compare_register);
        else destination = (address_is_immediately_loaded(block, instruction_index,
                                                          instruction.dest) ||
                            address_is_immediately_stored(block, instruction_index,
                                                          instruction.dest) ||
                            address_precedes_elided_copy(block, instruction_index,
                                                         instruction.dest) ||
                            skipped_position_ == position_ + 1) ?
          reg_operand(XR_RCX) : reg_operand(allocate_result(instruction.dest, out));
        MirInstruction lea = machine_instruction(MirInstruction::MI_LEA);
        append_operand(lea, destination);
        append_operand(lea, storage(instruction.first));
        out.push_back(lea);
      } else {
        destination = reg_operand(allocate_result(instruction.dest, out));
        append_move(out, destination, resolve(instruction.first));
      }
      define(instruction.dest, lowir_model::builtin_lowir_type(lowir_model::LTK_PTR), destination);
      if(instruction.first.kind == Operand::OP_SLOT) {
        values_[instruction.dest].has_frame_provenance = true;
        values_[instruction.dest].frame_provenance = storage(instruction.first).offset;
      }
      if(instruction.first.kind == Operand::OP_GLOBAL &&
         pointer_globals_.count(instruction.first.text))
        values_[instruction.dest].pointer_global_cell = instruction.first.text;
    } else if(instruction.kind == Instruction::IK_LOAD) {
      if(is_scalar_float(instruction.type)) {
        emit_float_load(instruction, out);
        return;
      }
      if(slot_load_feeds_direct_compare(block, instruction_index, instruction)) {
        define(instruction.dest, instruction.type, storage(instruction.first));
        return;
      }
      MirOperand destination;
      if((result_is_immediately_stored(block, instruction_index, instruction.dest) ||
          (result_is_immediate_return(block, instruction_index, instruction.dest) &&
           (instruction.type.kind == lowir_model::LTK_PTR ||
            instruction.type.bit_width == 64))))
        destination = reg_operand(XR_RAX);
      else destination = reg_operand(allocate_result(instruction.dest, out));
      MirInstruction load = machine_instruction(MirInstruction::MI_LOAD, instruction.type.text);
      append_operand(load, destination);
      append_operand(load, materialized_storage(instruction.first, out));
      out.push_back(load);
      if(is_integer_or_pointer(instruction.type))
        normalize_integer(instruction.type, destination, out);
      consume(instruction.first, destination.reg);
      define(instruction.dest, instruction.type, destination);
    } else if(instruction.kind == Instruction::IK_STORE) {
      if(instruction.second.kind == Operand::OP_SLOT &&
         discarded_slots_.count(instruction.second.text)) {
        consume(instruction.first);
        consume(instruction.second);
        return;
      }
      if(is_scalar_float(instruction.type)) {
        emit_float_store(instruction, out);
        return;
      }
      MirInstruction store = machine_instruction(MirInstruction::MI_STORE, instruction.type.text);
      MirOperand value = resolve(instruction.first);
      if(value.kind != MirOperand::OP_REG) {
        move_value_to_register(out, XR_RAX, value, operand_type(instruction.first));
        value = reg_operand(XR_RAX);
      }
      append_operand(store, materialized_storage(instruction.second, out));
      append_operand(store, value);
      out.push_back(store);
      consume(instruction.first);
      consume(instruction.second);
    } else if(instruction.kind == Instruction::IK_INDEX) {
      emit_index(instruction, out);
    } else if(instruction.kind == Instruction::IK_BINARY) {
      emit_binary(instruction, out);
    } else if(instruction.kind == Instruction::IK_CMP) {
      if(is_scalar_float(instruction.type) &&
         comparison_feeds_branch(block, instruction_index, instruction))
        emit_float_direct_compare_branch(
          instruction, block.instructions[instruction_index + 1], out);
      else if(is_scalar_float(instruction.type))
        emit_float_compare_value(instruction, block, instruction_index, out);
      else if(comparison_feeds_branch(block, instruction_index, instruction))
        emit_direct_compare_branch(instruction, block.instructions[instruction_index + 1], out);
      else emit_compare_value(instruction, out);
    } else if(instruction.kind == Instruction::IK_UNARY) {
      if(unary_not_feeds_branch(block, instruction_index, instruction))
        emit_direct_unary_not_branch(instruction,
          block.instructions[instruction_index + 1], out);
      else emit_unary_value(instruction, out);
    } else if(instruction.kind == Instruction::IK_CONVERT) {
      emit_convert(instruction, out);
    } else if(instruction.kind == Instruction::IK_CALL) {
      emit_call(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_COPYOBJ ||
              instruction.kind == Instruction::IK_ZEROINIT) {
      emit_bulk(instruction, out);
    } else if(instruction.kind == Instruction::IK_BRANCH) {
      emit_branch(instruction, out);
    } else if(instruction.kind == Instruction::IK_SWITCH) {
      emit_switch(instruction, out);
    } else if(instruction.kind == Instruction::IK_JUMP) {
      MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
      append_operand(jump, named_operand(MirOperand::OP_LABEL, instruction.first.text));
      out.push_back(jump);
    } else if(instruction.kind == Instruction::IK_RETURN) {
      emit_return(instruction, out);
    } else {
      throw std::runtime_error("foundation LowIR instruction is not implemented");
    }
  }
};

mir_model::MirGlobalDefinition lower_global(const lowir_model::LowirGlobalDefinition & source)
{
  mir_model::MirGlobalDefinition target;
  target.name = source.name;
  target.readonly = source.storage == lowir_model::GSM_READONLY;
  target.thread_local_storage = source.storage == lowir_model::GSM_THREAD_LOCAL;
  if(source.structured) {
    target.storage_kind = mir_model::MirGlobalDefinition::GS_DATA;
    for(std::size_t i = 0; i < source.data_items.size(); ++i) {
      const lowir_model::LowirGlobalDefinition::DataItem & item = source.data_items[i];
      mir_model::MirGlobalDefinition::DataItem lowered;
      lowered.type = item.type.text;
      if(item.kind == lowir_model::LowirGlobalDefinition::DataItem::ITEM_ZERO) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO;
        lowered.zero_bytes = item.zero_bytes;
      } else if(item.kind == lowir_model::LowirGlobalDefinition::DataItem::ITEM_ADDR) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_ADDR;
        lowered.symbol = item.symbol;
        lowered.addr_addend = item.addr_addend;
      } else if(item.type.kind >= lowir_model::LTK_F32 &&
                item.type.kind <= lowir_model::LTK_F80) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT;
        lowered.literal_text = item.literal_operand.text;
      } else {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_INTEGER;
        lowered.int_value = integer_literal(item.literal_operand.text);
      }
      target.data_items.push_back(lowered);
    }
  } else {
    target.storage_kind = mir_model::MirGlobalDefinition::GS_SCALAR;
    target.type = source.type.text;
    if(source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ADDR) {
      target.init_kind = mir_model::MirGlobalDefinition::GI_ADDR;
      target.symbol = source.init_operand.text;
      target.addr_addend = source.addr_addend;
    } else if(source.type.kind == lowir_model::LTK_F32 ||
              source.type.kind == lowir_model::LTK_F64) {
      target.init_kind = mir_model::MirGlobalDefinition::GI_FLOAT;
      target.literal_text = source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ZERO ?
        (source.type.kind == lowir_model::LTK_F32 ? "0.0f" : "0.0") :
        source.init_operand.text;
    } else {
      target.init_kind = mir_model::MirGlobalDefinition::GI_INTEGER;
      target.int_value = source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ZERO ?
        0 : integer_literal(source.init_operand.text);
    }
  }
  return target;
}

void append_startup_call(std::vector<MirInstruction> & startup,
                         const std::string & name)
{
  MirInstruction call = machine_instruction(MirInstruction::MI_CALL);
  append_operand(call, named_operand(MirOperand::OP_SYMBOL, name));
  startup.push_back(call);
}

void lower_startup(const lowir_model::LowirProgram & source,
                   mir_model::MirProgram & target)
{
  std::string entry;
  std::string init;
  std::string fini;
  for(std::size_t i = 0; i < source.functions.size(); ++i) {
    const lowir_model::LowirFunction & function = source.functions[i];
    if(function.metadata.role == lowir_model::SR_ENTRY) entry = function.name;
    if(function.metadata.role == lowir_model::SR_INIT) init = function.name;
    if(function.metadata.role == lowir_model::SR_FINI) fini = function.name;
  }
  if(entry.empty()) {
    for(std::size_t i = 0; i < source.functions.size(); ++i) {
      if(source.functions[i].name == "@main") entry = source.functions[i].name;
      if(init.empty() && source.functions[i].name == "@__cppgm_init") init = source.functions[i].name;
      if(fini.empty() && source.functions[i].name == "@__cppgm_fini") fini = source.functions[i].name;
    }
  }
  if(entry.empty()) return;
  if(!init.empty()) append_startup_call(target.startup, init);
  append_startup_call(target.startup, entry);
  if(!fini.empty()) {
    append_move(target.startup, reg_operand(XR_R12), reg_operand(XR_RAX));
    append_startup_call(target.startup, fini);
    append_move(target.startup, reg_operand(XR_RDI), reg_operand(XR_R12));
  } else {
    append_move(target.startup, reg_operand(XR_RDI), reg_operand(XR_RAX));
  }
  target.startup.push_back(machine_instruction(MirInstruction::MI_EXIT));
}

}  // namespace

mir_model::MirProgram lower_program(const lowir_model::LowirProgram & program,
                                    const std::string & target,
                                    Stats * stats)
{
  if(target != "linux") throw std::runtime_error("unsupported native target: " + target);
  mir_model::MirProgram result;
  result.target = target;
  lower_startup(program, result);
  std::unordered_set<std::string> pointer_globals;
  for(std::size_t i = 0; i < program.global_declarations.size(); ++i)
    if(program.global_declarations[i].has_type &&
       program.global_declarations[i].type.kind == lowir_model::LTK_PTR)
      pointer_globals.insert(program.global_declarations[i].name);
  for(std::size_t i = 0; i < program.globals.size(); ++i)
    if(!program.globals[i].structured &&
       program.globals[i].type.kind == lowir_model::LTK_PTR)
      pointer_globals.insert(program.globals[i].name);
  for(std::size_t i = 0; i < program.globals.size(); ++i)
    result.globals.push_back(lower_global(program.globals[i]));
  FunctionSignatureIndex signatures;
  for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
    FunctionSignature signature;
    signature.params = &program.function_declarations[i].params;
    signature.return_type = &program.function_declarations[i].return_type;
    signatures[program.function_declarations[i].name] = signature;
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i) {
    FunctionSignature signature;
    signature.params = &program.functions[i].params;
    signature.return_type = &program.functions[i].return_type;
    signatures[program.functions[i].name] = signature;
  }
  for(std::size_t i = 0; i < program.functions.size(); ++i)
    result.functions.push_back(
      FunctionLowerer(program.functions[i], pointer_globals, signatures).lower());
  result.object_aliases.reserve(program.object_aliases.size());
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    mir_model::MirObjectAlias alias;
    alias.object_symbol = program.object_aliases[i].object_symbol;
    alias.target = program.object_aliases[i].target;
    result.object_aliases.push_back(alias);
  }
  result.exported_symbols = program.exported_symbols;
  if(stats) {
    stats->functions = result.functions.size();
    for(std::size_t i = 0; i < program.functions.size(); ++i) {
      stats->blocks += program.functions[i].blocks.size();
      for(std::size_t j = 0; j < program.functions[i].blocks.size(); ++j)
        stats->lowir_instructions += program.functions[i].blocks[j].instructions.size();
    }
    stats->mir_instructions = result.startup.size();
    for(std::size_t i = 0; i < result.functions.size(); ++i)
      for(std::size_t j = 0; j < result.functions[i].blocks.size(); ++j)
        stats->mir_instructions += result.functions[i].blocks[j].instructions.size();
  }
  return result;
}

}  // namespace lowir_native
