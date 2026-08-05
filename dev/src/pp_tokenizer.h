#pragma once

#include <cstddef>
#include <string>

struct IPPTokenStream;

namespace cppgm
{

struct PPTokenizationStats
{
	std::size_t source_bytes;
	std::size_t decoded_code_points;
	std::size_t emitted_tokens;

	PPTokenizationStats();
};

// Execute translation phases 1-3 over one immutable source buffer. Tokens are
// emitted as they are recognized; no translated-source or token vector is
// retained.
void TokenizePreprocessingFile(const std::string& source,
	IPPTokenStream& output,
	PPTokenizationStats* stats = 0);

}
