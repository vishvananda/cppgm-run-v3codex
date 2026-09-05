#pragma once

#include "pa12_semantic.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm
{

struct LlvmIrExportStats
{
	SemanticAnalysisStats semantic;
	std::size_t semantic_nodes_lowered;
	std::size_t functions;
	std::size_t globals;
	std::size_t blocks;
	std::size_t instructions;
	std::uint64_t lowering_nanoseconds;
	std::uint64_t serialization_nanoseconds;

	LlvmIrExportStats();
};

// Experimental investigation boundary. The consumer lowers directly from the
// canonical semantic graph and does not construct or parse LowIR.
void WriteLlvmIrTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, LlvmIrExportStats* stats = 0);

}
