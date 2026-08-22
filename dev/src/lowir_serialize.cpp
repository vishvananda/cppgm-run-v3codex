#include "lowir_model.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lowir_model {
namespace {

const char * role_name(SymbolRole role)
{
  switch(role) {
  case SR_NONE: return 0;
  case SR_ENTRY: return "entry";
  case SR_INIT: return "init";
  case SR_FINI: return "fini";
  case SR_EH_TOP: return "eh_top";
  case SR_EH_VALUE: return "eh_value";
  case SR_EH_TYPE: return "eh_type";
  case SR_EH_UNHANDLED: return "eh_unhandled";
  case SR_EH_ALLOCATE_EXCEPTION: return "eh_allocate_exception";
  case SR_EH_BEGIN_CATCH: return "eh_begin_catch";
  case SR_EH_CALL_UNEXPECTED: return "eh_call_unexpected";
  case SR_EH_CURRENT_EXCEPTION_TYPE: return "eh_current_exception_type";
  case SR_EH_END_CATCH: return "eh_end_catch";
  case SR_EH_RETHROW: return "eh_rethrow";
  case SR_EH_THROW: return "eh_throw";
  case SR_EH_PERSONALITY: return "eh_personality";
  case SR_EH_RESUME: return "eh_resume";
  case SR_ALLOCATE_MEMORY: return "allocate_memory";
  case SR_FREE_MEMORY: return "free_memory";
  case SR_TERMINATE: return "terminate";
  case SR_PURE_VIRTUAL: return "pure_virtual";
  case SR_DYNAMIC_CAST: return "dynamic_cast";
  case SR_BAD_CAST: return "bad_cast";
  case SR_BAD_TYPEID: return "bad_typeid";
  case SR_RTTI_CLASS: return "rtti_class";
  case SR_RTTI_SI: return "rtti_si";
  case SR_RTTI_VMI: return "rtti_vmi";
  case SR_RTTI_DATA: return "rtti_data";
  case SR_UNREACHABLE: return "unreachable";
  }
  throw std::logic_error("invalid LowIR symbol role");
}

struct MetadataWriter
{
  std::ostream & out;
  bool any;

  explicit MetadataWriter(std::ostream & output) : out(output), any(false) {}

  void item(const char * key, const std::string & value)
  {
    if(value.empty()) return;
    if(!any) out << " [";
    else out << ", ";
    out << key << '=' << value;
    any = true;
  }

  void item(const char * key, const char * value)
  {
    if(value) item(key, std::string(value));
  }

  void flag(const char * key, bool value)
  {
    if(value) item(key, "yes");
  }

