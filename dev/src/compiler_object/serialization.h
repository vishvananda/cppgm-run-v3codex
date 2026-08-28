#pragma once

#include "compiler_object/model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace compiler_object
{

struct SerializationStats
{
	std::size_t reserved_bytes = 0;
	std::size_t output_bytes = 0;
	std::size_t buffer_growths = 0;
	std::size_t full_buffer_copies = 0;
	std::uint64_t elapsed_nanoseconds = 0;
};

bool UsesPrivateFormat(const std::string& path);
void Write(const std::string& path,
	const Object& object, SerializationStats* stats = 0);
std::vector<unsigned char> Serialize(
	const Object& object, SerializationStats* stats = 0);
Object Read(const std::string& path);
bool IsObject(const std::string& path);

}
}
