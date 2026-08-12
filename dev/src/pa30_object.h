#pragma once

#include "lowir_model.h"

#include <cstddef>
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
	std::size_t definitions;
	std::size_t coalesced_weak_definitions;

	LinkStats();
};

void WriteCompilerObject(const std::string& path,
	const CompilerObject& object);
CompilerObject ReadCompilerObject(const std::string& path);
bool IsCompilerObject(const std::string& path);

lowir_model::LowirProgram LinkCompilerObjects(
	std::vector<CompilerObject> objects,
	const std::string& target,
	LinkStats* stats = 0);

}
}
