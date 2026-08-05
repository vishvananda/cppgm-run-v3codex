// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "pa8_semantic.h"

namespace
{

std::string ReadSource(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
	if (!input) throw std::runtime_error("unable to open source file: " + path);
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

cppgm::PreprocessingOptions BuildOptions()
{
	const std::time_t now = std::time(0);
	const std::tm* local = std::localtime(&now);
	if (!local) throw std::runtime_error("unable to determine build time");
	const char* text = std::asctime(local);
	if (!text) throw std::runtime_error("unable to format build time");
	const std::string formatted(text);
	if (formatted.size() < 24) throw std::runtime_error("invalid asctime result");

	cppgm::PreprocessingOptions options;
	options.build_date = formatted.substr(4, 7) + formatted.substr(20, 4);
	options.build_time = formatted.substr(11, 8);
	options.author = "Vishvananda Abrams";
	return options;
}

void ReportStats(const cppgm::InitializationStats& stats)
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
		if (argc < 4 || std::string(argv[1]) != "-o")
			throw std::logic_error("invalid usage");
		const bool report_stats = std::getenv("CPPGM_FRONTEND_STATS") != 0;
		cppgm::InitializationStats stats;
		cppgm::InitializationProgram program(report_stats ? &stats : 0);
		const cppgm::PreprocessingOptions options = BuildOptions();
		for (int i = 3; i < argc; ++i)
		{
			const std::string path(argv[i]);
			program.AddTranslationUnit(path, ReadSource(path), options);
		}
		std::ofstream output(argv[2], std::ios::out | std::ios::binary |
			std::ios::trunc);
		if (!output) throw std::runtime_error("unable to open output file");
		program.WriteImage(output);
		if (report_stats) ReportStats(stats);
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
