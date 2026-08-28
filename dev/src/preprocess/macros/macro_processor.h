#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "preprocess/expressions/control_expression.h"

namespace cppgm
{

struct MacroProcessingStats
{
	PPTokenizationStats tokenization;
	PostTokenizationStats postprocessing;
	std::size_t logical_lines;
	std::size_t directive_lines;
	std::size_t source_tokens;
	std::size_t interned_identifiers;
	std::size_t interned_identifier_bytes;
	std::size_t macro_definitions;
	std::size_t macro_undefinitions;
	std::size_t macro_lookups;
	std::size_t macro_invocations;
	std::size_t argument_prescans;
	std::size_t expanded_tokens;
	std::size_t pasted_tokens;
	std::size_t pasted_spelling_bytes;
	std::size_t peak_line_tokens;
	std::size_t peak_line_storage_bytes;
	std::size_t peak_rescan_tokens;
	std::size_t peak_retained_replacement_tokens;
	std::size_t peak_expansion_frames;
	std::size_t peak_argument_storage_bytes;
	std::size_t paint_roots;
	std::size_t paint_singletons;
	std::size_t paint_nodes;
	std::uint64_t elapsed_nanoseconds;

	MacroProcessingStats();
};

struct PreprocessingOptions
{
	struct MacroAction
	{
		bool define;
		std::string argument;

		MacroAction(bool is_definition, const std::string& value)
			: define(is_definition), argument(value)
		{}
	};

	std::string build_date;
	std::string build_time;
	std::string author;
	std::vector<std::string> include_search_paths;
	std::vector<std::string> system_include_search_paths;
	std::vector<MacroAction> macro_actions;
	std::vector<std::string> forced_includes;
	const char* hosted_predefined_source;
	std::ostream* diagnostics;

	PreprocessingOptions();
};

struct PreprocessingStats
{
	MacroProcessingStats macros;
	ControlExpressionStats condition_evaluation;
	std::size_t source_files;
	std::size_t source_bytes;
	std::size_t peak_live_source_bytes;
	std::size_t conditional_directives;
	std::size_t controlling_expressions;
	std::size_t includes;
	std::size_t skipped_once_includes;
	std::size_t pragma_once_files;
	std::size_t pragma_operators;
	std::size_t builtin_probes;
	std::size_t include_probes;
	std::size_t peak_include_depth;
	std::size_t peak_conditional_depth;
	std::uint64_t elapsed_nanoseconds;

	PreprocessingStats();
};

// Execute PA4 preprocessing over one immutable source buffer and feed the
// expanded preprocessing-token events directly into the reusable PA2 phase.
void ProcessMacros(const std::string& source,
	IPostTokenStream& output,
	MacroProcessingStats* stats = 0);

// Execute PA5 preprocessing for one primary source. Macro, conditional, and
// pragma-once state is owned by this call and shared only with its includes.
void PreprocessFile(const std::string& path, const std::string& source,
	IPostTokenStream& output, const PreprocessingOptions& options,
	PreprocessingStats* stats = 0);

}
