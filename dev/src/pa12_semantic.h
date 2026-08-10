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
	std::size_t materialized_demand_visits;
	std::size_t nonthrowing_action_visits;
	std::size_t runtime_initializer_visits;
	std::size_t static_constant_initializer_visits;
	std::size_t static_constant_dependency_edges;
	std::size_t empty_destructor_chain_visits;
	std::size_t empty_destructor_chain_cache_hits;
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
	std::size_t polymorphic_classes;
	std::size_t virtual_slots;
	std::size_t virtual_signature_lookups;
	std::size_t virtual_overrides;
	std::size_t virtual_slot_lookups;
	std::size_t vtable_demands;
	std::size_t access_checks;
	std::size_t access_path_visits;
	std::size_t access_grant_probes;
	std::size_t template_specialization_requests;
	std::size_t template_specialization_cache_hits;
	std::size_t template_partial_candidates;
	std::size_t template_partial_order_comparisons;
	std::size_t template_partial_shape_materializations;
	std::size_t template_partial_shape_cache_hits;
	std::size_t template_partial_deduction_visits;
	std::size_t lambda_closure_requests;
	std::size_t lambda_closure_cache_hits;
	std::size_t constexpr_call_requests;
	std::size_t constexpr_call_cache_hits;
	std::size_t constexpr_local_index_probes;
	std::size_t constexpr_scope_index_probes;
	std::size_t constexpr_object_projection_visits;
	std::size_t constexpr_step_visits;
	std::size_t constexpr_max_depth;
	std::size_t constexpr_peak_locals;
	std::size_t constexpr_scratch_peak_nodes;
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
