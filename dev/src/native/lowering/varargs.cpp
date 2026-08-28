#include "native/lowering/varargs.h"

#include "native/mir/construction.h"

namespace lowir_native {
namespace varargs {
namespace {

lowir_model::LowType make_register_save_type()
{
  lowir_model::LowType type;
  type.kind = lowir_model::LTK_OBJECT;
  type.storage_size = 176;
  type.alignment = 16;
  return type;
}

}  // namespace

const lowir_model::LowType & register_save_type()
{
  static const lowir_model::LowType type = make_register_save_type();
  return type;
}

void append_register_save(long long frame_offset,
                          std::vector<mir_model::MirInstruction> & out)
{
  using namespace build;
  for(std::size_t i = 0; i < 6; ++i)
    append_store(out, frame_operand(frame_offset + static_cast<long long>(i * 8)),
                 reg_operand(abi::argument_register(i)), machine_type(lowir_model::LTK_I64));
  for(std::size_t i = 0; i < 8; ++i)
    append_float_move(out,
      frame_operand(frame_offset + 48 + static_cast<long long>(i * 16)),
      xmm_operand(static_cast<XmmRegister>(i)), machine_type(lowir_model::LTK_F64));
}

void append_va_start(const abi::VariadicState & state,
                     long long register_save_offset,
                     std::vector<mir_model::MirInstruction> & out)
{
  using namespace build;
  append_move(out, reg_operand(XR_RAX),
              immediate(static_cast<long long>(state.gp_offset)));
  append_store(out, dereference(XR_RCX), reg_operand(XR_RAX), machine_type(lowir_model::LTK_I32));
  append_move(out, reg_operand(XR_RAX),
              immediate(static_cast<long long>(state.fp_offset)));
  append_store(out, dereference(XR_RCX, 4), reg_operand(XR_RAX), machine_type(lowir_model::LTK_I32));

  mir_model::MirInstruction overflow =
    machine_instruction(mir_model::MirInstruction::MI_LEA);
  append_operand(overflow, reg_operand(XR_RDX));
  append_operand(overflow,
    frame_operand(static_cast<long long>(state.overflow_arg_offset)));
  out.push_back(overflow);
  append_store(out, dereference(XR_RCX, 8), reg_operand(XR_RDX), machine_type(lowir_model::LTK_PTR));

  mir_model::MirInstruction save =
    machine_instruction(mir_model::MirInstruction::MI_LEA);
  append_operand(save, reg_operand(XR_RDX));
  append_operand(save, frame_operand(register_save_offset));
  out.push_back(save);
  append_store(out, dereference(XR_RCX, 16), reg_operand(XR_RDX), machine_type(lowir_model::LTK_PTR));
}

void append_va_arg_address(bool floating,
                           std::vector<mir_model::MirInstruction> & out)
{
  using namespace build;
  const long long offset_field = floating ? 4 : 0;
  const long long limit = floating ? 176 : 48;
  const long long register_step = floating ? 16 : 8;
  append_load(out, reg_operand(XR_RAX),
              dereference(XR_RCX, offset_field), machine_type(lowir_model::LTK_I32));
  append_load(out, reg_operand(XR_RDX), dereference(XR_RCX, 16), machine_type(lowir_model::LTK_PTR));
  mir_model::MirInstruction register_address =
    machine_instruction(mir_model::MirInstruction::MI_ADD);
  append_operand(register_address, reg_operand(XR_RDX));
  append_operand(register_address, reg_operand(XR_RAX));
  out.push_back(register_address);
  append_load(out, reg_operand(XR_RSI), dereference(XR_RCX, 8), machine_type(lowir_model::LTK_PTR));
  mir_model::MirInstruction compare =
    machine_instruction(mir_model::MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
  append_operand(compare, reg_operand(XR_RAX));
  append_operand(compare, immediate(limit));
  out.push_back(compare);
  mir_model::MirInstruction below =
    machine_instruction(mir_model::MirInstruction::MI_SETCC);
  below.condition = XC_B;
  append_operand(below, reg_operand(XR_RDI));
  out.push_back(below);
  mir_model::MirInstruction widen =
    machine_instruction(mir_model::MirInstruction::MI_MOVZX);
  append_operand(widen, reg_operand(XR_RDI));
  append_operand(widen, reg_operand(XR_RDI));
  out.push_back(widen);
  mir_model::MirInstruction negate =
    machine_instruction(mir_model::MirInstruction::MI_NEG);
  append_operand(negate, reg_operand(XR_RDI));
  out.push_back(negate);

  append_move(out, reg_operand(XR_R8), reg_operand(XR_RDX));
  mir_model::MirInstruction difference =
    machine_instruction(mir_model::MirInstruction::MI_SUB);
  append_operand(difference, reg_operand(XR_R8));
  append_operand(difference, reg_operand(XR_RSI));
  out.push_back(difference);
  mir_model::MirInstruction select =
    machine_instruction(mir_model::MirInstruction::MI_AND);
  append_operand(select, reg_operand(XR_R8));
  append_operand(select, reg_operand(XR_RDI));
  out.push_back(select);
  mir_model::MirInstruction select_base =
    machine_instruction(mir_model::MirInstruction::MI_ADD);
  append_operand(select_base, reg_operand(XR_R8));
  append_operand(select_base, reg_operand(XR_RSI));
  out.push_back(select_base);

  append_move(out, reg_operand(XR_R9), reg_operand(XR_RDI));
  mir_model::MirInstruction register_increment =
    machine_instruction(mir_model::MirInstruction::MI_AND);
  append_operand(register_increment, reg_operand(XR_R9));
  append_operand(register_increment, immediate(register_step));
  out.push_back(register_increment);
  mir_model::MirInstruction advance_offset =
    machine_instruction(mir_model::MirInstruction::MI_ADD);
  append_operand(advance_offset, reg_operand(XR_RAX));
  append_operand(advance_offset, reg_operand(XR_R9));
  out.push_back(advance_offset);
  append_store(out, dereference(XR_RCX, offset_field),
               reg_operand(XR_RAX), machine_type(lowir_model::LTK_I32));

  append_move(out, reg_operand(XR_R10), reg_operand(XR_RDI));
  mir_model::MirInstruction invert =
    machine_instruction(mir_model::MirInstruction::MI_NOT);
  append_operand(invert, reg_operand(XR_R10));
  out.push_back(invert);
  mir_model::MirInstruction overflow_increment =
    machine_instruction(mir_model::MirInstruction::MI_AND);
  append_operand(overflow_increment, reg_operand(XR_R10));
  append_operand(overflow_increment, immediate(8));
  out.push_back(overflow_increment);
  mir_model::MirInstruction advance_overflow =
    machine_instruction(mir_model::MirInstruction::MI_ADD);
  append_operand(advance_overflow, reg_operand(XR_RSI));
  append_operand(advance_overflow, reg_operand(XR_R10));
  out.push_back(advance_overflow);
  append_store(out, dereference(XR_RCX, 8), reg_operand(XR_RSI), machine_type(lowir_model::LTK_PTR));
}

}  // namespace varargs
}  // namespace lowir_native
