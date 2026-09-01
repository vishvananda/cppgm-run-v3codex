#include "native/driver/program.h"

#include "native/errors.h"
#include "native/mir/construction.h"
#include "native/lowering/selection.h"


namespace lowir_native {
namespace program_lowering {
namespace {

using lowir_native::build::append_move;
using lowir_native::build::append_operand;
using lowir_native::build::machine_instruction;
using lowir_native::build::reg_operand;
using lowir_native::build::symbol_operand;
using mir_model::MirInstruction;
using mir_model::MirOperand;

bool is_floating(const lowir_model::LowType & type)
{
  return type.kind >= lowir_model::LTK_F32 &&
    type.kind <= lowir_model::LTK_F80;
}

void append_startup_call(std::vector<MirInstruction> & startup,
                         lowir_model::SymbolId symbol)
{
  MirInstruction call = machine_instruction(MirInstruction::MI_CALL);
  call.call_argument_registers_known = true;
  append_operand(call, symbol_operand(MirOperand::OP_SYMBOL, symbol));
  startup.push_back(call);
}

}  // namespace

mir_model::MirGlobalDefinition lower_global(
    const lowir_model::LowirGlobalDefinition & source)
{
  mir_model::MirGlobalDefinition target;
  target.symbol = source.symbol;
  if(source.metadata.object_symbol.valid())
    target.object_symbol = source.metadata.object_symbol;
  target.readonly = source.storage == lowir_model::GSM_READONLY;
  target.thread_local_storage = source.storage == lowir_model::GSM_THREAD_LOCAL;
  if(source.metadata.section_name.valid())
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
        lowered.symbol = item.symbol_id;
        lowered.addr_addend = item.addr_addend;
      } else if(is_floating(item.type)) {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_FLOAT;
        lowered.literal_low = item.literal_operand.literal_low;
        lowered.literal_high = item.literal_operand.literal_high;
        if(item.literal_operand.has_spelling)
          lowered.literal = item.literal_operand.literal;
      } else {
        lowered.kind = mir_model::MirGlobalDefinition::DataItem::ITEM_INTEGER;
        lowered.int_value = selection::integer_value(item.literal_operand);
		lowered.literal_high = item.literal_operand.int_high;
		if(item.type.kind == lowir_model::LTK_I128 &&
		   item.literal_operand.has_spelling)
		  lowered.literal = item.literal_operand.literal;
      }
      target.data_items.push_back(lowered);
    }
  } else {
    target.storage_kind = mir_model::MirGlobalDefinition::GS_SCALAR;
    target.type = source.type;
    if(source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ADDR) {
      target.init_kind = mir_model::MirGlobalDefinition::GI_ADDR;
      target.init_symbol = source.init_operand.symbol;
      target.addr_addend = source.addr_addend;
    } else if(is_floating(source.type)) {
      target.init_kind = mir_model::MirGlobalDefinition::GI_FLOAT;
      if(source.init_kind != lowir_model::LowirGlobalDefinition::INIT_ZERO) {
        target.literal_low = source.init_operand.literal_low;
        target.literal_high = source.init_operand.literal_high;
        if(source.init_operand.has_spelling)
          target.literal = source.init_operand.literal;
      }
    } else {
      target.init_kind = mir_model::MirGlobalDefinition::GI_INTEGER;
      target.int_value = source.init_kind == lowir_model::LowirGlobalDefinition::INIT_ZERO ?
        0 : selection::integer_value(source.init_operand);
	  target.literal_high = source.init_kind ==
	    lowir_model::LowirGlobalDefinition::INIT_ZERO ? 0 :
	    source.init_operand.int_high;
	  if(source.type.kind == lowir_model::LTK_I128 &&
	     source.init_kind != lowir_model::LowirGlobalDefinition::INIT_ZERO &&
	     source.init_operand.has_spelling)
		target.literal = source.init_operand.literal;
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
      native_errors::ThrowLowirInput("multiple TLS wrappers for " +
        lowir_model::lowir_symbol_name(
          source, function.metadata.tls_for_symbol_id));
    wrapper = function.symbol;
  }
  for(std::size_t i = 0; i < source.functions.size(); ++i) {
    const lowir_model::LowirFunction & function = source.functions[i];
    if(!function.metadata.tls_for_symbol_id.valid()) continue;
    lowir_model::SymbolId & wrapper =
      result[function.metadata.tls_for_symbol_id];
    if(wrapper.valid() && wrapper != function.symbol)
      native_errors::ThrowLowirInput("multiple TLS wrappers for " +
        lowir_model::lowir_symbol_name(
          source, function.metadata.tls_for_symbol_id));
    wrapper = function.symbol;
  }
  return result;
}

void lower_startup(const lowir_model::LowirProgram & source,
                   mir_model::MirProgram & target)
{
  lowir_model::SymbolId entry;
  lowir_model::SymbolId init;
  lowir_model::SymbolId fini;
  for(std::size_t i = 0; i < source.functions.size(); ++i) {
    const lowir_model::LowirFunction & function = source.functions[i];
    if(function.metadata.role == lowir_model::SR_ENTRY) entry = function.symbol;
    if(function.metadata.role == lowir_model::SR_INIT) init = function.symbol;
    if(function.metadata.role == lowir_model::SR_FINI) fini = function.symbol;
  }
  if(!entry.valid()) return;
  if(init.valid()) append_startup_call(target.startup, init);
  append_startup_call(target.startup, entry);
  if(fini.valid()) {
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
