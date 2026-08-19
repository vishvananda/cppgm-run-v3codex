#pragma once

#include "macro_processor.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm
{

enum TypeNamePathParseFamily
{
	TYPE_NAME_PATH_PARSE_USING,
	TYPE_NAME_PATH_PARSE_CLASS,
	TYPE_NAME_PATH_PARSE_ENUM,
	TYPE_NAME_PATH_PARSE_DECLARATOR,
	TYPE_NAME_PATH_PARSE_TYPE_LOOKUP,
	TYPE_NAME_PATH_PARSE_EXPRESSION,
	TYPE_NAME_PATH_PARSE_FAMILY_COUNT
};

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
	std::size_t name_path_parse_requests;
	std::size_t name_path_parse_components;
	std::size_t name_path_single_component_parses;
	std::size_t name_path_parse_families[
		TYPE_NAME_PATH_PARSE_FAMILY_COUNT];
	std::size_t lookup_spelling_requests;
	std::size_t lookup_spelling_families[
		TYPE_NAME_PATH_PARSE_FAMILY_COUNT];
	std::size_t structured_name_path_requests;
	std::size_t syntax_name_path_requests;
	std::size_t syntax_name_path_direct;
	std::size_t syntax_name_path_fallbacks;
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
