#pragma once

#include "syntax/model/arena.h"
#include "support/exception_types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppgm
{
namespace syntax
{

enum ParserAttemptStatus : std::uint8_t
{
	PARSER_MATCHED,
	PARSER_NO_MATCH,
	PARSER_EXPECTED_CLOSE_PAREN,
	PARSER_EXPECTED_PARAMETER,
	PARSER_EXPECTED_DEFAULT_ARGUMENT
};

struct ParserAttempt
{
	NodeId node;
	ParserAttemptStatus status;
	ParserAttempt(ParserAttemptStatus status_value,
		NodeId node_value = kNoNode) : node(node_value), status(status_value) {}
	bool CommittedError() const { return status >= PARSER_EXPECTED_CLOSE_PAREN; }
	const char* Diagnostic() const
	{
		if (status == PARSER_EXPECTED_PARAMETER)
			return "expected parameter declaration";
		if (status == PARSER_EXPECTED_DEFAULT_ARGUMENT)
			return "expected default argument";
		return "expected OP_RPAREN";
	}
};
static_assert(sizeof(ParserAttempt) == 8,
	"parser speculation result must stay register-sized");

template <class Derived>
class ParserCursor
{
protected:
	SyntaxError Error(const std::string& message) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		const std::string location =
			parser.position_ < parser.tokens_.size() &&
			parser.tokens_[parser.position_].source_file != 0 ?
			" at " + parser.strings_.Get(
				parser.tokens_[parser.position_].source_file) + ":" +
			std::to_string(parser.tokens_[parser.position_].source_line) + ":" +
			std::to_string(parser.tokens_[parser.position_].source_column) :
			std::string();
		return SyntaxError(message + location + " at token " +
			std::to_string(parser.position_) +
			(parser.position_ < parser.tokens_.size() ?
			 " (`" + Spelling(parser.position_) + "`)" : std::string()));
	}

	bool At(SimpleTokenKind kind) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.position_ < parser.tokens_.size() &&
			parser.tokens_[parser.position_].Kind() ==
				static_cast<std::uint16_t>(kind);
	}

	bool AtOffset(std::size_t offset, SimpleTokenKind kind) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.position_ + offset < parser.tokens_.size() &&
			parser.tokens_[parser.position_ + offset].Kind() ==
				static_cast<std::uint16_t>(kind);
	}

	bool AtIdentifier() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.position_ < parser.tokens_.size() &&
			parser.tokens_[parser.position_].Kind() == kIdentifierToken;
	}

	bool AtLiteral() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.position_ < parser.tokens_.size() &&
			parser.tokens_[parser.position_].Kind() == kLiteralToken;
	}

	bool AtEof() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.position_ < parser.tokens_.size() &&
			parser.tokens_[parser.position_].Kind() == kEofToken;
	}

	bool AtCloseAngle() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		if (parser.position_ >= parser.tokens_.size()) return false;
		const std::uint16_t kind = parser.tokens_[parser.position_].Kind();
		return kind == static_cast<std::uint16_t>(OP_GT) ||
			kind == kRShiftFirstToken || kind == kRShiftSecondToken;
	}

	bool Match(SimpleTokenKind kind)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!At(kind)) return false;
		++parser.position_;
		return true;
	}

	bool MatchCloseAngle()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!AtCloseAngle()) return false;
		++parser.position_;
		return true;
	}

	void Expect(SimpleTokenKind kind)
	{
		if (!Match(kind))
			throw Error(std::string("expected ") + SimpleTokenKindName(kind));
	}

	void ExpectCloseAngle()
	{
		if (!MatchCloseAngle()) throw Error("expected close angle bracket");
	}

	const std::string& Spelling(std::size_t position) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.strings_.Get(parser.tokens_[position].spelling);
	}

	std::string TokenDescription(std::size_t position) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		const SyntaxToken& token = parser.tokens_[position];
		if (token.Kind() == kIdentifierToken)
			return "TT_IDENTIFIER:" + Spelling(position);
		if (token.Kind() == kLiteralToken) return Spelling(position);
		if (token.Kind() == kRShiftFirstToken ||
			token.Kind() == kRShiftSecondToken)
			return "OP_RSHIFT:>>";
		return std::string(SimpleTokenKindName(
			static_cast<SimpleTokenKind>(token.Kind()))) + ":" +
			Spelling(position);
	}

	NodeId MakeTokenNode(const char* tag, std::size_t position)
	{
		Derived& parser = static_cast<Derived&>(*this);
		const NodeId node = parser.arena_.Make(tag, TokenDescription(position));
		parser.arena_.SetSemanticPayload(
			node, parser.tokens_[position].spelling);
		parser.arena_.SetTokenRange(node, position, position + 1);
		return node;
	}

	NodeId MakeStructuredNode(const char* tag, const std::string& spelling,
		NodeId structure)
	{
		Derived& parser = static_cast<Derived&>(*this);
		const NodeId node = parser.arena_.Make(tag, spelling);
		if (structure != kNoNode) parser.arena_.Add(node, structure);
		else if (spelling.find("::") == std::string::npos)
			parser.arena_.SetSemanticPayload(
				node, parser.arena_.PayloadId(node));
		return node;
	}

	std::string JoinSpellings(std::size_t first, std::size_t last) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		std::string result;
		for (std::size_t i = first; i < last; ++i)
		{
			if (i != first)
			{
				const std::uint16_t previous = parser.tokens_[i - 1].Kind();
				if (previous == static_cast<std::uint16_t>(KW_CONST) ||
					previous == static_cast<std::uint16_t>(KW_VOLATILE) ||
					previous == static_cast<std::uint16_t>(KW_TYPENAME) ||
					previous == static_cast<std::uint16_t>(KW_TEMPLATE) ||
					previous == static_cast<std::uint16_t>(KW_CLASS) ||
					previous == static_cast<std::uint16_t>(KW_STRUCT) ||
					previous == static_cast<std::uint16_t>(KW_UNION) ||
					previous == static_cast<std::uint16_t>(KW_ENUM))
					result += ' ';
			}
			result += Spelling(i);
		}
		return result;
	}
};

}
}
