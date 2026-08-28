#pragma once

#include "preprocess/hosted/builtin_registry.h"
#include "syntax/model/arena.h"

#include <string>

namespace cppgm
{
namespace hosted_builtin
{

template <class Derived>
class Syntax
{
protected:
	syntax::NodeId ParseBuiltinOffsetofExpression()
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtIdentifier() ||
			parser.Spelling(parser.position_) != "__builtin_offsetof" ||
			!parser.AtOffset(1, OP_LPAREN)) return kNoNode;
		parser.position_ += 2;
		const NodeId expression = parser.arena_.Make(
			"builtin-offsetof-expression", "__builtin_offsetof");
		const NodeId operand = parser.arena_.Make("builtin-type-operand");
		if (!parser.ParseTypeId(operand))
			throw parser.Error("expected offsetof object type");
		parser.arena_.Add(expression, operand);
		parser.Expect(OP_COMMA);
		if (!parser.AtIdentifier())
			throw parser.Error("expected offsetof member name");
		const std::size_t member = parser.position_++;
		const NodeId identifier = parser.arena_.Make(
			"identifier", parser.Spelling(member));
		parser.arena_.SetSemanticPayload(
			identifier, parser.tokens_[member].spelling);
		parser.arena_.Add(expression, identifier);
		parser.Expect(OP_RPAREN);
		return expression;
	}

	syntax::NodeId ParseBuiltinTypeTraitExpression()
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtIdentifier() || !parser.AtOffset(1, OP_LPAREN))
			return kNoNode;
		const std::string spelling = parser.Spelling(parser.position_);
		if (FindTypeTrait(spelling) == TYPE_TRAIT_NONE) return kNoNode;
		parser.position_ += 2;
		const NodeId trait = parser.arena_.Make(
			"builtin-type-trait-expression", spelling);
		if (parser.At(OP_RPAREN))
			throw parser.Error("builtin type trait requires an operand");
		while (true)
		{
			const NodeId operand = parser.arena_.Make("builtin-type-operand");
			if (!parser.ParseTypeId(operand))
				throw parser.Error("expected builtin type trait operand");
			if (parser.Match(OP_DOTS))
				parser.arena_.Add(operand,
					parser.arena_.Make("type-pack-expansion", "..."));
			parser.arena_.Add(trait, operand);
			if (!parser.Match(OP_COMMA)) break;
		}
		parser.Expect(OP_RPAREN);
		return trait;
	}
};

}
}