  void finish()
  {
    if(any) out << ']';
  }
};

void write_parameter_metadata(std::ostream & out, const ParameterMetadata & value)
{
  MetadataWriter metadata(out);
  switch(value.passing) {
  case PPM_DIRECT: break;
  case PPM_INDIRECT_RESULT: metadata.item("pass", "indirect_result"); break;
  case PPM_BY_ADDRESS: metadata.item("pass", "by_address"); break;
  case PPM_REFERENCE: metadata.item("pass", "reference"); break;
  case PPM_DECAY: metadata.item("pass", "decay"); break;
  }
  switch(value.capture) {
  case PCM_DEFAULT: break;
  case PCM_NOCAPTURE: metadata.item("capture", "nocapture"); break;
  case PCM_MAYCAPTURE: metadata.item("capture", "maycapture"); break;
  }
  switch(value.access) {
  case PAM_DEFAULT: break;
  case PAM_NONE: metadata.item("access", "none"); break;
  case PAM_READ: metadata.item("access", "read"); break;
  case PAM_WRITE: metadata.item("access", "write"); break;
  case PAM_READWRITE: metadata.item("access", "readwrite"); break;
  }
  if(value.alias == PALM_NOALIAS) metadata.item("alias", "noalias");
  metadata.finish();
}

void write_parameter(std::ostream & out, const Parameter & parameter,
                     const Program & program)
{
  out << '%' << lowir_parameter_name(program, parameter) << " : "
      << lowir_type_text(parameter.type);
  write_parameter_metadata(out, parameter.metadata);
}

void write_parameters(std::ostream & out,
                      const std::vector<Parameter> & parameters,
                      const Program & program)
{
  out << '(';
  for(std::size_t i = 0; i < parameters.size(); ++i) {
    if(i) out << ", ";
    write_parameter(out, parameters[i], program);
  }
  out << ')';
}

void write_boundary_metadata(MetadataWriter & metadata,
                             const FunctionBoundaryMetadata & value)
{
  switch(value.arity) {
  case CAM_FIXED: break;
  case CAM_VARIADIC: metadata.item("arity", "variadic"); break;
  case CAM_PROTOTYPE_RELAXED:
    metadata.item("arity", "prototype_relaxed"); break;
  }
  switch(value.effects) {
  case CFXM_DEFAULT: break;
  case CFXM_READNONE: metadata.item("effects", "readnone"); break;
  case CFXM_READONLY: metadata.item("effects", "readonly"); break;
  case CFXM_READWRITE: metadata.item("effects", "readwrite"); break;
  }
  switch(value.unwind) {
  case CUM_DEFAULT: break;
  case CUM_MAY: metadata.item("unwind", "may"); break;
  case CUM_NO: metadata.item("unwind", "no"); break;
  }
  switch(value.returns) {
  case CRM_DEFAULT: break;
  case CRM_RETURNS: metadata.item("return", "returns"); break;
  case CRM_NORETURN: metadata.item("return", "noreturn"); break;
  }
}

void write_symbol_metadata(MetadataWriter & metadata,
                           const SymbolMetadata & value,
                           const Program & program)
{
  if(!value.inferred_legacy_role)
    metadata.item("role", role_name(value.role));
  if(value.linkage == LLM_C) metadata.item("linkage", "c");
  else if(value.linkage == LLM_CPP) metadata.item("linkage", "cpp");
  if(value.binding == SBM_INTERNAL) metadata.item("binding", "internal");
  else if(value.binding == SBM_STRONG) metadata.item("binding", "strong");
  else if(value.binding == SBM_WEAK) metadata.item("binding", "weak");
  if(value.object_symbol.valid())
    metadata.item("object", program.strings.get(value.object_symbol));
  if(value.tls_for_symbol_id.valid())
    metadata.item("tls_for", "@" +
                  lowir_symbol_name(program, value.tls_for_symbol_id));
  metadata.flag("keep_alias", value.keep_internal_alias);
  metadata.flag("prefer_local", value.prefer_local_object_binding);
  metadata.flag("object_root", value.object_output_root);
  metadata.flag("trivial_lifecycle", value.object_trivial_lifecycle);
  metadata.flag("force_inline", value.force_inline);
  metadata.flag("inline_hint", value.inline_hint);
  metadata.flag("no_inline", value.no_inline);
}

void write_function_metadata(std::ostream & out,
                             const FunctionBoundaryMetadata & boundary,
                             const SymbolMetadata & symbol,
                             const Program & program)
{
  MetadataWriter metadata(out);
  write_boundary_metadata(metadata, boundary);
  write_symbol_metadata(metadata, symbol, program);
  metadata.finish();
}

void write_global_metadata(std::ostream & out, GlobalStorageMode storage,
                           const SymbolMetadata & symbol,
                           const Program & program)
{
  MetadataWriter metadata(out);
  if(storage == GSM_READONLY) metadata.item("storage", "readonly");
  else if(storage == GSM_THREAD_LOCAL)
    metadata.item("storage", "thread_local");
  write_symbol_metadata(metadata, symbol, program);
  metadata.finish();
}

void write_debug(std::ostream & out, const InstructionDebugLocation & location,
                 const Program & program)
{
  if(location.present())
    out << " !dbg(" << program.strings.get(location.file) << ", "
        << location.line << ", "
        << location.column << ')';
}

void write_operand(std::ostream & out, const Operand & operand,
                   const Program & program, const Function * function = 0)
{
  if(operand.kind == Operand::OP_TEMP) {
    if(!function) throw std::logic_error("LowIR value lacks a function");
    out << '%' << lowir_value_name(program.strings, *function, operand.value);
    return;
  }
  if(operand.kind == Operand::OP_LABEL) {
    if(!function) throw std::logic_error("LowIR block target lacks a function");
    out << '^' << lowir_block_label(program.strings, *function, operand.block);
    return;
  }
  if(operand.kind == Operand::OP_SLOT) {
    if(!function) throw std::logic_error("LowIR slot lacks a function");
    out << '$' << lowir_slot_name(program.strings, *function, operand.slot);
    return;
  }
  if(operand.kind == Operand::OP_GLOBAL) {
    out << '@' << lowir_symbol_name(program, operand.symbol);
    return;
  }
  if(operand.kind == Operand::OP_FLOAT ||
     operand.kind == Operand::OP_INTEGER) {
    const std::string spelling = lowir_literal_text(operand, &program.strings);
    if(operand.kind == Operand::OP_FLOAT &&
       (spelling == "INFINITY" || spelling == "+INFINITY")) {
      out << "inf";
      return;
    }
    if(operand.kind == Operand::OP_FLOAT && spelling == "-INFINITY") {
      out << "-inf";
      return;
    }
    out << spelling;
    return;
  }
  throw std::logic_error("unsupported LowIR operand identity");
}

void write_result(std::ostream & out, const Instruction & instruction,
                  const Program & program, const Function * function)
{
  if(!function || !instruction.dest.valid())
    throw std::logic_error("LowIR result lacks compact identity");
  out << '%' << lowir_value_name(program.strings, *function, instruction.dest);
}

const char * projection_name(IndexProjectionKind projection)
{
  switch(projection) {
  case IPK_NONE: return 0;
  case IPK_ARRAY_ELEMENT: return "array_element";
  case IPK_FIELD: return "field";
  case IPK_BASE_SUBOBJECT: return "base_subobject";
  case IPK_REFERENCE_FIELD: return "reference_field";
  }
  throw std::logic_error("invalid LowIR index projection");
}

void write_call(std::ostream & out, const Instruction & ins,
                const Program & program, const Function * function)
{
  if(ins.dest.valid()) { write_result(out, ins, program, function); out << " = "; }
  out << "call " << (ins.call_returns_void ? "void" : lowir_type_text(ins.type)) << ' ';
  write_operand(out, ins.first, program, function);
  out << '(';
  for(std::size_t i = 0; i < ins.args.size(); ++i) {
    if(i) out << ", ";
    write_operand(out, ins.args[i], program, function);
  }
  out << ')';
  if(ins.has_call_signature) {
    out << " as ";
    write_parameters(out, ins.call_params, program);
    out << " -> " << lowir_type_text(ins.call_return_type);
    MetadataWriter metadata(out);
    write_boundary_metadata(metadata, ins.call_boundary);
    metadata.finish();
  }
}

void write_instruction(std::ostream & out, const Instruction & ins,
                       const Program & program, const Function * function)
{
  switch(ins.kind) {
  case Instruction::IK_CONST:
    write_result(out, ins, program, function); out << " = const " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_COPY:
    write_result(out, ins, program, function); out << " = copy " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_ADDR:
    write_result(out, ins, program, function); out << " = addr "; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_LOAD:
    write_result(out, ins, program, function);
    out << (ins.volatile_access ? " = load volatile " : " = load ")
        << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_ATOMIC_LOAD:
    write_result(out, ins, program, function); out << " = atomic_load " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); out << ", "; write_operand(out, ins.args.at(0), program, function); break;
  case Instruction::IK_STORE:
    out << (ins.volatile_access ? "store volatile " : "store ")
        << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function);
    out << ", "; write_operand(out, ins.second, program, function); break;
  case Instruction::IK_ATOMIC_STORE:
    out << "atomic_store " << lowir_type_text(ins.type) << ' '; write_operand(out, ins.first, program, function);
    out << ", "; write_operand(out, ins.second, program, function); out << ", ";
    write_operand(out, ins.args.at(0), program, function); break;
  case Instruction::IK_ATOMIC_EXCHANGE:
    write_result(out, ins, program, function); out << " = atomic_exchange " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); out << ", "; write_operand(out, ins.second, program, function);
    out << ", "; write_operand(out, ins.args.at(0), program, function); break;
  case Instruction::IK_INDEX:
    write_result(out, ins, program, function); out << " = index " << lowir_type_text(ins.type);
    if(const char * projection = projection_name(ins.index_projection))
      out << " [projection=" << projection << ']';
    out << ' '; write_operand(out, ins.first, program, function); out << ", ";
    write_operand(out, ins.second, program, function); break;
  case Instruction::IK_UNARY:
    write_result(out, ins, program, function); out << " = unary " << ins.op << ' ' << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
    write_result(out, ins, program, function); out << (ins.kind == Instruction::IK_BINARY ? " = binary " : " = cmp ")
        << ins.op << ' ' << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); out << ", "; write_operand(out, ins.second, program, function); break;
  case Instruction::IK_CONVERT:
    write_result(out, ins, program, function); out << " = convert " << ins.op << ' ' << lowir_type_text(ins.type) << ' '
        << lowir_type_text(ins.source_type) << ' '; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_ATOMIC_ADD_FETCH:
    write_result(out, ins, program, function); out << " = atomic_add_fetch " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); out << ", "; write_operand(out, ins.second, program, function);
    out << ", "; write_operand(out, ins.args.at(0), program, function); break;
  case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
    write_result(out, ins, program, function); out << " = atomic_compare_exchange " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); out << ", "; write_operand(out, ins.second, program, function);
    out << ", "; write_operand(out, ins.third, program, function); out << ", ";
    write_operand(out, ins.args.at(0), program, function); out << ", "; write_operand(out, ins.args.at(1), program, function); break;
  case Instruction::IK_ATOMIC_THREAD_FENCE:
  case Instruction::IK_ATOMIC_SIGNAL_FENCE:
    out << (ins.kind == Instruction::IK_ATOMIC_THREAD_FENCE ?
      "atomic_thread_fence " : "atomic_signal_fence ");
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_VA_START:
    out << "va_start "; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_VA_ARG:
    write_result(out, ins, program, function); out << " = va_arg " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_STACK_ALLOC:
    write_result(out, ins, program, function); out << " = stack_alloc "; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_CALL: write_call(out, ins, program, function); break;
  case Instruction::IK_COPYOBJ:
    out << "copyobj " << ins.byte_count << 'x' << ins.byte_alignment << ' ';
    write_operand(out, ins.first, program, function); out << ", "; write_operand(out, ins.second, program, function); break;
  case Instruction::IK_ZEROINIT:
    out << "zeroinit " << ins.byte_count << 'x' << ins.byte_alignment << ' ';
    write_operand(out, ins.first, program, function); break;
  case Instruction::IK_EH_TRY:
    out << "eh_try "; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_EH_CLEANUP:
    out << "eh_cleanup "; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_EH_CLEANUP_CLAUSE: out << "eh_cleanup"; break;
  case Instruction::IK_EH_CATCH:
    out << "eh_catch "; write_operand(out, ins.first, program, function);
    if(ins.has_eh_selector) out << ", " << ins.eh_selector;
    break;
  case Instruction::IK_EH_FILTER:
    out << "eh_filter";
    for(std::size_t i = 0; i < ins.args.size(); ++i) {
      out << (i ? ", " : " "); write_operand(out, ins.args[i], program, function);
    }
    if(ins.has_eh_selector) out << (ins.args.empty() ? " " : ", ") << ins.eh_selector;
    break;
  case Instruction::IK_EH_CATCH_ALL:
    out << "eh_catch_all";
    if(ins.has_eh_selector) out << ", " << ins.eh_selector;
    break;
  case Instruction::IK_EH_END: out << "eh_end"; break;
  case Instruction::IK_THROW:
    out << "throw " << lowir_type_text(ins.type) << ' '; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_EXCEPTION:
    write_result(out, ins, program, function); out << " = exception " << lowir_type_text(ins.type); break;
  case Instruction::IK_EXCEPTION_SELECTOR:
    write_result(out, ins, program, function); out << " = exception_selector " << lowir_type_text(ins.type); break;
  case Instruction::IK_RESUME: out << "resume"; break;
  case Instruction::IK_JUMP:
    out << "jump "; write_operand(out, ins.first, program, function); break;
  case Instruction::IK_BRANCH:
    out << "branch "; write_operand(out, ins.first, program, function); out << ", ";
    write_operand(out, ins.second, program, function); out << ", ";
    write_operand(out, ins.third, program, function); break;
  case Instruction::IK_SWITCH:
    out << "switch "; write_operand(out, ins.first, program, function); out << ", ";
    write_operand(out, ins.second, program, function);
    for(std::size_t i = 0; i + 1 < ins.args.size(); i += 2) {
      out << ", "; write_operand(out, ins.args[i], program, function); out << ':';
      write_operand(out, ins.args[i + 1], program, function);
    }
    break;
  case Instruction::IK_RETURN:
    out << "return " << lowir_type_text(ins.type);
    if(ins.type.kind != LTK_VOID) { out << ' '; write_operand(out, ins.first, program, function); }
    break;
  case Instruction::IK_PHI:
    write_result(out, ins, program, function);
    out << " = phi " << lowir_type_text(ins.type) << " [";
    for(std::size_t i = 0; i + 1 < ins.args.size(); i += 2) {
      if(i) out << ", ";
      write_operand(out, ins.args[i], program, function);
      out << ": ";
      write_operand(out, ins.args[i + 1], program, function);
    }
    out << ']';
    break;
  }
  write_debug(out, ins.debug_location, program);
}

