#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lowir_native {

struct RelocatableLabel
{
  std::string name;
  std::size_t offset = 0;
};

struct RelocatableRelocation
{
  enum Kind { RELATIVE32, ABSOLUTE64 } kind = RELATIVE32;
  std::size_t offset = 0;
  std::string target;
  long long addend = 0;
};

struct RelocatableSection
{
  std::size_t alignment = 1;
  std::vector<unsigned char> bytes;
  std::vector<RelocatableLabel> labels;
  std::vector<RelocatableRelocation> relocations;
};

struct RelocatableObject
{
  std::vector<RelocatableSection> sections;
};

}  // namespace lowir_native
