#pragma once

#include "macro_processor.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cppgm
{

struct LowIRSource
{
	std::string path;
	std::string source;

	LowIRSource(const std::string& path_value, const std::string& source_value)
		: path(path_value), source(source_value) {}
};

struct LowIRLoweringStats
{
	std::size_t source_bytes;
	std::size_t tokens;
	std::size_t semantic_nodes;
	std::size_t semantic_edges;
	std::size_t lowered_nodes;
	std::size_t class_layouts;
	std::size_t class_layout_member_visits;
	std::size_t constructor_member_action_visits;
	std::size_t constructor_base_action_visits;
	std::size_t destructor_subobject_action_visits;
	std::size_t lexical_cleanup_action_visits;
	std::size_t namespace_object_actions;
	std::size_t associated_scope_visits;
	std::size_t associated_declaration_visits;
	std::size_t overload_candidates;
	std::size_t overload_order_comparisons;
	std::size_t conversion_checks;
	std::size_t functions;
	std::size_t globals;
	std::size_t blocks;
	std::size_t instructions;
	std::size_t binding_index_probes;
	std::size_t typed_storage_bytes;
	std::size_t output_bytes;
	std::uint64_t semantic_nanoseconds;
	std::uint64_t lowering_nanoseconds;
	std::uint64_t render_nanoseconds;

	LowIRLoweringStats();
};

// Analyze all inputs through PA12, lower directly from the borrowed canonical
// graph into one typed LowIR program, and serialize the PA15 assignment view.
void WriteLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	LowIRLoweringStats* stats = 0);

}
