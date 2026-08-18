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
  case SR_PURE_VIRTUAL: return "pure_virtual";
  case SR_DYNAMIC_CAST: return "dynamic_cast";
  case SR_BAD_CAST: return "bad_cast";
  case SR_BAD_TYPEID: return "bad_typeid";
  case SR_RTTI_CLASS: return "rtti_class";
  case SR_RTTI_SI: return "rtti_si";
  case SR_RTTI_VMI: return "rtti_vmi";
  case SR_RTTI_DATA: return "rtti_data";
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

void write_parameter(std::ostream & out, const Parameter & parameter)
{
  out << parameter.name << " : " << lowir_type_text(parameter.type);
  write_parameter_metadata(out, parameter.metadata);
}

void write_parameters(std::ostream & out,
                      const std::vector<Parameter> & parameters)
{
  out << '(';
  for(std::size_t i = 0; i < parameters.size(); ++i) {
    if(i) out << ", ";
    write_parameter(out, parameters[i]);
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
                           const SymbolMetadata & value)
{
  if(!value.inferred_legacy_role)
    metadata.item("role", role_name(value.role));
  if(value.linkage == LLM_C) metadata.item("linkage", "c");
  else if(value.linkage == LLM_CPP) metadata.item("linkage", "cpp");
  if(value.binding == SBM_INTERNAL) metadata.item("binding", "internal");
  else if(value.binding == SBM_STRONG) metadata.item("binding", "strong");
  else if(value.binding == SBM_WEAK) metadata.item("binding", "weak");
  metadata.item("object", value.object_symbol);
  metadata.item("tls_for", value.tls_for_symbol);
  metadata.flag("keep_alias", value.keep_internal_alias);
  metadata.flag("prefer_local", value.prefer_local_object_binding);
  metadata.flag("object_root", value.object_output_root);
  metadata.flag("trivial_lifecycle", value.object_trivial_lifecycle);
  metadata.flag("force_inline", value.force_inline);
  metadata.flag("no_inline", value.no_inline);
}

void write_function_metadata(std::ostream & out,
                             const FunctionBoundaryMetadata & boundary,
                             const SymbolMetadata & symbol)
{
  MetadataWriter metadata(out);
  write_boundary_metadata(metadata, boundary);
  write_symbol_metadata(metadata, symbol);
  metadata.finish();
}

void write_global_metadata(std::ostream & out, GlobalStorageMode storage,
                           const SymbolMetadata & symbol)
{
  MetadataWriter metadata(out);
  if(storage == GSM_READONLY) metadata.item("storage", "readonly");
  else if(storage == GSM_THREAD_LOCAL)
    metadata.item("storage", "thread_local");
  write_symbol_metadata(metadata, symbol);
  metadata.finish();
}

void write_debug(std::ostream & out, const InstructionDebugLocation & location)
{
  if(location.present())
    out << " !dbg(" << location.file << ", " << location.line << ", "
        << location.column << ')';
}

void write_operand(std::ostream & out, const Operand & operand)
{
  if(operand.text.empty()) throw std::logic_error("missing LowIR operand text");
  if(operand.kind == Operand::OP_FLOAT) {
    if(operand.text == "INFINITY" || operand.text == "+INFINITY") {
      out << "inf";
      return;
    }
    if(operand.text == "-INFINITY") {
      out << "-inf";
      return;
    }
  }
  out << operand.text;
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

void write_call(std::ostream & out, const Instruction & ins)
{
  if(!ins.dest.empty()) out << ins.dest << " = ";
  out << "call " << (ins.call_returns_void ? "void" : lowir_type_text(ins.type)) << ' ';
  write_operand(out, ins.first);
  out << '(';
  for(std::size_t i = 0; i < ins.args.size(); ++i) {
    if(i) out << ", ";
    write_operand(out, ins.args[i]);
  }
  out << ')';
  if(ins.has_call_signature) {
    out << " as ";
    write_parameters(out, ins.call_params);
    out << " -> " << lowir_type_text(ins.call_return_type);
    MetadataWriter metadata(out);
    write_boundary_metadata(metadata, ins.call_boundary);
    metadata.finish();
  }
}

void write_instruction(std::ostream & out, const Instruction & ins)
{
  switch(ins.kind) {
  case Instruction::IK_CONST:
    out << ins.dest << " = const " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); break;
  case Instruction::IK_COPY:
    out << ins.dest << " = copy " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); break;
  case Instruction::IK_ADDR:
    out << ins.dest << " = addr "; write_operand(out, ins.first); break;
  case Instruction::IK_LOAD:
    out << ins.dest << " = load " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); break;
  case Instruction::IK_ATOMIC_LOAD:
    out << ins.dest << " = atomic_load " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); out << ", "; write_operand(out, ins.args.at(0)); break;
  case Instruction::IK_STORE:
    out << "store " << lowir_type_text(ins.type) << ' '; write_operand(out, ins.first);
    out << ", "; write_operand(out, ins.second); break;
  case Instruction::IK_ATOMIC_STORE:
    out << "atomic_store " << lowir_type_text(ins.type) << ' '; write_operand(out, ins.first);
    out << ", "; write_operand(out, ins.second); out << ", ";
    write_operand(out, ins.args.at(0)); break;
  case Instruction::IK_ATOMIC_EXCHANGE:
    out << ins.dest << " = atomic_exchange " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); out << ", "; write_operand(out, ins.second);
    out << ", "; write_operand(out, ins.args.at(0)); break;
  case Instruction::IK_INDEX:
    out << ins.dest << " = index " << lowir_type_text(ins.type);
    if(const char * projection = projection_name(ins.index_projection))
      out << " [projection=" << projection << ']';
    out << ' '; write_operand(out, ins.first); out << ", ";
    write_operand(out, ins.second); break;
  case Instruction::IK_UNARY:
    out << ins.dest << " = unary " << ins.op << ' ' << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); break;
  case Instruction::IK_BINARY:
  case Instruction::IK_CMP:
    out << ins.dest << (ins.kind == Instruction::IK_BINARY ? " = binary " : " = cmp ")
        << ins.op << ' ' << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); out << ", "; write_operand(out, ins.second); break;
  case Instruction::IK_CONVERT:
    out << ins.dest << " = convert " << ins.op << ' ' << lowir_type_text(ins.type) << ' '
        << lowir_type_text(ins.source_type) << ' '; write_operand(out, ins.first); break;
  case Instruction::IK_ATOMIC_ADD_FETCH:
    out << ins.dest << " = atomic_add_fetch " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); out << ", "; write_operand(out, ins.second);
    out << ", "; write_operand(out, ins.args.at(0)); break;
  case Instruction::IK_ATOMIC_COMPARE_EXCHANGE:
    out << ins.dest << " = atomic_compare_exchange " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); out << ", "; write_operand(out, ins.second);
    out << ", "; write_operand(out, ins.third); out << ", ";
    write_operand(out, ins.args.at(0)); out << ", "; write_operand(out, ins.args.at(1)); break;
  case Instruction::IK_ATOMIC_THREAD_FENCE:
  case Instruction::IK_ATOMIC_SIGNAL_FENCE:
    out << (ins.kind == Instruction::IK_ATOMIC_THREAD_FENCE ?
      "atomic_thread_fence " : "atomic_signal_fence ");
    write_operand(out, ins.first); break;
  case Instruction::IK_VA_START:
    out << "va_start "; write_operand(out, ins.first); break;
  case Instruction::IK_VA_ARG:
    out << ins.dest << " = va_arg " << lowir_type_text(ins.type) << ' ';
    write_operand(out, ins.first); break;
  case Instruction::IK_STACK_ALLOC:
    out << ins.dest << " = stack_alloc "; write_operand(out, ins.first); break;
  case Instruction::IK_CALL: write_call(out, ins); break;
  case Instruction::IK_COPYOBJ:
    out << "copyobj " << ins.byte_count << 'x' << ins.byte_alignment << ' ';
    write_operand(out, ins.first); out << ", "; write_operand(out, ins.second); break;
  case Instruction::IK_ZEROINIT:
    out << "zeroinit " << ins.byte_count << 'x' << ins.byte_alignment << ' ';
    write_operand(out, ins.first); break;
  case Instruction::IK_EH_TRY:
    out << "eh_try "; write_operand(out, ins.first); break;
  case Instruction::IK_EH_CLEANUP:
    out << "eh_cleanup "; write_operand(out, ins.first); break;
  case Instruction::IK_EH_CLEANUP_CLAUSE: out << "eh_cleanup"; break;
  case Instruction::IK_EH_CATCH:
    out << "eh_catch "; write_operand(out, ins.first);
    if(ins.has_eh_selector) out << ", " << ins.eh_selector;
    break;
  case Instruction::IK_EH_FILTER:
    out << "eh_filter";
    for(std::size_t i = 0; i < ins.args.size(); ++i) {
      out << (i ? ", " : " "); write_operand(out, ins.args[i]);
    }
    if(ins.has_eh_selector) out << (ins.args.empty() ? " " : ", ") << ins.eh_selector;
    break;
  case Instruction::IK_EH_CATCH_ALL:
    out << "eh_catch_all";
    if(ins.has_eh_selector) out << ", " << ins.eh_selector;
    break;
  case Instruction::IK_EH_END: out << "eh_end"; break;
  case Instruction::IK_THROW:
    out << "throw " << lowir_type_text(ins.type) << ' '; write_operand(out, ins.first); break;
  case Instruction::IK_EXCEPTION:
    out << ins.dest << " = exception " << lowir_type_text(ins.type); break;
  case Instruction::IK_EXCEPTION_SELECTOR:
    out << ins.dest << " = exception_selector " << lowir_type_text(ins.type); break;
  case Instruction::IK_RESUME: out << "resume"; break;
  case Instruction::IK_JUMP:
    out << "jump "; write_operand(out, ins.first); break;
  case Instruction::IK_BRANCH:
    out << "branch "; write_operand(out, ins.first); out << ", ";
    write_operand(out, ins.second); out << ", "; write_operand(out, ins.third); break;
  case Instruction::IK_SWITCH:
    out << "switch "; write_operand(out, ins.first); out << ", ";
    write_operand(out, ins.second);
    for(std::size_t i = 0; i + 1 < ins.args.size(); i += 2) {
      out << ", "; write_operand(out, ins.args[i]); out << ':';
      write_operand(out, ins.args[i + 1]);
    }
    break;
  case Instruction::IK_RETURN:
    out << "return " << lowir_type_text(ins.type);
    if(ins.type.kind != LTK_VOID) { out << ' '; write_operand(out, ins.first); }
    break;
  }
  write_debug(out, ins.debug_location);
}

