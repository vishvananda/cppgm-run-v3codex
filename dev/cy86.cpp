// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "cy86_program.h"

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
	if (formatted.size() < 24)
		throw std::runtime_error("invalid asctime result");

	cppgm::PreprocessingOptions options;
	options.build_date = formatted.substr(4, 7) + formatted.substr(20, 4);
	options.build_time = formatted.substr(11, 8);
	options.author = "Vishvananda Abrams";
	return options;
}

void ReportStats(const cppgm::Cy86Stats& stats)
{
	std::cerr << "cy86_stats"
		<< " source_bytes=" << stats.source_bytes
		<< " tokens=" << stats.tokens
		<< " peak_statement_tokens=" << stats.peak_statement_tokens
		<< " identifiers=" << stats.identifiers
		<< " identifier_bytes=" << stats.identifier_bytes
		<< " literal_bytes=" << stats.literal_bytes
		<< " opcode_identities=" << stats.opcode_identities
		<< " statements=" << stats.statements
		<< " operands=" << stats.operands
		<< " labels=" << stats.labels
		<< " fixups=" << stats.fixups
		<< " instruction_bytes=" << stats.instruction_bytes
		<< " image_bytes=" << stats.image_bytes
		<< " peak_live_source_bytes=" << stats.peak_live_source_bytes
		<< " frontend_ns=" << stats.frontend_nanoseconds
		<< " lowering_ns=" << stats.lowering_nanoseconds
		<< " writing_ns=" << stats.writing_nanoseconds
		<< " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
}

}

int main(int argc, char** argv)
{
	try
	{
		std::ios_base::sync_with_stdio(false);
		std::string output_path;
		std::vector<std::string> source_paths;
		bool report_stats = false;
		for (int i = 1; i < argc; ++i)
		{
			const std::string argument(argv[i]);
			if (argument == "-o")
			{
				if (++i >= argc || !output_path.empty())
					throw std::logic_error("invalid usage");
				output_path = argv[i];
			}
			else if (argument == "--target")
			{
				if (++i >= argc) throw std::logic_error("invalid usage");
				// PA9's optional target selector does not change the x86-64 ELF path.
			}
			else if (argument == "--stats") report_stats = true;
			else
			{
				source_paths.push_back(argument);
			}
		}
		if (output_path.empty() || source_paths.empty())
			throw std::logic_error("invalid usage");

		cppgm::Cy86Stats stats;
		cppgm::Cy86Program program(report_stats ? &stats : 0);
		const cppgm::PreprocessingOptions options = BuildOptions();
		for (std::size_t i = 0; i < source_paths.size(); ++i)
		{
			const std::string source = ReadSource(source_paths[i]);
			program.AddTranslationUnit(source_paths[i], source, options);
		}
		program.WriteExecutable(output_path);
		if (report_stats) ReportStats(stats);
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
