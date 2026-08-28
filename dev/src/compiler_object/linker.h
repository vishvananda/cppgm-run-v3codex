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

lowir_model::LowirProgram Link(
	std::vector<Object> objects,
	const std::string& target,
	lowir_model::PresentationPolicy presentation_policy =
		lowir_model::PRESENTATION_OBJECT_ONLY,
	LinkStats* stats = 0);

}
}