void write_global_declaration(std::ostream & out, const GlobalDeclaration & item)
{
  out << "declare global " << item.name;
  if(item.has_type) out << " : " << lowir_type_text(item.type);
  write_global_metadata(out, item.storage, item.metadata);
  out << '\n';
}

void write_function_declaration(std::ostream & out,
                                const FunctionDeclaration & item)
{
  out << "declare function " << item.name;
  write_parameters(out, item.params);
  out << " -> " << lowir_type_text(item.return_type);
  write_function_metadata(out, item.boundary, item.metadata);
  out << '\n';
}

void write_global(std::ostream & out, const GlobalDefinition & item)
{
  out << "global " << item.name;
  if(!item.structured) out << " : " << lowir_type_text(item.type);
  write_global_metadata(out, item.storage, item.metadata);
  out << " = ";
  if(item.structured) {
    out << "{\n";
    for(std::size_t i = 0; i < item.data_items.size(); ++i) {
      const GlobalDefinition::DataItem & data = item.data_items[i];
      out << "  ";
      if(data.kind == GlobalDefinition::DataItem::ITEM_ZERO)
        out << "zero " << data.zero_bytes;
      else if(data.kind == GlobalDefinition::DataItem::ITEM_ADDR) {
        out << lowir_type_text(data.type) << " addr " << data.symbol;
        if(data.addr_addend > 0) out << " + " << data.addr_addend;
        else if(data.addr_addend < 0) out << " - " << -data.addr_addend;
      } else {
        out << lowir_type_text(data.type) << ' ';
        write_operand(out, data.literal_operand);
      }
      out << '\n';
    }
    out << "}\n";
    return;
  }
  if(item.init_kind == GlobalDefinition::INIT_ZERO) out << "zero";
  else if(item.init_kind == GlobalDefinition::INIT_INTEGER)
    write_operand(out, item.init_operand);
  else {
    out << "addr "; write_operand(out, item.init_operand);
    if(item.addr_addend > 0) out << " + " << item.addr_addend;
    else if(item.addr_addend < 0) out << " - " << -item.addr_addend;
  }
  out << '\n';
}

