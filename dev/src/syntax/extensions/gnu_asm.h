#pragma once

#include "syntax/model/arena.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace syntax
{

template <class Derived>
class GnuAsmSyntax
{
protected:
	GnuAsmSyntax() : pending_colons_(0) {}

	bool AtGnuAsmColon() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return pending_colons_ != 0 || parser.At(OP_COLON) ||
			parser.At(OP_COLON2);
	}

	bool MatchGnuAsmColon()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (pending_colons_ != 0)
		{
			--pending_colons_;
			return true;
		}
		if (parser.Match(OP_COLON)) return true;
		if (!parser.Match(OP_COLON2)) return false;
		pending_colons_ = 1;
		return true;
	}

	bool AtGnuAsmIntroducer() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.At(KW_ASM) ||
			(parser.AtIdentifier() &&
			 (parser.Spelling(parser.position_) == "__asm" ||
			  parser.Spelling(parser.position_) == "__asm__"));
	}

	void ConsumeGnuAsmIntroducer()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!AtGnuAsmIntroducer())
			throw parser.Error("expected GNU asm introducer");
		++parser.position_;
	}

	std::string ConsumeGnuAsmLiteralSequence(const char* expected)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtLiteral()) throw parser.Error(expected);
		const std::size_t first = parser.position_;
		do { ++parser.position_; } while (parser.AtLiteral());
		return parser.JoinSpellings(first, parser.position_);
	}

	syntax::NodeId ParseGnuAsmOperand(const char* tag)
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		std::string symbolic;
		if (parser.Match(OP_LSQUARE))
		{
			if (!parser.AtIdentifier())
				throw parser.Error("expected symbolic GNU asm operand name");
			symbolic = parser.Spelling(parser.position_++);
			parser.Expect(OP_RSQUARE);
		}
		const NodeId operand = parser.arena_.Make(tag,
			ConsumeGnuAsmLiteralSequence("expected GNU asm constraint"));
		if (!symbolic.empty())
			parser.arena_.Add(operand,
				parser.arena_.Make("gnu-asm-symbol", symbolic));
		parser.Expect(OP_LPAREN);
		const NodeId expression = parser.ParseExpression(2);
		if (expression == kNoNode)
			throw parser.Error("expected GNU asm operand expression");
		parser.Expect(OP_RPAREN);
		parser.arena_.Add(operand, expression);
		return operand;
	}

	void ParseGnuAsmOperandGroup(
		syntax::NodeId statement, const char* tag)
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		if (AtGnuAsmColon() || parser.At(OP_RPAREN)) return;
		do { parser.arena_.Add(statement, ParseGnuAsmOperand(tag)); }
		while (parser.Match(OP_COMMA));
	}

	syntax::NodeId TryParseGnuAsmStatement()
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		if (!AtGnuAsmIntroducer()) return kNoNode;
		pending_colons_ = 0;
		ConsumeGnuAsmIntroducer();
		if (parser.At(KW_VOLATILE) ||
			(parser.AtIdentifier() &&
			 (parser.Spelling(parser.position_) == "__volatile" ||
			  parser.Spelling(parser.position_) == "__volatile__")))
			++parser.position_;
		parser.Expect(OP_LPAREN);
		const NodeId statement = parser.arena_.Make("gnu-asm-statement",
			ConsumeGnuAsmLiteralSequence("expected GNU asm template"));
		if (MatchGnuAsmColon())
		{
			ParseGnuAsmOperandGroup(statement, "gnu-asm-output");
			if (MatchGnuAsmColon())
			{
				ParseGnuAsmOperandGroup(statement, "gnu-asm-input");
				if (MatchGnuAsmColon())
				{
					if (!AtGnuAsmColon() && !parser.At(OP_RPAREN))
						do { parser.arena_.Add(statement,
							parser.arena_.Make("gnu-asm-clobber",
								ConsumeGnuAsmLiteralSequence(
									"expected GNU asm clobber"))); }
						while (parser.Match(OP_COMMA));
					if (MatchGnuAsmColon())
					{
						if (!parser.AtIdentifier())
							throw parser.Error("expected GNU asm goto label");
						do {
							if (!parser.AtIdentifier())
								throw parser.Error("expected GNU asm goto label");
							parser.arena_.Add(statement, parser.arena_.Make(
								"gnu-asm-goto-label",
								parser.Spelling(parser.position_++)));
						} while (parser.Match(OP_COMMA));
					}
				}
			}
		}
		parser.Expect(OP_RPAREN);
		parser.Expect(OP_SEMICOLON);
		return statement;
	}

	bool TryParseGnuAsmLabel(syntax::NodeId declarator)
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		if (!AtGnuAsmIntroducer()) return false;
		ConsumeGnuAsmIntroducer();
		parser.Expect(OP_LPAREN);
		const NodeId label = parser.arena_.Make("gnu-asm-label",
			ConsumeGnuAsmLiteralSequence("expected GNU asm label"));
		parser.arena_.AddFlags(label, SYNTAX_FLAG_SEMANTIC_ONLY);
		parser.Expect(OP_RPAREN);
		parser.arena_.Add(declarator, label);
		return true;
	}

private:
	unsigned int pending_colons_;
};

}
}
