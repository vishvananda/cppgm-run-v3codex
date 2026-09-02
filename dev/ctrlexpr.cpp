// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

using namespace std;

#include "preprocess/expressions/control_expression.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"

// Mock identifier-definition policy for the standalone expression adapter.
// return true iff first code point is odd
bool MockIsDefinedIdentifier(const string& identifier)
{
	if (identifier.empty())
		return false;
	else
		return identifier[0] % 2;
}

int main(int argc, char** argv)
{
	try
	{
		if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--stats"))
			cppgm::driver_errors::ThrowInvocation("invalid usage");
		ios_base::sync_with_stdio(false);
		cin.tie(0);
		const string source((istreambuf_iterator<char>(cin)),
			istreambuf_iterator<char>());
		const bool report_stats = argc == 2;
		cppgm::ControlExpressionStats stats;
		cppgm::EvaluateControllingExpressions(source, cout,
			MockIsDefinedIdentifier, report_stats ? &stats : 0);
		if (report_stats)
		{
			cerr << "ctrlexpr_stats"
				 << " source_bytes=" << source.size()
				 << " pp_tokens="
				 << stats.tokenization.preprocessing.emitted_tokens
				 << " post_tokens=" << stats.tokenization.emitted_tokens
				 << " lines=" << stats.logical_lines
				 << " nonempty_lines=" << stats.nonempty_lines
				 << " errors=" << stats.error_lines
				 << " nodes=" << stats.syntax_nodes
				 << " evaluation_visits=" << stats.evaluation_visits
				 << " skipped=" << stats.skipped_subexpressions
				 << " peak_line_tokens=" << stats.peak_line_tokens
				 << " peak_line_nodes=" << stats.peak_line_nodes
				 << " peak_parser_operators="
				 << stats.peak_parser_operators
				 << " peak_parser_operands="
				 << stats.peak_parser_operands
				 << " peak_evaluation_frames="
				 << stats.peak_evaluation_frames
				 << " peak_line_storage_bytes="
				 << stats.peak_line_storage_bytes
				 << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
		}
		return EXIT_SUCCESS;
	}
	catch (const CompilerError& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
	catch (const exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
