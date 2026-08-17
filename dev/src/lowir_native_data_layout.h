#pragma once

#include <cstddef>
#include <string>

#include "mir_model.h"

namespace lowir_native {
namespace data_layout {

std::size_t type_size(const std::string & type);
unsigned type_width(const std::string & type);
std::size_t global_alignment(const mir_model::MirGlobalDefinition & global);

}  // namespace data_layout
}  // namespace lowir_native
