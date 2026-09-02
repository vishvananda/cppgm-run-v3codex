#include "semantic/type_view.h"

#include "semantic/semantic.h"
#include "semantic/analysis/analyzer.h"

#include <algorithm>
#include <chrono>
#include <ostream>

namespace cppgm
{
namespace semantic
{
namespace
{


class TypeViewConsumer : public SemanticGraphConsumer
{
public:
	TypeViewConsumer(std::ostream& output, TypeViewStats* stats)
		: output_(output), stats_(stats) {}

	void Consume(const SemanticGraphView& graph)
	{
#if CPPGM_TELEMETRY_ENABLED
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
#endif
		graph.program.Render(output_, stats_ ? &stats_->max_scope_depth : 0,
			stats_ ? &stats_->render_stack_storage_bytes : 0,
			stats_ ? &stats_->rendered_type_nodes : 0,
			PROGRAM_RENDER_SOURCE_TYPES);
		if (!stats_) return;
#if CPPGM_TELEMETRY_ENABLED
		stats_->interned_names = graph.program.names.Size();
		stats_->canonical_types = graph.program.types.Size();
		stats_->scopes = graph.program.ScopeCount();
		stats_->declarations = graph.program.bindings.size();
		stats_->lookup_queries = graph.program.lookup_queries;
		stats_->lookup_scope_visits = graph.program.lookup_scope_visits;
		stats_->lookup_edge_visits = graph.program.lookup_edge_visits;
		stats_->name_index_probes = graph.program.name_index_probes;
		stats_->type_index_probes = graph.program.types.IndexProbes();
		stats_->using_index_probes = graph.program.using_index_probes;
		stats_->semantic_storage_bytes = graph.program.StorageBytes();
		stats_->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
#endif
	}

private:
	std::ostream& output_;
	TypeViewStats* stats_;
};

}

TypeViewStats::TypeViewStats()
	: tokens(0), syntax_nodes(0), interned_names(0), canonical_types(0),
	  scopes(0), declarations(0), lookup_queries(0), lookup_scope_visits(0),
	  lookup_edge_visits(0), name_index_probes(0), type_index_probes(0),
	  using_index_probes(0), name_path_parse_requests(0),
	  name_path_parse_components(0), name_path_single_component_parses(0),
	  lookup_spelling_requests(0), structured_name_path_requests(0),
	  syntax_name_path_requests(0), syntax_name_path_direct(0),
	  syntax_name_path_fallbacks(0), rendered_type_nodes(0), max_scope_depth(0),
	  render_stack_storage_bytes(0), semantic_storage_bytes(0),
	  peak_stage_storage_bytes(0), analysis_nanoseconds(0),
	  render_nanoseconds(0), elapsed_nanoseconds(0)
{
	for (std::size_t family = 0;
		family < TYPE_NAME_PATH_PARSE_FAMILY_COUNT; ++family)
	{
		name_path_parse_families[family] = 0;
		lookup_spelling_families[family] = 0;
	}
}

void WriteTypeView(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, TypeViewStats* stats)
{
#if CPPGM_TELEMETRY_ENABLED
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
#endif
	if (stats) *stats = TypeViewStats();
	semantic::Stats semantic;
	TypeViewConsumer consumer(output, stats);
	ConsumeTranslationUnit(path, source, options, consumer,
		stats ? &semantic : 0, false, false, true);
	if (!stats) return;
#if CPPGM_TELEMETRY_ENABLED
	stats->preprocessing = semantic.preprocessing;
	stats->tokens = semantic.tokens;
	stats->syntax_nodes = semantic.syntax_nodes;
	stats->name_path_parse_requests = semantic.name_path_parse_requests;
	stats->name_path_parse_components = semantic.name_path_parse_components;
	stats->name_path_single_component_parses =
		semantic.name_path_single_component_parses;
	stats->name_path_parse_families[TYPE_NAME_PATH_PARSE_USING] =
		semantic.name_path_parse_families[
			NAME_PATH_PARSE_DECLARATION_USING];
	stats->name_path_parse_families[TYPE_NAME_PATH_PARSE_CLASS] =
		semantic.name_path_parse_families[
			NAME_PATH_PARSE_DECLARATION_CLASS];
	stats->name_path_parse_families[TYPE_NAME_PATH_PARSE_ENUM] =
		semantic.name_path_parse_families[
			NAME_PATH_PARSE_DECLARATION_ENUM];
	stats->name_path_parse_families[TYPE_NAME_PATH_PARSE_DECLARATOR] =
		semantic.name_path_parse_families[
			NAME_PATH_PARSE_DECLARATION_PARAMETER] +
		semantic.name_path_parse_families[
			NAME_PATH_PARSE_DECLARATION_MEMBER_POINTER];
	stats->name_path_parse_families[TYPE_NAME_PATH_PARSE_TYPE_LOOKUP] =
		semantic.name_path_parse_families[NAME_PATH_PARSE_SYNTAX_FALLBACK] +
		semantic.name_path_parse_families[
			NAME_PATH_PARSE_SEMANTIC_ID_RECOVERY];
	stats->name_path_parse_families[TYPE_NAME_PATH_PARSE_EXPRESSION] =
		semantic.name_path_parse_families[NAME_PATH_PARSE_CALL] +
		semantic.name_path_parse_families[NAME_PATH_PARSE_LITERAL];
	stats->lookup_spelling_requests = semantic.lookup_spelling_requests;
	stats->structured_name_path_requests =
		semantic.structured_name_path_requests;
	stats->syntax_name_path_requests = semantic.syntax_name_path_requests;
	stats->syntax_name_path_direct = semantic.syntax_name_path_direct;
	stats->syntax_name_path_fallbacks = semantic.syntax_name_path_fallbacks;
	stats->analysis_nanoseconds = semantic.analysis_nanoseconds;
	stats->semantic_storage_bytes = semantic.semantic_storage_bytes;
	stats->peak_stage_storage_bytes = std::max(
		semantic.peak_stage_storage_bytes,
		semantic.semantic_storage_bytes + stats->render_stack_storage_bytes);
	stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started).count());
#endif
}

}

}
