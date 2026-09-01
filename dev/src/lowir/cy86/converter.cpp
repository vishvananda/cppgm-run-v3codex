#include "lowir/cy86/converter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lowir_cy86 {
namespace {

using namespace lowir_model;

struct TypeShape
{
  std::size_t width;
  std::size_t size;
  std::size_t alignment;
  bool object;
  bool floating;
  bool signed_integer;
};

TypeShape shape(const LowType & type)
{
  if(type.kind == LTK_INVALID)
    ThrowLowirInputError("untyped LowIR value reached CY86 lowering");
  TypeShape result = {
    type.kind == LTK_I1 ? 8 : lowir_type_bit_width(type),
    type.storage_size,
    type.alignment,
    type.kind == LTK_OBJECT,
    type.kind >= LTK_F32 && type.kind <= LTK_F80,
    type.kind == LTK_I1 || type.kind == LTK_I8 || type.kind == LTK_I16 ||
      type.kind == LTK_I32 || type.kind == LTK_I64
  };
  return result;
}

std::string register_name(char bank, std::size_t width)
{
  std::ostringstream out;
  out << bank << width;
  return out.str();
}

std::string memory_bp(std::size_t offset)
{
  std::ostringstream out;
  out << "[bp-" << offset << ']';
  return out.str();
}

std::string memory_bp_plus(std::size_t offset)
{
  std::ostringstream out;
  out << "[bp+" << offset << ']';
  return out.str();
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

bool is_f80(const LowType & type) { return type.kind == LTK_F80; }
bool is_large_value(const LowType & type) { return is_f80(type) || shape(type).object; }

const LowType & instruction_result_type(const Instruction & ins)
{
  if(ins.kind == Instruction::IK_ADDR || ins.kind == Instruction::IK_INDEX)
    return builtin_lowir_type(LTK_PTR);
  if(ins.kind == Instruction::IK_CMP || ins.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
     ins.kind == Instruction::IK_EXCEPTION_SELECTOR) return builtin_lowir_type(LTK_I64);
  return ins.type;
}

class TextOutput
{
public:
  void Label(const std::string & value) { out_.append(value).append(":\n"); }
  void Instruction(const std::string & value) { out_.push_back('\t'); out_.append(value).append(";\n"); }
  void Blank() { out_.push_back('\n'); }
  std::string Take() { return std::move(out_); }

private:
  std::string out_;
};

struct Location
{
  std::size_t offset;
  bool address_value;
};

struct MemoryRef
{
  enum Kind
  {
    LOCAL,
    ADDRESS_REGISTER
  } kind;

  std::size_t local_offset;
  char bank;
};

class ProgramEmitter;

struct FunctionTarget
{
  const std::vector<Parameter> * params = 0;
  const Function * definition = 0;

  FunctionTarget() {}
  FunctionTarget(const std::vector<Parameter> * parameters,
                 const Function * body)
    : params(parameters), definition(body) {}
};

class FunctionEmitter
{
public:
  FunctionEmitter(ProgramEmitter & owner, const Function & function, TextOutput & out);
  void Emit();
  std::size_t InstructionCount() const { return instruction_count_; }

private:
  ProgramEmitter & owner_;
  const Function & function_;
  TextOutput & out_;
  std::vector<Location> value_locations_;
  std::vector<Location> slot_locations_;
  std::vector<Location> phi_stage_locations_;
  struct PhiEdgeCopy
  {
    BlockId target;
    ValueId destination;
    Operand source;
    LowType type;
  };
  std::vector<std::vector<PhiEdgeCopy> > phi_edge_copies_;
  Location return_location_;
  std::size_t frame_size_;
  std::size_t scratch_[4];
  std::size_t instruction_count_;
  bool has_f80_;
  bool indirect_result_;
  BlockId current_block_;
  std::size_t phi_edge_label_;

  void BuildLayout();
  Location AddLocation(const LowType & type, bool address_value,
                       std::size_t & used);
  void EmitPrologue();
  void EmitParameterCopies();
  void EmitBlock(const Block & block);
  void EmitInstruction(const Instruction & ins);
  void EmitConstOrCopy(const Instruction & ins);
  void EmitAddress(const Instruction & ins);
  void EmitLoad(const Instruction & ins, bool atomic);
  void EmitStore(const Instruction & ins, bool atomic);
  void EmitIndex(const Instruction & ins);
  void EmitUnary(const Instruction & ins);
  void EmitBinary(const Instruction & ins);
  void EmitCompare(const Instruction & ins);
  void EmitConvert(const Instruction & ins);
  void EmitCall(const Instruction & ins);
  const LowType & CallArgumentType(const Instruction & ins, std::size_t index,
                                   bool direct) const;
  void EmitBulk(const Instruction & ins);
  void EmitAtomic(const Instruction & ins);
  void EmitControl(const Instruction & ins);
  bool HasPhiCopies(BlockId predecessor, BlockId target) const;
  void EmitPhiCopies(BlockId predecessor, BlockId target);
  std::string PhiEdgeLabel(std::size_t ordinal) const;
  void EmitException(const Instruction & ins);
  void EmitResumeSequence();
  void EmitReturn(const Instruction & ins);
  void EmitScalarValue(const Operand & value, const LowType & type, char bank);
  void EmitAddressValue(const Operand & value, char bank);
  void EmitStorageLoad(const Operand & storage, const LowType & type, char bank);
  void EmitStorageStore(const Operand & storage, const LowType & type, char bank);
  void StoreScalarTemp(ValueId value, const LowType & type, char bank);
  void EmitSignExtend(char bank, std::size_t width);
  void LoadF80(const Operand & value, std::size_t scratch_index);
  void LoadF80FromAddress(char bank, std::size_t scratch_index);
  void StoreF80Temp(ValueId value, std::size_t scratch_index);
  void ZeroF80Padding(std::size_t scratch_index);
  void CopyBytes(const MemoryRef & source, const MemoryRef & dest, std::size_t bytes);
  std::string MemoryAt(const MemoryRef & memory, std::size_t byte_offset) const;
  std::string ValueMemory(ValueId value) const;
  std::string SlotMemory(SlotId slot) const;
  std::string SlotAddress(SlotId slot) const;
  std::string BlockLabel(const std::string & block) const;
  std::string BlockLabel(const Operand & block) const;
  std::string EpilogueLabel() const;
};

class ProgramEmitter
{
public:
  ProgramEmitter(const Program & program, Stats * stats)
    : program_(program), stats_(stats), uses_eh_(false), eh_label_counter_(0),
      atomic_label_counter_(0)
  {
	functions_by_id_.resize(program.symbol_names.size());
	function_known_.assign(program.symbol_names.size(), 0);
    for(std::size_t i = 0; i < program.function_declarations.size(); ++i) {
      const FunctionDeclaration & function = program.function_declarations[i];
	  functions_by_id_[function.symbol] = FunctionTarget{&function.params, 0};
	  function_known_[function.symbol] = 1;
    }
    for(std::size_t i = 0; i < program.functions.size(); ++i) {
      const Function & function = program.functions[i];
	  functions_by_id_[function.symbol] = FunctionTarget{&function.params, &function};
	  function_known_[function.symbol] = 1;
    }
    FindRoles();
  }

  std::string Emit()
  {
    DetectEh();
    EmitStart();
    for(std::size_t i = 0; i < program_.functions.size(); ++i) {
      FunctionEmitter emitter(*this, program_.functions[i], out_);
      emitter.Emit();
      if(stats_) {
        ++stats_->functions;
        stats_->blocks += program_.functions[i].blocks.size();
        stats_->instructions += emitter.InstructionCount();
      }
    }
    if(uses_eh_) EmitDefaultUnhandled();
    for(std::size_t i = 0; i < program_.globals.size(); ++i) EmitGlobal(program_.globals[i]);
    if(uses_eh_) EmitDefaultEhGlobals();
    std::string result = out_.Take();
    if(stats_) stats_->output_bytes = result.size();
    return result;
  }

  std::string SymbolLabel(SymbolId symbol) const
  {
    const std::string & name = lowir_symbol_name(program_, symbol);
    return (function_known_[symbol] ? "fn__" : "g__") + name;
  }

  bool IsFunction(SymbolId symbol) const { return function_known_[symbol] != 0; }
  const FunctionTarget * FindFunction(SymbolId symbol) const
  {
    return function_known_[symbol] ? &functions_by_id_[symbol] : 0;
  }
  const std::string & OperandText(const Operand & operand) const
  {
    if((operand.kind != Operand::OP_FLOAT &&
        operand.kind != Operand::OP_INTEGER) || !operand.has_spelling)
      ThrowLowirInternalError("CY86 operand has no literal spelling");
    return program_.strings.get(operand.literal);
  }

  std::string EhTopLabel() const
  {
    return "g____cppgm_eh_top";
  }
  std::string EhValueLabel() const
  {
    return "g____cppgm_eh_value";
  }
  std::string EhUnhandledLabel() const
  {
    return "fn____cppgm_eh_unhandled";
  }

private:
  friend class FunctionEmitter;
  const Program & program_;
  Stats * stats_;
  TextOutput out_;
  std::vector<FunctionTarget> functions_by_id_;
  std::vector<unsigned char> function_known_;
  SymbolId entry_;
  SymbolId init_;
  SymbolId fini_;
  bool uses_eh_;
  std::size_t eh_label_counter_;
  std::size_t atomic_label_counter_;

  void FindRoles();
  SymbolId FindSymbol(const std::string & name) const;
  void RecordRole(SymbolId symbol, SymbolRole role);
  void DetectEh();
  void EmitStart();
  void EmitGlobal(const GlobalDefinition & global);
  void EmitDataItem(const GlobalDefinition::DataItem & item, std::size_t & offset);
  void EmitPadding(std::size_t bytes, std::size_t & offset);
  void EmitF80Data(const std::string & literal, std::size_t & offset);
  void EmitDefaultUnhandled();
  void EmitDefaultEhGlobals();
};

SymbolId ProgramEmitter::FindSymbol(const std::string & name) const
{
  for(std::size_t i = 0; i < program_.symbol_names.size(); ++i)
    if(lowir_model::lowir_symbol_name(
         program_, SymbolId(static_cast<std::uint32_t>(i))) == name)
      return SymbolId(static_cast<std::uint32_t>(i));
  return SymbolId();
}

void ProgramEmitter::RecordRole(SymbolId symbol, SymbolRole role)
{
  if(role == SR_ENTRY) entry_ = symbol;
  else if(role == SR_INIT) init_ = symbol;
  else if(role == SR_FINI) fini_ = symbol;
}

void ProgramEmitter::FindRoles()
{
  for(std::size_t i = 0; i < program_.functions.size(); ++i)
    RecordRole(program_.functions[i].symbol, program_.functions[i].metadata.role);
  const SymbolId main_symbol = FindSymbol("main");
  const SymbolId init_symbol = FindSymbol("__cppgm_init");
  const SymbolId fini_symbol = FindSymbol("__cppgm_fini");
  const FunctionTarget * main = main_symbol.valid() ? FindFunction(main_symbol) : 0;
  const FunctionTarget * init = init_symbol.valid() ? FindFunction(init_symbol) : 0;
  const FunctionTarget * fini = fini_symbol.valid() ? FindFunction(fini_symbol) : 0;
  if(!entry_.valid() && main && main->definition) entry_ = main_symbol;
  if(!init_.valid() && init && init->definition) init_ = init_symbol;
  if(!fini_.valid() && fini && fini->definition) fini_ = fini_symbol;
  if(!entry_.valid())
    ThrowLowirInputError("LowIR program has no entry definition");
}

void ProgramEmitter::DetectEh()
{
  for(std::size_t i = 0; i < program_.functions.size(); ++i)
    for(std::size_t j = 0; j < program_.functions[i].blocks.size(); ++j)
      for(std::size_t k = 0; k < program_.functions[i].blocks[j].instructions.size(); ++k) {
        const Instruction::Kind kind = program_.functions[i].blocks[j].instructions[k].kind;
        if(kind >= Instruction::IK_EH_TRY && kind <= Instruction::IK_RESUME) uses_eh_ = true;
      }
}

void ProgramEmitter::EmitStart()
{
  out_.Label("start");
  out_.Instruction("move64 bp sp");
  if(init_.valid()) out_.Instruction("call " + SymbolLabel(init_));
  out_.Instruction("call " + SymbolLabel(entry_));
  if(fini_.valid()) {
    out_.Instruction("isub64 sp sp 8");
    out_.Instruction("move64 [sp] x64");
    out_.Instruction("call " + SymbolLabel(fini_));
    out_.Instruction("move64 x64 [sp]");
    out_.Instruction("iadd64 sp sp 8");
  }
  out_.Instruction("syscall1 t64 60 x64");
  out_.Blank();
}

void ProgramEmitter::EmitPadding(std::size_t bytes, std::size_t & offset)
{
  for(std::size_t i = 0; i < bytes; ++i) out_.Instruction("data8 0");
  offset += bytes;
}

void ProgramEmitter::EmitF80Data(const std::string & literal, std::size_t & offset)
{
  const long double value = std::strtold(literal.c_str(), 0);
  std::int64_t low = 0;
  std::uint16_t high = 0;
  std::memcpy(&low, &value, sizeof(low));
  std::memcpy(&high, reinterpret_cast<const char *>(&value) + 8, sizeof(high));
  out_.Instruction("data64 " + std::to_string(low));
  out_.Instruction("data16 " + std::to_string(high));
  offset += 10;
  EmitPadding(6, offset);
}

void ProgramEmitter::EmitDataItem(const GlobalDefinition::DataItem & item,
                                  std::size_t & offset)
{
  if(item.kind == GlobalDefinition::DataItem::ITEM_ZERO) {
    EmitPadding(item.zero_bytes, offset);
    return;
  }
  const TypeShape item_shape = shape(item.type);
  const std::size_t aligned = align_up(offset, item_shape.alignment);
  EmitPadding(aligned - offset, offset);
  if(item.kind == GlobalDefinition::DataItem::ITEM_ADDR) {
    std::string value = SymbolLabel(item.symbol_id);
    if(item.addr_addend > 0) value = '(' + value + '+' + std::to_string(item.addr_addend) + ')';
    if(item.addr_addend < 0) value = '(' + value + std::to_string(item.addr_addend) + ')';
    out_.Instruction("data64 " + value);
    offset += 8;
  } else if(item.type.kind == LTK_F80) EmitF80Data(OperandText(item.literal_operand), offset);
  else {
    out_.Instruction("data" + std::to_string(item_shape.width) + " " +
                     OperandText(item.literal_operand));
    offset += item_shape.size;
  }
}

void ProgramEmitter::EmitGlobal(const GlobalDefinition & global)
{
  out_.Label(SymbolLabel(global.symbol));
  std::size_t offset = 0;
  if(global.structured) {
    for(std::size_t i = 0; i < global.data_items.size(); ++i)
      EmitDataItem(global.data_items[i], offset);
  } else if(global.type.kind == LTK_F80) {
    if(global.init_kind == GlobalDefinition::INIT_ZERO) EmitF80Data("0.0L", offset);
    else EmitF80Data(OperandText(global.init_operand), offset);
  } else {
    const TypeShape global_shape = shape(global.type);
    std::string value = "0";
    if(global.init_kind == GlobalDefinition::INIT_INTEGER)
      value = OperandText(global.init_operand);
    else if(global.init_kind == GlobalDefinition::INIT_ADDR) {
      value = SymbolLabel(global.init_operand.symbol);
      if(global.addr_addend > 0) value = '(' + value + '+' + std::to_string(global.addr_addend) + ')';
      if(global.addr_addend < 0) value = '(' + value + std::to_string(global.addr_addend) + ')';
    }
    out_.Instruction("data" + std::to_string(global_shape.width) + " " + value);
  }
  out_.Blank();
}

void ProgramEmitter::EmitDefaultUnhandled()
{
  out_.Label("fn____cppgm_eh_unhandled");
  out_.Instruction("syscall1 t64 60 x64");
  out_.Blank();
}

void ProgramEmitter::EmitDefaultEhGlobals()
{
  out_.Label("g____cppgm_eh_top"); out_.Instruction("data64 0"); out_.Blank();
  out_.Label("g____cppgm_eh_value"); out_.Instruction("data64 0"); out_.Blank();
}

FunctionEmitter::FunctionEmitter(ProgramEmitter & owner, const Function & function,
                                 TextOutput & out)
  : owner_(owner), function_(function), out_(out), frame_size_(0),
    instruction_count_(0), has_f80_(false),
    indirect_result_(is_large_value(function.return_type)),
    phi_edge_label_(0)
{
  std::fill(scratch_, scratch_ + 4, 0);
  value_locations_.resize(function_.value_names.size());
  slot_locations_.resize(function_.slot_names.size());
  phi_stage_locations_.resize(function_.value_names.size());
  phi_edge_copies_.resize(function_.next_block_id);
  for(std::size_t b = 0; b < function_.blocks.size(); ++b) {
    const Block & block = function_.blocks[b];
    for(std::size_t i = 0; i < block.instructions.size(); ++i) {
      const Instruction & phi = block.instructions[i];
      if(phi.kind != Instruction::IK_PHI) break;
      for(std::size_t incoming = 0; incoming + 1 < phi.args.size(); incoming += 2) {
        const BlockId predecessor = phi.args[incoming].block;
        if(static_cast<std::uint32_t>(predecessor) >= phi_edge_copies_.size())
          ThrowLowirInputError("invalid phi predecessor identity");
        phi_edge_copies_[predecessor].push_back(
          PhiEdgeCopy{block.id, phi.dest, phi.args[incoming + 1], phi.type});
      }
    }
  }
  BuildLayout();
}

Location FunctionEmitter::AddLocation(const LowType & type,
                                      bool address_value, std::size_t & used)
{
  const TypeShape item = shape(type);
  const std::size_t bytes = is_large_value(type) ? item.size : 8;
  used = align_up(used, 8) + bytes;
  if(is_f80(type)) has_f80_ = true;
  return Location{used, address_value};
}

void FunctionEmitter::BuildLayout()
{
  std::size_t used = 0;
  if(indirect_result_)
    return_location_ = AddLocation(
      builtin_lowir_type(LTK_PTR), false, used);
  for(std::size_t i = 0; i < function_.params.size(); ++i) {
    const Parameter & param = function_.params[i];
    value_locations_[param.value] =
      AddLocation(param.type, is_f80(param.type), used);
  }
  for(std::size_t i = 0; i < function_.slots.size(); ++i) {
    const SlotId slot = function_.slots[i];
    slot_locations_[slot] = AddLocation(
      lowir_model::lowir_slot_type(function_, slot), true, used);
  }
  for(std::size_t b = 0; b < function_.blocks.size(); ++b)
    for(std::size_t i = 0; i < function_.blocks[b].instructions.size(); ++i) {
      const Instruction & ins = function_.blocks[b].instructions[i];
      if(ins.dest.valid()) {
        const LowType & result_type = instruction_result_type(ins);
        value_locations_[ins.dest] = AddLocation(
          result_type, is_large_value(result_type), used);
        if(ins.kind == Instruction::IK_PHI)
          phi_stage_locations_[ins.dest] = AddLocation(
            result_type, false, used);
      }
      if(is_f80(ins.type) || is_f80(ins.source_type) || ins.kind == Instruction::IK_CONVERT)
        has_f80_ = true;
    }
  if(has_f80_) {
    for(std::size_t i = 0; i < 4; ++i) {
      used += 16;
      scratch_[i] = used;
    }
  }
  frame_size_ = used;
}

std::string FunctionEmitter::ValueMemory(ValueId value) const
{
  if(static_cast<std::uint32_t>(value) >= value_locations_.size())
    ThrowLowirInternalError("missing LowIR value layout");
  return memory_bp(value_locations_[value].offset);
}

std::string FunctionEmitter::SlotMemory(SlotId slot) const
{
  if(static_cast<std::uint32_t>(slot) >= slot_locations_.size())
    ThrowLowirInternalError("missing LowIR slot layout");
  return memory_bp(slot_locations_[slot].offset);
}

std::string FunctionEmitter::SlotAddress(SlotId slot) const
{
  if(static_cast<std::uint32_t>(slot) >= slot_locations_.size())
    ThrowLowirInternalError("missing LowIR slot layout");
  return std::to_string(slot_locations_[slot].offset);
}

std::string FunctionEmitter::BlockLabel(const std::string & block) const
{
  return "fn__" + lowir_model::lowir_symbol_name(
    owner_.program_, function_.symbol) + "__" + block;
}

std::string FunctionEmitter::BlockLabel(const Operand & block) const
{
  return BlockLabel(lowir_model::lowir_block_label(
    owner_.program_.strings, function_, block.block));
}

std::string FunctionEmitter::EpilogueLabel() const
{
  return "fn__" + lowir_model::lowir_symbol_name(
    owner_.program_, function_.symbol) + "__epilogue";
}

void FunctionEmitter::Emit()
{
  EmitPrologue();
  for(std::size_t i = 0; i < function_.blocks.size(); ++i) EmitBlock(function_.blocks[i]);
  out_.Label(EpilogueLabel());
  out_.Instruction("move64 sp bp");
  out_.Instruction("move64 bp [sp]");
  out_.Instruction("iadd64 sp sp 8");
  out_.Instruction("ret");
  out_.Blank();
}

void FunctionEmitter::EmitPrologue()
{
  out_.Label(owner_.SymbolLabel(function_.symbol));
  out_.Instruction("isub64 sp sp 8");
  out_.Instruction("move64 [sp] bp");
  out_.Instruction("move64 bp sp");
  if(frame_size_) out_.Instruction("isub64 sp sp " + std::to_string(frame_size_));
  EmitParameterCopies();
}

void FunctionEmitter::EmitParameterCopies()
{
  static const char banks[] = {'x', 'y', 'z', 't'};
  std::size_t abi_index = 0;
  if(indirect_result_) {
    out_.Instruction("move64 " + memory_bp(return_location_.offset) + " x64");
    ++abi_index;
  }
  for(std::size_t i = 0; i < function_.params.size(); ++i, ++abi_index) {
    const Parameter & param = function_.params[i];
    const TypeShape item = shape(param.type);
    const std::string dest = ValueMemory(param.value);
    if(abi_index < 4) {
      const char bank = banks[abi_index];
      if(is_large_value(param.type)) {
        out_.Instruction("move64 x64 " + register_name(bank, 64));
        CopyBytes(MemoryRef{MemoryRef::ADDRESS_REGISTER, 0, 'x'},
                  MemoryRef{MemoryRef::LOCAL,
                    value_locations_[param.value].offset, 0}, item.size);
      } else {
        out_.Instruction("move" + std::to_string(item.width) + " " + dest + " " +
                         register_name(bank, item.width));
      }
    } else {
      out_.Instruction("move64 x64 " + memory_bp_plus(16 + (abi_index - 4) * 8));
      out_.Instruction("move64 " + dest + " x64");
    }
  }
}

void FunctionEmitter::EmitBlock(const Block & block)
{
  current_block_ = block.id;
  out_.Label(BlockLabel(lowir_model::lowir_block_label(
    owner_.program_.strings, function_, block.id)));
  for(std::size_t i = 0; i < block.instructions.size(); ++i) {
    EmitInstruction(block.instructions[i]);
    ++instruction_count_;
  }
}

void FunctionEmitter::EmitInstruction(const Instruction & ins)
{
  switch(ins.kind) {
  case Instruction::IK_CONST: case Instruction::IK_COPY: EmitConstOrCopy(ins); break;
  case Instruction::IK_ADDR: EmitAddress(ins); break;
  case Instruction::IK_LOAD: EmitLoad(ins, false); break;
  case Instruction::IK_ATOMIC_LOAD: EmitLoad(ins, true); break;
  case Instruction::IK_STORE: EmitStore(ins, false); break;
  case Instruction::IK_ATOMIC_STORE: EmitStore(ins, true); break;
  case Instruction::IK_INDEX: EmitIndex(ins); break;
  case Instruction::IK_UNARY: EmitUnary(ins); break;
  case Instruction::IK_BINARY: EmitBinary(ins); break;
  case Instruction::IK_CMP: EmitCompare(ins); break;
  case Instruction::IK_CONVERT: EmitConvert(ins); break;
  case Instruction::IK_CALL: EmitCall(ins); break;
  case Instruction::IK_COPYOBJ: case Instruction::IK_ZEROINIT: EmitBulk(ins); break;
  case Instruction::IK_ATOMIC_ADD_FETCH: case Instruction::IK_ATOMIC_EXCHANGE:
  case Instruction::IK_ATOMIC_COMPARE_EXCHANGE: EmitAtomic(ins); break;
  case Instruction::IK_ATOMIC_THREAD_FENCE: case Instruction::IK_ATOMIC_SIGNAL_FENCE: break;
  case Instruction::IK_JUMP: case Instruction::IK_BRANCH: case Instruction::IK_SWITCH:
    EmitControl(ins); break;
  case Instruction::IK_EH_TRY: case Instruction::IK_EH_CLEANUP: case Instruction::IK_EH_END:
  case Instruction::IK_THROW: case Instruction::IK_EXCEPTION: case Instruction::IK_RESUME:
    EmitException(ins); break;
  case Instruction::IK_RETURN: EmitReturn(ins); break;
  case Instruction::IK_UNREACHABLE: break;
  case Instruction::IK_PHI: break;
  default:
    ThrowLowirInputError("unsupported PA13 instruction in CY86 emitter");
  }
}

void FunctionEmitter::EmitScalarValue(const Operand & value, const LowType & type, char bank)
{
  const TypeShape item = shape(type);
  const std::string reg = register_name(bank, item.width);
  const std::string reg64 = register_name(bank, 64);
  if(value.kind == Operand::OP_INTEGER || value.kind == Operand::OP_FLOAT) {
    const std::string & spelling = owner_.OperandText(value);
    const std::string literal = spelling == "nullptr" ? "0" : spelling;
    const std::size_t move_width = item.floating ? item.width : 64;
    out_.Instruction("move" + std::to_string(move_width) + " " +
                     register_name(bank, move_width) + " " + literal);
  } else if(value.kind == Operand::OP_SLOT) {
    out_.Instruction("isub64 " + reg64 + " bp " + SlotAddress(value.slot));
  } else if(value.kind == Operand::OP_GLOBAL) {
    out_.Instruction("move64 " + reg64 + " " + owner_.SymbolLabel(value.symbol));
  } else {
    const Location & location = value_locations_.at(value.value);
    if(location.address_value)
      out_.Instruction("isub64 " + reg64 + " bp " + std::to_string(location.offset));
    else {
      if(item.width < 32) out_.Instruction("move64 " + reg64 + " 0");
      out_.Instruction("move" + std::to_string(item.width) + " " + reg + " " +
                       memory_bp(location.offset));
    }
  }
}

void FunctionEmitter::EmitAddressValue(const Operand & value, char bank)
{
  const std::string reg = register_name(bank, 64);
  if(value.kind == Operand::OP_SLOT) {
    out_.Instruction("isub64 " + reg + " bp " + SlotAddress(value.slot));
  } else if(value.kind == Operand::OP_GLOBAL) {
    out_.Instruction("move64 " + reg + " " + owner_.SymbolLabel(value.symbol));
  } else if(value.kind == Operand::OP_TEMP) {
    const Location & location = value_locations_.at(value.value);
    if(location.address_value)
      out_.Instruction("isub64 " + reg + " bp " + std::to_string(location.offset));
    else out_.Instruction("move64 " + reg + " " + memory_bp(location.offset));
  } else {
    const std::string & spelling = owner_.OperandText(value);
    out_.Instruction("move64 " + reg + " " +
                     (spelling == "nullptr" ? "0" : spelling));
  }
}

void FunctionEmitter::EmitStorageLoad(const Operand & storage, const LowType & type, char bank)
{
  const TypeShape item = shape(type);
  const std::string reg = register_name(bank, item.width);
  if(storage.kind == Operand::OP_SLOT)
    out_.Instruction("move" + std::to_string(item.width) + " " + reg + " " +
                     SlotMemory(storage.slot));
  else if(storage.kind == Operand::OP_GLOBAL)
    out_.Instruction("move" + std::to_string(item.width) + " " + reg + " [" +
                     owner_.SymbolLabel(storage.symbol) + "]");
  else {
    EmitAddressValue(storage, bank);
    out_.Instruction("move" + std::to_string(item.width) + " " + reg + " [" +
                     register_name(bank, 64) + "]");
  }
}

void FunctionEmitter::EmitStorageStore(const Operand & storage, const LowType & type, char bank)
{
  const TypeShape item = shape(type);
  const std::string reg = register_name(bank, item.width);
  if(storage.kind == Operand::OP_SLOT)
    out_.Instruction("move" + std::to_string(item.width) + " " +
                     SlotMemory(storage.slot) + " " + reg);
  else if(storage.kind == Operand::OP_GLOBAL)
    out_.Instruction("move" + std::to_string(item.width) + " [" + owner_.SymbolLabel(storage.symbol) +
                     "] " + reg);
  else {
    EmitAddressValue(storage, 'y');
    out_.Instruction("move" + std::to_string(item.width) + " [y64] " + reg);
  }
}

void FunctionEmitter::StoreScalarTemp(ValueId value, const LowType & type, char bank)
{
  const TypeShape item = shape(type);
  out_.Instruction("move" + std::to_string(item.width) + " " + ValueMemory(value) + " " +
                   register_name(bank, item.width));
}

void FunctionEmitter::EmitSignExtend(char bank, std::size_t width)
{
  if(width >= 64) return;
  const std::size_t shift = 64 - width;
  out_.Instruction("move8 t8 " + std::to_string(shift));
  out_.Instruction("lshift64 " + register_name(bank, 64) + " " + register_name(bank, 64) + " t8");
  out_.Instruction("srshift64 " + register_name(bank, 64) + " " + register_name(bank, 64) + " t8");
}

void FunctionEmitter::EmitConstOrCopy(const Instruction & ins)
{
  if(is_f80(ins.type)) {
    LoadF80(ins.first, 0); StoreF80Temp(ins.dest, 0); return;
  }
  EmitScalarValue(ins.first, ins.type, 'x');
  StoreScalarTemp(ins.dest, ins.type, 'x');
}

void FunctionEmitter::EmitAddress(const Instruction & ins)
{
  EmitAddressValue(ins.first, 'x');
  StoreScalarTemp(ins.dest, instruction_result_type(ins), 'x');
}

void FunctionEmitter::EmitLoad(const Instruction & ins, bool atomic)
{
  if(is_f80(ins.type)) {
    EmitAddressValue(ins.first, 'x'); LoadF80FromAddress('x', 0); StoreF80Temp(ins.dest, 0); return;
  }
  if(atomic) {
    EmitAddressValue(ins.first, 'y');
    const std::string width = std::to_string(shape(ins.type).width);
    out_.Instruction("move" + width + " x" + width + " [y64]");
  } else EmitStorageLoad(ins.first, ins.type, 'x');
  const TypeShape item = shape(ins.type);
  if(item.signed_integer) EmitSignExtend('x', item.width);
  StoreScalarTemp(ins.dest, ins.type, 'x');
}

void FunctionEmitter::EmitStore(const Instruction & ins, bool atomic)
{
  if(is_f80(ins.type)) {
    LoadF80(ins.first, 0); EmitAddressValue(ins.second, 'x');
    CopyBytes(MemoryRef{MemoryRef::LOCAL, scratch_[0], 0},
              MemoryRef{MemoryRef::ADDRESS_REGISTER, 0, 'x'}, 16);
    return;
  }
  if(atomic) {
    EmitAddressValue(ins.second, 'y');
    EmitScalarValue(ins.first, ins.type, 'x');
    const std::string width = std::to_string(shape(ins.type).width);
    out_.Instruction("move" + width + " [y64] x" + width);
  } else {
    EmitScalarValue(ins.first, ins.type, 'x');
    EmitStorageStore(ins.second, ins.type, 'x');
  }
}

void FunctionEmitter::EmitIndex(const Instruction & ins)
{
  EmitAddressValue(ins.first, 'y');
  EmitScalarValue(ins.second, builtin_lowir_type(LTK_I64), 'x');
  if(shape(ins.type).size != 1) {
    out_.Instruction("move64 z64 " + std::to_string(shape(ins.type).size));
    out_.Instruction("smul64 x64 x64 z64");
  }
  out_.Instruction("iadd64 x64 y64 x64");
  StoreScalarTemp(ins.dest, instruction_result_type(ins), 'x');
}

void FunctionEmitter::EmitUnary(const Instruction & ins)
{
  if(is_f80(ins.type)) {
    LoadF80(ins.first, 0);
    if(ins.op.kind != LowOperation::LOP_NEG)
      ThrowLowirInputError("unsupported f80 unary operation");
    out_.Instruction("move80 " + memory_bp(scratch_[1]) + " 0.0L");
    ZeroF80Padding(1);
    out_.Instruction("fsub80 " + memory_bp(scratch_[2]) + " " + memory_bp(scratch_[1]) +
                     " " + memory_bp(scratch_[0]));
    ZeroF80Padding(2); StoreF80Temp(ins.dest, 2); return;
  }
  EmitScalarValue(ins.first, ins.type, 'x');
  const TypeShape item = shape(ins.type);
  const std::string width = std::to_string(item.width);
  if(ins.op.kind == LowOperation::LOP_NEG) {
    out_.Instruction("move" + width + " y" + width + " 0");
    out_.Instruction((item.floating ? "fsub" : "isub") + width + " x" + width + " y" + width + " x" + width);
  } else if(ins.op.kind == LowOperation::LOP_NOT) {
    out_.Instruction("ieq" + width + " z8 x" + width + " 0");
    out_.Instruction("move64 x64 0"); out_.Instruction("move8 x8 z8");
  } else if(ins.op.kind == LowOperation::LOP_BITNOT) out_.Instruction("not" + width + " x" + width + " x" + width);
  else if(ins.op.kind == LowOperation::LOP_BSWAP) out_.Instruction("bswap" + width + " x" + width + " x" + width);
  else ThrowLowirInputError("unsupported unary operator");
  StoreScalarTemp(ins.dest, instruction_result_type(ins), 'x');
}

void FunctionEmitter::EmitBinary(const Instruction & ins)
{
  if(is_f80(ins.type)) {
    LoadF80(ins.first, 0); LoadF80(ins.second, 1);
    const std::string opcode = std::string("f") + lowir_operation_text(ins.op) + "80";
    out_.Instruction(opcode + " " + memory_bp(scratch_[2]) + " " + memory_bp(scratch_[0]) +
                     " " + memory_bp(scratch_[1]));
    ZeroF80Padding(2); StoreF80Temp(ins.dest, 2); return;
  }
  const TypeShape item = shape(ins.type);
  EmitScalarValue(ins.first, ins.type, 'y');
  EmitScalarValue(ins.second, ins.type, 'x');
  const std::string width = std::to_string(item.width);
  std::string opcode;
  if(item.floating) opcode = std::string("f") + lowir_operation_text(ins.op);
  else if(ins.op.kind == LowOperation::LOP_ADD) opcode = "iadd";
  else if(ins.op.kind == LowOperation::LOP_SUB) opcode = "isub";
  else if(ins.op.kind == LowOperation::LOP_MUL) opcode = "smul";
  else if(ins.op.kind == LowOperation::LOP_DIV) opcode = "sdiv";
  else if(ins.op.kind == LowOperation::LOP_MOD) opcode = "smod";
  else if(ins.op.kind == LowOperation::LOP_UDIV || ins.op.kind == LowOperation::LOP_UMOD)
    opcode = lowir_operation_text(ins.op);
  else if(ins.op.kind == LowOperation::LOP_AND || ins.op.kind == LowOperation::LOP_OR ||
          ins.op.kind == LowOperation::LOP_XOR)
    opcode = lowir_operation_text(ins.op);
  else if(ins.op.kind == LowOperation::LOP_SHL) opcode = "lshift";
  else if(ins.op.kind == LowOperation::LOP_SHR) opcode = "srshift";
  else if(ins.op.kind == LowOperation::LOP_USHR) opcode = "urshift";
  else ThrowLowirInputError("unsupported binary operator");
  if(ins.op.kind == LowOperation::LOP_SHL || ins.op.kind == LowOperation::LOP_SHR || ins.op.kind == LowOperation::LOP_USHR) {
    out_.Instruction("move64 z64 x64");
    out_.Instruction("move8 x8 z8");
    out_.Instruction(opcode + width + " x" + width + " y" + width + " x8");
  } else out_.Instruction(opcode + width + " x" + width + " y" + width + " x" + width);
  StoreScalarTemp(ins.dest, ins.type, 'x');
}

void FunctionEmitter::EmitCompare(const Instruction & ins)
{
  if(is_f80(ins.type)) {
    LoadF80(ins.first, 0); LoadF80(ins.second, 1);
    out_.Instruction(std::string("f") + lowir_operation_text(ins.op) + "80 z8 " +
                     memory_bp(scratch_[0]) + " " + memory_bp(scratch_[1]));
  } else {
    const TypeShape item = shape(ins.type);
    EmitScalarValue(ins.first, ins.type, 'y'); EmitScalarValue(ins.second, ins.type, 'x');
    std::string prefix;
    if(item.floating) prefix = "f";
    else if(ins.op.kind == LowOperation::LOP_EQ || ins.op.kind == LowOperation::LOP_NE) prefix = "i";
    else if(ins.op.kind == LowOperation::LOP_ULT || ins.op.kind == LowOperation::LOP_ULE ||
            ins.op.kind == LowOperation::LOP_UGT || ins.op.kind == LowOperation::LOP_UGE)
      prefix = "";
    else prefix = "s";
    out_.Instruction(prefix + lowir_operation_text(ins.op) +
                     std::to_string(item.width) + " z8 " +
                     register_name('y', item.width) + " " + register_name('x', item.width));
  }
  out_.Instruction("move64 x64 0"); out_.Instruction("move8 x8 z8");
  StoreScalarTemp(ins.dest, builtin_lowir_type(LTK_I64), 'x');
}

void FunctionEmitter::ZeroF80Padding(std::size_t scratch_index)
{
  const std::size_t base = scratch_[scratch_index];
  out_.Instruction("move64 z64 0");
  out_.Instruction("move32 " + memory_bp(base - 10) + " z32");
  out_.Instruction("move16 " + memory_bp(base - 14) + " z16");
}

std::string FunctionEmitter::MemoryAt(const MemoryRef & memory,
                                      std::size_t byte_offset) const
{
  if(memory.kind == MemoryRef::LOCAL) {
    if(byte_offset > memory.local_offset)
      ThrowLowirInternalError("invalid local memory span");
    return memory_bp(memory.local_offset - byte_offset);
  }
  const std::string reg = register_name(memory.bank, 64);
  return byte_offset ? "[" + reg + "+" + std::to_string(byte_offset) + "]" : "[" + reg + "]";
}

void FunctionEmitter::CopyBytes(const MemoryRef & source, const MemoryRef & dest,
                                std::size_t bytes)
{
  std::size_t offset = 0;
  while(offset < bytes) {
    std::size_t width = 8;
    while(width > 1 && (bytes - offset < width || offset % width)) width /= 2;
    const std::string width_text = std::to_string(width * 8);
    out_.Instruction("move" + width_text + " z" + width_text + " " + MemoryAt(source, offset));
    out_.Instruction("move" + width_text + " " + MemoryAt(dest, offset) + " z" + width_text);
    offset += width;
  }
}

void FunctionEmitter::LoadF80FromAddress(char bank, std::size_t scratch_index)
{
  const std::string address = register_name(bank, 64);
  out_.Instruction("move64 z64 [" + address + "]");
  out_.Instruction("move64 " + memory_bp(scratch_[scratch_index]) + " z64");
  out_.Instruction("move64 z64 [" + address + "+8]");
  out_.Instruction("move64 " + memory_bp(scratch_[scratch_index] - 8) + " z64");
}

void FunctionEmitter::LoadF80(const Operand & value, std::size_t scratch_index)
{
  if(value.kind == Operand::OP_FLOAT || value.kind == Operand::OP_INTEGER) {
    out_.Instruction("move80 " + memory_bp(scratch_[scratch_index]) + " " +
                     owner_.OperandText(value));
    ZeroF80Padding(scratch_index);
  } else {
    EmitAddressValue(value, 'x');
    LoadF80FromAddress('x', scratch_index);
  }
}

void FunctionEmitter::StoreF80Temp(ValueId value, std::size_t scratch_index)
{
  const std::size_t dest = value_locations_.at(value).offset;
  CopyBytes(MemoryRef{MemoryRef::LOCAL, scratch_[scratch_index], 0},
            MemoryRef{MemoryRef::LOCAL, dest, 0}, 16);
}

void FunctionEmitter::EmitConvert(const Instruction & ins)
{
  const TypeShape source = shape(ins.source_type);
  const TypeShape dest = shape(ins.type);
  if((ins.op.kind == LowOperation::LOP_SEXT || ins.op.kind == LowOperation::LOP_ZEXT || ins.op.kind == LowOperation::LOP_TRUNC) &&
     !source.floating && !dest.floating) {
    EmitScalarValue(ins.first, ins.source_type, 'x');
    if(ins.op.kind == LowOperation::LOP_SEXT) EmitSignExtend('x', source.width);
    StoreScalarTemp(ins.dest, ins.type, 'x');
    return;
  }
  if(is_f80(ins.source_type)) LoadF80(ins.first, 0);
  else {
    EmitScalarValue(ins.first, ins.source_type, 'x');
    std::string source_prefix;
    if(source.floating) source_prefix = "f" + std::to_string(source.width);
    else source_prefix = (ins.op.kind == LowOperation::LOP_UITOFP ? "u" : "s") + std::to_string(source.width);
    out_.Instruction(source_prefix + "convf80 " + memory_bp(scratch_[0]) + " " +
                     register_name('x', source.width));
    ZeroF80Padding(0);
  }
  if(is_f80(ins.type)) StoreF80Temp(ins.dest, 0);
  else {
    std::string dest_prefix;
    if(dest.floating) dest_prefix = "f" + std::to_string(dest.width);
    else dest_prefix = (ins.op.kind == LowOperation::LOP_FPTOUI ? "u" : "s") + std::to_string(dest.width);
    out_.Instruction("f80conv" + dest_prefix + " " + ValueMemory(ins.dest) + " " + memory_bp(scratch_[0]));
  }
}

void FunctionEmitter::EmitCall(const Instruction & ins)
{
  static const char banks[] = {'x', 'y', 'z', 't'};
  const bool large_return = is_large_value(ins.type);
  const bool direct = ins.first.kind == Operand::OP_GLOBAL &&
    owner_.IsFunction(ins.first.symbol);
  const std::size_t abi_count = ins.args.size() + (large_return ? 1 : 0);
  const std::size_t extras = abi_count > 4 ? abi_count - 4 : 0;
  const std::size_t reserve = extras ? extras * 8 + (direct ? 0 : 8) : (direct ? 0 : 8);
  if(!direct) EmitAddressValue(ins.first, 'x');
  if(reserve) out_.Instruction("isub64 sp sp " + std::to_string(reserve));
  const std::size_t callee_offset = extras * 8;
  if(!direct) {
    const std::string callee = callee_offset ? "[sp+" + std::to_string(callee_offset) + "]" : "[sp]";
    out_.Instruction("move64 " + callee + " x64");
  }

  if(extras) {
    const std::size_t first_arg_index = large_return ? 1 : 0;
    for(std::size_t i = 0; i < ins.args.size(); ++i) {
      const std::size_t abi_index = first_arg_index + i;
      if(abi_index < 4) continue;
      const LowType & arg_type = CallArgumentType(ins, i, direct);
      if(is_large_value(arg_type) || ins.args[i].kind == Operand::OP_SLOT)
        EmitAddressValue(ins.args[i], 'x');
      else EmitScalarValue(ins.args[i], arg_type, 'x');
      const std::size_t stack_offset = (abi_index - 4) * 8;
      const std::string target = stack_offset ? "[sp+" + std::to_string(stack_offset) + "]" : "[sp]";
      out_.Instruction("move64 " + target + " 0");
      out_.Instruction("move64 " + target + " x64");
    }

    std::size_t abi_index = 0;
    if(large_return) {
      Operand result;
      result.kind = Operand::OP_TEMP;
      result.value = ins.dest;
      EmitAddressValue(result, banks[abi_index++]);
    }
    for(std::size_t i = 0; i < ins.args.size() && abi_index < 4; ++i, ++abi_index) {
      const LowType & arg_type = CallArgumentType(ins, i, direct);
      if(is_large_value(arg_type) || ins.args[i].kind == Operand::OP_SLOT)
        EmitAddressValue(ins.args[i], banks[abi_index]);
      else EmitScalarValue(ins.args[i], arg_type, banks[abi_index]);
    }
    const std::string callee = direct ? owner_.SymbolLabel(ins.first.symbol) :
      "[sp+" + std::to_string(callee_offset) + "]";
    out_.Instruction("call " + callee);
    out_.Instruction("iadd64 sp sp " + std::to_string(reserve));
    if(!ins.call_returns_void && !large_return) StoreScalarTemp(ins.dest, ins.type, 'x');
    return;
  }

  std::size_t abi_index = 0;
  if(large_return) {
    Operand result;
    result.kind = Operand::OP_TEMP;
    result.value = ins.dest;
    EmitAddressValue(result, 'x');
    out_.Instruction("move64 " + register_name(banks[abi_index], 64) + " x64");
    ++abi_index;
  }
  for(std::size_t i = 0; i < ins.args.size(); ++i, ++abi_index) {
    const LowType & arg_type = CallArgumentType(ins, i, direct);
    if(abi_index < 4) {
      if(is_large_value(arg_type) || ins.args[i].kind == Operand::OP_SLOT) {
        EmitAddressValue(ins.args[i], 'x');
        out_.Instruction("move64 " + register_name(banks[abi_index], 64) + " x64");
      } else EmitScalarValue(ins.args[i], arg_type, banks[abi_index]);
    } else {
      if(is_large_value(arg_type)) EmitAddressValue(ins.args[i], 'x');
      else EmitScalarValue(ins.args[i], arg_type, 'x');
      const std::size_t stack_offset = (abi_index - 4) * 8;
      const std::string target = stack_offset ? "[sp+" + std::to_string(stack_offset) + "]" : "[sp]";
      out_.Instruction("move64 " + target + " 0");
      out_.Instruction("move64 " + target + " x64");
    }
  }
  out_.Instruction("call " +
    (direct ? owner_.SymbolLabel(ins.first.symbol) : "[sp]"));
  if(reserve) out_.Instruction("iadd64 sp sp " + std::to_string(reserve));
  if(!ins.call_returns_void && !large_return) StoreScalarTemp(ins.dest, ins.type, 'x');
}

const LowType & FunctionEmitter::CallArgumentType(const Instruction & ins,
                                                  std::size_t index,
                                                  bool direct) const
{
  if(ins.has_call_signature && index < ins.call_params.size()) return ins.call_params[index].type;
  const FunctionTarget * function = direct ?
    owner_.FindFunction(ins.first.symbol) : 0;
  if(function && index < function->params->size()) return (*function->params)[index].type;
  return builtin_lowir_type(LTK_I64);
}

void FunctionEmitter::EmitBulk(const Instruction & ins)
{
  const bool copy = ins.kind == Instruction::IK_COPYOBJ;
  if(copy) {
    EmitAddressValue(ins.second, 'x'); EmitAddressValue(ins.first, 'y');
  } else {
    EmitAddressValue(ins.first, 'x'); out_.Instruction("move64 z64 0");
  }
  std::size_t offset = 0;
  std::size_t previous_width = 0;
  while(offset < ins.byte_count) {
    std::size_t width = 8;
    while(width > 1 && (ins.byte_count - offset < width || offset % width)) width /= 2;
    if(offset) {
      out_.Instruction("iadd64 x64 x64 " + std::to_string(previous_width));
      if(copy) out_.Instruction("iadd64 y64 y64 " + std::to_string(previous_width));
    }
    if(copy) out_.Instruction("move" + std::to_string(width * 8) + " z" +
      std::to_string(width * 8) + " [y64]");
    out_.Instruction("move" + std::to_string(width * 8) + " [x64] z" +
                     std::to_string(width * 8));
    offset += width;
    previous_width = width;
  }
}

void FunctionEmitter::EmitAtomic(const Instruction & ins)
{
  const std::string width = std::to_string(shape(ins.type).width);
  if(ins.kind == Instruction::IK_ATOMIC_ADD_FETCH) {
    EmitAddressValue(ins.first, 'y'); out_.Instruction("move" + width + " x" + width + " [y64]");
    EmitScalarValue(ins.second, ins.type, 'z');
    out_.Instruction("iadd" + width + " x" + width + " x" + width + " z" + width);
    out_.Instruction("move" + width + " [y64] x" + width);
    StoreScalarTemp(ins.dest, ins.type, 'x');
  } else if(ins.kind == Instruction::IK_ATOMIC_EXCHANGE) {
    EmitAddressValue(ins.first, 'y'); EmitScalarValue(ins.second, ins.type, 'x');
    out_.Instruction("move" + width + " t" + width + " [y64]");
    out_.Instruction("move" + width + " [y64] x" + width);
    out_.Instruction("move64 x64 0"); out_.Instruction("move" + width + " x" + width + " t" + width);
    StoreScalarTemp(ins.dest, ins.type, 'x');
  } else {
    const std::size_t success = owner_.atomic_label_counter_++;
    const std::size_t end = owner_.atomic_label_counter_++;
    EmitAddressValue(ins.first, 'y'); EmitAddressValue(ins.second, 'z');
    out_.Instruction("move" + width + " t" + width + " [y64]");
    out_.Instruction("move" + width + " x" + width + " [z64]");
    out_.Instruction("ieq" + width + " x8 t" + width + " x" + width);
    out_.Instruction("jumpif x8 __atomic_cmpxchg_success__" + std::to_string(success));
    out_.Instruction("move" + width + " [z64] t" + width);
    out_.Instruction("move64 x64 0");
    StoreScalarTemp(ins.dest, builtin_lowir_type(LTK_I64), 'x');
    out_.Instruction("jump __atomic_cmpxchg_end__" + std::to_string(end));
    out_.Label("__atomic_cmpxchg_success__" + std::to_string(success));
    EmitScalarValue(ins.third, ins.type, 'x'); out_.Instruction("move" + width + " [y64] x" + width);
    out_.Instruction("move64 x64 1");
    StoreScalarTemp(ins.dest, builtin_lowir_type(LTK_I64), 'x');
    out_.Label("__atomic_cmpxchg_end__" + std::to_string(end));
  }
}

bool FunctionEmitter::HasPhiCopies(BlockId predecessor, BlockId target) const
{
  const std::uint32_t id = predecessor;
  if(id >= phi_edge_copies_.size()) return false;
  const std::vector<PhiEdgeCopy> & copies = phi_edge_copies_[id];
  for(std::size_t i = 0; i < copies.size(); ++i)
    if(copies[i].target == target) return true;
  return false;
}

void FunctionEmitter::EmitPhiCopies(BlockId predecessor, BlockId target)
{
  const std::uint32_t id = predecessor;
  if(id >= phi_edge_copies_.size()) return;
  const std::vector<PhiEdgeCopy> & copies = phi_edge_copies_[id];
  // Stage every source before writing any destination.  Phi assignments are
  // parallel, so this also handles swaps around loop backedges.
  for(std::size_t i = 0; i < copies.size(); ++i) {
    if(copies[i].target != target) continue;
    const TypeShape item = shape(copies[i].type);
    EmitScalarValue(copies[i].source, copies[i].type, 'x');
    out_.Instruction("move" + std::to_string(item.width) + " " +
      memory_bp(phi_stage_locations_[copies[i].destination].offset) + " " +
      register_name('x', item.width));
  }
  for(std::size_t i = 0; i < copies.size(); ++i) {
    if(copies[i].target != target) continue;
    const TypeShape item = shape(copies[i].type);
    out_.Instruction("move" + std::to_string(item.width) + " " +
      register_name('x', item.width) + " " +
      memory_bp(phi_stage_locations_[copies[i].destination].offset));
    StoreScalarTemp(copies[i].destination, copies[i].type, 'x');
  }
}

std::string FunctionEmitter::PhiEdgeLabel(std::size_t ordinal) const
{
  return "fn__" + lowir_model::lowir_symbol_name(
    owner_.program_, function_.symbol) + "__phi_edge__" +
    std::to_string(ordinal);
}

void FunctionEmitter::EmitControl(const Instruction & ins)
{
  if(ins.kind == Instruction::IK_JUMP) {
    EmitPhiCopies(current_block_, ins.first.block);
    out_.Instruction("jump " + BlockLabel(ins.first));
  }
  else if(ins.kind == Instruction::IK_BRANCH) {
    EmitScalarValue(ins.first, builtin_lowir_type(LTK_I64), 'x');
    out_.Instruction("ieq64 z8 x64 0");
    const bool false_copies = HasPhiCopies(current_block_, ins.third.block);
    const std::size_t false_edge = phi_edge_label_++;
    out_.Instruction("jumpif z8 " +
      (false_copies ? PhiEdgeLabel(false_edge) : BlockLabel(ins.third)));
    EmitPhiCopies(current_block_, ins.second.block);
    out_.Instruction("jump " + BlockLabel(ins.second));
    if(false_copies) {
      out_.Label(PhiEdgeLabel(false_edge));
      EmitPhiCopies(current_block_, ins.third.block);
      out_.Instruction("jump " + BlockLabel(ins.third));
    }
  } else {
    const LowType & type = builtin_lowir_type(LTK_I64);
    EmitScalarValue(ins.first, type, 'x');
    struct CaseEdge { std::size_t label; BlockId target; };
    std::vector<CaseEdge> copied_cases;
    for(std::size_t i = 0; i < ins.args.size(); i += 2) {
      EmitScalarValue(ins.args[i], type, 't');
      out_.Instruction("ieq64 z8 x64 t64");
      const BlockId target = ins.args[i + 1].block;
      if(HasPhiCopies(current_block_, target)) {
        const std::size_t label = phi_edge_label_++;
        copied_cases.push_back(CaseEdge{label, target});
        out_.Instruction("jumpif z8 " + PhiEdgeLabel(label));
      } else out_.Instruction("jumpif z8 " + BlockLabel(ins.args[i + 1]));
    }
    EmitPhiCopies(current_block_, ins.second.block);
    out_.Instruction("jump " + BlockLabel(ins.second));
    for(std::size_t i = 0; i < copied_cases.size(); ++i) {
      out_.Label(PhiEdgeLabel(copied_cases[i].label));
      EmitPhiCopies(current_block_, copied_cases[i].target);
      Operand target;
      target.kind = Operand::OP_LABEL;
      target.block = copied_cases[i].target;
      out_.Instruction("jump " + BlockLabel(target));
    }
  }
}

void FunctionEmitter::EmitException(const Instruction & ins)
{
  if(ins.kind == Instruction::IK_EH_TRY || ins.kind == Instruction::IK_EH_CLEANUP) {
    out_.Instruction("isub64 sp sp 32");
    out_.Instruction("move64 z64 [" + owner_.EhTopLabel() + "]");
    out_.Instruction("move64 [sp] z64");
    out_.Instruction("move64 z64 " + BlockLabel(ins.first));
    out_.Instruction("move64 [sp+8] z64");
    out_.Instruction("move64 [sp+16] bp");
    out_.Instruction("move64 z64 sp"); out_.Instruction("iadd64 z64 z64 32");
    out_.Instruction("move64 [sp+24] z64"); out_.Instruction("move64 z64 sp");
    out_.Instruction("move64 [" + owner_.EhTopLabel() + "] z64");
  } else if(ins.kind == Instruction::IK_EH_END) {
    out_.Instruction("move64 x64 [" + owner_.EhTopLabel() + "]");
    out_.Instruction("move64 y64 [x64]"); out_.Instruction("move64 [" + owner_.EhTopLabel() + "] y64");
    out_.Instruction("move64 sp x64"); out_.Instruction("iadd64 sp sp 32");
  } else if(ins.kind == Instruction::IK_EXCEPTION) {
    out_.Instruction("move64 x64 [" + owner_.EhValueLabel() + "]");
    StoreScalarTemp(ins.dest, ins.type, 'x');
  } else if(ins.kind == Instruction::IK_THROW) {
    EmitScalarValue(ins.first, ins.type, 'x');
    out_.Instruction("move64 [" + owner_.EhValueLabel() + "] x64");
    EmitResumeSequence();
  } else if(ins.kind == Instruction::IK_RESUME) EmitResumeSequence();
}

void FunctionEmitter::EmitResumeSequence()
{
  const std::size_t handler = owner_.eh_label_counter_++;
  const std::size_t unhandled = owner_.eh_label_counter_++;
  out_.Instruction("move64 x64 [" + owner_.EhTopLabel() + "]");
  out_.Instruction("ieq64 z8 x64 0");
  out_.Instruction("jumpif z8 __eh_unhandled__" + std::to_string(unhandled));
  out_.Label("__eh_handler__" + std::to_string(handler));
  out_.Instruction("move64 y64 [x64]"); out_.Instruction("move64 [" + owner_.EhTopLabel() + "] y64");
  out_.Instruction("move64 z64 [x64+8]"); out_.Instruction("move64 bp [x64+16]");
  out_.Instruction("move64 sp [x64+24]"); out_.Instruction("jump z64");
  out_.Label("__eh_unhandled__" + std::to_string(unhandled));
  out_.Instruction("move64 x64 [" + owner_.EhValueLabel() + "]");
  out_.Instruction("call " + owner_.EhUnhandledLabel());
  out_.Instruction("syscall1 t64 60 x64");
  out_.Blank();
}

void FunctionEmitter::EmitReturn(const Instruction & ins)
{
  if(ins.type.kind != LTK_VOID) {
    if(is_large_value(ins.type)) {
      if(is_f80(ins.type)) {
        LoadF80(ins.first, 0);
        out_.Instruction("move64 x64 " + memory_bp(return_location_.offset));
        CopyBytes(MemoryRef{MemoryRef::LOCAL, scratch_[0], 0},
                  MemoryRef{MemoryRef::ADDRESS_REGISTER, 0, 'x'}, 16);
      } else {
        EmitAddressValue(ins.first, 'x');
        out_.Instruction("move64 y64 " + memory_bp(return_location_.offset));
        CopyBytes(MemoryRef{MemoryRef::ADDRESS_REGISTER, 0, 'x'},
                  MemoryRef{MemoryRef::ADDRESS_REGISTER, 0, 'y'}, shape(ins.type).size);
      }
    } else EmitScalarValue(ins.first, ins.type, 'x');
  }
  out_.Instruction("jump " + EpilogueLabel());
}

}  // namespace

std::string render_program(const lowir_model::LowirProgram & program, Stats * stats)
{
  return ProgramEmitter(program, stats).Emit();
}

}  // namespace lowir_cy86
