#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "post_tokenizer.h"

namespace cppgm
{

typedef bool (*DefinedIdentifierQuery)(const std::string& identifier);

struct ControlExpressionStats
{
	PostTokenizationStats tokenization;
	std::size_t logical_lines;
	std::size_t nonempty_lines;
	std::size_t error_lines;
	std::size_t syntax_nodes;
	std::size_t evaluation_visits;
	std::size_t skipped_subexpressions;
	std::size_t peak_line_tokens;
	std::size_t peak_line_nodes;
	std::size_t peak_parser_operators;
	std::size_t peak_parser_operands;
	std::size_t peak_evaluation_frames;
	std::size_t peak_line_storage_bytes;
	std::uint64_t elapsed_nanoseconds;

	ControlExpressionStats();
};

// Apply phases 1-3, PA2 token conversion, PA3 controlling-expression parsing,
// and evaluation in one forward flow.  Only compact tokens and expression
// nodes for the current logical line are retained.
void EvaluateControllingExpressions(const std::string& source,
	std::ostream& output, DefinedIdentifierQuery is_defined,
	ControlExpressionStats* stats = 0);

}
