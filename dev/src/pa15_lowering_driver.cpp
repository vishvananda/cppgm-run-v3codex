#include "pa15_lowering.h"

#include "pa12_semantic.h"
#include "pa15_graph_lowering.h"
#include "pa15_lowir_model.h"
#include "pa15_lowir_render.h"
#include "pa15_lowering_support.h"

#include <chrono>
#include <ostream>
#include <stdexcept>

namespace cppgm
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_detail;
using namespace pa15_lowering_support;

namespace
{

class GraphConsumer : public SemanticGraphConsumer
{
public:
	GraphConsumer(TypedProgram& program, LowIRLoweringStats* stats,
		std::size_t source_ordinal)
		: program_(program), stats_(stats), source_ordinal_(source_ordinal) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		LowerSemanticGraph(graph, program_, stats_, source_ordinal_);
		if (stats_)
			stats_->lowering_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count());
	}

private:
	TypedProgram& program_;
	LowIRLoweringStats* stats_;
	std::size_t source_ordinal_;
};

}

LowIRLoweringStats::LowIRLoweringStats()
	: source_bytes(0), tokens(0), semantic_nodes(0), semantic_edges(0),
	  lowered_nodes(0), class_layouts(0), class_layout_member_visits(0),
	  constructor_member_action_visits(0),
	  constructor_base_action_visits(0),
	  destructor_subobject_action_visits(0),
	  lexical_cleanup_action_visits(0), namespace_object_actions(0),
	  overload_candidates(0), overload_order_comparisons(0),
	  conversion_checks(0), functions(0), globals(0), blocks(0), instructions(0),
	  binding_index_probes(0), typed_storage_bytes(0), output_bytes(0),
	  semantic_nanoseconds(0), lowering_nanoseconds(0), render_nanoseconds(0)
{
}

void WriteLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	LowIRLoweringStats* stats)
{
	if (sources.empty()) throw std::runtime_error("no PA15 source inputs");
	if (stats) *stats = LowIRLoweringStats();
	TypedProgram program;
	for (std::size_t i = 0; i < sources.size(); ++i)
	{
		GraphConsumer consumer(program, stats, i);
		SemanticAnalysisStats semantic_stats;
		ConsumeSemanticTranslationUnit(sources[i].path, sources[i].source,
			options, consumer, stats ? &semantic_stats : 0);
		if (stats)
		{
			stats->source_bytes += sources[i].source.size();
			stats->tokens += semantic_stats.tokens;
			stats->semantic_nodes += semantic_stats.semantic_nodes;
			stats->semantic_edges += semantic_stats.semantic_edges;
			stats->class_layouts += semantic_stats.class_layouts;
			stats->class_layout_member_visits +=
				semantic_stats.class_layout_member_visits;
			stats->constructor_member_action_visits +=
				semantic_stats.constructor_member_action_visits;
			stats->constructor_base_action_visits +=
				semantic_stats.constructor_base_action_visits;
			stats->destructor_subobject_action_visits +=
				semantic_stats.destructor_subobject_action_visits;
			stats->lexical_cleanup_action_visits +=
				semantic_stats.lexical_cleanup_action_visits;
			stats->namespace_object_actions +=
				semantic_stats.namespace_object_actions;
			stats->overload_candidates += semantic_stats.overload_candidates;
			stats->overload_order_comparisons +=
				semantic_stats.overload_order_comparisons;
			stats->conversion_checks += semantic_stats.conversion_checks;
			stats->semantic_nanoseconds += semantic_stats.analysis_nanoseconds;
		}
	}
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	CountingStreamBuffer buffer(output.rdbuf());
	std::ostream rendered(&buffer);
	RenderLowIRProgram(program, rendered);
	rendered.flush();
	if (!rendered || !output)
		throw std::runtime_error("unable to write LowIR output");
	if (stats)
	{
		stats->typed_storage_bytes = TypedStorageBytes(program);
		stats->output_bytes = buffer.Bytes();
		stats->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - render_started).count());
	}
}

}
