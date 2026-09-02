#include "preprocess/tokens/pp_tokenizer.h"
#include "support/exception_types.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

#include "preprocess/tokens/IPPTokenStream.h"

#if __has_attribute(cppgm_stable_prefix)
#define CPPGM_STABLE_PREFIX_QUERY __attribute__((cppgm_stable_prefix))
#else
#define CPPGM_STABLE_PREFIX_QUERY
#endif

#ifdef __CPPGM__
#define CPPGM_HOST_CURSOR_NOINLINE
#else
#define CPPGM_HOST_CURSOR_NOINLINE __attribute__((noinline))
#endif

namespace cppgm
{
namespace
{

const int kEndOfFile = -1;

__attribute__((cold, noinline, noreturn))
void ThrowLexicalSourceError(const char* message)
{
	throw SourceError(message, CompilerErrorDomain::LEXICAL);
}

__attribute__((cold, noinline, noreturn))
void ThrowLexicalInternalError(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::LEXICAL);
}

template <typename T, std::size_t Capacity>
class FixedQueue
{
public:
	FixedQueue() : begin_(0), size_(0) {}

	bool empty() const { return size_ == 0; }
	std::size_t size() const { return size_; }

	const T& front() const { return (*this)[0]; }
	const T& back() const { return (*this)[size_ - 1]; }

	const T& operator[](std::size_t offset) const
	{
		if (offset >= size_)
			throw InternalCompilerError("fixed lookahead queue underflow",
				CompilerErrorDomain::LEXICAL);
		return unchecked(offset);
	}

	// For callers whose control flow already guarantees offset < size().
	const T& unchecked(std::size_t offset) const
	{
		return data_[(begin_ + offset) % Capacity];
	}

	void push_back(const T& value)
	{
		if (size_ == Capacity)
			throw InternalCompilerError("fixed lookahead queue overflow",
				CompilerErrorDomain::LEXICAL);
		data_[(begin_ + size_) % Capacity] = value;
		++size_;
	}

	void pop_front()
	{
		if (empty())
			throw InternalCompilerError("fixed lookahead queue underflow",
				CompilerErrorDomain::LEXICAL);
		begin_ = (begin_ + 1) % Capacity;
		--size_;
	}

private:
	T data_[Capacity];
	std::size_t begin_;
	std::size_t size_;
};

struct CodePointRange
{
	int first;
	int last;
};

struct LocatedCodePoint
{
	int value;
	std::size_t line, column;

	LocatedCodePoint(int code_point = kEndOfFile,
		std::size_t source_line = 1, std::size_t source_column = 1)
		: value(code_point), line(source_line), column(source_column)
	{}
};

const CodePointRange kAnnexE1Ranges[] = {
	{0xA8, 0xA8}, {0xAA, 0xAA}, {0xAD, 0xAD}, {0xAF, 0xAF},
	{0xB2, 0xB5}, {0xB7, 0xBA}, {0xBC, 0xBE}, {0xC0, 0xD6},
	{0xD8, 0xF6}, {0xF8, 0xFF}, {0x100, 0x167F},
	{0x1681, 0x180D}, {0x180F, 0x1FFF}, {0x200B, 0x200D},
	{0x202A, 0x202E}, {0x203F, 0x2040}, {0x2054, 0x2054},
	{0x2060, 0x206F}, {0x2070, 0x218F}, {0x2460, 0x24FF},
	{0x2776, 0x2793}, {0x2C00, 0x2DFF}, {0x2E80, 0x2FFF},
	{0x3004, 0x3007}, {0x3021, 0x302F}, {0x3031, 0x303F},
	{0x3040, 0xD7FF}, {0xF900, 0xFD3D}, {0xFD40, 0xFDCF},
	{0xFDF0, 0xFE44}, {0xFE47, 0xFFFD},
	{0x10000, 0x1FFFD}, {0x20000, 0x2FFFD},
	{0x30000, 0x3FFFD}, {0x40000, 0x4FFFD},
	{0x50000, 0x5FFFD}, {0x60000, 0x6FFFD},
	{0x70000, 0x7FFFD}, {0x80000, 0x8FFFD},
	{0x90000, 0x9FFFD}, {0xA0000, 0xAFFFD},
	{0xB0000, 0xBFFFD}, {0xC0000, 0xCFFFD},
	{0xD0000, 0xDFFFD}, {0xE0000, 0xEFFFD}
};

const CodePointRange kAnnexE2Ranges[] = {
	{0x300, 0x36F}, {0x1DC0, 0x1DFF},
	{0x20D0, 0x20FF}, {0xFE20, 0xFE2F}
};

bool IsInRanges(int code_point, const CodePointRange* ranges,
	std::size_t count)
{
	std::size_t first = 0;
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		if (code_point < ranges[middle].first)
			count = middle;
		else if (code_point > ranges[middle].last)
			first = middle + 1;
		else
			return true;
	}
	return false;
}

