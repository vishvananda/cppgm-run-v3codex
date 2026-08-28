#pragma once

#include "syntax/model/arena.h"

namespace cppgm
{
namespace syntax
{


template <class Derived>
class RegionSyntax
{
protected:
	NodeId ParseStatementExpression()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(OP_LPAREN) || !parser.AtOffset(1, OP_LBRACE))
			return kNoNode;
		parser.Expect(OP_LPAREN);
		const NodeId expression = parser.arena_.Make("statement-expression");
		const NodeId body = parser.ParseCompoundStatement();
		if (body == kNoNode)
			throw parser.Error("expected statement-expression body");
		parser.arena_.Add(expression, body);
		parser.Expect(OP_RPAREN);
		return expression;
	}

	NodeId ParseExceptionHandler()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(KW_CATCH)) return kNoNode;
		const NodeId handler = parser.arena_.Make("handler");
		parser.Expect(OP_LPAREN);
		const NodeId declaration =
			parser.arena_.Make("exception-declaration");
		if (parser.Match(OP_DOTS))
			parser.arena_.Add(declaration,
				parser.arena_.Make("ellipsis", "..."));
		else
		{
			const NodeId specifiers = parser.ParseDeclSpecifierSeq(false);
			if (specifiers == kNoNode)
				throw parser.Error("expected exception declaration");
			parser.arena_.Add(declaration, specifiers);
			const typename Derived::Mark declarator_mark = parser.Checkpoint();
			const NodeId declarator = parser.ParseDeclarator(false);
			if (declarator != kNoNode)
				parser.arena_.Add(declaration, declarator);
			else parser.Rollback(declarator_mark);
		}
		parser.Expect(OP_RPAREN);
		parser.arena_.Add(handler, declaration);
		const NodeId body = parser.ParseCompoundStatement();
		if (body == kNoNode) throw parser.Error("expected handler body");
		parser.arena_.Add(handler, body);
		return handler;
	}

	void ParseExceptionHandlers(NodeId parent)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(KW_CATCH)) throw parser.Error("expected catch handler");
		while (parser.At(KW_CATCH))
			parser.arena_.Add(parent, ParseExceptionHandler());
	}

	NodeId ParseTryStatement()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(KW_TRY)) return kNoNode;
		const NodeId statement = parser.arena_.Make("try-block");
		const NodeId body = parser.ParseCompoundStatement();
		if (body == kNoNode) throw parser.Error("expected try body");
		parser.arena_.Add(statement, body);
		ParseExceptionHandlers(statement);
		return statement;
	}

	NodeId ParseFunctionTryBlock(bool constructor)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(KW_TRY)) return kNoNode;
		const NodeId statement = parser.arena_.Make("function-try-block");
		if (constructor && parser.At(OP_COLON))
			parser.arena_.Add(statement, parser.ParseCtorInitializer());
		const NodeId body = parser.ParseCompoundStatement();
		if (body == kNoNode)
			throw parser.Error("expected function-try body");
		parser.arena_.Add(statement, body);
		ParseExceptionHandlers(statement);
		return statement;
	}
};

}
}
