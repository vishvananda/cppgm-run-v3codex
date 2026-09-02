// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "preprocess/tool_support.h"
#include "recognition/recognizer.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"

namespace
{

void ReportStats(const std::string& path, const cppgm::recognition::Stats& stats,
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
		<< " token_storage_bytes=" << stats.token_storage_bytes
		<< " identifier_storage_bytes=" << stats.identifier_storage_bytes
		<< " angle_scratch_bytes=" << stats.angle_scratch_bytes
		<< " angle_opens=" << stats.angle_openings
		<< " angle_closes=" << stats.angle_closings
		<< " memo_queries=" << stats.memo_queries
		<< " memo_hits=" << stats.memo_hits
		<< " rule_evals=" << stats.rule_evaluations
		<< " expression_evals=" << stats.expression_evaluations
		<< " memo_entries=" << stats.memo_entries
		<< " memo_storage_bytes=" << stats.memo_storage_bytes
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
		output << "recog " << input_end - 3 << '\n';
		const cppgm::PreprocessingOptions options =
			cppgm::BuildPreprocessingOptions();

		for (int i = 3; i < input_end; ++i)
		{
			const std::string path(argv[i]);
			bool accepted = false;
			try
			{
				const std::string source =
					cppgm::ReadPreprocessingSource(path);
				cppgm::recognition::Stats stats;
				accepted = cppgm::recognition::RecognizeTranslationUnit(path, source,
					options, report_stats ? &stats : 0);
				if (report_stats) ReportStats(path, stats, accepted);
			}
			catch (const CompilerError& error)
			{
				std::cerr << path << ": " << error.what() << '\n';
			}
			catch (const std::exception& error)
			{
				std::cerr << path << ": " << error.what() << '\n';
			}
			output << path << (accepted ? " OK\n" : " BAD\n");
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
