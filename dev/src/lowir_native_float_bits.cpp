#include "lowir_native_float_bits.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace lowir_native {
namespace float_bits {
namespace {

std::string unsuffixed(const std::string & text)
{
  if(!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                       text.back() == 'l' || text.back() == 'L'))
    return text.substr(0, text.size() - 1);
  return text;
}

}  // namespace

std::uint64_t scalar(const std::string & text,
                     const lowir_model::LowType & type)
{
  const std::string number = unsuffixed(text);
  if(number == "snan" && type.kind == lowir_model::LTK_F32)
    return UINT64_C(0x7fa00000);
  if(number == "snan" && type.kind == lowir_model::LTK_F64)
    return UINT64_C(0x7ff4000000000000);
  errno = 0;
  char * end = 0;
  if(type.kind == lowir_model::LTK_F32) {
    const float value = std::strtof(number.c_str(), &end);
    if(errno || !end || *end)
      throw std::runtime_error("invalid f32 literal: " + text);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }
  if(type.kind == lowir_model::LTK_F64) {
    const double value = std::strtod(number.c_str(), &end);
    if(errno || !end || *end)
      throw std::runtime_error("invalid f64 literal: " + text);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }
  throw std::logic_error("floating literal requires f32 or f64");
}

std::pair<std::uint64_t, std::uint64_t> extended(const std::string & text)
{
  const std::string number = unsuffixed(text);
  if(number == "snan")
    return std::make_pair(UINT64_C(0xa000000000000000), UINT64_C(0x7fff));
  errno = 0;
  char * end = 0;
  const long double value = std::strtold(number.c_str(), &end);
  if(errno || !end || *end)
    throw std::runtime_error("invalid f80 literal: " + text);
  unsigned char bytes[16] = {};
  unsigned char native[sizeof(long double)] = {};
  std::memcpy(native, &value, sizeof(value));
  const std::size_t payload = std::min<std::size_t>(10, sizeof(value));
  std::copy(native, native + payload, bytes);
  std::uint64_t low = 0, high = 0;
  std::memcpy(&low, bytes, 8);
  std::memcpy(&high, bytes + 8, 8);
  return std::make_pair(low, high);
}

}  // namespace float_bits
}  // namespace lowir_native
