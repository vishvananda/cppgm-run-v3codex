#pragma once

#include "macro_processor.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm
{

struct SemanticAnalysisStats
{
	PreprocessingStats preprocessing;
	std::size_t tokens;
	std::size_t syntax_nodes;
	std::size_t semantic_nodes;
	std::size_t semantic_edges;
	std::size_t interned_names;
	std::size_t canonical_types;
	std::size_t scopes;
	std::size_t declarations;
	std::size_t expressions;
	std::size_t lookup_queries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t overload_candidates;
	std::size_t conversion_checks;
	std::size_t semantic_storage_bytes;
	std::size_t peak_stage_storage_bytes;
	std::uint64_t analysis_nanoseconds;
	std::uint64_t render_nanoseconds;
	std::uint64_t elapsed_nanoseconds;

	SemanticAnalysisStats();
};

// Parse through the shared PA10 boundary, construct canonical PA12 semantic
// facts, and render the deterministic assignment view. The syntax arena is
// phase-local; canonical types, bindings, and dump nodes are translation-unit
// owned and are released together after rendering.
void WriteSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, SemanticAnalysisStats* stats = 0);

}