void write_function(std::ostream & out, const Function & function)
{
  out << "function " << function.name;
  write_parameters(out, function.params);
  out << " -> " << lowir_type_text(function.return_type);
  write_function_metadata(out, function.boundary, function.metadata);
  write_debug(out, function.debug_location);
  out << " {\n";
  for(std::size_t i = 0; i < function.slots.size(); ++i)
    out << "  slot " << function.slots[i].first << " : "
        << lowir_type_text(function.slots[i].second) << '\n';
  if(!function.slots.empty()) out << '\n';
  for(std::size_t i = 0; i < function.blocks.size(); ++i) {
    if(i) out << '\n';
    out << "  block " << function.blocks[i].label << ":\n";
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j) {
      out << "    ";
      write_instruction(out, function.blocks[i].instructions[j]);
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
    WRITER(out, (VECTOR)[i]); \
    wrote = true; \
  }
  WRITE_GROUP(program.global_declarations, write_global_declaration)
  WRITE_GROUP(program.function_declarations, write_function_declaration)
  WRITE_GROUP(program.globals, write_global)
  WRITE_GROUP(program.functions, write_function)
  for(std::size_t i = 0; i < program.object_aliases.size(); ++i) {
    out << "alias object " << program.object_aliases[i].object_symbol << " = "
        << program.object_aliases[i].target << '\n';
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
