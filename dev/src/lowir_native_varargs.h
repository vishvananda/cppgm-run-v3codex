#pragma once

#include <vector>

#include "lowir_model.h"
#include "lowir_native_abi.h"
#include "mir_model.h"

namespace lowir_native {
namespace varargs {

const lowir_model::LowType & register_save_type();
void append_register_save(long long frame_offset,
                          std::vector<mir_model::MirInstruction> & out);
void append_va_start(const abi::VariadicState & state,
                     long long register_save_offset,
                     std::vector<mir_model::MirInstruction> & out);

}  // namespace varargs
}  // namespace lowir_native
