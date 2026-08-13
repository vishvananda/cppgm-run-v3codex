#include "pa12_semantic_detail.h"

#include "pa10_syntax_driver_detail.h"

#include <chrono>
#include <ostream>
#include <streambuf>

namespace cppgm
{

namespace
{

class NullStreamBuffer : public std::streambuf
{
protected:
	int_type overflow(int_type value)
		{ return traits_type::not_eof(value); }
};

void PublishDriverStats(const std::string& source,
	const SyntaxStats& syntax, const std::chrono::steady_clock::time_point& start,
	SemanticAnalysisStats* stats)
{
	if (!stats) return;
	stats->preprocessing = syntax.preprocessing;
	stats->tokens = syntax.tokens;
	stats->syntax_nodes = syntax.syntax_nodes;
	stats->peak_stage_storage_bytes = source.size() +
		syntax.token_storage_bytes + syntax.syntax_storage_bytes +
		syntax.parser_storage_bytes + stats->semantic_storage_bytes;
	stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - start).count());
}

}

void WriteSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, SemanticAnalysisStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = SemanticAnalysisStats();
	SyntaxStats syntax;
	pa12_semantic_detail::SemanticGraphStorage graph;
	pa12_semantic_detail::SemanticAnalyzer analyzer(graph, output, stats);
	pa10_syntax_detail::RunSyntaxTranslationUnit(path, source, options,
		0, &analyzer, stats ? &syntax : 0, &analyzer.SharedStrings());
	PublishDriverStats(source, syntax, started, stats);
}

void ConsumeSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	pa12_semantic_detail::SemanticGraphConsumer& consumer,
	SemanticAnalysisStats* stats, bool complete_constructor_unwind,
	bool host_object_emission)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = SemanticAnalysisStats();
	SyntaxStats syntax;
	NullStreamBuffer sink_buffer;
	std::ostream sink(&sink_buffer);
	pa12_semantic_detail::SemanticGraphStorage graph;
	{
		pa12_semantic_detail::SemanticAnalyzer analyzer(graph, sink, stats,
			true, false, complete_constructor_unwind, host_object_emission);
		pa10_syntax_detail::RunSyntaxTranslationUnit(path, source, options,
			0, &analyzer, stats ? &syntax : 0, &analyzer.SharedStrings());
	}
	// Token, parser, syntax, substitution, lookup, and demand scratch are dead.
	// The typed next phase borrows only the canonical graph owner.
	consumer.Consume(graph.View());
	PublishDriverStats(source, syntax, started, stats);
}

SemanticAnalysisStats::SemanticAnalysisStats()
	: tokens(0), syntax_nodes(0), semantic_nodes(0), semantic_edges(0),
	  interned_names(0), canonical_types(0), scopes(0), declarations(0),
	  expressions(0), class_layouts(0), class_layout_member_visits(0),
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
	  lookup_edge_visits(0), lookup_cache_hits(0), lookup_cache_misses(0),
	  lookup_cache_invalidations(0), lookup_cache_dependency_edges(0),
	  lookup_cache_invalidation_pushes(0), base_path_queries(0),
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
	  demanded_function_emissions(0), default_constructor_emissions(0),
	  semantic_storage_bytes(0), peak_stage_storage_bytes(0),
	  analysis_nanoseconds(0), render_nanoseconds(0), elapsed_nanoseconds(0)
{
}

}
