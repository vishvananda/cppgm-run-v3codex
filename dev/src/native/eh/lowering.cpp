#include "native/eh/lowering.h"
#include "native/mir/block_labels.h"
#include "native/mir/construction.h"

namespace lowir_native {
namespace eh {
namespace {

bool runtime_kind(lowir_model::SymbolRole role,
                  mir_model::RuntimeFunction::Kind & target)
{
  switch(role) {
  case lowir_model::SR_EH_ALLOCATE_EXCEPTION:
    target = mir_model::RuntimeFunction::RF_EH_ALLOCATE; return true;
  case lowir_model::SR_EH_BEGIN_CATCH:
    target = mir_model::RuntimeFunction::RF_EH_BEGIN_CATCH; return true;
  case lowir_model::SR_EH_END_CATCH:
    target = mir_model::RuntimeFunction::RF_EH_END_CATCH; return true;
  case lowir_model::SR_EH_RETHROW:
    target = mir_model::RuntimeFunction::RF_EH_RETHROW; return true;
  case lowir_model::SR_EH_THROW:
    target = mir_model::RuntimeFunction::RF_EH_THROW; return true;
  case lowir_model::SR_EH_PERSONALITY:
    target = mir_model::RuntimeFunction::RF_EH_PERSONALITY; return true;
  case lowir_model::SR_EH_RESUME:
    target = mir_model::RuntimeFunction::RF_EH_RESUME; return true;
  case lowir_model::SR_ALLOCATE_MEMORY:
    target = mir_model::RuntimeFunction::RF_ALLOCATE_MEMORY; return true;
  case lowir_model::SR_FREE_MEMORY:
    target = mir_model::RuntimeFunction::RF_FREE_MEMORY; return true;
  case lowir_model::SR_TERMINATE:
    target = mir_model::RuntimeFunction::RF_TERMINATE; return true;
  case lowir_model::SR_PURE_VIRTUAL:
    target = mir_model::RuntimeFunction::RF_PURE_VIRTUAL; return true;
  case lowir_model::SR_DYNAMIC_CAST:
    target = mir_model::RuntimeFunction::RF_DYNAMIC_CAST; return true;
  case lowir_model::SR_BAD_CAST:
    target = mir_model::RuntimeFunction::RF_BAD_CAST; return true;
  case lowir_model::SR_BAD_TYPEID:
    target = mir_model::RuntimeFunction::RF_BAD_TYPEID; return true;
  default: return false;
  }
}

bool data_kind(lowir_model::SymbolRole role,
               mir_model::RuntimeData::Kind & target)
{
  switch(role) {
  case lowir_model::SR_RTTI_CLASS:
    target = mir_model::RuntimeData::RD_RTTI_CLASS; return true;
  case lowir_model::SR_RTTI_SI:
    target = mir_model::RuntimeData::RD_RTTI_SI; return true;
  case lowir_model::SR_RTTI_VMI:
    target = mir_model::RuntimeData::RD_RTTI_VMI; return true;
  case lowir_model::SR_RTTI_DATA:
    target = mir_model::RuntimeData::RD_OPAQUE; return true;
  default: return false;
  }
}

bool is_eh_instruction(lowir_model::Instruction::Kind kind)
{
  return kind >= lowir_model::Instruction::IK_EH_TRY &&
    kind <= lowir_model::Instruction::IK_RESUME;
}

}  // namespace

void plan_program(const lowir_model::LowirProgram & source,
                  mir_model::MirProgram & target)
{
  for(std::size_t i = 0; i < source.global_declarations.size(); ++i) {
    const lowir_model::GlobalDeclaration & declaration =
      source.global_declarations[i];
    mir_model::MirRuntimeData data;
    if(!data_kind(declaration.metadata.role, data.kind)) continue;
    data.symbol = declaration.symbol;
    if(declaration.metadata.object_symbol.valid())
      data.object_symbol = declaration.metadata.object_symbol;
    target.runtime_data.push_back(data);
  }
  for(std::size_t i = 0; i < source.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & declaration =
      source.function_declarations[i];
    mir_model::MirRuntimeFunction runtime;
    if(!runtime_kind(declaration.metadata.role, runtime.kind)) continue;
    runtime.symbol = declaration.symbol;
    if(declaration.metadata.object_symbol.valid())
      runtime.object_symbol = declaration.metadata.object_symbol;
    target.runtime_functions.push_back(runtime);
    if(runtime.kind <= mir_model::RuntimeFunction::RF_EH_RESUME)
      target.uses_eh = true;
  }
  for(std::size_t i = 0; i < source.functions.size(); ++i)
    for(std::size_t j = 0; j < source.functions[i].blocks.size(); ++j)
      for(std::size_t k = 0;
          k < source.functions[i].blocks[j].instructions.size(); ++k)
        if(is_eh_instruction(
             source.functions[i].blocks[j].instructions[k].kind))
          target.uses_eh = true;
}

lowir_model::SymbolId runtime_data_symbol(
    const std::vector<mir_model::MirRuntimeData> & data,
    mir_model::RuntimeData::Kind kind)
{
  for(std::size_t i = 0; i < data.size(); ++i)
    if(data[i].kind == kind) return data[i].symbol;
  return lowir_model::SymbolId();
}

bool lower_marker(const lowir_model::Program & program,
                  const lowir_model::Function & function,
                  const lowir_model::Instruction & source,
                  std::vector<mir_model::MirInstruction> & target)
{
  using namespace lowir_native::build;
  typedef lowir_model::Instruction LowInstruction;
  typedef mir_model::MirInstruction MirInstruction;
  if(source.kind == LowInstruction::IK_EH_TRY ||
     source.kind == LowInstruction::IK_EH_CLEANUP) {
    MirInstruction push = machine_instruction(MirInstruction::MI_EH_PUSH);
    append_operand(push, native_block_operand(function, source.first));
    append_operand(push, immediate(source.kind == LowInstruction::IK_EH_CLEANUP));
    target.push_back(push);
  } else if(source.kind == LowInstruction::IK_EH_END)
    target.push_back(machine_instruction(MirInstruction::MI_EH_POP));
  else if(source.kind == LowInstruction::IK_EH_CATCH ||
          source.kind == LowInstruction::IK_EH_CATCH_ALL) {
    MirInstruction match = machine_instruction(MirInstruction::MI_EH_CATCH);
    append_operand(match, immediate(source.eh_selector));
    if(source.kind == LowInstruction::IK_EH_CATCH) append_operand(match,
      symbol_operand(mir_model::MirOperand::OP_SYMBOL, source.first.symbol));
    target.push_back(match);
  } else if(source.kind == LowInstruction::IK_EH_FILTER) {
    MirInstruction filter = machine_instruction(MirInstruction::MI_EH_FILTER);
    append_operand(filter, immediate(source.eh_selector));
    for(std::size_t i = 0; i < source.args.size(); ++i)
      append_operand(filter, symbol_operand(
        mir_model::MirOperand::OP_SYMBOL, source.args[i].symbol));
    target.push_back(filter);
  } else if(source.kind == LowInstruction::IK_RESUME)
    target.push_back(machine_instruction(MirInstruction::MI_RESUME));
  else if(source.kind == LowInstruction::IK_EH_CLEANUP_CLAUSE)
    target.push_back(machine_instruction(
      MirInstruction::MI_EH_CLEANUP_CLAUSE));
  else return false;
  return true;
}

}  // namespace eh
}  // namespace lowir_native
