#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa30
{

struct CompilerObject
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

struct ObjectSerializationStats
{
	std::size_t reserved_bytes = 0;
	std::size_t output_bytes = 0;
	std::size_t buffer_growths = 0;
	std::size_t full_buffer_copies = 0;
	std::uint64_t elapsed_nanoseconds = 0;
};

void WriteCompilerObject(const std::string& path,
	const CompilerObject& object);
std::vector<unsigned char> SerializeCompilerObject(
	const CompilerObject& object, ObjectSerializationStats* stats = 0);
CompilerObject ReadCompilerObject(const std::string& path);
bool IsCompilerObject(const std::string& path);

lowir_model::LowirProgram LinkCompilerObjects(
	std::vector<CompilerObject> objects,
	const std::string& target,
	LinkStats* stats = 0);

}
}
