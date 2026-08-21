#pragma once

#include "lowir_model.h"

namespace lowir_phi_edges {

bool has_critical_phi_edges(const lowir_model::LowirProgram & program);
void split_critical_phi_edges(lowir_model::LowirProgram * program);

}  // namespace lowir_phi_edges
