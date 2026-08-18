#pragma once

#include "lowir_model.h"
#include "mir_model.h"

#include <string>

namespace lowir_native {

struct ValueFact
{
  mir_model::MirOperand location;
  lowir_model::LowType type;
  bool parameter = false, fixed_register_home = false;
  bool frame_address = false, has_frame_provenance = false;
  long long frame_provenance = 0;
  // A compiler-created scalar home remains attached to the value even while
  // the current location is a register.  Its binding ordinal distinguishes
  // disjoint lifetimes that reuse the same physical frame offset.
  bool has_spill_home = false;
  mir_model::MirOperand spill_home;
  mir_model::MirOperand pointer_global_cell;
  std::string forwarded_parameter;
  // A directly selected address keeps its LowIR inputs live until the memory
  // instruction consumes the address.  This avoids manufacturing a pointer
  // temporary solely to feed an x86 addressing mode.
  bool deferred_address = false;
  lowir_model::Operand deferred_address_base;
  lowir_model::Operand deferred_address_index;
};

struct GprMove
{
  X64Register destination = XR_RDI;
  mir_model::MirOperand source;
  lowir_model::LowType type;
  bool source_is_address = false;
  bool object_chunk = false;
  lowir_model::Operand object_source;
  std::size_t chunk_offset = 0;
  bool pending = true;
};

}  // namespace lowir_native
