#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace compiler_object
{

struct Object
{
	std::string target;
	lowir_model::LowirProgram lowir;
};

struct LinkStats
{
	std::size_t objects;
	std::size_t symbols;
	std::size_t symbol_probes;
	std::size_t rename_probes;
	std::size_t definitions;
	std::size_t coalesced_weak_definitions;
	std::uint64_t link_nanoseconds;

	LinkStats();
};

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

lowir_model::LowirProgram Link(
	std::vector<Object> objects,
	const std::string& target,
	lowir_model::PresentationPolicy presentation_policy =
		lowir_model::PRESENTATION_OBJECT_ONLY,
	LinkStats* stats = 0);

}
}
