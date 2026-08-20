#include "pa11_mainline_view.h"

#include "pa12_semantic.h"
#include "pa12_semantic_detail.h"

#include <chrono>
#include <ostream>

namespace cppgm
{
namespace
{

using namespace pa12_semantic_detail;

class TypeViewConsumer : public SemanticGraphConsumer
{
public:
	TypeViewConsumer(std::ostream& output, TypeAnalysisStats* stats)
		: output_(output), stats_(stats) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		graph.program.Render(output_, stats_ ? &stats_->max_scope_depth : 0,
			stats_ ? &stats_->render_stack_storage_bytes : 0,
			stats_ ? &stats_->rendered_type_nodes : 0);
		if (!stats_) return;
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
	}

private:
	std::ostream& output_;
	TypeAnalysisStats* stats_;
};

}

void WriteMainlineTypeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, TypeAnalysisStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = TypeAnalysisStats();
	SemanticAnalysisStats semantic;
	TypeViewConsumer consumer(output, stats);
	ConsumeSemanticTranslationUnit(path, source, options, consumer,
		stats ? &semantic : 0);
	if (!stats) return;
	stats->preprocessing = semantic.preprocessing;
	stats->tokens = semantic.tokens;
	stats->syntax_nodes = semantic.syntax_nodes;
	stats->analysis_nanoseconds = semantic.analysis_nanoseconds;
	stats->peak_stage_storage_bytes = semantic.peak_stage_storage_bytes +
		stats->render_stack_storage_bytes;
	stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started).count());
}

}
