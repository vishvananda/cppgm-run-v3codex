#pragma once

#include "pa12_semantic.h"

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
	SemanticAnalysisStats semantic;
	std::size_t lowered_nodes;
	std::size_t functions;
	std::size_t globals;
	std::size_t blocks;
	std::size_t instructions;
	std::size_t binding_index_probes;
	std::size_t slot_implicit_object_fact_reads;
	std::size_t virtual_calls;
	std::size_t vptr_stores;
	std::size_t vtable_offset_rows;
	std::size_t vtable_slots;
	std::size_t deleting_destructors;
	std::size_t rtti_graph_nodes_visited;
	std::size_t rtti_demand_requests;
	std::size_t rtti_types_demanded;
	std::size_t rtti_symbol_lookups;
	std::size_t rtti_base_dependency_visits;
	std::size_t cleanup_dispatch_probes;
	std::size_t cleanup_dispatch_cache_hits;
	std::size_t cleanup_dispatch_entries;
	std::size_t conditional_lifetime_slots;
	std::size_t conditional_lifetime_marks;
	std::size_t branch_cleanup_actions;
	std::size_t exception_selector_resets;
	std::size_t exception_selector_table_growth;
	std::size_t exception_selector_assignments;
	std::size_t typed_storage_bytes;
	std::size_t output_bytes;
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
