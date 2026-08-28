#pragma once

#include "lowir/model/program.h"
#include "native/mir/model.h"

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
  lowir_model::ValueId forwarded_parameter;
  // A zero-cost alias carries the identity of the optional parameter-home
  // transfer that established its register value.
  lowir_model::ValueId selected_parameter_home;
  // A directly selected address keeps its LowIR inputs live until the memory
  // instruction consumes the address.  This avoids manufacturing a pointer
  // temporary solely to feed an x86 addressing mode.
  bool deferred_address = false;
  // True when every register named by the deferred address is held by an
  // unspillable value for the address lifetime.  Cross-instruction address
  // composition is restricted to this form so a later spill cannot stale the
  // captured MIR operand.
  bool deferred_address_stable = false;
  // A constant INDEX whose base has stable storage can be replayed at each
  // storage consumer.  The base remains a semantic dependency; location is
  // deliberately not a captured physical register.
  bool rematerialized_constant_index = false;
  long long rematerialized_index_offset = 0;
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
