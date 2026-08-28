#pragma once

#include "lowir/model/program.h"

namespace lowir_opt {

struct Stats;

bool remove_dead_slots(lowir_model::Function * function, Stats * stats);
bool local_slot_forward(lowir_model::Function * function, Stats * stats);
bool forward_single_store_slots(
  lowir_model::Function * function, Stats * stats);

}  // namespace lowir_opt
