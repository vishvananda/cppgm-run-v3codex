// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "preprocess/macros/macro_processor.h"
#include "preprocess/tool_support.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"

namespace
{

char HexDigit(unsigned int value)
{
	return value < 10 ? static_cast<char>('0' + value) :
		static_cast<char>('A' + value - 10);
}

std::string HexDump(const void* data, std::size_t size)
{
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	std::string result(size * 2, '0');
	for (std::size_t i = 0; i < size; ++i)
	{
		result[i * 2] = HexDigit(bytes[i] >> 4);
		result[i * 2 + 1] = HexDigit(bytes[i] & 0xF);
	}
	return result;
}

class PreprocessorOutput : public cppgm::IPostTokenStream
{
public:
	explicit PreprocessorOutput(std::ostream& output) : output_(output) {}

	void EmitInvalid(const std::string& source)
	{
		(void)source;
		cppgm::driver_errors::ThrowLexicalSource("invalid phase-7 token");
	}

	void EmitSimple(const std::string& source, cppgm::SimpleTokenKind kind)
	{
		output_ << "simple " << source << ' ' <<
			cppgm::SimpleTokenKindName(kind) << '\n';
	}

	void EmitIdentifier(const std::string& source)
	{
		output_ << "identifier " << source << '\n';
	}

	void EmitLiteral(const std::string& source,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "literal " << source << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << HexDump(data, size) << '\n';
	}

	void EmitLiteralArray(const std::string& source, std::size_t elements,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "literal " << source << " array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << HexDump(data, size) << '\n';
	}

	void EmitUserDefinedCharacter(const std::string& source,
		const std::string& suffix, cppgm::FundamentalType type,
		const void* data, std::size_t size)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" character " << cppgm::FundamentalTypeName(type) << ' ' <<
			HexDump(data, size) << '\n';
	}

	void EmitUserDefinedString(const std::string& source,
		const std::string& suffix, std::size_t elements,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" string array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << HexDump(data, size) << '\n';
	}

	void EmitUserDefinedInteger(const std::string& source,
		const std::string& suffix, const std::string& prefix)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" integer " << prefix << '\n';
	}

	void EmitUserDefinedFloating(const std::string& source,
		const std::string& suffix, const std::string& prefix)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" floating " << prefix << '\n';
	}

	void EmitEof()
	{
		output_ << "eof\n";
	}

private:
	std::ostream& output_;
};

void ReportStats(const std::string& path,
	const cppgm::PreprocessingStats& stats)
{
	std::cerr << "preproc_stats"
		<< " file=" << path
		<< " source_files=" << stats.source_files
		<< " source_bytes=" << stats.source_bytes
		<< " peak_source_bytes=" << stats.peak_live_source_bytes
		<< " pp_tokens=" << stats.macros.tokenization.emitted_tokens
		<< " source_tokens=" << stats.macros.source_tokens
		<< " post_tokens=" << stats.macros.postprocessing.emitted_tokens
		<< " directives=" << stats.macros.directive_lines
		<< " conditions=" << stats.controlling_expressions
		<< " condition_nodes=" << stats.condition_evaluation.syntax_nodes
		<< " condition_visits=" << stats.condition_evaluation.evaluation_visits
		<< " condition_skips=" << stats.condition_evaluation.skipped_subexpressions
		<< " condition_peak_bytes=" <<
			stats.condition_evaluation.peak_line_storage_bytes
		<< " condition_ns=" << stats.condition_evaluation.elapsed_nanoseconds
		<< " includes=" << stats.includes
		<< " once_skips=" << stats.skipped_once_includes
		<< " once_files=" << stats.pragma_once_files
		<< " lookups=" << stats.macros.macro_lookups
		<< " invocations=" << stats.macros.macro_invocations
		<< " argument_prescans=" << stats.macros.argument_prescans
		<< " expanded_tokens=" << stats.macros.expanded_tokens
		<< " pasted_tokens=" << stats.macros.pasted_tokens
		<< " interned_ids=" << stats.macros.interned_identifiers
		<< " interned_bytes=" << stats.macros.interned_identifier_bytes
		<< " retained_replacements=" <<
			stats.macros.peak_retained_replacement_tokens
		<< " paint_roots=" << stats.macros.paint_roots
		<< " paint_singletons=" << stats.macros.paint_singletons
		<< " paint_nodes=" << stats.macros.paint_nodes
		<< " peak_line_tokens=" << stats.macros.peak_line_tokens
		<< " peak_line_bytes=" << stats.macros.peak_line_storage_bytes
		<< " peak_rescan_tokens=" << stats.macros.peak_rescan_tokens
		<< " peak_argument_bytes=" << stats.macros.peak_argument_storage_bytes
		<< " peak_posttoken_bytes=" <<
			stats.macros.postprocessing.peak_phase_storage_bytes
		<< " peak_frames=" << stats.macros.peak_expansion_frames
		<< " peak_include_depth=" << stats.peak_include_depth
		<< " peak_conditional_depth=" << stats.peak_conditional_depth
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

		const cppgm::PreprocessingOptions options =
			cppgm::BuildPreprocessingOptions();
		std::ofstream output(argv[2], std::ios::out | std::ios::trunc);
		if (!output)
			cppgm::driver_errors::ThrowInputOutput("unable to open output file");
		output << "preproc " << input_end - 3 << '\n';
		PreprocessorOutput tokens(output);

		for (int i = 3; i < input_end; ++i)
		{
			const std::string path(argv[i]);
			const std::string source = cppgm::ReadPreprocessingSource(path);
			output << "sof " << path << '\n';
			cppgm::PreprocessingStats stats;
			cppgm::PreprocessFile(path, source, tokens, options,
				report_stats ? &stats : 0);
			if (report_stats)
				ReportStats(path, stats);
		}
		if (!output)
			cppgm::driver_errors::ThrowInputOutput("unable to write output file");
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
