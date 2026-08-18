#include "lowir_native_program.h"

#include "lowir_native_mir.h"
#include "lowir_native_selection.h"

#include <stdexcept>

namespace lowir_native {
namespace program_lowering {
namespace {

using lowir_native::build::append_move;
using lowir_native::build::append_operand;
using lowir_native::build::machine_instruction;
using lowir_native::build::named_operand;
using lowir_native::build::reg_operand;
using mir_model::MirInstruction;
using mir_model::MirOperand;

bool is_floating(const lowir_model::LowType & type)
{
  return type.kind >= lowir_model::LTK_F32 &&
    type.kind <= lowir_model::LTK_F80;
}

void append_startup_call(std::vector<MirInstruction> & startup,
                         const std::string & name)
{
  MirInstruction call = machine_instruction(MirInstruction::MI_CALL);
  call.call_argument_registers_known = true;
  append_operand(call, named_operand(MirOperand::OP_SYMBOL, name));
  startup.push_back(call);
}

}  // namespace

mir_model::MirGlobalDefinition lower_global(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirGlobalDefinition & source)
{
  mir_model::MirGlobalDefinition target;
  target.name = source.name;
  target.object_symbol = source.metadata.object_symbol;
  target.readonly = source.storage == lowir_model::GSM_READONLY;
  target.thread_local_storage = source.storage == lowir_model::GSM_THREAD_LOCAL;
  target.section_segment = source.metadata.section_segment;
  target.section_name = source.metadata.section_name;
  if(source.structured) {
    target.storage_kind = mir_model::MirGlobalDefinition::GS_DATA;
    for(std::size_t i = 0; i < source.data_items.size(); ++i) {
      const lowir_model::LowirGlobalDefinition::DataItem & item = source.data_items[i];
      mir_model::MirGlobalDefinition::DataItem lowered;
      lowered.type = item.type;
      if(item.kind == lowir_model::LowirGlobalDefinition::DataItem::ITEM_ZERO) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_ZERO;
        lowered.zero_bytes = item.zero_bytes;
      } else if(item.kind == lowir_model::LowirGlobalDefinition::DataItem::ITEM_ADDR) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_ADDR;
        lowered.symbol = lowir_model::lowir_symbol_name(program, item.symbol_id);
        lowered.addr_addend = item.addr_addend;
      } else if(is_floating(item.type)) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT;
        lowered.literal_text = item.literal_operand.text;
      } else {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_INTEGER;
        lowered.int_value = selection::integer_value(item.literal_operand);
		lowered.literal_text = item.literal_operand.text;
      }
      target.data_items.push_back(lowered);
    }
  } else {
    target.storage_kind = mir_model::MirGlobalDefinition::GS_SCALAR;
    target.type = source.type;
    if(source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ADDR) {
      target.init_kind = mir_model::MirGlobalDefinition::GI_ADDR;
      target.symbol =
        lowir_model::lowir_symbol_name(program, source.init_operand.symbol);
      target.addr_addend = source.addr_addend;
    } else if(is_floating(source.type)) {
      target.init_kind = mir_model::MirGlobalDefinition::GI_FLOAT;
      target.literal_text = source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ZERO ?
        (source.type.kind == lowir_model::LTK_F32 ? "0.0f" :
         (source.type.kind == lowir_model::LTK_F80 ? "0.0L" : "0.0")) :
        source.init_operand.text;
    } else {
      target.init_kind = mir_model::MirGlobalDefinition::GI_INTEGER;
      target.int_value = source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ZERO ?
        0 : selection::integer_value(source.init_operand);
	  target.literal_text = source.init_kind ==
		lowir_model::LowirGlobalDefinition::INIT_ZERO ? "0" :
		source.init_operand.text;
    }
  }
  return target;
}

std::vector<lowir_model::SymbolId> tls_wrapper_index(
    const lowir_model::LowirProgram & source)
{
  std::vector<lowir_model::SymbolId> result(source.symbol_names.size());
  for(std::size_t i = 0; i < source.function_declarations.size(); ++i) {
    const lowir_model::FunctionDeclaration & function =
      source.function_declarations[i];
    if(!function.metadata.tls_for_symbol_id.valid()) continue;
    lowir_model::SymbolId & wrapper =
      result[function.metadata.tls_for_symbol_id];
    if(wrapper.valid() && wrapper != function.symbol)
      throw std::runtime_error("multiple TLS wrappers for " +
                               function.metadata.tls_for_symbol);
    wrapper = function.symbol;
  }
  for(std::size_t i = 0; i < source.functions.size(); ++i) {
    const lowir_model::LowirFunction & function = source.functions[i];
    if(!function.metadata.tls_for_symbol_id.valid()) continue;
    lowir_model::SymbolId & wrapper =
      result[function.metadata.tls_for_symbol_id];
    if(wrapper.valid() && wrapper != function.symbol)
      throw std::runtime_error("multiple TLS wrappers for " +
                               function.metadata.tls_for_symbol);
    wrapper = function.symbol;
  }
  return result;
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

}  // namespace program_lowering
}  // namespace lowir_native
