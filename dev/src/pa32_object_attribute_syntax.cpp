#include "pa32_object_attribute_syntax.h"

#include "hosted_extension_registry.h"

#include <stdexcept>
#include <string>
#include <vector>

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
	std::vector<std::vector<TextId> > arguments;
	TextId identifier_argument;
	bool literal_argument_list;

	GnuObjectAttributeSyntaxFact()
		: name(0), identifier_argument(0), literal_argument_list(true) {}
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
		!hosted_extension::IsGnuAttributeIntroducer(
			strings.Get(tokens[*position].spelling)))
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
			std::vector<TextId> argument;
			TextId direct_identifier = 0;
			std::size_t direct_tokens = 0;
			bool direct_comma = false;
			bool saw_argument_token = false;
			while (depth != 0)
			{
				if (*position >= tokens.size() ||
					tokens[*position].Kind() == kEofToken)
					throw std::runtime_error("unterminated GNU attribute argument");
				if (At(tokens, *position, OP_LPAREN))
				{
					if (depth == 1) fact.literal_argument_list = false;
					++depth;
				}
				else if (At(tokens, *position, OP_RPAREN))
				{
					if (depth == 1)
					{
						if (!argument.empty()) fact.arguments.push_back(argument);
						else if (saw_argument_token)
							fact.literal_argument_list = false;
					}
					--depth;
				}
				else if (depth == 1 && At(tokens, *position, OP_COMMA))
				{
					direct_comma = true;
					saw_argument_token = true;
					if (argument.empty()) fact.literal_argument_list = false;
					else
					{
						fact.arguments.push_back(argument);
						argument.clear();
					}
				}
				else if (depth == 1 &&
					tokens[*position].Kind() == kLiteralToken)
				{
					++direct_tokens;
					saw_argument_token = true;
					argument.push_back(tokens[*position].spelling);
				}
				else if (depth == 1)
				{
					++direct_tokens;
					if (tokens[*position].Kind() == kIdentifierToken)
						direct_identifier = tokens[*position].spelling;
					saw_argument_token = true;
					fact.literal_argument_list = false;
				}
				++*position;
			}
			if (!direct_comma && direct_tokens == 1)
				fact.identifier_argument = direct_identifier;
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
		if (facts[i].identifier_argument != 0)
		{
			const NodeId identifier = arena.Make(
				"gnu-attribute-identifier-argument",
				strings.Get(facts[i].identifier_argument));
			arena.AddFlags(identifier, SYNTAX_FLAG_SEMANTIC_ONLY);
			arena.Add(attribute, identifier);
		}
		if (!facts[i].literal_argument_list)
		{
			const NodeId invalid = arena.Make(
				"gnu-attribute-nonliteral-argument", std::string());
			arena.AddFlags(invalid, SYNTAX_FLAG_SEMANTIC_ONLY);
			arena.Add(attribute, invalid);
		}
		for (std::size_t argument_index = 0;
			argument_index < facts[i].arguments.size(); ++argument_index)
		{
			std::string spelling;
			for (std::size_t part = 0;
				part < facts[i].arguments[argument_index].size(); ++part)
			{
				if (!spelling.empty()) spelling += ' ';
				spelling += strings.Get(facts[i].arguments[argument_index][part]);
			}
			const NodeId argument = arena.Make(
				"gnu-attribute-argument", spelling);
			arena.AddFlags(argument, SYNTAX_FLAG_SEMANTIC_ONLY);
			arena.Add(attribute, argument);
		}
		attributes->push_back(attribute);
	}
	return true;
}

bool ConsumeLeadingStandardObjectAttribute(
	const std::vector<SyntaxToken>& tokens, const StringTable& strings,
	SyntaxArena& arena, std::size_t* position,
	std::vector<NodeId>* attributes)
{
	if (!position || !attributes)
		throw std::logic_error("missing standard attribute parser destination");
	if (!At(tokens, *position, OP_LSQUARE) ||
		!At(tokens, *position + 1, OP_LSQUARE)) return false;
	*position += 2;
	bool no_unique_address = false;
	while (!At(tokens, *position, OP_RSQUARE) ||
		!At(tokens, *position + 1, OP_RSQUARE))
	{
		if (*position >= tokens.size() || tokens[*position].Kind() == kEofToken)
			throw std::runtime_error("unterminated standard attribute");
		if (tokens[*position].Kind() == kIdentifierToken)
		{
			const std::string& name = strings.Get(tokens[*position].spelling);
			no_unique_address = no_unique_address ||
				name == "no_unique_address" || name == "__no_unique_address__";
		}
		++*position;
	}
	*position += 2;
	if (no_unique_address)
	{
		const NodeId attribute =
			arena.Make("standard-attribute", "no_unique_address");
		arena.AddFlags(attribute, SYNTAX_FLAG_SEMANTIC_ONLY);
		attributes->push_back(attribute);
	}
	return true;
}

}
}
