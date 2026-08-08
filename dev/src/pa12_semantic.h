#pragma once

#include "macro_processor.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm
{

namespace pa12_semantic_detail
{
class SemanticGraphConsumer;
}

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
	std::size_t class_layouts;
	std::size_t class_layout_member_visits;
	std::size_t class_zero_offset_subobject_visits;
	std::size_t special_member_fact_lookups;
	std::size_t special_member_subobject_visits;
	std::size_t constructor_member_action_visits;
	std::size_t constructor_base_action_visits;
	std::size_t constructor_delegation_action_visits;
	std::size_t destructor_subobject_action_visits;
	std::size_t lexical_cleanup_action_visits;
	std::size_t unwind_cleanup_scope_visits;
	std::size_t unwind_cleanup_action_visits;
	std::size_t temporary_dependency_visits;
	std::size_t namespace_object_actions;
	std::size_t lookup_queries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t lookup_cache_hits;
	std::size_t lookup_cache_misses;
	std::size_t lookup_cache_invalidations;
	std::size_t lookup_cache_dependency_edges;
	std::size_t lookup_cache_invalidation_pushes;
	std::size_t associated_scope_visits;
	std::size_t associated_declaration_visits;
	std::size_t overload_candidates;
	std::size_t overload_order_comparisons;
	std::size_t conversion_checks;
	std::size_t call_conversion_cache_hits;
	std::size_t call_conversion_cache_misses;
	std::size_t braced_fact_cache_hits;
	std::size_t braced_fact_cache_misses;
	std::size_t function_signature_lookups;
	std::size_t access_checks;
	std::size_t access_path_visits;
	std::size_t access_grant_probes;
	std::size_t template_specialization_requests;
	std::size_t template_specialization_cache_hits;
	std::size_t demand_worklist_pushes;
	std::size_t demanded_function_emissions;
	std::size_t default_constructor_emissions;
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

// Parse and analyze once, then synchronously expose the canonical semantic
// graph to a typed next-phase consumer. The graph view is borrowed and is no
// longer valid when this function returns.
void ConsumeSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	pa12_semantic_detail::SemanticGraphConsumer& consumer,
	SemanticAnalysisStats* stats = 0);

}
