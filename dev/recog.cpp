// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "pa6_recognizer.h"

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

void ReportStats(const std::string& path, const cppgm::RecognitionStats& stats,
	bool accepted)
{
	std::cerr << "recog_stats"
		<< " file=" << path
		<< " accepted=" << (accepted ? 1 : 0)
		<< " source_bytes=" << stats.preprocessing.source_bytes
		<< " pp_tokens=" << stats.preprocessing.macros.tokenization.emitted_tokens
		<< " post_tokens=" << stats.tokens
		<< " interned_ids=" << stats.interned_identifiers
		<< " interned_bytes=" << stats.interned_identifier_bytes
		<< " angle_opens=" << stats.angle_openings
		<< " angle_closes=" << stats.angle_closings
		<< " memo_queries=" << stats.memo_queries
		<< " memo_hits=" << stats.memo_hits
		<< " rule_evals=" << stats.rule_evaluations
		<< " expression_evals=" << stats.expression_evaluations
		<< " memo_entries=" << stats.memo_entries
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
		output << "recog " << argc - 3 << '\n';
		const cppgm::PreprocessingOptions options = BuildOptions();
		const bool report_stats = std::getenv("CPPGM_FRONTEND_STATS") != 0;

		for (int i = 3; i < argc; ++i)
		{
			const std::string path(argv[i]);
			bool accepted = false;
			try
			{
				const std::string source = ReadSource(path);
				cppgm::RecognitionStats stats;
				accepted = cppgm::RecognizeTranslationUnit(path, source,
					options, report_stats ? &stats : 0);
				if (report_stats) ReportStats(path, stats, accepted);
			}
			catch (const std::exception& error)
			{
				std::cerr << path << ": " << error.what() << '\n';
			}
			output << path << (accepted ? " OK\n" : " BAD\n");
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
