#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "post_tokenizer.h"

namespace cppgm
{

struct MacroProcessingStats
{
	PPTokenizationStats tokenization;
	PostTokenizationStats postprocessing;
	std::size_t logical_lines;
	std::size_t directive_lines;
	std::size_t source_tokens;
	std::size_t interned_identifiers;
	std::size_t interned_identifier_bytes;
	std::size_t macro_definitions;
	std::size_t macro_undefinitions;
	std::size_t macro_lookups;
	std::size_t macro_invocations;
	std::size_t argument_prescans;
	std::size_t expanded_tokens;
	std::size_t pasted_tokens;
	std::size_t pasted_spelling_bytes;
	std::size_t peak_line_tokens;
	std::size_t peak_rescan_tokens;
	std::size_t peak_retained_replacement_tokens;
	std::size_t peak_expansion_frames;
	std::size_t peak_argument_storage_bytes;
	std::size_t paint_roots;
	std::size_t paint_nodes;
	std::uint64_t elapsed_nanoseconds;

	MacroProcessingStats();
};

// Execute PA4 preprocessing over one immutable source buffer and feed the
// expanded preprocessing-token events directly into the reusable PA2 phase.
void ProcessMacros(const std::string& source,
	IPostTokenStream& output,
	MacroProcessingStats* stats = 0);

}
