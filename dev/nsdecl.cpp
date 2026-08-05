// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "pa7_semantic.h"

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

void ReportStats(const std::string& path,
	const cppgm::SemanticAnalysisStats& stats)
{
	std::cerr << "nsdecl_stats"
		<< " file=" << path
		<< " source_bytes=" << stats.preprocessing.source_bytes
		<< " pp_tokens="
		<< stats.preprocessing.macros.tokenization.emitted_tokens
		<< " post_tokens=" << stats.tokens
		<< " interned_ids=" << stats.identifiers
		<< " interned_bytes=" << stats.identifier_bytes
		<< " canonical_types=" << stats.canonical_types
		<< " namespaces=" << stats.namespaces
		<< " declarations=" << stats.declarations
		<< " using_edges=" << stats.using_edges
		<< " lookup_queries=" << stats.lookup_queries
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
		if (argc < 4 || std::string(argv[1]) != "-o")
			throw std::logic_error("invalid usage");
		std::ofstream output(argv[2], std::ios::out | std::ios::trunc);
		if (!output) throw std::runtime_error("unable to open output file");
		output << argc - 3 << " translation units\n";
		const cppgm::PreprocessingOptions options = BuildOptions();
		const bool report_stats = std::getenv("CPPGM_FRONTEND_STATS") != 0;

		for (int i = 3; i < argc; ++i)
		{
			const std::string path(argv[i]);
			output << "start translation unit " << path << '\n';
			const std::string source = ReadSource(path);
			cppgm::SemanticAnalysisStats stats;
			cppgm::SemanticTranslationUnit unit(path, source, options,
				report_stats ? &stats : 0);
			unit.Render(output);
			output << "end translation unit\n";
			if (report_stats) ReportStats(path, stats);
		}
		if (!output) throw std::runtime_error("unable to write output file");
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
