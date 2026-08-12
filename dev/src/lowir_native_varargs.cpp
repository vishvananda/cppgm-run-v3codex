#include "lowir_native_varargs.h"

#include "lowir_native_mir.h"

namespace lowir_native {
namespace varargs {
namespace {

lowir_model::LowType make_register_save_type()
{
  lowir_model::LowType type;
  type.text = "obj<176x16>";
  type.kind = lowir_model::LTK_OBJECT;
  type.bit_width = 176 * 8;
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
                 reg_operand(abi::argument_register(i)), "i64");
  for(std::size_t i = 0; i < 8; ++i)
    append_float_move(out,
      frame_operand(frame_offset + 48 + static_cast<long long>(i * 16)),
      xmm_operand(static_cast<XmmRegister>(i)), "f64");
}

void append_va_start(const abi::VariadicState & state,
                     long long register_save_offset,
                     std::vector<mir_model::MirInstruction> & out)
{
  using namespace build;
  append_move(out, reg_operand(XR_RAX),
              immediate(static_cast<long long>(state.gp_offset)));
  append_store(out, dereference(XR_RCX), reg_operand(XR_RAX), "i32");
  append_move(out, reg_operand(XR_RAX),
              immediate(static_cast<long long>(state.fp_offset)));
  append_store(out, dereference(XR_RCX, 4), reg_operand(XR_RAX), "i32");

  mir_model::MirInstruction overflow =
    machine_instruction(mir_model::MirInstruction::MI_LEA);
  append_operand(overflow, reg_operand(XR_RDX));
  append_operand(overflow,
    frame_operand(static_cast<long long>(state.overflow_arg_offset)));
  out.push_back(overflow);
  append_store(out, dereference(XR_RCX, 8), reg_operand(XR_RDX), "ptr");

  mir_model::MirInstruction save =
    machine_instruction(mir_model::MirInstruction::MI_LEA);
  append_operand(save, reg_operand(XR_RDX));
  append_operand(save, frame_operand(register_save_offset));
  out.push_back(save);
  append_store(out, dereference(XR_RCX, 16), reg_operand(XR_RDX), "ptr");
}

}  // namespace varargs
}  // namespace lowir_native