void write_global_declaration(std::ostream & out, const GlobalDeclaration & item,
                              const Program & program)
{
  out << "declare global @" << lowir_symbol_name(program, item.symbol);
  if(item.has_type) out << " : " << lowir_type_text(item.type);
  write_global_metadata(out, item.storage, item.metadata, program);
  out << '\n';
}

void write_function_declaration(std::ostream & out,
                                const FunctionDeclaration & item,
                                const Program & program)
{
  out << "declare function @" << lowir_symbol_name(program, item.symbol);
  write_parameters(out, item.params, program);
  out << " -> " << lowir_type_text(item.return_type);
  write_function_metadata(out, item.boundary, item.metadata, program);
  out << '\n';
}

void write_global(std::ostream & out, const GlobalDefinition & item,
                  const Program & program)
{
  out << "global @" << lowir_symbol_name(program, item.symbol);
  if(!item.structured) out << " : " << lowir_type_text(item.type);
  write_global_metadata(out, item.storage, item.metadata, program);
  out << " = ";
  if(item.structured) {
    out << "{\n";
    for(std::size_t i = 0; i < item.data_items.size(); ++i) {
      const GlobalDefinition::DataItem & data = item.data_items[i];
      out << "  ";
      if(data.kind == GlobalDefinition::DataItem::ITEM_ZERO)
        out << "zero " << data.zero_bytes;
      else if(data.kind == GlobalDefinition::DataItem::ITEM_ADDR) {
        out << lowir_type_text(data.type) << " addr @"
            << lowir_symbol_name(program, data.symbol_id);
        if(data.addr_addend > 0) out << " + " << data.addr_addend;
        else if(data.addr_addend < 0) out << " - " << -data.addr_addend;
      } else {
        out << lowir_type_text(data.type) << ' ';
        write_operand(out, data.literal_operand, program);
      }
      out << '\n';
    }
    out << "}\n";
    return;
  }
  if(item.init_kind == GlobalDefinition::INIT_ZERO) out << "zero";
  else if(item.init_kind == GlobalDefinition::INIT_INTEGER)
    write_operand(out, item.init_operand, program);
  else {
    out << "addr "; write_operand(out, item.init_operand, program);
    if(item.addr_addend > 0) out << " + " << item.addr_addend;
    else if(item.addr_addend < 0) out << " - " << -item.addr_addend;
  }
  out << '\n';
}

