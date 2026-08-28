#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "preprocess/tokens/post_tokenizer.h"

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

// Typed PA3 adapter for clients that already own expanded preprocessing
// tokens. Feed one expression through IPostTokenStream callbacks, then call
// Finish. Identifiers that survive macro replacement evaluate as zero.
class ControllingExpressionEvaluator : public IPostTokenStream
{
public:
	explicit ControllingExpressionEvaluator(ControlExpressionStats* stats = 0);
	~ControllingExpressionEvaluator();

	bool Finish(bool* value);

	void EmitInvalid(const std::string& source);
	void EmitSimple(const std::string& source, SimpleTokenKind kind);
	void EmitIdentifier(const std::string& source);
	void EmitLiteral(const std::string& source,
		FundamentalType type, const void* data, std::size_t size);
	void EmitLiteralArray(const std::string& source, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size);
	void EmitUserDefinedCharacter(const std::string& source,
		const std::string& suffix, FundamentalType type,
		const void* data, std::size_t size);
	void EmitUserDefinedString(const std::string& source,
		const std::string& suffix, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size);
	void EmitUserDefinedInteger(const std::string& source,
		const std::string& suffix, const std::string& prefix);
	void EmitUserDefinedFloating(const std::string& source,
		const std::string& suffix, const std::string& prefix);
	void EmitEof();

private:
	ControllingExpressionEvaluator(const ControllingExpressionEvaluator&);
	ControllingExpressionEvaluator& operator=(
		const ControllingExpressionEvaluator&);

	struct Impl;
	Impl* impl_;
};

}
