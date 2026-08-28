#pragma once

#include <vector>

#include "lowir/model/program.h"
#include "native/lowering/abi.h"
#include "native/mir/model.h"

namespace lowir_native {
namespace varargs {

const lowir_model::LowType & register_save_type();
void append_register_save(long long frame_offset,
                          std::vector<mir_model::MirInstruction> & out);
void append_va_start(const abi::VariadicState & state,
                     long long register_save_offset,
                     std::vector<mir_model::MirInstruction> & out);

// Consumes a va_list address in RCX and leaves the selected scalar argument
// address in R8 while advancing the register-save or overflow cursor.
void append_va_arg_address(bool floating,
                           std::vector<mir_model::MirInstruction> & out);

}  // namespace varargs
}  // namespace lowir_native
