#ifndef CPPGM_PA10_PARSER_NAME_FACTS_H
#define CPPGM_PA10_PARSER_NAME_FACTS_H

#include "pa10_syntax_model.h"
#include "pa10_parser_token_classification.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
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
	void ParseBuiltinVaArgArguments(NodeId arguments)
	{
		Derived& parser = static_cast<Derived&>(*this);
		const NodeId argument = parser.ParseExpression(2);
		if (argument == kNoNode)
			throw parser.Error("expected va_arg list expression");
		parser.arena_.Add(arguments, argument);
		parser.Expect(OP_COMMA);
		if (!parser.ParseTypeId(arguments))
			throw parser.Error("expected va_arg result type");
	}

	NodeId ParseDecltypeValueName()
	{
		Derived& parser = static_cast<Derived&>(*this);
		const std::size_t first = parser.position_++;
		parser.Expect(OP_LPAREN);
		const NodeId operand = parser.ParseExpression();
		if (operand == kNoNode)
			throw parser.Error("expected decltype expression");
		parser.Expect(OP_RPAREN);
		const NodeId dependent = parser.arena_.Make("decltype-name");
		parser.arena_.AddFlags(dependent, SYNTAX_FLAG_SEMANTIC_ONLY);
		parser.arena_.Add(dependent, operand);
		parser.arena_.Add(dependent,
			ParseDecltypeQualifiedName("qualified-name"));
		const NodeId expression = parser.arena_.Make("id-expression",
			parser.JoinSpellings(first, parser.position_));
		parser.arena_.Add(expression, dependent);
		return expression;
	}

	NodeId ParseDecltypeQualifiedName(const char* tag)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.Match(OP_COLON2)) return kNoNode;
		parser.Match(KW_TEMPLATE);
		std::string qualified;
		NodeId structure = kNoNode;
		if (!parser.ParseName(&qualified, true, true, true, &structure))
			throw parser.Error("expected qualified decltype name");
		if (structure == kNoNode)
		{
			structure = parser.arena_.Make("structured-type-name");
			parser.arena_.AddFlags(structure, SYNTAX_FLAG_SEMANTIC_ONLY);
			const TextId name = parser.strings_.Intern(qualified);
			const NodeId component = parser.arena_.Make(
				"name-component", qualified);
			parser.arena_.SetSemanticPayload(component, name);
			parser.arena_.Add(structure, component);
		}
		return parser.MakeStructuredNode(tag, qualified, structure);
	}

	bool StartsDependentNonTypeTemplateParameter()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(KW_TYPENAME)) return false;
		const typename Derived::Mark mark = parser.Checkpoint();
		++parser.position_;
		std::string type_name;
		const bool result = parser.ParseName(
			&type_name, true, true, true) &&
			type_name.find("::") != std::string::npos;
		parser.Rollback(mark);
		return result;
	}

	NodeId TryConsumeTemplateArguments(bool retain_types = false)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.At(OP_LT)) return kNoNode;
		if (parser.stats_) ++parser.stats_->template_argument_probes;
		const std::size_t opener = parser.position_;
		const std::uint32_t no_match =
			std::numeric_limits<std::uint32_t>::max() - 1;
		if (opener >= no_match)
			throw std::runtime_error("too many syntax tokens");
		const typename Derived::AngleMatch& cached =
			parser.angle_matches_[opener];
		if (cached.fact_revision == parser.name_fact_revision_)
		{
			if (parser.stats_) ++parser.stats_->template_argument_cache_hits;
			if (cached.close != no_match)
			{
				parser.position_ = static_cast<std::size_t>(cached.close) + 1;
				return retain_types ?
					parser.ParseMatchedTemplateTypeArguments(
						opener, parser.position_) : kNoNode;
			}
			return kNoNode;
		}
		const std::string candidate = parser.position_ == 0 ?
			std::string() : parser.Spelling(parser.position_ - 1);
		const TextId candidate_id = parser.position_ == 0 ? 0 :
			parser.tokens_[parser.position_ - 1].spelling;
		const bool qualified_candidate = parser.position_ >= 2 &&
			parser.tokens_[parser.position_ - 2].Kind() ==
				static_cast<std::uint16_t>(OP_COLON2);
		const bool known_template = parser.HasNameFact(
			candidate_id, Derived::kKnownTemplate);
		const bool known_non_template = parser.HasNameFact(
			candidate_id, Derived::kKnownNonTemplate);
		const bool active_non_type_parameter = parser.HasNameFact(
			candidate_id, Derived::kActiveNonTypeParameter);
		const bool explicitly_templated = opener >= 2 &&
			parser.tokens_[opener - 2].Kind() ==
				static_cast<std::uint16_t>(KW_TEMPLATE);
		const std::uint16_t first_argument_kind = opener + 1 <
			parser.tokens_.size() ? parser.tokens_[opener + 1].Kind() : 0;
		const bool fundamental_type_argument =
			IsFundamentalKind(first_argument_kind) &&
			(opener + 2 >= parser.tokens_.size() ||
			 parser.tokens_[opener + 2].Kind() !=
				static_cast<std::uint16_t>(OP_LPAREN));
		const bool unambiguous_type_argument = fundamental_type_argument ||
			first_argument_kind == static_cast<std::uint16_t>(KW_TYPENAME) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_DECLTYPE) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_CLASS) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_STRUCT) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_UNION) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_ENUM) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_CONST) ||
			first_argument_kind == static_cast<std::uint16_t>(KW_VOLATILE);
		if (!qualified_candidate && known_non_template &&
			(!known_template || active_non_type_parameter) &&
			!unambiguous_type_argument)
		{
			parser.angle_matches_[opener].close = no_match;
			parser.angle_matches_[opener].fact_revision =
				parser.name_fact_revision_;
			return kNoNode;
		}
		const bool trusted = qualified_candidate || known_template ||
			unambiguous_type_argument ||
			(!known_non_template && candidate.find('T') != std::string::npos);
		const typename Derived::Mark mark = parser.Checkpoint();
		++parser.position_;
		const std::size_t scan_start = parser.position_;
		if (parser.stats_) ++parser.stats_->template_argument_scans;
		std::size_t paren = 0, square = 0, brace = 0, angle = 1;
		std::vector<std::size_t> open_angles(1, opener);
		const bool reject_candidate =
			parser.retained_template_argument_depth_ != 0 &&
			qualified_candidate &&
			!explicitly_templated && !known_template;
		bool saw_expression_operator = false;
		bool hit_untrusted_limit = false;
		// Unclassified identifiers are ambiguous. Keep speculative lookahead
		// bounded; established template names may consume full argument lists.
		const std::size_t untrusted_scan_limit = 256;
		while (parser.position_ < parser.tokens_.size() && !parser.AtEof())
		{
			if (paren == 0 && square == 0 && brace == 0 &&
				(parser.At(OP_SEMICOLON) ||
				 (!trusted && parser.At(OP_LBRACE)))) break;
			if (!trusted &&
				parser.position_ - scan_start >= untrusted_scan_limit)
			{
				hit_untrusted_limit = true;
				break;
			}
			if (parser.At(OP_LPAREN)) ++paren;
			else if (parser.At(OP_RPAREN))
			{
				if (paren == 0) break;
				--paren;
			}
			else if (parser.At(OP_LSQUARE)) ++square;
			else if (parser.At(OP_RSQUARE))
			{
				if (square == 0) break;
				--square;
			}
			else if (parser.At(OP_LBRACE)) ++brace;
			else if (parser.At(OP_RBRACE))
			{
				if (brace == 0) break;
				--brace;
			}
			else if (paren == 0 && square == 0 && brace == 0)
			{
				if (parser.At(OP_LOR) || parser.At(OP_LAND) ||
					parser.At(OP_QMARK) || parser.At(OP_COLON) ||
					parser.At(OP_PLUSASS) || parser.At(OP_MINUSASS) ||
					parser.At(OP_ASS))
					saw_expression_operator = true;
				if (parser.At(OP_LT))
				{
					const bool identifier_candidate = parser.position_ != 0 &&
						parser.tokens_[parser.position_ - 1].Kind() ==
							kIdentifierToken;
					const std::uint16_t previous_kind = parser.position_ == 0 ?
						0 : parser.tokens_[parser.position_ - 1].Kind();
					const bool cast_candidate =
						previous_kind == static_cast<std::uint16_t>(KW_STATIC_CAST) ||
						previous_kind == static_cast<std::uint16_t>(KW_DYNAMIC_CAST) ||
						previous_kind == static_cast<std::uint16_t>(KW_CONST_CAST) ||
						previous_kind == static_cast<std::uint16_t>(KW_REINTERPET_CAST);
					const TextId nested_candidate_id = parser.position_ == 0 ?
						0 : parser.tokens_[parser.position_ - 1].spelling;
					const bool explicitly_templated = parser.position_ >= 2 &&
						parser.tokens_[parser.position_ - 2].Kind() ==
							static_cast<std::uint16_t>(KW_TEMPLATE);
					const bool qualified_candidate = parser.position_ >= 2 &&
						parser.tokens_[parser.position_ - 2].Kind() ==
							static_cast<std::uint16_t>(OP_COLON2);
					const bool active_non_type = parser.HasNameFact(
						nested_candidate_id, Derived::kActiveNonTypeParameter);
					const bool known_template = identifier_candidate &&
						parser.HasNameFact(
							nested_candidate_id, Derived::kKnownTemplate) &&
						(qualified_candidate || !active_non_type);
					if (cast_candidate || explicitly_templated ||
						known_template ||
						(identifier_candidate && !qualified_candidate &&
						 !parser.HasNameFact(
							nested_candidate_id, Derived::kKnownNonTemplate)))
					{
						++angle;
						open_angles.push_back(parser.position_);
					}
					else saw_expression_operator = true;
				}
				else if (parser.AtCloseAngle())
				{
					if (open_angles.empty())
						throw std::logic_error("invalid angle lookahead stack");
					const std::size_t matched_opener = open_angles.back();
					open_angles.pop_back();
					--angle;
					const std::size_t matched_close = parser.position_++;
					if (angle == 0)
					{
						if (reject_candidate ||
							(!trusted && saw_expression_operator))
						{
							parser.angle_matches_[matched_opener].close =
								no_match;
							parser.angle_matches_[matched_opener].fact_revision =
								parser.name_fact_revision_;
							parser.RecordTemplateArgumentScan(scan_start, true);
							parser.Rollback(mark);
							return kNoNode;
						}
						parser.angle_matches_[matched_opener].close =
							static_cast<std::uint32_t>(matched_close);
						parser.angle_matches_[matched_opener].fact_revision =
							parser.name_fact_revision_;
						parser.RecordTemplateArgumentScan(scan_start, false);
						return retain_types ?
							parser.ParseMatchedTemplateTypeArguments(
								opener, parser.position_) : kNoNode;
					}
					continue;
				}
			}
			++parser.position_;
		}
		if (hit_untrusted_limit)
		{
			parser.angle_matches_[opener].close = no_match;
			parser.angle_matches_[opener].fact_revision =
				parser.name_fact_revision_;
		}
		else
		{
			for (std::size_t i = 0; i < open_angles.size(); ++i)
			{
				parser.angle_matches_[open_angles[i]].close = no_match;
				parser.angle_matches_[open_angles[i]].fact_revision =
					parser.name_fact_revision_;
			}
		}
		parser.RecordTemplateArgumentScan(scan_start, true);
		parser.Rollback(mark);
		return kNoNode;
	}

	NodeId ParseMemberPointerOperator()
	{
		Derived& parser = static_cast<Derived&>(*this);
		const typename Derived::Mark mark = parser.Checkpoint();
		const std::size_t owner_first = parser.position_;
		const bool global = parser.Match(OP_COLON2);
		std::vector<TextId> names;
		std::vector<NodeId> arguments;
		while (true)
		{
			if (!parser.AtIdentifier())
			{
				parser.Rollback(mark);
				return kNoNode;
			}
			names.push_back(parser.tokens_[parser.position_++].spelling);
			arguments.push_back(TryConsumeTemplateArguments(true));
			if (!parser.Match(OP_COLON2))
			{
				parser.Rollback(mark);
				return kNoNode;
			}
			if (parser.Match(OP_STAR)) break;
			parser.Match(KW_TEMPLATE);
		}

		const std::string owner = parser.JoinSpellings(
			owner_first, parser.position_ - 2);
		const NodeId pointer = parser.arena_.Make(
			"ptr-operator", owner + "::*");
		const NodeId structure = parser.arena_.Make("structured-type-name");
		parser.arena_.AddFlags(structure, SYNTAX_FLAG_SEMANTIC_ONLY);
		if (global)
			parser.arena_.Add(structure,
				parser.arena_.Make("global-qualifier"));
		for (std::size_t i = 0; i < names.size(); ++i)
		{
			const NodeId component = parser.arena_.Make(
				"name-component", parser.strings_.Get(names[i]));
			parser.arena_.SetSemanticPayload(component, names[i]);
			if (arguments[i] != kNoNode)
				parser.arena_.Add(component, arguments[i]);
			parser.arena_.Add(structure, component);
		}
		parser.arena_.Add(pointer, structure);
		return pointer;
	}

	bool StartsParenthesizedMemberPointerDeclarator(std::size_t offset = 0)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtOffset(offset, OP_LPAREN)) return false;
		const typename Derived::Mark mark = parser.Checkpoint();
		parser.position_ += offset + 1;
		const bool result = ParseMemberPointerOperator() != kNoNode;
		parser.Rollback(mark);
		return result;
	}

	bool StartsKnownTemplateId(bool call_only = false)
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtIdentifier() || !parser.AtOffset(1, OP_LT) ||
			!parser.HasNameFact(parser.tokens_[parser.position_].spelling,
				Derived::kKnownTemplate)) return false;
		const typename Derived::Mark mark = parser.Checkpoint();
		++parser.position_;
		const std::size_t opener = parser.position_;
		TryConsumeTemplateArguments();
		const bool result = parser.position_ > opener &&
			(parser.At(OP_LPAREN) || (!call_only && parser.At(OP_COLON2)));
		parser.Rollback(mark);
		return result;
	}

	bool StartsQualifiedCallArgument()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (parser.position_ + 1 >= parser.tokens_.size() ||
			parser.tokens_[parser.position_ + 1].Kind() != kIdentifierToken)
			return false;
		const typename Derived::Mark mark = parser.Checkpoint();
		++parser.position_;
		std::string name;
		TextId terminal = 0;
		const bool parsed = parser.ParseName(
			&name, true, true, true, 0, &terminal);
		const bool result = parsed && name.find("::") != std::string::npos &&
			parser.At(OP_LPAREN) &&
			!parser.HasNameFact(terminal, Derived::kKnownType);
		parser.Rollback(mark);
		return result;
	}

	bool StartsQualifiedCallExpression()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtIdentifier() && !parser.At(OP_COLON2)) return false;
		const typename Derived::Mark mark = parser.Checkpoint();
		std::string name;
		TextId terminal = 0;
		const bool parsed = parser.ParseName(
			&name, true, true, true, 0, &terminal);
		const bool result = parsed && name.find("::") != std::string::npos &&
			parser.At(OP_LPAREN) && !parser.HasNameFact(
				terminal, Derived::kKnownType);
		parser.Rollback(mark);
		return result;
	}

	bool QualifiedStartsTypeAt(std::size_t scan) const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		bool qualified = false;
		if (scan < parser.tokens_.size() && parser.tokens_[scan].Kind() ==
			static_cast<std::uint16_t>(OP_COLON2))
		{
			qualified = true;
			++scan;
		}
		std::size_t last = parser.tokens_.size();
		while (scan < parser.tokens_.size() &&
			parser.tokens_[scan].Kind() == kIdentifierToken)
		{
			last = scan++;
			if (scan >= parser.tokens_.size() || parser.tokens_[scan].Kind() !=
				static_cast<std::uint16_t>(OP_COLON2)) break;
			qualified = true;
			++scan;
		}
		const bool terminal_template = last != parser.tokens_.size() &&
			parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownTemplate);
		const bool qualified_template_id = qualified && terminal_template &&
			scan < parser.tokens_.size() && parser.tokens_[scan].Kind() ==
				static_cast<std::uint16_t>(OP_LT);
		if (!qualified_template_id && last != parser.tokens_.size() &&
			parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownNonTemplate) &&
			!parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownType)) return false;
		return last != parser.tokens_.size() &&
			(parser.HasNameFact(parser.tokens_[last].spelling,
				Derived::kKnownType) || parser.IsLikelyTypeIdentifier(last) ||
			 qualified_template_id || (qualified && scan < parser.tokens_.size() &&
			 parser.tokens_[scan].Kind() == kIdentifierToken));
	}

	bool QualifiedStartsType() const
	{
		const Derived& parser = static_cast<const Derived&>(*this);
		return QualifiedStartsTypeAt(parser.position_);
	}

	void PublishClassNameFacts(std::size_t mark)
	{
		Derived& parser = static_cast<Derived&>(*this);
		struct PublishedFact
		{
			TextId name;
			std::uint8_t facts;
			PublishedFact(TextId name_value, std::uint8_t facts_value)
				: name(name_value), facts(facts_value) {}
		};
		std::vector<PublishedFact> published;
		for (std::size_t i = mark; i < parser.name_fact_changes_.size(); ++i)
		{
			const TextId name = parser.name_fact_changes_[i].name;
			const std::uint8_t facts = name < parser.name_facts_.size() ?
				static_cast<std::uint8_t>(parser.name_facts_[name] &
					(Derived::kKnownType | Derived::kKnownTemplate |
					 Derived::kKnownNonTemplate)) : 0;
			if (facts != 0) published.push_back(PublishedFact(name, facts));
		}
		parser.RestoreNameFacts(mark);
		for (std::size_t i = 0; i < published.size(); ++i)
		{
			const TextId name = published[i].name;
			if (name >= parser.name_facts_.size())
				parser.name_facts_.resize(name + 1, 0);
			const std::uint8_t before = parser.name_facts_[name];
			const std::uint8_t after = static_cast<std::uint8_t>(
				before | published[i].facts);
			if (before == after) continue;
			parser.name_facts_[name] = after;
			parser.AdvanceNameFactRevision();
			if (parser.stats_) ++parser.stats_->parser_fact_changes;
		}
	}
};

}
}

#endif
