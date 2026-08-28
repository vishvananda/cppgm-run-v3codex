#pragma once

#include "native/mir/model.h"

#include <vector>

namespace lowir_native {
namespace stack {

// Consumes the requested byte count in RAX, rounds it to the target stack
// alignment, and leaves RSP pointing at the new allocation.
void append_dynamic_allocation(
  std::vector<mir_model::MirInstruction> & out);

}  // namespace stack
}  // namespace lowir_native
