#pragma once

#include "pa10_syntax_model.h"

#include <cstddef>
#include <vector>

namespace cppgm
{
namespace pa34_syntax_detail
{

using namespace pa10_syntax_detail;

template <class Derived>
class TemplateSyntax
{
protected:
	NodeId ParseExplicitSpecifier()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(KW_EXPLICIT)) return kNoNode;
		const NodeId specifier = parser.arena_.Make("specifier", "explicit");
		if (parser.Match(OP_LPAREN))
		{
			const NodeId condition = parser.ParseExpression(2);
			if (condition == kNoNode)
				throw parser.Error("expected explicit-specifier condition");
			parser.Expect(OP_RPAREN);
			parser.arena_.Add(specifier, condition);
		}
		return specifier;
	}

	std::vector<NodeId> ParseSpecialMemberSpecifiers()
	{
		Derived& parser = static_cast<Derived&>(*this);
		std::vector<NodeId> result;
		while (parser.At(KW_INLINE) || parser.At(KW_VIRTUAL) ||
			parser.At(KW_EXPLICIT) || parser.At(KW_CONSTEXPR) ||
			parser.At(KW_FRIEND) || parser.At(KW_STATIC))
		{
			if (parser.At(KW_EXPLICIT))
				result.push_back(ParseExplicitSpecifier());
			else
			{
				const std::size_t token = parser.position_++;
				result.push_back(parser.MakeTokenNode("specifier", token));
			}
		}
		return result;
	}

	NodeId TryParseDeductionGuideDeclaration()
	{
		Derived& parser = static_cast<Derived&>(*this);
		const auto mark = parser.Checkpoint();
		const std::size_t first = parser.position_;
		const NodeId explicit_specifier = parser.At(KW_EXPLICIT) ?
			ParseExplicitSpecifier() : kNoNode;
		if (!parser.AtIdentifier() || !parser.HasNameFact(
			parser.tokens_[parser.position_].spelling, Derived::kKnownTemplate))
		{
			parser.Rollback(mark);
			return kNoNode;
		}
		const std::size_t name_token = parser.position_++;
		const NodeId parameters = parser.ParseParameterClause();
		if (parameters == kNoNode || !parser.Match(OP_ARROW))
		{
			parser.Rollback(mark);
			return kNoNode;
		}
		const NodeId declaration = parser.arena_.Make(
			"deduction-guide-declaration", parser.Spelling(name_token));
		parser.arena_.SetSemanticPayload(
			declaration, parser.tokens_[name_token].spelling);
		if (explicit_specifier != kNoNode)
			parser.arena_.Add(declaration, explicit_specifier);
		parser.arena_.Add(declaration, parameters);
		if (!parser.ParseTypeId(declaration))
			throw parser.Error("expected deduction-guide result type");
		parser.Expect(OP_SEMICOLON);
		parser.arena_.SetTokenRange(declaration, first, parser.position_);
		return declaration;
	}
};

}
}
