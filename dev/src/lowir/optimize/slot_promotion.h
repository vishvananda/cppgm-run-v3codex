#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

bool slot_is_phi_scalar_type(const lowir_model::LowType & type);

std::vector<unsigned char> find_promotable_slots(
  const lowir_model::Function & function, std::size_t * count);

bool eliminate_dead_slot_stores(lowir_model::Function * function,
                                Stats * stats);

}  // namespace lowir_opt
