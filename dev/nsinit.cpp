// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "namespace_initialization/driver.h"
#include "preprocess/tool_support.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"

namespace
{

void ReportStats(const cppgm::namespace_initialization::Stats& stats)
{
	std::cerr << "nsinit_stats"
		<< " source_bytes=" << stats.source_bytes
		<< " tokens=" << stats.tokens
		<< " token_storage_bytes=" << stats.token_storage_bytes
		<< " literal_bytes=" << stats.literal_bytes
		<< " interned_ids=" << stats.identifiers
		<< " identifier_bytes=" << stats.identifier_bytes
		<< " canonical_types=" << stats.canonical_types
		<< " canonical_type_bytes=" << stats.canonical_type_bytes
		<< " scopes=" << stats.scopes
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
		<< " linkage_candidates=" << stats.linkage_candidates
		<< " declarator_frames=" << stats.declarator_frames
		<< " declarator_cache_hits=" << stats.declarator_cache_hits
		<< " declarator_cache_misses=" << stats.declarator_cache_misses
		<< " declarator_memo_entries=" << stats.declarator_memo_entries
		<< " peak_parser_scratch_bytes=" << stats.peak_parser_scratch_bytes
		<< " parser_memo_storage_bytes=" << stats.parser_memo_storage_bytes
		<< " temporaries=" << stats.temporaries
		<< " strings=" << stats.strings
		<< " semantic_storage_bytes=" << stats.semantic_storage_bytes
		<< " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
		<< " image_bytes=" << stats.image_bytes
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
		cppgm::namespace_initialization::Stats stats;
		cppgm::namespace_initialization::Program program(report_stats ? &stats : 0);
		const cppgm::PreprocessingOptions options =
			cppgm::BuildPreprocessingOptions();
		for (int i = 3; i < input_end; ++i)
		{
			const std::string path(argv[i]);
			program.AddTranslationUnit(path,
				cppgm::ReadPreprocessingSource(path), options);
		}
		std::ofstream output(argv[2], std::ios::out | std::ios::binary |
			std::ios::trunc);
		if (!output) cppgm::driver_errors::ThrowInputOutput("unable to open output file");
		program.WriteImage(output);
		if (report_stats) ReportStats(stats);
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
