// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "namespace_semantics/analysis.h"
#include "preprocess/tool_support.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"

namespace
{

void ReportStats(const std::string& path,
	const cppgm::namespace_semantics::Stats& stats)
{
	std::cerr << "nsdecl_stats"
		<< " file=" << path
		<< " source_bytes=" << stats.preprocessing.source_bytes
		<< " pp_tokens="
		<< stats.preprocessing.macros.tokenization.emitted_tokens
		<< " post_tokens=" << stats.tokens
		<< " declarator_frames=" << stats.declarator_frames
		<< " declarator_cache_hits=" << stats.declarator_cache_hits
		<< " declarator_cache_misses=" << stats.declarator_cache_misses
		<< " declarator_memo_entries=" << stats.declarator_memo_entries
		<< " peak_parser_scratch_bytes=" << stats.peak_parser_scratch_bytes
		<< " parser_memo_storage_bytes="
		<< stats.parser_memo_storage_bytes
		<< " interned_ids=" << stats.identifiers
		<< " interned_bytes=" << stats.identifier_bytes
		<< " canonical_types=" << stats.canonical_types
		<< " namespaces=" << stats.namespaces
		<< " declarations=" << stats.declarations
		<< " using_edges=" << stats.using_edges
		<< " lookup_queries=" << stats.lookup_queries
		<< " lookup_cache_hits=" << stats.lookup_cache_hits
		<< " lookup_cache_misses=" << stats.lookup_cache_misses
		<< " lookup_cache_invalidations="
		<< stats.lookup_cache_invalidations
		<< " lookup_cache_entries=" << stats.lookup_cache_entries
		<< " lookup_scope_visits=" << stats.lookup_scope_visits
		<< " lookup_edge_visits=" << stats.lookup_edge_visits
		<< " token_storage_bytes=" << stats.token_storage_bytes
		<< " semantic_storage_bytes=" << stats.semantic_storage_bytes
		<< " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
		<< " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
}

}

int main(int argc, char** argv)
{
	try
	{
		std::ios_base::sync_with_stdio(false);
		const bool report_stats = argc > 1 &&
			std::string(argv[argc - 1]) == "--stats";
		const int input_end = report_stats ? argc - 1 : argc;
		if (input_end < 4 || std::string(argv[1]) != "-o")
			cppgm::driver_errors::ThrowInvocation("invalid usage");
		std::ofstream output(argv[2], std::ios::out | std::ios::trunc);
		if (!output) cppgm::driver_errors::ThrowInputOutput("unable to open output file");
		output << input_end - 3 << " translation units\n";
		const cppgm::PreprocessingOptions options =
			cppgm::BuildPreprocessingOptions();

		for (int i = 3; i < input_end; ++i)
		{
			const std::string path(argv[i]);
			output << "start translation unit " << path << '\n';
			const std::string source = cppgm::ReadPreprocessingSource(path);
			cppgm::namespace_semantics::Stats stats;
			cppgm::namespace_semantics::TranslationUnit unit(path, source, options,
				report_stats ? &stats : 0);
			unit.Render(output);
			output << "end translation unit\n";
			if (report_stats) ReportStats(path, stats);
		}
		if (!output) cppgm::driver_errors::ThrowInputOutput("unable to write output file");
		return EXIT_SUCCESS;
	}
	catch (const CompilerError& error)
	{
		std::cerr << "ERROR: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
