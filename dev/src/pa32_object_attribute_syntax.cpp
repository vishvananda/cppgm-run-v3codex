#include "pa32_object_attribute_syntax.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa32_syntax_detail
{
namespace
{

using namespace pa10_syntax_detail;

struct GnuObjectAttributeSyntaxFact
{
	TextId name;
	TextId argument;
	bool has_argument;

	GnuObjectAttributeSyntaxFact()
		: name(0), argument(0), has_argument(false) {}
};

bool At(const std::vector<SyntaxToken>& tokens, std::size_t position,
	SimpleTokenKind kind)
{
	return position < tokens.size() && tokens[position].Kind() ==
		static_cast<std::uint16_t>(kind);
}

void Expect(const std::vector<SyntaxToken>& tokens, std::size_t* position,
	SimpleTokenKind kind)
{
	if (!At(tokens, *position, kind))
		throw std::runtime_error("malformed GNU attribute token sequence");
	++*position;
}

bool ConsumeGnuObjectAttributeFacts(
	const std::vector<SyntaxToken>& tokens, const StringTable& strings,
	std::size_t* position, std::vector<GnuObjectAttributeSyntaxFact>* facts)
{
	if (!position || !facts)
		throw std::logic_error("missing GNU attribute parser destination");
	if (*position >= tokens.size() ||
		tokens[*position].Kind() != kIdentifierToken ||
		strings.Get(tokens[*position].spelling) != "__attribute__")
		return false;
	++*position;
	Expect(tokens, position, OP_LPAREN);
	Expect(tokens, position, OP_LPAREN);
	while (!At(tokens, *position, OP_RPAREN))
	{
		if (*position >= tokens.size() || tokens[*position].Kind() == kEofToken)
			throw std::runtime_error("unterminated GNU attribute");
		if (tokens[*position].Kind() != kIdentifierToken)
		{
			++*position;
			continue;
		}
		GnuObjectAttributeSyntaxFact fact;
		fact.name = tokens[(*position)++].spelling;
		if (At(tokens, *position, OP_LPAREN))
		{
			++*position;
			std::size_t depth = 1;
			while (depth != 0)
			{
				if (*position >= tokens.size() ||
					tokens[*position].Kind() == kEofToken)
					throw std::runtime_error("unterminated GNU attribute argument");
				if (At(tokens, *position, OP_LPAREN)) ++depth;
				else if (At(tokens, *position, OP_RPAREN)) --depth;
				else if (depth == 1 && !fact.has_argument &&
					tokens[*position].Kind() == kLiteralToken)
				{
					fact.argument = tokens[*position].spelling;
					fact.has_argument = true;
				}
				++*position;
			}
		}
		facts->push_back(fact);
		if (At(tokens, *position, OP_COMMA)) ++*position;
	}
	Expect(tokens, position, OP_RPAREN);
	Expect(tokens, position, OP_RPAREN);
	return true;
}

}

bool ConsumeLeadingGnuObjectAttribute(
	const std::vector<SyntaxToken>& tokens, const StringTable& strings,
	SyntaxArena& arena, std::size_t* position,
	std::vector<NodeId>* attributes)
{
	std::vector<GnuObjectAttributeSyntaxFact> facts;
	if (!ConsumeGnuObjectAttributeFacts(tokens, strings, position, &facts))
		return false;
	for (std::size_t i = 0; i < facts.size(); ++i)
	{
		const NodeId attribute = arena.Make(
			"gnu-attribute", strings.Get(facts[i].name));
		arena.AddFlags(attribute, SYNTAX_FLAG_SEMANTIC_ONLY);
		if (facts[i].has_argument)
		{
			const NodeId argument = arena.Make(
				"gnu-attribute-argument", strings.Get(facts[i].argument));
			arena.AddFlags(argument, SYNTAX_FLAG_SEMANTIC_ONLY);
			arena.Add(attribute, argument);
		}
		attributes->push_back(attribute);
	}
	return true;
}

}
}
