#include "pa15_lowering.h"

#include "pa12_semantic.h"
#include "pa15_graph_lowering.h"
#include "pa15_lowir_model.h"
#include "pa15_lowir_render.h"
#include "pa15_lowering_support.h"

#include <algorithm>
#include <chrono>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

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

SymbolId AddLifecycleHelperSymbol(TypedProgram* program,
	const std::string& proposed)
{
	std::size_t& count = program->symbol_name_counts[proposed];
	const std::string name = count++ == 0 ? proposed :
		proposed + "__sym" + std::to_string(count);
	const SymbolId symbol = static_cast<SymbolId>(program->symbols.size());
	program->symbols.push_back(Symbol(Symbol::FUNCTION_SYMBOL, name,
		std::string(), false, true, false));
	Symbol& record = program->symbols.back();
	record.definition_emitted = true;
	record.referenced = true;
	return symbol;
}

void CoalesceLifecycleRole(TypedProgram* program, LowIRLoweringStats* stats,
	bool initializer)
{
	std::vector<std::size_t> owners;
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		const Function& function = program->functions[i];
		if (initializer ? function.initializer : function.finalizer)
			owners.push_back(i);
	}
	if (owners.size() <= 1) return;

	const SymbolId role_symbol = program->functions[owners[0]].symbol;
	std::vector<SymbolId> helpers;
	helpers.reserve(owners.size());
	for (std::size_t i = 0; i < owners.size(); ++i)
	{
		Function& function = program->functions[owners[i]];
		if (i == 0)
			function.symbol = AddLifecycleHelperSymbol(program,
				initializer ? "__cppgm_tu_init" : "__cppgm_tu_fini");
		function.initializer = false;
		function.finalizer = false;
		program->symbols[function.symbol].referenced = true;
		helpers.push_back(function.symbol);
	}

	Function aggregate;
	aggregate.symbol = role_symbol;
	aggregate.result = LowVoid();
	aggregate.initializer = initializer;
	aggregate.finalizer = !initializer;
	Block entry("entry");
	entry.selected = true;
	for (std::size_t i = 0; i < helpers.size(); ++i)
	{
		const std::size_t index = initializer ? i : helpers.size() - i - 1;
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION, helpers[index], LowPtr());
		entry.instructions.push_back(call);
	}
	entry.instructions.push_back(Instruction(Instruction::RETURN_VOID));
	entry.terminated = true;
	aggregate.blocks.push_back(entry);
	aggregate.block_order.push_back(static_cast<BlockId>(0));
	program->functions.push_back(aggregate);
	if (stats)
	{
		++stats->functions;
		++stats->blocks;
		stats->instructions += helpers.size() + 1;
	}
}

void CoalesceLifecycleFunctions(TypedProgram* program,
	LowIRLoweringStats* stats)
{
	CoalesceLifecycleRole(program, stats, true);
	CoalesceLifecycleRole(program, stats, false);
	std::size_t initializer = program->functions.size();
	std::size_t finalizer = program->functions.size();
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		if (program->functions[i].initializer) initializer = i;
		if (program->functions[i].finalizer) finalizer = i;
	}
	if (initializer < program->functions.size() && finalizer < initializer)
		std::rotate(program->functions.begin() + finalizer,
			program->functions.begin() + initializer,
			program->functions.begin() + initializer + 1);
}

}

LowIRLoweringStats::LowIRLoweringStats()
	: source_bytes(0), tokens(0), semantic_nodes(0), semantic_edges(0),
	  lowered_nodes(0), class_layouts(0), class_layout_member_visits(0),
	  constructor_member_action_visits(0),
	  constructor_base_action_visits(0),
	  destructor_subobject_action_visits(0),
	  lexical_cleanup_action_visits(0), namespace_object_actions(0),
	  associated_scope_visits(0), associated_declaration_visits(0),
	  overload_candidates(0),
	  overload_order_comparisons(0),
	  conversion_checks(0), function_signature_lookups(0), access_checks(0),
	  access_path_visits(0), access_grant_probes(0),
	  functions(0), globals(0), blocks(0), instructions(0),
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
			stats->associated_scope_visits +=
				semantic_stats.associated_scope_visits;
			stats->associated_declaration_visits +=
				semantic_stats.associated_declaration_visits;
			stats->overload_candidates += semantic_stats.overload_candidates;
			stats->overload_order_comparisons +=
				semantic_stats.overload_order_comparisons;
			stats->conversion_checks += semantic_stats.conversion_checks;
			stats->function_signature_lookups +=
				semantic_stats.function_signature_lookups;
			stats->access_checks += semantic_stats.access_checks;
			stats->access_path_visits += semantic_stats.access_path_visits;
			stats->access_grant_probes += semantic_stats.access_grant_probes;
			stats->semantic_nanoseconds += semantic_stats.analysis_nanoseconds;
		}
	}
	std::chrono::steady_clock::time_point coalesce_started;
	if (stats) coalesce_started = std::chrono::steady_clock::now();
	CoalesceLifecycleFunctions(&program, stats);
	if (stats)
		stats->lowering_nanoseconds += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - coalesce_started).count());
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
