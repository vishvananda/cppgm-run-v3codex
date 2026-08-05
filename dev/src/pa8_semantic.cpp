#include "pa8_semantic.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "pa8_program.h"

namespace cppgm
{
namespace
{

const std::uint16_t kIdentifierToken =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kLiteralToken = kIdentifierToken + 1;
const std::uint16_t kEofToken = kIdentifierToken + 2;

class InitializationTokenSink : public IPostTokenStream
{
public:
	InitializationTokenSink(pa8::IdentifierTable& identifiers,
		pa8::TokenBuffer& output)
		: identifiers_(identifiers), output_(output) {}

	void EmitInvalid(const std::string& source)
	{
		throw std::runtime_error("invalid phase-7 token: " + source);
	}

	void EmitSimple(const std::string&, SimpleTokenKind kind)
	{
		output_.tokens.push_back(pa8::Token(static_cast<std::uint16_t>(kind)));
	}

	void EmitIdentifier(const std::string& source)
	{
		pa8::Token token(kIdentifierToken);
		token.name = identifiers_.Intern(source);
		output_.tokens.push_back(token);
	}

	void EmitLiteral(const std::string& source, FundamentalType type,
		const void* data, std::size_t size)
	{
		if (size > 16) throw std::runtime_error("oversized scalar literal");
		if (output_.bytes.size() > std::numeric_limits<std::uint32_t>::max() -
			size) throw std::runtime_error("literal storage is too large");
		pa8::Token token(kLiteralToken);
		token.literal_type = type;
		token.byte_offset = static_cast<std::uint32_t>(output_.bytes.size());
		token.byte_size = static_cast<std::uint32_t>(size);
		token.integer_literal = pa8::IsIntegralFundamental(type) &&
			source.find('\'') == std::string::npos;
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		output_.bytes.insert(output_.bytes.end(), bytes, bytes + size);
		output_.tokens.push_back(token);
	}

	void EmitLiteralArray(const std::string&, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size)
	{
		if (elements > std::numeric_limits<std::uint32_t>::max() ||
			output_.bytes.size() > std::numeric_limits<std::uint32_t>::max() -
				size)
			throw std::runtime_error("string literal storage is too large");
		pa8::Token token(kLiteralToken);
		token.literal_type = type;
		token.byte_offset = static_cast<std::uint32_t>(output_.bytes.size());
		token.byte_size = static_cast<std::uint32_t>(size);
		token.elements = static_cast<std::uint32_t>(elements);
		token.literal_array = true;
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		output_.bytes.insert(output_.bytes.end(), bytes, bytes + size);
		output_.tokens.push_back(token);
	}

	void EmitUserDefinedCharacter(const std::string&, const std::string&,
		FundamentalType, const void*, std::size_t)
	{
		throw std::runtime_error("user-defined literal in PA8 input");
	}

	void EmitUserDefinedString(const std::string&, const std::string&,
		std::size_t, FundamentalType, const void*, std::size_t)
	{
		throw std::runtime_error("user-defined literal in PA8 input");
	}

	void EmitUserDefinedInteger(const std::string&, const std::string&,
		const std::string&)
	{
		throw std::runtime_error("user-defined literal in PA8 input");
	}

	void EmitUserDefinedFloating(const std::string&, const std::string&,
		const std::string&)
	{
		throw std::runtime_error("user-defined literal in PA8 input");
	}

	void EmitEof()
	{
		output_.tokens.push_back(pa8::Token(kEofToken));
	}

private:
	pa8::IdentifierTable& identifiers_;
	pa8::TokenBuffer& output_;
};

}

struct InitializationProgram::Impl
{
	explicit Impl(InitializationStats* stats_value)
		: stats(stats_value), model(stats_value),
		  started(std::chrono::steady_clock::now()) {}

	InitializationStats* stats;
	pa8::ProgramModel model;
	std::chrono::steady_clock::time_point started;
};

InitializationStats::InitializationStats()
	: source_bytes(0), tokens(0), token_storage_bytes(0), literal_bytes(0),
	  identifiers(0), identifier_bytes(0), canonical_types(0),
	  canonical_type_bytes(0), scopes(0), declarations(0), using_edges(0),
	  lookup_queries(0), lookup_cache_hits(0), lookup_cache_misses(0),
	  lookup_cache_invalidations(0), lookup_cache_entries(0),
	  lookup_scope_visits(0), lookup_edge_visits(0), linkage_candidates(0),
	  declarator_frames(0), declarator_cache_hits(0),
	  declarator_cache_misses(0), declarator_memo_entries(0),
	  peak_parser_scratch_bytes(0), parser_memo_storage_bytes(0),
	  temporaries(0), strings(0), semantic_storage_bytes(0),
	  peak_stage_storage_bytes(0), image_bytes(0), elapsed_nanoseconds(0)
{
}

InitializationProgram::InitializationProgram(InitializationStats* stats)
	: impl_(new Impl(stats))
{
	if (stats) *stats = InitializationStats();
}

InitializationProgram::~InitializationProgram() {}

void InitializationProgram::AddTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options)
{
	pa8::TokenBuffer tokens;
	InitializationTokenSink sink(impl_->model.identifiers, tokens);
	PreprocessFile(path, source, sink, options, 0);
	const pa8::ScopeId root = impl_->model.NewTranslationUnit();
	const std::uint32_t unit = impl_->model.CurrentUnit() - 1;
	pa8::ParseTranslationUnit(tokens, impl_->model, root, unit);
	if (impl_->stats)
	{
		impl_->stats->source_bytes += source.size();
		impl_->stats->tokens += tokens.tokens.size();
		impl_->stats->literal_bytes += tokens.bytes.size();
		const std::size_t token_storage = tokens.tokens.capacity() *
			sizeof(pa8::Token) + tokens.bytes.capacity();
		impl_->stats->token_storage_bytes = std::max(
			impl_->stats->token_storage_bytes, token_storage);
		impl_->stats->peak_stage_storage_bytes = std::max(
			impl_->stats->peak_stage_storage_bytes,
			impl_->model.StorageBytes() + source.size() + token_storage +
			impl_->stats->peak_parser_scratch_bytes +
			impl_->stats->parser_memo_storage_bytes);
	}
}

void InitializationProgram::WriteImage(std::ostream& output)
{
	impl_->model.WriteImage(output);
	impl_->model.FinishStats();
	if (impl_->stats)
	{
		impl_->stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - impl_->started).count());
	}
}

}
