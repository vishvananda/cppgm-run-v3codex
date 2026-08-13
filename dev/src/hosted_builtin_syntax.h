#pragma once

#include "hosted_builtin_registry.h"
#include "pa10_syntax_model.h"

#include <string>

namespace cppgm
{
namespace hosted_builtin
{

template <class Derived>
class Syntax
{
protected:
	pa10_syntax_detail::NodeId ParseBuiltinTypeTraitExpression()
	{
		using namespace pa10_syntax_detail;
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
