#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace cppgm
{

enum FundamentalType
{
	FT_SIGNED_CHAR,
	FT_SHORT_INT,
	FT_INT,
	FT_LONG_INT,
	FT_LONG_LONG_INT,
	FT_UNSIGNED_CHAR,
	FT_UNSIGNED_SHORT_INT,
	FT_UNSIGNED_INT,
	FT_UNSIGNED_LONG_INT,
	FT_UNSIGNED_LONG_LONG_INT,
	FT_WCHAR_T,
	FT_CHAR,
	FT_CHAR16_T,
	FT_CHAR32_T,
	FT_BOOL,
	FT_FLOAT,
	FT_DOUBLE,
	FT_LONG_DOUBLE,
	FT_VOID,
	FT_NULLPTR_T,
	FT_FLOAT16,
	FT_FLOAT32,
	FT_FLOAT32X,
	FT_FLOAT64,
	FT_FLOAT64X,
	FT_FLOAT128
};

enum SimpleTokenKind
{
	KW_ALIGNAS,
	KW_ALIGNOF,
	KW_ASM,
	KW_AUTO,
	KW_BOOL,
	KW_BREAK,
	KW_CASE,
	KW_CATCH,
	KW_CHAR,
	KW_CHAR16_T,
	KW_CHAR32_T,
	KW_CLASS,
	KW_CONST,
	KW_CONSTEXPR,
	KW_CONST_CAST,
	KW_CONTINUE,
	KW_DECLTYPE,
	KW_DEFAULT,
	KW_DELETE,
	KW_DO,
	KW_DOUBLE,
	KW_DYNAMIC_CAST,
	KW_ELSE,
	KW_ENUM,
	KW_EXPLICIT,
	KW_EXPORT,
	KW_EXTERN,
	KW_FALSE,
	KW_FLOAT,
	KW_FOR,
	KW_FRIEND,
	KW_GOTO,
	KW_IF,
	KW_INLINE,
	KW_INT,
	KW_LONG,
	KW_MUTABLE,
	KW_NAMESPACE,
	KW_NEW,
	KW_NOEXCEPT,
	KW_NULLPTR,
	KW_OPERATOR,
	KW_PRIVATE,
	KW_PROTECTED,
	KW_PUBLIC,
	KW_REGISTER,
	KW_REINTERPET_CAST,
	KW_RETURN,
	KW_SHORT,
	KW_SIGNED,
	KW_SIZEOF,
	KW_STATIC,
	KW_STATIC_ASSERT,
	KW_STATIC_CAST,
	KW_STRUCT,
	KW_SWITCH,
	KW_TEMPLATE,
	KW_THIS,
	KW_THREAD_LOCAL,
	KW_THROW,
	KW_TRUE,
	KW_TRY,
	KW_TYPEDEF,
	KW_TYPEID,
	KW_TYPENAME,
	KW_UNION,
	KW_UNSIGNED,
	KW_USING,
	KW_VIRTUAL,
	KW_VOID,
	KW_VOLATILE,
	KW_WCHAR_T,
	KW_WHILE,
	OP_LBRACE,
	OP_RBRACE,
	OP_LSQUARE,
	OP_RSQUARE,
	OP_LPAREN,
	OP_RPAREN,
	OP_BOR,
	OP_XOR,
	OP_COMPL,
	OP_AMP,
	OP_LNOT,
	OP_SEMICOLON,
	OP_COLON,
	OP_DOTS,
	OP_QMARK,
	OP_COLON2,
	OP_DOT,
	OP_DOTSTAR,
	OP_PLUS,
	OP_MINUS,
	OP_STAR,
	OP_DIV,
	OP_MOD,
	OP_ASS,
	OP_LT,
	OP_GT,
	OP_PLUSASS,
	OP_MINUSASS,
	OP_STARASS,
	OP_DIVASS,
	OP_MODASS,
	OP_XORASS,
	OP_BANDASS,
	OP_BORASS,
	OP_LSHIFT,
	OP_RSHIFT,
	OP_RSHIFTASS,
	OP_LSHIFTASS,
	OP_EQ,
	OP_NE,
	OP_LE,
	OP_GE,
	OP_LAND,
	OP_LOR,
	OP_INC,
	OP_DEC,
	OP_COMMA,
	OP_ARROWSTAR,
	OP_ARROW
};

const char* FundamentalTypeName(FundamentalType type);
const char* SimpleTokenKindName(SimpleTokenKind kind);
// Classifies one fixed operator/keyword spelling into its simple-token kind.
bool ClassifySimpleSpelling(const std::string& spelling,
	SimpleTokenKind* kind);

struct IPostTokenStream
{
	// Sources, suffixes, prefixes, and byte ranges are borrowed callback data.
	// A consumer that needs them after return must retain its own typed facts.
	virtual void SetSourceLocation(const std::string& file,
		std::size_t line, std::size_t column)
		{ (void)file; (void)line; (void)column; }
	virtual void EmitInvalid(const std::string& source) = 0;
	virtual void EmitSimple(const std::string& source,
		SimpleTokenKind kind) = 0;
	virtual void EmitIdentifier(const std::string& source) = 0;
	// Integrated-path variants carrying the producer's compact spelling
	// identity; the defaults preserve the plain textual adapter contract.
	virtual void EmitSimpleId(std::uint32_t producer_spelling,
		const std::string& source, SimpleTokenKind kind)
		{ (void)producer_spelling; EmitSimple(source, kind); }
	virtual void EmitIdentifierId(std::uint32_t producer_spelling,
		const std::string& source)
		{ (void)producer_spelling; EmitIdentifier(source); }
	virtual void EmitLiteral(const std::string& source,
		FundamentalType type, const void* data, std::size_t size) = 0;
	virtual void EmitLiteralArray(const std::string& source,
		std::size_t elements, FundamentalType type,
		const void* data, std::size_t size) = 0;
	virtual void EmitUserDefinedCharacter(const std::string& source,
		const std::string& suffix, FundamentalType type,
		const void* data, std::size_t size) = 0;
	virtual void EmitUserDefinedString(const std::string& source,
		const std::string& suffix, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size) = 0;
	virtual void EmitUserDefinedInteger(const std::string& source,
		const std::string& suffix, const std::string& prefix) = 0;
	virtual void EmitUserDefinedFloating(const std::string& source,
		const std::string& suffix, const std::string& prefix) = 0;
	// Active preprocessing pragmas that alter object layout cross the phase
	// boundary as typed events.  Earlier token-only consumers intentionally
	// ignore them; the integrated syntax/semantic path retains them.
	virtual void EmitPragmaPackPush(std::size_t alignment)
		{ (void)alignment; }
	virtual void EmitPragmaPackPop() {}
	virtual void EmitEof() = 0;

	virtual ~IPostTokenStream() {}
};

struct PostTokenizationStats
{
	PPTokenizationStats preprocessing;
	std::size_t preprocessing_tokens;
	std::size_t emitted_tokens;
	std::size_t decoded_literal_units;
	std::size_t string_sequences;
	std::size_t max_pending_string_tokens;
	// Source-spelling payload retained for the largest maximal sequence.
	std::size_t peak_pending_string_bytes;
	// Peak capacities owned by PA2 while a string sequence is analyzed.
	std::size_t peak_literal_bytes;
	std::size_t peak_phase_storage_bytes;
	std::uint64_t elapsed_nanoseconds;

	PostTokenizationStats();
};

// Reusable preprocessing-token -> post-token session.  The ordinary PA2 path
// feeds one complete source file to this stream.  Consumers with a stronger
// grammatical boundary, such as PA3 logical lines, may flush the one pending
// string-literal sequence at that boundary without serializing and retokenizing
// source text.
class PostTokenizationSession : public IPPTokenStream
{
public:
	PostTokenizationSession(IPostTokenStream& output,
		PostTokenizationStats* stats = 0);
	~PostTokenizationSession();

	void FlushPendingTokens();
	void SetSourceLocation(const std::string& file,
		std::size_t line, std::size_t column);

	void emit_whitespace_sequence();
	void emit_new_line();
	void emit_header_name(const std::string& data);
	void emit_identifier(const std::string& data);
	void emit_identifier_id(const std::string& data,
		std::uint32_t producer_spelling);
	void emit_pp_number(const std::string& data);
	void emit_character_literal(const std::string& data);
	void emit_user_defined_character_literal(const std::string& data);
	void emit_string_literal(const std::string& data);
	void emit_user_defined_string_literal(const std::string& data);
	void emit_preprocessing_op_or_punc(const std::string& data);
	void emit_preprocessing_op_or_punc_id(const std::string& data,
		std::uint32_t producer_spelling);
	void emit_non_whitespace_char(const std::string& data);
	void emit_eof();
	void EmitPragmaPackPush(std::size_t alignment);
	void EmitPragmaPackPop();

private:
	PostTokenizationSession(const PostTokenizationSession&);
	PostTokenizationSession& operator=(const PostTokenizationSession&);

	struct Impl;
	Impl* impl_;
};

// Decode one non-prefixed, non-raw, non-user-defined string-literal with the
// same escape and UTF-8 rules used by PA2. The returned value excludes the
// terminating null code unit. This is the typed phase boundary used by
// directives that consume a string value instead of emitting a phase-7 token.
bool DecodeOrdinaryStringLiteral(const std::string& source,
	std::string* value);

// Decode one narrow ordinary or UTF-8 string literal, including raw forms,
// while rejecting user-defined suffixes. The value excludes the null unit.
bool DecodeNarrowStringLiteral(const std::string& source,
	std::string* value);

// Decode one or more adjacent narrow string literals. The source uses spaces
// between phase-7 literal spellings and the value excludes the final null.
bool DecodeNarrowStringLiteralSequence(const std::string& source,
	std::string* value);

// Decode one ordinary string-literal into typed code units, including its
// terminating null unit.  This retains the phase-6 encoding decision for
// semantic consumers that need array element constants.
std::size_t StringLiteralDecodeCalls();
bool DecodeStringLiteralCodeUnits(const std::string& source,
	FundamentalType* type, std::vector<std::uint32_t>* units);

// Recognize the conditionally-supported ordinary multi-character form for
// compiler stages that choose to accept it.  The PA2 post-token stream itself
// continues to diagnose this form as invalid.
bool DecodeOrdinaryMulticharacterLiteral(const std::string& source,
	std::uint32_t* value);

// Run phases 1-3 through the shared PA1 tokenizer, apply the PA2 phase-6 and
// token recognition rules, and emit typed events without retaining a token
// vector. Only one joined spelling plus compact ranges for the current maximal
// adjacent string-literal sequence is delayed.
void TokenizePostTokens(const std::string& source,
	IPostTokenStream& output,
	PostTokenizationStats* stats = 0);

}
