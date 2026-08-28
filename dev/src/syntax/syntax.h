#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "support/interning/frontend_intern.h"
#include "preprocess/macros/macro_processor.h"

namespace cppgm
{

namespace syntax
{
class SyntaxTreeConsumer;

struct InterningStats
{
	InternedStringStats table;
	std::size_t source_location_calls;
	std::size_t token_spelling_calls;
	std::size_t syntax_tag_calls;
	std::size_t syntax_payload_calls;
	std::size_t syntax_tag_query_calls;
	std::size_t syntax_payload_update_calls;
	std::size_t syntax_tag_cache_hits;
	std::size_t syntax_tag_cache_misses;
	std::size_t source_file_cache_hits;
	std::size_t source_file_cache_misses;

	InterningStats();
	void Accumulate(const InterningStats& other);
};

struct Stats
{
	PreprocessingStats preprocessing;
	InterningStats interning;
	std::size_t tokens;
	std::size_t interned_spellings;
	std::size_t spelling_bytes;
	std::size_t syntax_nodes;
	std::size_t syntax_edges;
	std::size_t syntax_output_bytes;
	std::size_t max_syntax_depth;
	std::size_t parser_checkpoints;
	std::size_t parser_rollbacks;
	std::size_t template_argument_probes;
	std::size_t template_argument_scans;
	std::size_t template_argument_cache_hits;
	std::size_t template_argument_scan_tokens;
	std::size_t max_template_argument_scan_tokens;
	std::size_t failed_template_argument_scans;
	std::size_t parser_fact_changes;
	std::size_t token_storage_bytes;
	std::size_t syntax_storage_bytes;
	std::size_t parser_storage_bytes;
	std::size_t render_stack_storage_bytes;
	std::size_t peak_stage_storage_bytes;
	std::uint64_t parse_nanoseconds;
	std::uint64_t render_nanoseconds;
	std::uint64_t elapsed_nanoseconds;

	Stats();
};

// Parse one translation unit through phase 7 and write the PA10 syntax-tree
// view. All retained token and syntax storage is owned by this call.
void WriteTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, Stats* stats = 0);

// Parse one translation unit through the shared PA10 syntax boundary and give
// a phase-local, read-only arena view to a semantic consumer. The arena and
// retained tokens are released when this call returns.
void ConsumeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	SyntaxTreeConsumer& consumer,
	Stats* stats = 0);

}
}
