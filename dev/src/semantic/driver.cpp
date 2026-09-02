#include "semantic/analysis/analyzer.h"

#include "syntax/driver_detail.h"

#include <chrono>
#include <ostream>
#include <streambuf>

namespace cppgm
{

namespace semantic
{

namespace
{

class NullStreamBuffer : public std::streambuf
{
protected:
	int_type overflow(int_type value)
		{ return traits_type::not_eof(value); }
};

#if CPPGM_TELEMETRY_ENABLED
void PublishDriverStats(const std::string& source,
	const syntax::Stats& syntax, const std::chrono::steady_clock::time_point& start,
	Stats* stats)
{
	if (!stats) return;
	stats->preprocessing = syntax.preprocessing;
	stats->interning = syntax.interning;
	stats->tokens = syntax.tokens;
	stats->syntax_nodes = syntax.syntax_nodes;
	stats->parse_nanoseconds = syntax.parse_nanoseconds;
	stats->peak_stage_storage_bytes = source.size() +
		syntax.token_storage_bytes + syntax.syntax_storage_bytes +
		syntax.parser_storage_bytes + stats->semantic_storage_bytes;
	stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - start).count());
}
#endif

}

void WriteTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, semantic::Stats* stats)
{
#if CPPGM_TELEMETRY_ENABLED
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
#endif
	if (stats) *stats = Stats();
	syntax::Stats syntax;
	GraphStorage graph;
	Analyzer analyzer(graph, output, stats);
	syntax::RunTranslationUnit(path, source, options,
		0, &analyzer, stats ? &syntax : 0, &analyzer.SharedStrings());
#if CPPGM_TELEMETRY_ENABLED
	PublishDriverStats(source, syntax, started, stats);
#endif
}

void ConsumeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	SemanticGraphConsumer& consumer,
	Stats* stats, bool complete_constructor_unwind,
	bool host_object_emission, bool source_type_view)
{
#if CPPGM_TELEMETRY_ENABLED
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
#endif
	if (stats) *stats = Stats();
	syntax::Stats syntax;
	NullStreamBuffer sink_buffer;
	std::ostream sink(&sink_buffer);
	GraphStorage graph;
	{
		Analyzer analyzer(graph, sink, stats,
			true, false, complete_constructor_unwind, host_object_emission,
			source_type_view);
		syntax::RunTranslationUnit(path, source, options,
			0, &analyzer, stats ? &syntax : 0, &analyzer.SharedStrings());
	}
#if CPPGM_TELEMETRY_ENABLED
	PublishDriverStats(source, syntax, started, stats);
#endif
	// Token, parser, syntax, substitution, lookup, and demand scratch are dead.
	// The typed next phase borrows only the canonical graph owner.
	consumer.Consume(graph.View());
}

