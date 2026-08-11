#pragma once

#include "pa10_syntax_model.h"

#include <cstddef>

namespace cppgm
{
namespace pa25_syntax_detail
{

using namespace pa10_syntax_detail;

template <class Derived>
class LambdaCaptureSyntax
{
protected:
	NodeId ParseLambdaIntroducer()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(OP_LSQUARE)) return kNoNode;
		const std::size_t first = parser.position_++;
		const NodeId introducer = parser.arena_.Make("lambda-introducer");
		const auto add_fact = [&parser, introducer](const char* tag,
			std::size_t name, bool named)
		{
			const NodeId fact = parser.arena_.Make(tag);
			if (named)
				parser.arena_.SetSemanticPayload(
					fact, parser.tokens_[name].spelling);
			parser.arena_.AddFlags(fact, SYNTAX_FLAG_SEMANTIC_ONLY);
			parser.arena_.Add(introducer, fact);
		};
		if (!parser.At(OP_RSQUARE))
		{
			bool has_default = false;
			if (parser.At(OP_ASS) ||
				(parser.At(OP_AMP) &&
				 (parser.AtOffset(1, OP_COMMA) ||
				  parser.AtOffset(1, OP_RSQUARE))))
			{
				const bool reference = parser.At(OP_AMP);
				++parser.position_;
				add_fact(reference ? "lambda-capture-default-reference" :
					"lambda-capture-default-copy", 0, false);
				has_default = true;
			}
			if (!has_default || parser.Match(OP_COMMA))
			{
				while (true)
				{
					const char* tag = 0;
					std::size_t name = 0;
					bool named = false;
					if (parser.Match(KW_THIS))
						tag = "lambda-capture-this";
					else
					{
						const bool reference = parser.Match(OP_AMP);
						if (!parser.AtIdentifier())
							throw parser.Error("expected lambda capture");
						name = parser.position_++;
						named = true;
						const bool pack = parser.Match(OP_DOTS);
						if (reference)
							tag = pack ? "lambda-capture-reference-pack" :
								"lambda-capture-reference";
						else
							tag = pack ? "lambda-capture-copy-pack" :
								"lambda-capture-copy";
					}
					add_fact(tag, name, named);
					if (!parser.Match(OP_COMMA)) break;
				}
			}
		}
		parser.Expect(OP_RSQUARE);
		parser.arena_.SetPayload(
			introducer, parser.JoinSpellings(first, parser.position_));
		parser.arena_.SetTokenRange(introducer, first, parser.position_);
		return introducer;
	}
};

}
}
