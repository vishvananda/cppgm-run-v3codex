#pragma once

#include "syntax/model/arena.h"

#include <cstddef>
#include <vector>

namespace cppgm
{
namespace syntax
{


template <class Derived>
class LambdaCaptureSyntax
{
protected:
	NodeId ParseLambdaExpression()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(OP_LSQUARE)) return kNoNode;
		const NodeId lambda = parser.arena_.Make("lambda-expression");
		const NodeId introducer = ParseLambdaIntroducer();
		parser.arena_.Add(lambda, introducer);
		std::size_t identity_last = parser.arena_.TokenLast(introducer);
		const std::size_t parameter_mark =
			parser.active_non_type_parameter_names_.size();
		const bool generic = parser.At(OP_LT);
		if (generic || parser.At(OP_LPAREN))
		{
			const NodeId declarator = parser.arena_.Make("lambda-declarator");
			if (generic)
			{
				parser.arena_.Add(declarator,
					parser.ParseNestedTemplateParameterClause());
				const NodeId name = parser.arena_.Make("identifier", "operator()");
				parser.arena_.SetSemanticPayload(
					name, parser.strings_.Intern("operator()"));
				parser.arena_.AddFlags(name, SYNTAX_FLAG_SEMANTIC_ONLY);
				parser.arena_.Add(declarator, name);
				const NodeId specifiers =
					parser.arena_.Make("decl-specifier-seq");
				parser.arena_.Add(specifiers,
					parser.arena_.Make("decl-specifier", "auto"));
				parser.arena_.Add(declarator, specifiers);
			}
			if (parser.At(OP_LPAREN))
				parser.arena_.Add(declarator, parser.ParseParameterClause());
			else parser.arena_.Add(
				declarator, parser.arena_.Make("parameter-clause"));
			std::vector<NodeId> attributes;
			while (parser.ParseLeadingAttribute(&attributes)) {}
			for (std::size_t i = 0; i < attributes.size(); ++i)
				parser.arena_.Add(declarator, attributes[i]);
			const bool mutable_call = parser.Match(KW_MUTABLE);
			if (mutable_call)
				parser.arena_.Add(declarator,
					parser.arena_.Make("lambda-specifier", "KW_MUTABLE:mutable"));
			else if (generic)
				parser.arena_.Add(declarator,
					parser.arena_.Make("cv-qualifier", "const"));
			attributes.clear();
			while (parser.ParseLeadingAttribute(&attributes)) {}
			for (std::size_t i = 0; i < attributes.size(); ++i)
				parser.arena_.Add(declarator, attributes[i]);
			if (parser.Match(KW_NOEXCEPT))
			{
				const NodeId specification = parser.arena_.Make(
					"noexcept-specification");
				if (parser.Match(OP_LPAREN))
				{
					const NodeId value = parser.ParseExpression();
					if (value == kNoNode)
						throw parser.Error("expected noexcept value");
					parser.Expect(OP_RPAREN);
					parser.arena_.Add(specification, value);
				}
				parser.arena_.Add(declarator, specification);
			}
			if (parser.Match(OP_ARROW))
			{
				const NodeId trailing =
					parser.arena_.Make("trailing-return-type");
				if (!parser.ParseTypeId(trailing))
					throw parser.Error("expected lambda result type");
				parser.arena_.Add(declarator, trailing);
			}
			parser.arena_.Add(lambda, declarator);
			identity_last = parser.position_;
		}
		parser.arena_.SetTokenRange(lambda,
			parser.arena_.TokenLast(introducer), identity_last);
		const NodeId body = parser.ParseCompoundStatement();
		if (body == kNoNode) throw parser.Error("expected lambda body");
		parser.arena_.Add(lambda, body);
		while (parser.active_non_type_parameter_names_.size() > parameter_mark)
		{
			parser.SetNameFact(
				parser.active_non_type_parameter_names_.back(),
				Derived::kActiveNonTypeParameter, false);
			parser.active_non_type_parameter_names_.pop_back();
		}
		return lambda;
	}

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
