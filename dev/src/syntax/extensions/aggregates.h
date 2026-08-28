#ifndef CPPGM_SYNTAX_EXTENSIONS_AGGREGATES_H
#define CPPGM_SYNTAX_EXTENSIONS_AGGREGATES_H

#include "syntax/model/arena.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace syntax
{


template <class Derived>
class AggregateSyntax
{
protected:
	bool ParseStructuredBindingDeclarator(NodeId declarator)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(OP_LSQUARE)) return false;
		std::size_t scan = parser.position_ + 1;
		while (scan < parser.tokens_.size())
		{
			if (parser.tokens_[scan].Kind() != kIdentifierToken) return false;
			++scan;
			if (scan >= parser.tokens_.size()) return false;
			const std::uint16_t kind = parser.tokens_[scan].Kind();
			if (kind == static_cast<std::uint16_t>(OP_RSQUARE)) break;
			if (kind != static_cast<std::uint16_t>(OP_COMMA)) return false;
			++scan;
		}
		parser.Expect(OP_LSQUARE);
		const NodeId bindings = parser.arena_.Make("structured-binding");
		while (true)
		{
			if (!parser.AtIdentifier())
				throw parser.Error("expected structured binding identifier");
			const std::size_t token = parser.position_++;
			const NodeId binding = parser.arena_.Make(
				"binding-identifier", parser.Spelling(token));
			parser.arena_.SetSemanticPayload(
				binding, parser.tokens_[token].spelling);
			parser.arena_.Add(bindings, binding);
			if (!parser.Match(OP_COMMA)) break;
		}
		parser.Expect(OP_RSQUARE);
		parser.arena_.Add(declarator, bindings);
		return true;
	}

	NodeId TryParseDesignatedInitializer()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(OP_DOT)) return kNoNode;
		if (!parser.AtIdentifier())
			throw parser.Error("expected designated member name");
		const std::size_t token = parser.position_++;
		const NodeId designated = parser.arena_.Make(
			"designated-initializer", parser.Spelling(token));
		parser.arena_.SetSemanticPayload(
			designated, parser.tokens_[token].spelling);
		parser.Expect(OP_ASS);
		const NodeId initializer = parser.arena_.Make("initializer");
		parser.arena_.SetSemanticPayload(initializer,
			parser.arena_.SharedStrings().Intern("copy"));
		const NodeId value = parser.At(OP_LBRACE) ?
			parser.ParseBracedInitList() : parser.ParseExpression(2);
		if (value == kNoNode)
			throw parser.Error("expected designated initializer value");
		parser.arena_.Add(initializer, value);
		parser.arena_.Add(designated, initializer);
		return designated;
	}

	void AppendDeclaratorNames(NodeId declarator,
		syntax::TextId fallback,
		std::vector<syntax::TextId>* names) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		for (std::uint32_t edge = parser.arena_.FirstEdge(declarator);
			edge != kNoEdge; edge = parser.arena_.NextEdge(edge))
		{
			const NodeId child = parser.arena_.EdgeChild(edge);
			if (!parser.arena_.IsTag(child, ::cppgm::syntax::STAG_STRUCTURED_BINDING)) continue;
			for (std::uint32_t binding = parser.arena_.FirstEdge(child);
				binding != kNoEdge; binding = parser.arena_.NextEdge(binding))
				names->push_back(parser.arena_.PayloadId(
					parser.arena_.EdgeChild(binding)));
			return;
		}
		names->push_back(fallback);
	}
};

}
}

#endif
