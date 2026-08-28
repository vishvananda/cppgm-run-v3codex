#pragma once

#include <cstddef>
#include <string>

#include "native/mir/model.h"

namespace lowir_native {
namespace data_layout {

std::size_t type_size(const lowir_model::LowType & type);
unsigned type_width(const lowir_model::LowType & type);
std::size_t global_alignment(const mir_model::MirGlobalDefinition & global);

}  // namespace data_layout
}  // namespace lowir_native
