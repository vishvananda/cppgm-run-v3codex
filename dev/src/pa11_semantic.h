#pragma once

#include "macro_processor.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm
{

struct TypeAnalysisStats
{
	PreprocessingStats preprocessing;
	std::size_t tokens;
	std::size_t syntax_nodes;
	std::size_t interned_names;
	std::size_t canonical_types;
	std::size_t scopes;
	std::size_t declarations;
	std::size_t lookup_queries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t name_index_probes;
	std::size_t type_index_probes;
	std::size_t using_index_probes;
	std::size_t rendered_type_nodes;
	std::size_t max_scope_depth;
	std::size_t render_stack_storage_bytes;
	std::size_t semantic_storage_bytes;
	std::size_t peak_stage_storage_bytes;
	std::uint64_t analysis_nanoseconds;
	std::uint64_t render_nanoseconds;
	std::uint64_t elapsed_nanoseconds;

	TypeAnalysisStats();
};

// Parse through the PA10 boundary, construct the PA11 canonical scope/type
// graph, and write its deterministic view. Syntax storage is phase-local.
void WriteTypeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, TypeAnalysisStats* stats = 0);

}