bool IsAsciiDigit(int c)
{
	return c >= '0' && c <= '9';
}

bool IsHexDigit(int c)
{
	return IsAsciiDigit(c) || (c >= 'a' && c <= 'f') ||
		(c >= 'A' && c <= 'F');
}

int HexValue(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	ThrowLexicalInternalError("hex value requested for non-hex character");
}

bool IsIdentifierNondigit(int c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
		return true;
	return IsInRanges(c, kAnnexE1Ranges,
		sizeof(kAnnexE1Ranges) / sizeof(kAnnexE1Ranges[0]));
}

bool IsIdentifierInitial(int c)
{
	return IsIdentifierNondigit(c) &&
		!IsInRanges(c, kAnnexE2Ranges,
			sizeof(kAnnexE2Ranges) / sizeof(kAnnexE2Ranges[0]));
}

bool IsIdentifierBody(int c)
{
	return IsIdentifierNondigit(c) || IsAsciiDigit(c);
}

bool IsHorizontalWhitespace(int c)
{
	return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r';
}

bool IsSimpleEscape(int c)
{
	const char* escapes = "'\"?\\abfnrtv";
	return c >= 0 && c <= 0x7F && std::strchr(escapes, c) != 0;
}

void AppendUTF8(int code_point, std::string* output)
{
	if (code_point < 0 || code_point > 0x10FFFF ||
		(code_point >= 0xD800 && code_point <= 0xDFFF))
		ThrowLexicalInternalError("invalid Unicode code point");
	if (code_point <= 0x7F)
		output->push_back(static_cast<char>(code_point));
	else if (code_point <= 0x7FF)
	{
		output->push_back(static_cast<char>(0xC0 | (code_point >> 6)));
		output->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else if (code_point <= 0xFFFF)
	{
		output->push_back(static_cast<char>(0xE0 | (code_point >> 12)));
		output->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
		output->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else
	{
		output->push_back(static_cast<char>(0xF0 | (code_point >> 18)));
		output->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
		output->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
		output->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
}

class PhysicalCursor
{
public:
	PhysicalCursor(const std::string& source, PPTokenizationStats* stats)
		: source_(source), position_(0), final_newline_pending_(false),
		  line_(1), column_(1), last_line_(1), last_column_(1), stats_(stats)
	{
		if (source_.size() >= 3 &&
			static_cast<unsigned char>(source_[0]) == 0xEF &&
			static_cast<unsigned char>(source_[1]) == 0xBB &&
			static_cast<unsigned char>(source_[2]) == 0xBF)
			position_ = 3;
		final_newline_pending_ = position_ < source_.size() &&
			source_[source_.size() - 1] != '\n';
	}

	CPPGM_HOST_CURSOR_NOINLINE int Next()
	{
		if (position_ < source_.size())
		{
			const int code_point = DecodeOne();
			last_line_ = line_;
			last_column_ = column_;
			if (code_point == '\n')
			{
				++line_;
				column_ = 1;
			}
			else ++column_;
			if (stats_)
				++stats_->decoded_code_points;
			return code_point;
		}
		if (final_newline_pending_)
		{
			final_newline_pending_ = false;
			last_line_ = line_;
			last_column_ = column_;
			++line_;
			column_ = 1;
			return '\n';
		}
		last_line_ = line_;
		last_column_ = column_;
		return kEndOfFile;
	}

	std::size_t LastLine() const { return last_line_; }
	std::size_t LastColumn() const { return last_column_; }

private:
	static int DecodeWindows1252Byte(int byte)
	{
		static const int replacements[32] = {
			0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
			0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
			0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
			0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
		};
		return byte >= 0x80 && byte <= 0x9F ?
			replacements[byte - 0x80] : byte;
	}

	int Continuation(std::size_t offset) const
	{
		if (position_ + offset >= source_.size())
			ThrowLexicalSourceError("truncated UTF-8 character");
		const int byte = static_cast<unsigned char>(source_[position_ + offset]);
		if ((byte & 0xC0) != 0x80)
			ThrowLexicalSourceError("invalid UTF-8 continuation byte");
		return byte & 0x3F;
	}

	int DecodeOne()
	{
		const int first = static_cast<unsigned char>(source_[position_]);
		if (first <= 0x7F)
		{
			++position_;
			return first;
		}
		if (first >= 0xC2 && first <= 0xDF)
		{
			const int value = ((first & 0x1F) << 6) | Continuation(1);
			position_ += 2;
			return value;
		}
		if (first >= 0xE0 && first <= 0xEF)
		{
			const int second = Continuation(1);
			const int third = Continuation(2);
			if ((first == 0xE0 && second < 0x20) ||
				(first == 0xED && second >= 0x20))
				ThrowLexicalSourceError("invalid UTF-8 scalar value");
			position_ += 3;
			return ((first & 0x0F) << 12) | (second << 6) | third;
		}
		if (first >= 0xF0 && first <= 0xF4)
		{
			const int second = Continuation(1);
			const int third = Continuation(2);
			const int fourth = Continuation(3);
			if ((first == 0xF0 && second < 0x10) ||
				(first == 0xF4 && second >= 0x10))
				ThrowLexicalSourceError("invalid UTF-8 scalar value");
			position_ += 4;
			return ((first & 0x07) << 18) | (second << 12) |
				(third << 6) | fourth;
		}
		if (first >= 0x80 && first <= 0xBF)
		{
			// Hosted vendor sources occasionally retain a Windows-1252 byte in
			// comments. Preserve a deterministic character mapping without
			// changing valid UTF-8 decoding.
			++position_;
			return DecodeWindows1252Byte(first);
		}
		ThrowLexicalSourceError("invalid UTF-8 leading byte");
	}

	const std::string& source_;
	std::size_t position_;
	bool final_newline_pending_;
	std::size_t line_, column_, last_line_, last_column_;
	PPTokenizationStats* stats_;
};

int TrigraphReplacement(int c)
{
	switch (c)
	{
	case '=': return '#';
	case '/': return '\\';
	case '\'': return '^';
	case '(': return '[';
	case ')': return ']';
	case '!': return '|';
	case '<': return '{';
	case '>': return '}';
	case '-': return '~';
	default: return kEndOfFile;
	}
}

class TranslationCursor
{
public:
	TranslationCursor(const std::string& source, PPTokenizationStats* stats,
		bool apply_translation)
		: physical_(source, stats), suppress_ucn_once_(false), stats_(stats),
		  apply_translation_(apply_translation), last_line_(1), last_column_(1)
	{}

	__attribute__((noinline)) int Next()
	{
		if (!apply_translation_)
		{
			const int current = physical_.Next();
			last_line_ = physical_.LastLine();
			last_column_ = physical_.LastColumn();
			CountTranslated(current);
			return current;
		}
		while (true)
		{
			// Most source characters cannot begin a trigraph, universal
			// character name, or line splice. Avoid routing those characters
			// through all three lookahead queues.
			if (physical_pending_.empty() && phase1_pending_.empty() &&
				ucn_pending_.empty())
			{
				const int direct = physical_.Next();
				if (direct != '?' && direct != '\\')
				{
					last_line_ = physical_.LastLine();
					last_column_ = physical_.LastColumn();
					CountTranslated(direct);
					return direct;
				}
				physical_pending_.push_back(LocatedCodePoint(direct,
					physical_.LastLine(), physical_.LastColumn()));
			}
			const LocatedCodePoint current = TakeUCN();
			if (current.value != '\\' || PeekUCN().value != '\n')
			{
				last_line_ = current.line;
				last_column_ = current.column;
				CountTranslated(current.value);
				return current.value;
			}
			TakeUCN();
		}
	}

	std::size_t LastLine() const { return last_line_; }
	std::size_t LastColumn() const { return last_column_; }

	int NextRawCodePoint()
	{
		if (!physical_pending_.empty() || !phase1_pending_.empty() ||
			!ucn_pending_.empty())
			ThrowLexicalInternalError(
				"raw mode entered with translated lookahead");
		const int result = physical_.Next();
		last_line_ = physical_.LastLine();
		last_column_ = physical_.LastColumn();
		CountTranslated(result);
		return result;
	}

private:
	void CountTranslated(int code_point)
	{
		if (stats_ && code_point != kEndOfFile)
			++stats_->translated_code_points;
	}

	LocatedCodePoint TakePhysical()
	{
		if (physical_pending_.empty())
		{
			const int value = physical_.Next();
			return LocatedCodePoint(value,
				physical_.LastLine(), physical_.LastColumn());
		}
		const LocatedCodePoint result = physical_pending_.front();
		physical_pending_.pop_front();
		return result;
	}

	const LocatedCodePoint& PeekPhysical(std::size_t offset)
	{
		while (physical_pending_.size() <= offset)
		{
			if (!physical_pending_.empty() &&
				physical_pending_.back().value == kEndOfFile)
				return physical_pending_.back();
			const int value = physical_.Next();
			physical_pending_.push_back(LocatedCodePoint(value,
				physical_.LastLine(), physical_.LastColumn()));
		}
		return physical_pending_[offset];
	}

	LocatedCodePoint PullPhase1()
	{
		const LocatedCodePoint current = TakePhysical();
		if (current.value != '?' || PeekPhysical(0).value != '?')
			return current;
		const int replacement = TrigraphReplacement(PeekPhysical(1).value);
		if (replacement == kEndOfFile)
			return current;
		TakePhysical();
		TakePhysical();
		return LocatedCodePoint(replacement, current.line, current.column);
	}

	LocatedCodePoint TakePhase1()
	{
		if (phase1_pending_.empty())
			return PullPhase1();
		const LocatedCodePoint result = phase1_pending_.front();
		phase1_pending_.pop_front();
		return result;
	}

	const LocatedCodePoint& PeekPhase1()
	{
		if (phase1_pending_.empty())
			phase1_pending_.push_back(PullPhase1());
		return phase1_pending_.front();
	}

	LocatedCodePoint PullUCN()
	{
		const LocatedCodePoint current = TakePhase1();
		if (suppress_ucn_once_)
		{
			suppress_ucn_once_ = false;
			return current;
		}
		if (current.value != '\\')
			return current;
		const int next = PeekPhase1().value;
		if (next != 'u' && next != 'U')
		{
			suppress_ucn_once_ = next == '\\';
			return current;
		}
		const int marker = TakePhase1().value;
		const int digits = marker == 'u' ? 4 : 8;
		std::uint32_t value = 0;
		for (int i = 0; i < digits; ++i)
		{
			const int digit = TakePhase1().value;
			if (!IsHexDigit(digit))
				ThrowLexicalSourceError("invalid universal character name");
			value = (value << 4) | HexValue(digit);
		}
		if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ||
			(value < 0xA0 && value != '$' && value != '@' && value != '`'))
			ThrowLexicalSourceError("invalid universal character value");
		return LocatedCodePoint(
			static_cast<int>(value), current.line, current.column);
	}

	LocatedCodePoint TakeUCN()
	{
		if (ucn_pending_.empty())
			return PullUCN();
		const LocatedCodePoint result = ucn_pending_.front();
		ucn_pending_.pop_front();
		return result;
	}

	const LocatedCodePoint& PeekUCN()
	{
		if (ucn_pending_.empty())
			ucn_pending_.push_back(PullUCN());
		return ucn_pending_.front();
	}

	PhysicalCursor physical_;
	FixedQueue<LocatedCodePoint, 2> physical_pending_;
	FixedQueue<LocatedCodePoint, 1> phase1_pending_;
	FixedQueue<LocatedCodePoint, 1> ucn_pending_;
	bool suppress_ucn_once_;
	PPTokenizationStats* stats_;
	bool apply_translation_;
	std::size_t last_line_, last_column_;
};

bool IsNamedOperator(const std::string& spelling)
{
	const char* operators[] = {
		"new", "delete", "and", "and_eq", "bitand", "bitor", "compl",
		"not", "not_eq", "or", "or_eq", "xor", "xor_eq"
	};
	for (std::size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i)
		if (spelling == operators[i])
			return true;
	return false;
}

class Lexer
{
public:
	Lexer(const std::string& source, IPPTokenStream& output,
		PPTokenizationStats* stats, bool apply_translation)
		: translation_(source, stats, apply_translation), output_(output),
		  stats_(stats),
		  at_line_start_(true), header_context_(kNoHeaderContext),
		  last_token_was_operator_(false)
	{}

	void Run()
	{
		while (Peek(0) != kEndOfFile)
		{
			if (Peek(0) == '\n')
			{
				const std::size_t line = PeekLine(0);
				const std::size_t column = PeekColumn(0);
				Take();
				output_.set_source_location(line, column);
				EmitNewLine();
			}
			else if (ScanWhitespaceAndComments())
			{}
			else if (header_context_ == kAfterInclude && ScanHeaderName())
			{}
			else if (ScanLiteral())
			{}
			else if (IsIdentifierInitial(Peek(0)))
				ScanIdentifier();
			else if (IsAsciiDigit(Peek(0)) ||
				(Peek(0) == '.' && IsAsciiDigit(Peek(1))))
				ScanPPNumber();
			else if (!ScanPunctuator())
				ScanNonWhitespaceCharacter();
		}
		output_.emit_eof();
	}

private:
	enum HeaderContext
	{
		kNoHeaderContext,
		kAfterDirectiveMarker,
		kAfterInclude
	};

	int Peek(std::size_t offset) CPPGM_STABLE_PREFIX_QUERY
	{
		while (lookahead_.size() <= offset)
		{
			if (!lookahead_.empty() &&
				lookahead_.back().value == kEndOfFile)
				return kEndOfFile;
			const int value = translation_.Next();
			lookahead_.push_back(LocatedCodePoint(value,
				translation_.LastLine(), translation_.LastColumn()));
		}
		return lookahead_.unchecked(offset).value;
	}

	int Take()
	{
		if (lookahead_.empty())
			return translation_.Next();
		const int result = lookahead_.unchecked(0).value;
		lookahead_.pop_front();
		return result;
	}

	std::size_t PeekLine(std::size_t offset)
	{
		Peek(offset);
		return lookahead_.unchecked(offset).line;
	}

	std::size_t PeekColumn(std::size_t offset)
	{
		Peek(offset);
		return lookahead_.unchecked(offset).column;
	}

	void AppendTake(std::string* spelling)
	{
		AppendUTF8(Take(), spelling);
	}

	std::string& StartTokenSpelling()
	{
		output_.set_source_location(PeekLine(0), PeekColumn(0));
		spelling_.clear();
		return spelling_;
	}

	void CountToken(std::size_t token_bytes = 0)
	{
		if (stats_)
		{
			++stats_->emitted_tokens;
			stats_->emitted_token_bytes += token_bytes;
			stats_->peak_token_buffer_bytes = std::max(
				stats_->peak_token_buffer_bytes, spelling_.capacity());
		}
	}

	void EmitWhitespace()
	{
		output_.emit_whitespace_sequence();
		CountToken();
	}

	void EmitNewLine()
	{
		output_.emit_new_line();
		CountToken();
		at_line_start_ = true;
		header_context_ = kNoHeaderContext;
	}

	void EmitIdentifier(const std::string& spelling)
	{
		if (IsNamedOperator(spelling))
		{
			EmitPunctuator(spelling);
			return;
		}
		output_.emit_identifier(spelling);
		CountToken(spelling.size());
		at_line_start_ = false;
		header_context_ = header_context_ == kAfterDirectiveMarker &&
			(spelling == "include" || spelling == "include_next") ?
			kAfterInclude : kNoHeaderContext;
		last_token_was_operator_ = spelling == "operator";
	}

	void EmitPunctuator(const std::string& spelling)
	{
		const bool was_at_line_start = at_line_start_;
		output_.emit_preprocessing_op_or_punc(spelling);
		CountToken(spelling.size());
		at_line_start_ = false;
		header_context_ = was_at_line_start &&
			(spelling == "#" || spelling == "%:") ?
			kAfterDirectiveMarker : kNoHeaderContext;
		last_token_was_operator_ = false;
	}

	void ClearTokenContext()
	{
		at_line_start_ = false;
		header_context_ = kNoHeaderContext;
		last_token_was_operator_ = false;
	}

	bool ScanWhitespaceAndComments()
	{
		if (!IsHorizontalWhitespace(Peek(0)) &&
			!(Peek(0) == '/' && (Peek(1) == '/' || Peek(1) == '*')))
			return false;
		while (true)
		{
			while (IsHorizontalWhitespace(Peek(0)))
				Take();
			if (Peek(0) == '/' && Peek(1) == '/')
			{
				Take();
				Take();
				while (Peek(0) != '\n' && Peek(0) != kEndOfFile)
					Take();
				break;
			}
			if (Peek(0) != '/' || Peek(1) != '*')
				break;
			Take();
			Take();
			while (!(Peek(0) == '*' && Peek(1) == '/'))
			{
				if (Peek(0) == kEndOfFile)
					ThrowLexicalSourceError("unterminated block comment");
				Take();
			}
			Take();
			Take();
		}
		EmitWhitespace();
		return true;
	}

	bool ScanHeaderName()
	{
		const int opening = Peek(0);
		if ((opening != '<' && opening != '"') || Peek(1) == '\n' ||
			Peek(1) == kEndOfFile || Peek(1) == (opening == '<' ? '>' : '"'))
			return false;
		const int closing = opening == '<' ? '>' : '"';
		std::string& spelling = StartTokenSpelling();
		AppendTake(&spelling);
		while (Peek(0) != closing)
		{
			if (Peek(0) == '\n' || Peek(0) == kEndOfFile)
				ThrowLexicalSourceError("unterminated header name");
			AppendTake(&spelling);
		}
		AppendTake(&spelling);
		output_.emit_header_name(spelling);
		CountToken(spelling.size());
		ClearTokenContext();
		return true;
	}

	bool MatchesAscii(const char* spelling)
	{
		for (std::size_t i = 0; spelling[i] != '\0'; ++i)
			if (Peek(i) != static_cast<unsigned char>(spelling[i]))
				return false;
		return true;
	}

	void ConsumeAscii(std::size_t count, std::string* spelling)
	{
		for (std::size_t i = 0; i < count; ++i)
			AppendTake(spelling);
	}

	bool ScanLiteral()
	{
		if (Peek(0) == 'R' && Peek(1) == '"')
		{
			ScanRawLiteral(2);
			return true;
		}
		if (Peek(0) == 'u' && Peek(1) == '8' && Peek(2) == 'R' &&
			Peek(3) == '"')
		{
			ScanRawLiteral(4);
			return true;
		}
		if ((Peek(0) == 'u' || Peek(0) == 'U' || Peek(0) == 'L') &&
			Peek(1) == 'R' && Peek(2) == '"')
		{
			ScanRawLiteral(3);
			return true;
		}
		if (Peek(0) == '"')
		{
			ScanQuotedLiteral(1, '"');
			return true;
		}
		if (Peek(0) == '\'')
		{
			ScanQuotedLiteral(1, '\'');
			return true;
		}
		if (Peek(0) == 'u' && Peek(1) == '8' && Peek(2) == '"')
		{
			ScanQuotedLiteral(3, '"');
			return true;
		}
		if ((Peek(0) == 'u' || Peek(0) == 'U' || Peek(0) == 'L') &&
			(Peek(1) == '"' || Peek(1) == '\''))
		{
			ScanQuotedLiteral(2, Peek(1));
			return true;
		}
		return false;
	}

	void ScanQuotedLiteral(std::size_t opener_length, int quote)
	{
		std::string& spelling = StartTokenSpelling();
		ConsumeAscii(opener_length, &spelling);
		bool has_character = false;
		while (Peek(0) != quote)
		{
			if (Peek(0) == '\n' || Peek(0) == kEndOfFile)
				ThrowLexicalSourceError("unterminated quoted literal");
			if (Peek(0) == '\\')
				ScanEscapeSequence(&spelling);
			else
				AppendTake(&spelling);
			has_character = true;
		}
		AppendTake(&spelling);
		// The literal-operator-id max-munch exception tokenizes operator""x
		// as operator, "", x. Other non-underscore suffixes stay attached so
		// PA2 can diagnose them as invalid user-defined literals.
		const bool split_literal_operator = quote == '"' &&
			opener_length == 1 && !has_character && last_token_was_operator_;
		const bool has_suffix = !split_literal_operator &&
			ScanIdentifierSuffix(&spelling);
		if (quote == '\'')
		{
			if (has_suffix)
				output_.emit_user_defined_character_literal(spelling);
			else
				output_.emit_character_literal(spelling);
		}
		else if (has_suffix)
			output_.emit_user_defined_string_literal(spelling);
		else
			output_.emit_string_literal(spelling);
		CountToken(spelling.size());
		ClearTokenContext();
	}

	void ScanEscapeSequence(std::string* spelling)
	{
		AppendTake(spelling);
		const int escaped = Peek(0);
		if (escaped == '\n' || escaped == kEndOfFile)
			ThrowLexicalSourceError("unterminated escape sequence");
		AppendTake(spelling);
		if (IsSimpleEscape(escaped))
			return;
		if (escaped >= '0' && escaped <= '7')
		{
			for (int i = 0; i < 2 && Peek(0) >= '0' && Peek(0) <= '7'; ++i)
				AppendTake(spelling);
			return;
		}
		if (escaped == 'x')
		{
			if (!IsHexDigit(Peek(0)))
				ThrowLexicalSourceError("hex escape has no digits");
			while (IsHexDigit(Peek(0)))
				AppendTake(spelling);
			return;
		}
	ThrowLexicalSourceError("invalid escape sequence");
	}

	bool ScanIdentifierSuffix(std::string* spelling)
	{
		if (!IsIdentifierInitial(Peek(0)))
			return false;
		do
		{
			AppendTake(spelling);
		} while (IsIdentifierBody(Peek(0)));
		return true;
	}

	bool IsRawDelimiterCharacter(int c)
	{
		return c != kEndOfFile && c != ' ' && c != '(' && c != ')' &&
			c != '\\' && c != '\t' && c != '\v' && c != '\f' && c != '\n';
	}

	void ScanRawLiteral(std::size_t opener_length)
	{
		std::string& spelling = StartTokenSpelling();
		ConsumeAscii(opener_length, &spelling);
		int delimiter[16];
		std::size_t delimiter_size = 0;
		while (true)
		{
			const int current = translation_.NextRawCodePoint();
			if (current == '(')
			{
				AppendUTF8(current, &spelling);
				break;
			}
			if (!IsRawDelimiterCharacter(current))
				ThrowLexicalSourceError("invalid raw string delimiter");
			if (delimiter_size == 16)
				ThrowLexicalSourceError("raw string delimiter is too long");
			delimiter[delimiter_size++] = current;
			AppendUTF8(current, &spelling);
		}
		std::size_t matched = 0;
		const std::size_t terminator_size = delimiter_size + 2;
		while (matched != terminator_size)
		{
			const int current = translation_.NextRawCodePoint();
			if (current == kEndOfFile)
				ThrowLexicalSourceError("unterminated raw string literal");
			AppendUTF8(current, &spelling);
			const int expected = matched == 0 ? ')' :
				(matched <= delimiter_size ? delimiter[matched - 1] : '"');
			if (current == expected)
				++matched;
			else
				matched = current == ')' ? 1 : 0;
		}
		const bool has_suffix = ScanIdentifierSuffix(&spelling);
		if (has_suffix)
			output_.emit_user_defined_string_literal(spelling);
		else
			output_.emit_string_literal(spelling);
		CountToken(spelling.size());
		ClearTokenContext();
	}

	void ScanIdentifier()
	{
		std::string& spelling = StartTokenSpelling();
		do
		{
			AppendTake(&spelling);
		} while (IsIdentifierBody(Peek(0)));
		EmitIdentifier(spelling);
	}

	void ScanPPNumber()
	{
		std::string& spelling = StartTokenSpelling();
		AppendTake(&spelling);
		while (IsAsciiDigit(Peek(0)) || IsIdentifierNondigit(Peek(0)) ||
			Peek(0) == '.')
		{
			const int current = Peek(0);
			AppendTake(&spelling);
			const bool hexadecimal = spelling.size() >= 2 && spelling[0] == '0' &&
				(spelling[1] == 'x' || spelling[1] == 'X');
			if ((current == 'e' || current == 'E' ||
				 (hexadecimal && (current == 'p' || current == 'P'))) &&
				(Peek(0) == '+' || Peek(0) == '-'))
				AppendTake(&spelling);
		}
		output_.emit_pp_number(spelling);
		CountToken(spelling.size());
		ClearTokenContext();
	}

	bool ScanPunctuator()
	{
		if (Peek(0) == '<' && Peek(1) == ':' && Peek(2) == ':' &&
			Peek(3) != ':' && Peek(3) != '>')
		{
			std::string& spelling = StartTokenSpelling();
			AppendTake(&spelling);
			EmitPunctuator(spelling);
			return true;
		}
		std::size_t length = 0;
		const int first = Peek(0);
		const int second = Peek(1);
		const int third = Peek(2);
		switch (first)
		{
		case '%':
			length = second == ':' && third == '%' && Peek(3) == ':' ? 4 :
				(second == '=' || second == '>' || second == ':') ? 2 : 1;
			break;
		case '.':
			length = second == '.' && third == '.' ? 3 :
				second == '*' ? 2 : 1;
			break;
		case '-':
			length = second == '>' && third == '*' ? 3 :
				(second == '=' || second == '-' || second == '>') ? 2 : 1;
			break;
		case '<':
			length = second == '<' && third == '=' ? 3 :
				(second == ':' || second == '%' || second == '<' ||
				 second == '=') ? 2 : 1;
			break;
		case '>':
			length = second == '>' && third == '=' ? 3 :
				(second == '>' || second == '=') ? 2 : 1;
			break;
		case '#': length = second == '#' ? 2 : 1; break;
		case ':': length = second == '>' || second == ':' ? 2 : 1; break;
		case '+': length = second == '=' || second == '+' ? 2 : 1; break;
		case '*': length = second == '=' ? 2 : 1; break;
		case '/': length = second == '=' ? 2 : 1; break;
		case '^': length = second == '=' ? 2 : 1; break;
		case '&': length = second == '=' || second == '&' ? 2 : 1; break;
		case '|': length = second == '=' || second == '|' ? 2 : 1; break;
		case '=': length = second == '=' ? 2 : 1; break;
		case '!': length = second == '=' ? 2 : 1; break;
		case '{': case '}': case '[': case ']': case '(':
		case ')': case ';': case '?': case '~': case ',':
			length = 1;
			break;
		default: return false;
		}
		std::string& spelling = StartTokenSpelling();
		ConsumeAscii(length, &spelling);
		EmitPunctuator(spelling);
		return true;
	}

	void ScanNonWhitespaceCharacter()
	{
		if (Peek(0) == '\'' || Peek(0) == '"')
			ThrowLexicalSourceError("unrecognized quote");
		std::string& spelling = StartTokenSpelling();
		AppendTake(&spelling);
		output_.emit_non_whitespace_char(spelling);
		CountToken(spelling.size());
		ClearTokenContext();
	}

	TranslationCursor translation_;
	IPPTokenStream& output_;
	PPTokenizationStats* stats_;
	FixedQueue<LocatedCodePoint, 4> lookahead_;
	std::string spelling_;
	bool at_line_start_;
	HeaderContext header_context_;
	bool last_token_was_operator_;
};

}

#undef CPPGM_STABLE_PREFIX_QUERY
#undef CPPGM_HOST_CURSOR_NOINLINE
PPTokenizationStats::PPTokenizationStats()
	: source_bytes(0), decoded_code_points(0), translated_code_points(0),
	  emitted_tokens(0), emitted_token_bytes(0), peak_token_buffer_bytes(0),
	  elapsed_nanoseconds(0)
{}

void TokenizePreprocessingFile(const std::string& source,
	IPPTokenStream& output, PPTokenizationStats* stats)
{
	const std::chrono::steady_clock::time_point start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	if (stats)
	{
		*stats = PPTokenizationStats();
		stats->source_bytes = source.size();
	}
	Lexer lexer(source, output, stats, true);
	lexer.Run();
	if (stats)
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count());
}

void TokenizeGeneratedPreprocessingToken(const std::string& spelling,
	IPPTokenStream& output)
{
	Lexer lexer(spelling, output, 0, false);
	lexer.Run();
}

}
