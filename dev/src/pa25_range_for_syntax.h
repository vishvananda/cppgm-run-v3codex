#ifndef CPPGM_PA25_RANGE_FOR_SYNTAX_H
#define CPPGM_PA25_RANGE_FOR_SYNTAX_H

#include "pa10_syntax_model.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace pa25_syntax_detail
{

using namespace pa10_syntax_detail;

template <class Derived>
class RangeForSyntax
{
protected:
	NodeId TryParseRangeForStatement(std::size_t fact_mark)
	{
		Derived& parser = static_cast<Derived&>(*this);
		const auto range_mark = parser.Checkpoint();
		const NodeId specifiers = parser.ParseDeclSpecifierSeq(false);
		if (specifiers == kNoNode)
		{
			parser.Rollback(range_mark);
			return kNoNode;
		}
		std::string name;
		const NodeId declarator = parser.ParseDeclarator(false, &name);
		parser.SkipAttributes();
		if (declarator == kNoNode || !parser.Match(OP_COLON))
		{
			parser.Rollback(range_mark);
			return kNoNode;
		}
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
		if (!name.empty())
		{
			parser.SetNameFact(name, Derived::kKnownType, false);
			parser.SetNameFact(name, Derived::kActiveNonTypeParameter);
			parser.SetNameFact(name, Derived::kKnownNonTemplate);
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
