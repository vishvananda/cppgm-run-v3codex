#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "macro_processor.h"

namespace cppgm
{

struct RecognitionStats
{
	PreprocessingStats preprocessing;
	std::size_t tokens;
	std::size_t interned_identifiers;
	std::size_t interned_identifier_bytes;
	std::size_t angle_openings;
	std::size_t angle_closings;
	std::size_t memo_queries;
	std::size_t memo_hits;
	std::size_t rule_evaluations;
	std::size_t expression_evaluations;
	std::size_t memo_entries;
	std::uint64_t elapsed_nanoseconds;

	RecognitionStats();
};

// Run the existing PA5 source-to-phase-7 path and recognize the resulting
// token cursor against the fixed PA6 translation-unit grammar. Translation
// failures throw; a syntactically invalid token sequence returns false.
bool RecognizeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	RecognitionStats* stats = 0);

}
