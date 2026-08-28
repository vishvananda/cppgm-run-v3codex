#pragma once

#include "syntax/model/arena.h"

#include <cstddef>

namespace cppgm
{
namespace syntax
{


template <class Derived>
class ControlFlowSyntax
{
protected:
	bool IsContextualCoroutineKeyword(const char* spelling) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return parser.template_declaration_depth_ != 0 && parser.AtIdentifier() &&
			parser.Spelling(parser.position_) == spelling &&
			!parser.HasNameFact(parser.tokens_[parser.position_].spelling,
				Derived::kKnownNonTemplate);
	}

	NodeId TryParseCoroutineAwaitExpression()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!IsContextualCoroutineKeyword("co_await")) return kNoNode;
		++parser.position_;
		const NodeId operand = parser.ParseUnaryExpression();
		if (operand == kNoNode)
			throw parser.Error("expected co_await operand");
		const NodeId expression =
			parser.arena_.Make("coroutine-await-expression");
		parser.arena_.Add(expression, operand);
		return expression;
	}

	NodeId TryParseCoroutineStatement()
	{
		Derived& parser = static_cast<Derived&>(*this);
		const bool is_return = IsContextualCoroutineKeyword("co_return");
		const bool is_yield = IsContextualCoroutineKeyword("co_yield");
		if (!is_return && !is_yield) return kNoNode;
		++parser.position_;
		const NodeId statement = parser.arena_.Make(is_return ?
			"coroutine-return-statement" : "coroutine-yield-statement");
		if (!parser.At(OP_SEMICOLON))
		{
			const NodeId value = parser.ParseExpression();
			if (value == kNoNode)
				throw parser.Error("expected coroutine statement operand");
			parser.arena_.Add(statement, value);
		}
		parser.Expect(OP_SEMICOLON);
		return statement;
	}

	bool SelectionHasInitStatement() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		std::size_t paren = 0, square = 0, brace = 0;
		for (std::size_t scan = parser.position_;
			scan < parser.tokens_.size(); ++scan)
		{
			const std::uint16_t kind = parser.tokens_[scan].Kind();
			if (kind == static_cast<std::uint16_t>(OP_RPAREN) &&
				paren == 0 && square == 0 && brace == 0) return false;
			if (kind == static_cast<std::uint16_t>(OP_SEMICOLON) &&
				paren == 0 && square == 0 && brace == 0) return true;
			if (kind == static_cast<std::uint16_t>(OP_LPAREN)) ++paren;
			else if (kind == static_cast<std::uint16_t>(OP_RPAREN) && paren) --paren;
			else if (kind == static_cast<std::uint16_t>(OP_LSQUARE)) ++square;
			else if (kind == static_cast<std::uint16_t>(OP_RSQUARE) && square) --square;
			else if (kind == static_cast<std::uint16_t>(OP_LBRACE)) ++brace;
			else if (kind == static_cast<std::uint16_t>(OP_RBRACE) && brace) --brace;
		}
		return false;
	}

	NodeId ParseSelectionInitStatement()
	{
		Derived& parser = static_cast<Derived&>(*this);
		const auto mark = parser.Checkpoint();
		const NodeId init = parser.arena_.Make("selection-init-statement");
		NodeId value = kNoNode;
		if (parser.At(KW_USING)) value = parser.ParseUsing();
		else
		{
			const auto declaration_mark = parser.Checkpoint();
			value = parser.ParseSimpleOrFunction(false);
			if (value == kNoNode)
			{
				parser.Rollback(declaration_mark);
				value = parser.ParseExpression();
				if (value != kNoNode) parser.Expect(OP_SEMICOLON);
			}
		}
		if (value == kNoNode)
		{
			parser.Rollback(mark);
			throw parser.Error("expected selection init-statement");
		}
		parser.arena_.Add(init, value);
		return init;
	}

	NodeId TryParseHostedIfStatement()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(KW_IF)) return kNoNode;
		const std::size_t fact_mark = parser.name_fact_changes_.size();
		const NodeId statement = parser.arena_.Make("if-statement");
		if (parser.Match(KW_CONSTEXPR))
		{
			const NodeId marker = parser.arena_.Make("constexpr-selection");
			parser.arena_.AddFlags(marker, SYNTAX_FLAG_SEMANTIC_ONLY);
			parser.arena_.Add(statement, marker);
		}
		parser.Expect(OP_LPAREN);
		if (SelectionHasInitStatement())
			parser.arena_.Add(statement, ParseSelectionInitStatement());
		parser.arena_.Add(statement, parser.ParseCondition());
		parser.Expect(OP_RPAREN);
		const NodeId then_node = parser.arena_.Make("then");
		const NodeId then_statement = parser.ParseStatement();
		if (then_statement == kNoNode)
			throw parser.Error("expected if body");
		parser.arena_.Add(then_node, then_statement);
		parser.arena_.Add(statement, then_node);
		if (parser.Match(KW_ELSE))
		{
			const NodeId else_node = parser.arena_.Make("else");
			const NodeId else_statement = parser.ParseStatement();
			if (else_statement == kNoNode)
				throw parser.Error("expected else body");
			parser.arena_.Add(else_node, else_statement);
			parser.arena_.Add(statement, else_node);
		}
		parser.RestoreNameFacts(fact_mark);
		return statement;
	}
};

}
}
