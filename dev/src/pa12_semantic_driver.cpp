#include "pa12_semantic_detail.h"

#include <chrono>
#include <sstream>

namespace cppgm
{

namespace
{

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
	pa12_semantic_detail::SemanticAnalyzer analyzer(output, stats);
	ConsumeSyntaxTranslationUnit(path, source, options, analyzer,
		stats ? &syntax : 0);
	PublishDriverStats(source, syntax, started, stats);
}

void ConsumeSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	pa12_semantic_detail::SemanticGraphConsumer& consumer,
	SemanticAnalysisStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = SemanticAnalysisStats();
	SyntaxStats syntax;
	std::ostringstream output;
	pa12_semantic_detail::SemanticAnalyzer analyzer(output, stats,
		&consumer, false);
	ConsumeSyntaxTranslationUnit(path, source, options, analyzer,
		stats ? &syntax : 0);
	PublishDriverStats(source, syntax, started, stats);
}

}
