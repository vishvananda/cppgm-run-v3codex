#ifndef CPPGM_PA10_PARSER_NAME_FACTS_H
#define CPPGM_PA10_PARSER_NAME_FACTS_H

#include "pa10_syntax_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa10_syntax_detail
{

template <class Derived>
class ParserNameFacts
{
protected:
	std::string UnqualifiedClassName(std::string owner) const
	{
		std::size_t separator = std::string::npos;
		std::size_t angle_depth = 0;
		for (std::size_t i = 0; i + 1 < owner.size(); ++i)
		{
			if (owner[i] == '<') ++angle_depth;
			else if (owner[i] == '>' && angle_depth != 0) --angle_depth;
			else if (angle_depth == 0 && owner[i] == ':' && owner[i + 1] == ':')
				separator = i;
		}
		if (separator != std::string::npos) owner.erase(0, separator + 2);
		const std::size_t arguments = owner.find('<');
		if (arguments != std::string::npos) owner.erase(arguments);
		return owner;
	}

	bool QualifiedStartsType() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		std::size_t scan = parser.position_;
		bool qualified = false;
		if (scan < parser.tokens_.size() && parser.tokens_[scan].kind ==
			static_cast<std::uint16_t>(OP_COLON2))
		{
			qualified = true;
			++scan;
		}
		std::size_t last = parser.tokens_.size();
		while (scan < parser.tokens_.size() &&
			parser.tokens_[scan].kind == kIdentifierToken)
		{
			last = scan++;
			if (scan >= parser.tokens_.size() || parser.tokens_[scan].kind !=
				static_cast<std::uint16_t>(OP_COLON2)) break;
			qualified = true;
			++scan;
		}
		const bool terminal_template = last != parser.tokens_.size() &&
			parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownTemplate);
		const bool qualified_template_id = qualified && terminal_template &&
			scan < parser.tokens_.size() && parser.tokens_[scan].kind ==
				static_cast<std::uint16_t>(OP_LT);
		if (!qualified_template_id && last != parser.tokens_.size() &&
			parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownNonTemplate)) return false;
		return last != parser.tokens_.size() &&
			(parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownType) || parser.IsLikelyTypeIdentifier(last) ||
			 qualified_template_id || (qualified && scan < parser.tokens_.size() &&
			 parser.tokens_[scan].kind == kIdentifierToken));
	}

	void PublishClassNameFacts(std::size_t mark)
	{
		Derived& parser = static_cast<Derived&>(*this);
		std::vector<TextId> types, templates, non_templates;
		for (std::size_t i = mark; i < parser.name_fact_changes_.size(); ++i)
		{
			const TextId name = parser.name_fact_changes_[i].name;
			if (parser.HasNameFact(name, Derived::kKnownType)) types.push_back(name);
			if (parser.HasNameFact(name, Derived::kKnownTemplate))
				templates.push_back(name);
			if (parser.HasNameFact(name, Derived::kKnownNonTemplate))
				non_templates.push_back(name);
		}
		parser.RestoreNameFacts(mark);
		for (std::size_t i = 0; i < types.size(); ++i)
			parser.SetNameFact(types[i], Derived::kKnownType);
		for (std::size_t i = 0; i < templates.size(); ++i)
			parser.SetNameFact(templates[i], Derived::kKnownTemplate);
		for (std::size_t i = 0; i < non_templates.size(); ++i)
			parser.SetNameFact(non_templates[i], Derived::kKnownNonTemplate);
	}
};

}
}

#endif
