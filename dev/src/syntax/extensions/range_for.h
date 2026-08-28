#ifndef CPPGM_SYNTAX_EXTENSIONS_RANGE_FOR_H
#define CPPGM_SYNTAX_EXTENSIONS_RANGE_FOR_H

#include "syntax/model/arena.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppgm
{
namespace syntax
{


template <class Derived>
class RangeForSyntax
{
protected:
	bool StartsRangeForStatement() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		std::size_t parentheses = 0;
		std::size_t brackets = 0;
		std::size_t braces = 0;
		std::size_t conditionals = 0;
		for (std::size_t position = parser.position_;
			position < parser.tokens_.size(); ++position)
		{
			const std::uint16_t kind = parser.tokens_[position].Kind();
			if (kind == static_cast<std::uint16_t>(OP_LPAREN)) ++parentheses;
			else if (kind == static_cast<std::uint16_t>(OP_LSQUARE)) ++brackets;
			else if (kind == static_cast<std::uint16_t>(OP_LBRACE)) ++braces;
			else if (kind == static_cast<std::uint16_t>(OP_RPAREN))
			{
				if (parentheses == 0)
					return false;
				--parentheses;
			}
			else if (kind == static_cast<std::uint16_t>(OP_RSQUARE))
			{
				if (brackets == 0) return false;
				--brackets;
			}
			else if (kind == static_cast<std::uint16_t>(OP_RBRACE))
			{
				if (braces == 0) return false;
				--braces;
			}
			if (parentheses != 0 || brackets != 0 || braces != 0) continue;
			if (kind == static_cast<std::uint16_t>(OP_QMARK))
			{
				++conditionals;
				continue;
			}
			if (kind == static_cast<std::uint16_t>(OP_COLON))
			{
				if (conditionals != 0)
				{
					--conditionals;
					continue;
				}
				return true;
			}
			if (kind == static_cast<std::uint16_t>(OP_SEMICOLON) ||
				kind == kEofToken)
				return false;
		}
		return false;
	}

	NodeId TryParseRangeForStatement(std::size_t fact_mark)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!StartsRangeForStatement()) return kNoNode;
		const NodeId specifiers = parser.ParseDeclSpecifierSeq(false);
		if (specifiers == kNoNode)
			throw parser.Error("expected range declaration specifiers");
		syntax::TextId name = 0;
		const NodeId declarator = parser.ParseDeclarator(false, &name);
		parser.SkipAttributes();
		if (declarator == kNoNode)
			throw parser.Error("expected range declarator");
		parser.Expect(OP_COLON);
		const NodeId statement = parser.arena_.Make("range-for-statement");
		const NodeId declaration = parser.arena_.Make("range-declaration");
		parser.arena_.Add(declaration, specifiers);
		parser.arena_.Add(declaration, declarator);
		parser.arena_.Add(statement, declaration);
		const NodeId initializer = parser.arena_.Make("range-initializer");
		const NodeId value = parser.At(OP_LBRACE) ?
			parser.ParseBracedInitList() : parser.ParseExpression();
		if (value == kNoNode)
			throw parser.Error("expected range initializer");
		parser.arena_.Add(initializer, value);
		parser.arena_.Add(statement, initializer);
		parser.Expect(OP_RPAREN);
		if (name != 0)
		{
			parser.SetNameFact(name, Derived::kKnownType, false);
			parser.SetNameFact(name, Derived::kActiveNonTypeParameter);
			parser.SetNameFact(name, Derived::kKnownNonTemplate);
		}
		for (std::uint32_t edge = parser.arena_.FirstEdge(declarator);
			edge != kNoEdge; edge = parser.arena_.NextEdge(edge))
		{
			const NodeId bindings = parser.arena_.EdgeChild(edge);
			if (!parser.arena_.IsTag(bindings, ::cppgm::syntax::STAG_STRUCTURED_BINDING)) continue;
			for (std::uint32_t binding = parser.arena_.FirstEdge(bindings);
				binding != kNoEdge; binding = parser.arena_.NextEdge(binding))
			{
				const std::string spelling = parser.arena_.Payload(
					parser.arena_.EdgeChild(binding));
				parser.SetNameFact(spelling, Derived::kKnownType, false);
				parser.SetNameFact(spelling, Derived::kActiveNonTypeParameter);
				parser.SetNameFact(spelling, Derived::kKnownNonTemplate);
			}
		}
		const NodeId body = parser.ParseStatement();
		if (body == kNoNode) throw parser.Error("expected range-for body");
		parser.arena_.Add(statement, body);
		parser.RestoreNameFacts(fact_mark);
		return statement;
	}
};

}
}

#endif
