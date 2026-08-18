#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "lowir_model.h"

namespace lowir_native {
namespace float_bits {

std::uint64_t scalar(const std::string & text,
                     const lowir_model::LowType & type);
std::pair<std::uint64_t, std::uint64_t> extended(const std::string & text);

}  // namespace float_bits
}  // namespace lowir_native