void write_function(std::ostream & out, const Function & function,
                    const Program & program)
{
  out << "function @" << lowir_symbol_name(program, function.symbol);
  write_parameters(out, function.params, program);
  out << " -> " << lowir_type_text(function.return_type);
  write_function_metadata(
    out, function.boundary, function.metadata, program);
  write_debug(out, function.debug_location, program);
  out << " {\n";
  for(std::size_t i = 0; i < function.slots.size(); ++i)
    out << "  slot $" << lowir_slot_name(program.strings, function, function.slots[i]) << " : "
        << lowir_type_text(lowir_slot_type(function, function.slots[i])) << '\n';
  if(!function.slots.empty()) out << '\n';
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    if(i) out << '\n';
    out << "  block ^" << lowir_block_label(program.strings, function, function.blocks[i].id)
        << ":\n";
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      out << "    ";
      write_instruction(out, function.blocks[i].instructions[j], program, &function);
      out << '\n';
    }
  }
  out << "}\n";
}

}  // namespace

std::string serialize_lowir_program(const LowirProgram & program)
{
  std::ostringstream out;
  bool wrote = false;
#define WRITE_GROUP(VECTOR, WRITER) \
  if(!(VECTOR).empty() && wrote) out << '\n'; \
  for(std::size_t i = 0; i < (VECTOR).size(); ++i) { \
    WRITER(out, (VECTOR)[i], program); \
    wrote = true; \
  }
  WRITE_GROUP(program.global_declarations, write_global_declaration)
  WRITE_GROUP(program.function_declarations, write_function_declaration)
  WRITE_GROUP(program.globals, write_global)
  WRITE_GROUP(program.functions, write_function)
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    out << "alias object "
        << program.strings.get(program.object_aliases[i].object_symbol) << " = "
        << '@' << lowir_symbol_name(
             program, program.object_aliases[i].target_id) << '\n';
    wrote = true;
  }
#undef WRITE_GROUP
  return out.str();
}

void write_lowir_program_file(const std::string & path,
                              const LowirProgram & program)
{
  std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc |
                                     std::ios::binary);
  if(!output) throw ParseError("unable to open LowIR output: " + path);
  output << serialize_lowir_program(program);
  if(!output) throw ParseError("unable to write LowIR output: " + path);
}

}  // namespace lowir_model
