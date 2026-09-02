#include "abi/itanium/abi_mangle_presentation.h"

#include "abi/itanium/abi_mangle_errors.h"

#include <algorithm>
#include <limits>

namespace abi_mangle {
namespace detail {

std::string source_name(const std::string & name)
{
  return std::to_string(name.size()) + name;
}

std::string number(long long value)
{
  if(value >= 0) return std::to_string(value);
  const unsigned long long magnitude =
    static_cast<unsigned long long>(-(value + 1)) + 1;
  return "n" + std::to_string(magnitude);
}

std::string base36(std::size_t value)
{
  const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string result;
  do {
    result.push_back(digits[value % 36]);
    value /= 36;
  } while(value != 0);
  std::reverse(result.begin(), result.end());
  return result;
}

std::string discriminator(const std::string & occurrence)
{
  if(occurrence.empty() || occurrence == "0") return std::string();
  std::size_t value = 0;
  for(char ch : occurrence) {
    if(ch < '0' || ch > '9') {
	  ThrowAbiFactInput(
        "invalid local discriminator '" + occurrence + "'");
    }
    const std::size_t digit = static_cast<std::size_t>(ch - '0');
    if(value > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
	  ThrowAbiFactInput(
        "local discriminator out of range '" + occurrence + "'");
    }
    value = value * 10 + digit;
  }
  if(value == 0) ThrowAbiFactInput("invalid local discriminator");
  return local_discriminator(value);
}

std::string local_discriminator(std::size_t ordinal)
{
  if(ordinal == 0) return std::string();
  const std::size_t value = ordinal - 1;
  if(value < 10) return "_" + std::to_string(value);
  return "__" + std::to_string(value) + "_";
}

std::string lambda_discriminator(std::size_t ordinal)
{
  return ordinal == 0 ? std::string() : std::to_string(ordinal - 1);
}

void append_generated_lambda_source(std::string & output,
                                    std::size_t ordinal)
{
  const std::string digits = std::to_string(ordinal);
  output += std::to_string(2 + digits.size());
  output += "$_";
  output += digits;
}

}  // namespace detail
}  // namespace abi_mangle
