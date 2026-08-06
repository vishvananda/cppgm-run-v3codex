#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "macro_processor.h"

namespace cppgm
{

struct SyntaxStats
{
	PreprocessingStats preprocessing;
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

	SyntaxStats();
};

// Parse one translation unit through phase 7 and write the PA10 syntax-tree
// view. All retained token and syntax storage is owned by this call.
void WriteSyntaxTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, SyntaxStats* stats = 0);

}
