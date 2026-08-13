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
  std::string pointer_global_cell, forwarded_parameter;
};

}  // namespace lowir_native
