#pragma once

// Final text presentation for the Itanium ABI encoder.

#include <cstddef>
#include <string>

namespace abi_mangle {
namespace detail {

std::string source_name(const std::string & name);
std::string number(long long value);
std::string base36(std::size_t value);
std::string discriminator(const std::string & occurrence);
std::string local_discriminator(std::size_t ordinal);
std::string lambda_discriminator(std::size_t ordinal);
void append_generated_lambda_source(std::string & output,
                                    std::size_t ordinal);

}  // namespace detail
}  // namespace abi_mangle