Stats::Stats()
	: tokens(0), syntax_nodes(0), semantic_nodes(0), semantic_edges(0),
	  interned_names(0), canonical_types(0), scopes(0), declarations(0),
	  expressions(0), name_path_parse_requests(0),
	  name_path_parse_components(0), name_path_single_component_parses(0),
	  structured_name_path_requests(0), syntax_name_path_requests(0),
	  syntax_name_path_direct(0), syntax_name_path_fallbacks(0),
	  lookup_spelling_requests(0),
	  declarator_name_requests(0), declarator_name_path_requests(0),
	  scope_prefix_requests(0), scope_prefix_cache_hits(0),
	  class_layouts(0), class_layout_member_visits(0),
	  virtual_base_layout_edge_visits(0), virtual_base_layout_facts(0),
	  virtual_base_layout_lookups(0), virtual_base_layout_probes(0),
	  direct_base_validation_visits(0),
	  class_zero_offset_subobject_visits(0),
	  special_member_fact_lookups(0), special_member_subobject_visits(0),
	  constructor_member_action_visits(0),
	  constructor_base_action_visits(0),
	  constructor_delegation_action_visits(0),
	  destructor_subobject_action_visits(0),
	  lexical_cleanup_action_visits(0),
	  unwind_cleanup_scope_visits(0), unwind_cleanup_action_visits(0),
	  enclosing_lifetime_queries(0), initializer_list_lifetime_queries(0),
	  temporary_dependency_visits(0), materialized_demand_visits(0),
	  nonthrowing_action_visits(0), runtime_initializer_visits(0),
	  static_constant_initializer_visits(0),
	  static_constant_dependency_edges(0),
	  empty_constructor_chain_requests(0),
	  empty_constructor_chain_cache_hits(0),
	  empty_constructor_chain_entity_visits(0),
	  empty_constructor_chain_dependency_edges(0),
	  empty_destructor_chain_visits(0),
	  empty_destructor_chain_cache_hits(0),
	  namespace_object_actions(0),
	  lookup_queries(0), lookup_scope_visits(0),
	  lookup_edge_visits(0), base_path_queries(0),
	  base_path_cache_hits(0), base_path_cache_misses(0),
	  base_path_edge_visits(0),
	  virtual_base_path_visits(0),
	  associated_scope_visits(0),
	  associated_declaration_visits(0), function_candidate_index_visits(0),
	  overload_candidates(0),
	  overload_order_comparisons(0), conversion_checks(0),
	  call_conversion_cache_hits(0), call_conversion_cache_misses(0),
	  braced_fact_cache_hits(0), braced_fact_cache_misses(0),
	  function_signature_lookups(0), polymorphic_classes(0),
	  virtual_slots(0), virtual_signature_lookups(0), virtual_overrides(0),
	  polymorphic_virtual_view_lookups(0),
	  polymorphic_virtual_view_merges(0),
	  virtual_slot_lookups(0), vtable_demands(0), access_checks(0),
	  access_path_visits(0), access_grant_probes(0),
	  template_specialization_requests(0),
	  template_specialization_cache_hits(0),
	  function_template_default_materializations(0),
	  function_template_default_request_cache_hits(0),
	  function_template_default_failure_cache_hits(0),
	  function_template_exception_specification_requests(0),
	  function_template_exception_specification_cache_hits(0),
	  function_template_exception_specification_evaluations(0),
	  template_argument_list_requests(0),
	  template_argument_list_cache_hits(0),
	  template_argument_list_index_probes(0), template_partition_requests(0),
	  template_partition_cache_hits(0), template_partition_index_probes(0),
	  function_template_result_identity_requests(0),
	  function_template_result_identity_cache_hits(0),
	  function_template_result_identity_index_probes(0),
	  function_template_result_identity_atom_visits(0),
	  function_template_result_identity_syntax_visits(0),
	  function_template_result_identity_environment_probes(0),
	  function_template_result_identity_alias_expansions(0),
	  template_partial_candidates(0),
	  template_partial_order_comparisons(0),
	  template_partial_shape_materializations(0),
	  template_partial_shape_cache_hits(0),
	  template_partial_deduction_visits(0),
	  function_template_deduction_visits(0), lambda_closure_requests(0),
	  lambda_closure_cache_hits(0), lambda_capture_summary_requests(0),
	  lambda_capture_summary_cache_hits(0), lambda_capture_syntax_visits(0),
	  lambda_capture_name_uses(0), constexpr_call_requests(0),
	  constexpr_call_cache_hits(0), constant_conversion_fact_requests(0),
	  constant_conversion_fact_cache_hits(0),
	  constexpr_local_index_probes(0),
	  constexpr_scope_index_probes(0),
	  constexpr_object_projection_visits(0),
	  constexpr_step_visits(0),
	  constexpr_max_depth(0), constexpr_peak_locals(0),
	  constexpr_scratch_peak_nodes(0), demand_worklist_pushes(0),
	  demanded_function_emissions(0),
	  definition_validation_only_completions(0),
	  definition_emission_required_completions(0),
	  default_constructor_emissions(0),
	  demand_requests(0), demand_unique_edges(0), demand_root_edges(0),
	  demand_dependency_edges(0), demand_replayed_functions(0),
	  demand_replayed_edges(0), demand_evaluated_use_requests(0),
	  demand_retained_call_requests(0), demand_address_requests(0),
	  demand_lifecycle_requests(0), demand_vtable_requests(0),
	  demand_static_lifecycle_requests(0),
	  demand_exception_cleanup_requests(0),
	  demand_explicit_instantiation_requests(0),
	  demand_abi_support_requests(0),
	  binding_layout_fact_records(0), binding_template_fact_records(0),
	  binding_output_fact_records(0), binding_operator_fact_records(0),
	  binding_value_records(0), binding_record_size(0), entity_record_size(0),
	  function_info_size(0), dump_node_size(0),
	  semantic_program_storage_bytes(0), semantic_dump_storage_bytes(0),
	  semantic_side_storage_bytes(0), semantic_shared_string_bytes(0),
	  semantic_storage_bytes(0), peak_stage_storage_bytes(0),
	  parse_nanoseconds(0), analysis_nanoseconds(0), render_nanoseconds(0),
	  elapsed_nanoseconds(0)
{
	for (std::size_t i = 0; i < NAME_PATH_PARSE_FAMILY_COUNT; ++i)
		name_path_parse_families[i] = 0;
	for (std::size_t i = 0; i < syntax::STAG_COUNT; ++i)
		syntax_name_path_fallback_tags[i] = 0;
	for (std::size_t i = 0; i < SEMANTIC_PRESENTATION_FAMILY_COUNT; ++i)
	{
		presentation_renders[i] = 0;
		presentation_render_components[i] = 0;
		presentation_render_bytes[i] = 0;
	}
	for (std::size_t i = 0; i < SEMANTIC_PRESENTATION_READ_FAMILY_COUNT; ++i)
	{
		presentation_reads[i] = 0;
		presentation_retained_values[i] = 0;
		presentation_retained_bytes[i] = 0;
	}
	for (std::size_t i = 0; i < SEMANTIC_GENERATED_IDENTITY_FAMILY_COUNT; ++i)
	{
		generated_identity_renders[i] = 0;
		generated_identity_render_components[i] = 0;
		generated_identity_render_bytes[i] = 0;
	}
}

}

}
