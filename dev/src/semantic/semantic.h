#pragma once

#include "preprocess/macros/macro_processor.h"
#include "syntax/syntax.h"
#include "syntax/model/tags.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm
{

namespace semantic
{

enum NamePathParseFamily
{
	NAME_PATH_PARSE_SYNTAX_FALLBACK,
	NAME_PATH_PARSE_DECLARATION_CLASS,
	NAME_PATH_PARSE_DECLARATION_ENUM,
	NAME_PATH_PARSE_DECLARATION_PARAMETER,
	NAME_PATH_PARSE_DECLARATION_MEMBER_POINTER,
	NAME_PATH_PARSE_DECLARATION_USING,
	NAME_PATH_PARSE_CALL,
	NAME_PATH_PARSE_LITERAL,
	NAME_PATH_PARSE_TEMPLATE,
	NAME_PATH_PARSE_FRIEND,
	NAME_PATH_PARSE_GENERATED_LIBRARY,
	NAME_PATH_PARSE_SEMANTIC_ID_RECOVERY,
	NAME_PATH_PARSE_AMBIGUITY,
	NAME_PATH_PARSE_FAMILY_COUNT
};

enum SemanticPresentationFamily
{
	SEMANTIC_PRESENTATION_SCOPE_PREFIX,
	SEMANTIC_PRESENTATION_DISPLAY_NAME,
	SEMANTIC_PRESENTATION_EMISSION_NAME,
	SEMANTIC_PRESENTATION_CLASS_SPECIALIZATION,
	SEMANTIC_PRESENTATION_CLASS_STORAGE,
	SEMANTIC_PRESENTATION_CLASS_SCOPE_SLOT,
	SEMANTIC_PRESENTATION_LAMBDA_IDENTITY,
	SEMANTIC_PRESENTATION_GENERATED_IDENTITY,
	SEMANTIC_PRESENTATION_FAMILY_COUNT
};

enum SemanticPresentationReadFamily
{
	SEMANTIC_PRESENTATION_READ_FUNCTION_DISPLAY,
	SEMANTIC_PRESENTATION_READ_BINDING_QUALIFIED,
	SEMANTIC_PRESENTATION_READ_ENTITY_PRESENTATION,
	SEMANTIC_PRESENTATION_READ_SCOPE_EMISSION,
	SEMANTIC_PRESENTATION_READ_FAMILY_COUNT
};

enum SemanticGeneratedIdentityFamily
{
	SEMANTIC_GENERATED_LOCAL_TYPE,
	SEMANTIC_GENERATED_ANONYMOUS_UNION_TYPE,
	SEMANTIC_GENERATED_ANONYMOUS_ENUM,
	SEMANTIC_GENERATED_ANONYMOUS_UNION_STORAGE,
	SEMANTIC_GENERATED_CONSTRUCTOR_BASE_ENTRY,
	SEMANTIC_GENERATED_DESTRUCTOR_BASE_ENTRY,
	SEMANTIC_GENERATED_FUNCTION_TEMPLATE_RESULT_SHAPE,
	SEMANTIC_GENERATED_FUNCTION_TEMPLATE_PARAMETER_SHAPE,
	SEMANTIC_GENERATED_FUNCTION_TEMPLATE_NONDEDUCED_SHAPE,
	SEMANTIC_GENERATED_CLASS_TEMPLATE_NONDEDUCED_SHAPE,
	SEMANTIC_GENERATED_DEPENDENT_MEMBER_TEMPLATE_SHAPE,
	SEMANTIC_GENERATED_DEPENDENT_QUALIFIED_TYPE_SHAPE,
	SEMANTIC_GENERATED_RANGE_FOR_HIDDEN,
	SEMANTIC_GENERATED_STRUCTURED_BINDING_STORAGE,
	SEMANTIC_GENERATED_IDENTITY_FAMILY_COUNT
};

class SemanticGraphConsumer;

struct Stats
{
	PreprocessingStats preprocessing;
	syntax::InterningStats interning;
	std::size_t tokens;
	std::size_t syntax_nodes;
	std::size_t semantic_nodes;
	std::size_t semantic_edges;
	std::size_t interned_names;
	std::size_t canonical_types;
	std::size_t scopes;
	std::size_t declarations;
	std::size_t expressions;
	std::size_t name_path_parse_requests, name_path_parse_components;
	std::size_t name_path_single_component_parses;
	std::size_t name_path_parse_families[NAME_PATH_PARSE_FAMILY_COUNT];
	std::size_t syntax_name_path_fallback_tags[
		syntax::STAG_COUNT];
	std::size_t structured_name_path_requests;
	std::size_t syntax_name_path_requests;
	std::size_t syntax_name_path_direct;
	std::size_t syntax_name_path_fallbacks;
	std::size_t lookup_spelling_requests;
	std::size_t semantic_integer_parses;
	std::size_t declarator_name_requests;
	std::size_t declarator_name_path_requests;
	std::size_t presentation_renders[SEMANTIC_PRESENTATION_FAMILY_COUNT];
	std::size_t presentation_render_components[
		SEMANTIC_PRESENTATION_FAMILY_COUNT];
	std::size_t presentation_render_bytes[
		SEMANTIC_PRESENTATION_FAMILY_COUNT];
	std::size_t presentation_reads[
		SEMANTIC_PRESENTATION_READ_FAMILY_COUNT];
	std::size_t presentation_retained_values[
		SEMANTIC_PRESENTATION_READ_FAMILY_COUNT];
	std::size_t presentation_retained_bytes[
		SEMANTIC_PRESENTATION_READ_FAMILY_COUNT];
	std::size_t generated_identity_renders[
		SEMANTIC_GENERATED_IDENTITY_FAMILY_COUNT];
	std::size_t generated_identity_render_components[
		SEMANTIC_GENERATED_IDENTITY_FAMILY_COUNT];
	std::size_t generated_identity_render_bytes[
		SEMANTIC_GENERATED_IDENTITY_FAMILY_COUNT];
	std::size_t scope_prefix_requests;
	std::size_t scope_prefix_cache_hits;
	std::size_t class_layouts;
	std::size_t class_layout_member_visits;
	std::size_t virtual_base_layout_edge_visits;
	std::size_t virtual_base_layout_facts;
	std::size_t virtual_base_layout_lookups;
	std::size_t virtual_base_layout_probes;
	std::size_t direct_base_validation_visits;
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
	std::size_t enclosing_lifetime_queries;
	std::size_t initializer_list_lifetime_queries;
	std::size_t temporary_dependency_visits;
	std::size_t materialized_demand_visits;
	std::size_t nonthrowing_action_visits;
	std::size_t runtime_initializer_visits;
	std::size_t static_constant_initializer_visits;
	std::size_t static_constant_dependency_edges;
	std::size_t empty_constructor_chain_requests;
	std::size_t empty_constructor_chain_cache_hits;
	std::size_t empty_constructor_chain_entity_visits;
	std::size_t empty_constructor_chain_dependency_edges;
	std::size_t empty_destructor_chain_visits;
	std::size_t empty_destructor_chain_cache_hits;
	std::size_t namespace_object_actions;
	std::size_t lookup_queries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t base_path_queries;
	std::size_t base_path_cache_hits;
	std::size_t base_path_cache_misses;
	std::size_t base_path_edge_visits;
	std::size_t virtual_base_path_visits;
	std::size_t associated_scope_visits;
	std::size_t associated_declaration_visits;
	std::size_t function_candidate_index_visits;
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
	std::size_t polymorphic_virtual_view_lookups;
	std::size_t polymorphic_virtual_view_merges;
	std::size_t virtual_slot_lookups;
	std::size_t vtable_demands;
	std::size_t access_checks;
	std::size_t access_path_visits;
	std::size_t access_grant_probes;
	std::size_t template_specialization_requests;
	std::size_t template_specialization_cache_hits;
	std::size_t function_template_default_materializations;
	std::size_t function_template_default_request_cache_hits;
	std::size_t function_template_default_failure_cache_hits;
	std::size_t function_template_exception_specification_requests;
	std::size_t function_template_exception_specification_cache_hits;
	std::size_t function_template_exception_specification_evaluations;
	std::size_t template_argument_list_requests;
	std::size_t template_argument_list_cache_hits;
	std::size_t template_argument_list_index_probes;
	std::size_t template_partition_requests;
	std::size_t template_partition_cache_hits;
	std::size_t template_partition_index_probes;
	std::size_t function_template_result_identity_requests;
	std::size_t function_template_result_identity_cache_hits;
	std::size_t function_template_result_identity_index_probes;
	std::size_t function_template_result_identity_atom_visits;
	std::size_t function_template_result_identity_syntax_visits;
	std::size_t function_template_result_identity_environment_probes;
	std::size_t function_template_result_identity_alias_expansions;
	std::size_t template_partial_candidates;
	std::size_t template_partial_order_comparisons;
	std::size_t template_partial_shape_materializations;
	std::size_t template_partial_shape_cache_hits;
	std::size_t template_partial_deduction_visits;
	std::size_t function_template_deduction_visits;
	std::size_t lambda_closure_requests;
	std::size_t lambda_closure_cache_hits;
	std::size_t lambda_capture_summary_requests;
	std::size_t lambda_capture_summary_cache_hits;
	std::size_t lambda_capture_syntax_visits;
	std::size_t lambda_capture_name_uses;
	std::size_t constexpr_call_requests;
	std::size_t constexpr_call_cache_hits;
	std::size_t constant_conversion_fact_requests;
	std::size_t constant_conversion_fact_cache_hits;
	std::size_t constexpr_local_index_probes;
	std::size_t constexpr_scope_index_probes;
	std::size_t constexpr_object_projection_visits;
	std::size_t constexpr_step_visits;
	std::size_t constexpr_max_depth;
	std::size_t constexpr_peak_locals;
	std::size_t constexpr_scratch_peak_nodes;
	std::size_t demand_worklist_pushes;
	std::size_t demanded_function_emissions;
	std::size_t definition_validation_only_completions;
	std::size_t definition_emission_required_completions;
	std::size_t default_constructor_emissions;
	std::size_t demand_requests;
	std::size_t demand_unique_edges;
	std::size_t demand_root_edges;
	std::size_t demand_dependency_edges;
	std::size_t demand_replayed_functions;
	std::size_t demand_replayed_edges;
	std::size_t demand_evaluated_use_requests;
	std::size_t demand_retained_call_requests;
	std::size_t demand_address_requests;
	std::size_t demand_lifecycle_requests;
	std::size_t demand_vtable_requests;
	std::size_t demand_static_lifecycle_requests;
	std::size_t demand_exception_cleanup_requests;
	std::size_t demand_explicit_instantiation_requests;
	std::size_t demand_abi_support_requests;
	std::size_t binding_layout_fact_records;
	std::size_t binding_template_fact_records;
	std::size_t binding_output_fact_records;
	std::size_t binding_operator_fact_records;
	std::size_t binding_value_records;
	std::size_t binding_record_size;
	std::size_t entity_record_size;
	std::size_t function_info_size;
	std::size_t dump_node_size;
	std::size_t semantic_program_storage_bytes;
	std::size_t semantic_dump_storage_bytes;
	std::size_t semantic_side_storage_bytes;
	std::size_t semantic_shared_string_bytes;
	std::size_t semantic_storage_bytes;
	std::size_t peak_stage_storage_bytes;
	std::uint64_t parse_nanoseconds;
	std::uint64_t analysis_nanoseconds;
	std::uint64_t render_nanoseconds;
	std::uint64_t elapsed_nanoseconds;

	Stats();
};

// Parse through the shared PA10 boundary, construct canonical PA12 semantic
// facts, and render the deterministic assignment view. The syntax arena is
// phase-local; canonical types, bindings, and dump nodes are translation-unit
// owned and are released together after rendering.
void WriteTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, Stats* stats = 0);

// Parse and analyze once, then synchronously expose the canonical semantic
// graph to a typed next-phase consumer. The graph view is borrowed and is no
// longer valid when this function returns.
void ConsumeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	SemanticGraphConsumer& consumer,
	Stats* stats = 0,
	bool complete_constructor_unwind = false,
	bool host_object_emission = false,
	bool source_type_view = false);

}

}
