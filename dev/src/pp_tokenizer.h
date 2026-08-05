#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct IPPTokenStream;

namespace cppgm
{

struct PPTokenizationStats
{
	// Optional low-overhead stage telemetry. The peak covers the reusable
	// spelling buffer; the immutable source storage remains source_bytes.
	std::size_t source_bytes;
	std::size_t decoded_code_points;
	std::size_t translated_code_points;
	std::size_t emitted_tokens;
	std::size_t emitted_token_bytes;
	std::size_t peak_token_buffer_bytes;
	std::uint64_t elapsed_nanoseconds;

	PPTokenizationStats();
};

// Execute translation phases 1-3 over one immutable source buffer. Tokens are
// emitted as they are recognized; no translated-source or token vector is
// retained.
void TokenizePreprocessingFile(const std::string& source,
	IPPTokenStream& output,
	PPTokenizationStats* stats = 0);

// Retokenize a phase-4 spelling produced by ##. The spelling has already
// passed translation phases 1-2, so trigraph replacement, UCN conversion, and
// line splicing must not be applied a second time.
void TokenizeGeneratedPreprocessingToken(const std::string& spelling,
	IPPTokenStream& output);

}
