#include "pa10_syntax.h"
#include "pa10_syntax_driver_detail.h"
#include "pa10_syntax_model.h"
#include "pa10_parser_name_facts.h"
#include "pa10_parser_token_classification.h"
#include "hosted_builtin_syntax.h"
#include "hosted_extension_syntax.h"
#include "pa32_object_attribute_syntax.h"
#include "pa25_lambda_capture_syntax.h"
#include "pa25_range_for_syntax.h"
#include "pa30_region_syntax.h"
#include "pa34_gnu_asm_syntax.h"
#include "pa34_aggregate_syntax.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace cppgm
{
namespace
{
using namespace pa10_syntax_detail;
class Parser : private ParserNameFacts<Parser>,
	private hosted_builtin::Syntax<Parser>,
	private hosted_extension::Syntax<Parser>,
	private pa25_syntax_detail::LambdaCaptureSyntax<Parser>,
	private pa25_syntax_detail::RangeForSyntax<Parser>,
	private pa30_syntax_detail::RegionSyntax<Parser>,
	private pa34_syntax_detail::GnuAsmSyntax<Parser>,
	private pa34_syntax_detail::AggregateSyntax<Parser>
{
friend class ParserNameFacts<Parser>; friend class hosted_builtin::Syntax<Parser>;
friend class hosted_extension::Syntax<Parser>; friend class pa25_syntax_detail::LambdaCaptureSyntax<Parser>;
friend class pa25_syntax_detail::RangeForSyntax<Parser>; friend class pa30_syntax_detail::RegionSyntax<Parser>;
friend class pa34_syntax_detail::GnuAsmSyntax<Parser>; friend class pa34_syntax_detail::AggregateSyntax<Parser>; public:
	Parser(const std::vector<SyntaxToken>& tokens, StringTable& strings,
		SyntaxArena& arena, SyntaxStats* stats)
		: tokens_(tokens), strings_(strings), arena_(arena), stats_(stats),
		  position_(0), angle_stop_depth_(0), compound_depth_(0), retained_template_argument_depth_(0),
		  name_fact_revision_(1), angle_matches_(tokens.size())
	{
		if (tokens.size() >= std::numeric_limits<std::uint32_t>::max() - 1)
			throw std::runtime_error("too many syntax tokens");
		SetNameFact("nullptr_t", kKnownType);
	}
	NodeId ParseTranslationUnit()
	{
		const NodeId root = arena_.Make("translation-unit");
		while (!AtEof())
		{
			const std::size_t start = position_;
			const NodeId declaration = ParseDeclaration(false);
			if (declaration == kNoNode || position_ == start)
				throw Error("expected declaration");
			arena_.Add(root, declaration);
		}
		++position_;
		if (position_ != tokens_.size()) throw Error("tokens after EOF");
		return root;
	}
	std::size_t StorageBytes() const
	{
		std::size_t bytes =
			name_facts_.capacity() * sizeof(std::uint8_t) +
			name_fact_changes_.capacity() * sizeof(NameFactChange) +
			angle_matches_.capacity() * sizeof(AngleMatch) +
			(last_declared_names_.capacity() + parameter_names_.capacity() +
			 active_non_type_parameter_names_.capacity() +
			 current_classes_.capacity()) * sizeof(TextId);
		return bytes;
	}
private:
	struct Mark
	{
		std::size_t position;
		std::size_t nodes;
		std::size_t edges;
		std::size_t fact_changes;
	};
	enum NameFact
	{
		kKnownType = 1,
		kKnownTemplate = 2,
		kKnownNonTemplate = 4,
		kActiveNonTypeParameter = 8
	};
	struct NameFactChange
	{
		TextId name;
		std::uint8_t previous;
		NameFactChange(TextId name_value, std::uint8_t previous_value)
			: name(name_value), previous(previous_value) {}
	};
	struct AngleMatch
	{
		std::uint32_t close;
		std::uint32_t fact_revision;
		AngleMatch()
			: close(std::numeric_limits<std::uint32_t>::max()),
			  fact_revision(0) {}
	};
	void AdvanceNameFactRevision()
	{
		++name_fact_revision_;
		if (name_fact_revision_ != 0) return;
		name_fact_revision_ = 1;
		for (std::size_t i = 0; i < angle_matches_.size(); ++i)
			angle_matches_[i].fact_revision = 0;
	}
	Mark Checkpoint()
	{
		if (stats_) ++stats_->parser_checkpoints;
		Mark mark = {position_, arena_.NodeMark(), arena_.EdgeMark(),
			name_fact_changes_.size()};
		return mark;
	}
	void Rollback(const Mark& mark)
	{
		if (stats_) ++stats_->parser_rollbacks;
		position_ = mark.position;
		arena_.Rollback(mark.nodes, mark.edges);
		while (name_fact_changes_.size() > mark.fact_changes)
		{
			const NameFactChange change = name_fact_changes_.back();
			name_fact_changes_.pop_back();
			name_facts_[change.name] = change.previous;
			AdvanceNameFactRevision();
		}
	}
	bool HasNameFact(TextId name, NameFact fact) const
	{
		return name < name_facts_.size() &&
			(name_facts_[name] & static_cast<std::uint8_t>(fact)) != 0;
	}
	void SetNameFact(TextId name, NameFact fact, bool enabled = true)
	{
		if (name >= name_facts_.size()) name_facts_.resize(name + 1, 0);
		const std::uint8_t before = name_facts_[name];
		const std::uint8_t bit = static_cast<std::uint8_t>(fact);
		const std::uint8_t after = enabled ?
			static_cast<std::uint8_t>(before | bit) :
			static_cast<std::uint8_t>(before & ~bit);
		if (before == after) return;
		name_fact_changes_.push_back(NameFactChange(name, before));
		name_facts_[name] = after;
		AdvanceNameFactRevision();
		if (stats_) ++stats_->parser_fact_changes;
	}
	void SetNameFact(const std::string& name, NameFact fact,
		bool enabled = true)
	{
		SetNameFact(strings_.Intern(name), fact, enabled);
	}
	void RestoreNameFacts(std::size_t mark) {
		while (name_fact_changes_.size() > mark)
		{
			const NameFactChange change = name_fact_changes_.back(); name_fact_changes_.pop_back();
			name_facts_[change.name] = change.previous; AdvanceNameFactRevision(); } }
	void ApplyFunctionParameterFacts(NodeId declarator) {
		parameter_names_.clear();
		arena_.AppendImmediateParameterNames(declarator, &parameter_names_);
		for (std::size_t i = 0; i < parameter_names_.size(); ++i)
		{ SetNameFact(parameter_names_[i], kKnownType, false);
			SetNameFact(parameter_names_[i], kKnownNonTemplate); } }
	std::runtime_error Error(const std::string& message) const
	{
		return std::runtime_error(message + " at token " +
			std::to_string(position_) +
			(position_ < tokens_.size() ? " (`" + Spelling(position_) + "`)" :
			 std::string()));
	}
	bool At(SimpleTokenKind kind) const
	{
		return position_ < tokens_.size() && tokens_[position_].Kind() ==
			static_cast<std::uint16_t>(kind);
	}
	bool AtOffset(std::size_t offset, SimpleTokenKind kind) const
	{
		return position_ + offset < tokens_.size() &&
			tokens_[position_ + offset].Kind() ==
				static_cast<std::uint16_t>(kind);
	}
	bool AtIdentifier() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].Kind() == kIdentifierToken;
	}
	bool AtLiteral() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].Kind() == kLiteralToken;
	}
	bool AtEof() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].Kind() == kEofToken;
	}
	bool AtCloseAngle() const
	{
		if (position_ >= tokens_.size()) return false;
		const std::uint16_t kind = tokens_[position_].Kind();
		return kind == static_cast<std::uint16_t>(OP_GT) ||
			kind == kRShiftFirstToken || kind == kRShiftSecondToken;
	}
	bool Match(SimpleTokenKind kind)
	{
		if (!At(kind)) return false;
		++position_;
		return true;
	}
	bool MatchCloseAngle()
	{
		if (!AtCloseAngle()) return false;
		++position_;
		return true;
	}
	void Expect(SimpleTokenKind kind)
	{
		if (!Match(kind))
			throw Error(std::string("expected ") + SimpleTokenKindName(kind));
	}
	void ExpectCloseAngle()
	{
		if (!MatchCloseAngle()) throw Error("expected close angle bracket");
	}
	const std::string& Spelling(std::size_t position) const
	{
		return strings_.Get(tokens_[position].spelling);
	}
	std::string TokenDescription(std::size_t position) const
	{
		const SyntaxToken& token = tokens_[position];
		if (token.Kind() == kIdentifierToken) return "TT_IDENTIFIER:" +
			Spelling(position);
		if (token.Kind() == kLiteralToken) return Spelling(position);
		if (token.Kind() == kRShiftFirstToken ||
			token.Kind() == kRShiftSecondToken) return "OP_RSHIFT:>>";
		return std::string(SimpleTokenKindName(
			static_cast<SimpleTokenKind>(token.Kind()))) + ":" +
			Spelling(position);
	}
	NodeId MakeTokenNode(const char* tag, std::size_t position)
	{
		const NodeId node = arena_.Make(tag, TokenDescription(position));
		arena_.SetSemanticPayload(node, tokens_[position].spelling);
		return node;
	}
	NodeId MakeStructuredNode(const char* tag, const std::string& spelling,
		NodeId structure)
	{
		const NodeId node = arena_.Make(tag, spelling);
		if (structure != kNoNode) arena_.Add(node, structure);
		return node;
	}
	std::string JoinSpellings(std::size_t first, std::size_t last) const
	{
		std::string result;
		for (std::size_t i = first; i < last; ++i)
		{
			if (i != first)
			{
				const std::uint16_t previous = tokens_[i - 1].Kind();
				if (previous == static_cast<std::uint16_t>(KW_CONST) ||
					previous == static_cast<std::uint16_t>(KW_VOLATILE) ||
					previous == static_cast<std::uint16_t>(KW_TYPENAME) ||
					previous == static_cast<std::uint16_t>(KW_TEMPLATE) ||
					previous == static_cast<std::uint16_t>(KW_CLASS) ||
					previous == static_cast<std::uint16_t>(KW_STRUCT) ||
					previous == static_cast<std::uint16_t>(KW_UNION) ||
					previous == static_cast<std::uint16_t>(KW_ENUM))
					result += ' ';
			}
			result += Spelling(i);
		}
		return result;
	}
	bool ParseOperatorFunctionSuffix()
	{
		if (At(KW_NEW) || At(KW_DELETE))
		{
			++position_;
			if (Match(OP_LSQUARE)) Expect(OP_RSQUARE);
			return true;
		}
		if (Match(OP_LPAREN))
		{
			Expect(OP_RPAREN);
			return true;
		}
		if (Match(OP_LSQUARE))
		{
			Expect(OP_RSQUARE);
			return true;
		}
		if (position_ + 1 < tokens_.size() &&
			tokens_[position_].Kind() == kRShiftFirstToken &&
			tokens_[position_ + 1].Kind() == kRShiftSecondToken)
		{
			position_ += 2;
			return true;
		}
		if (position_ < tokens_.size() &&
			IsOperatorNameToken(tokens_[position_].Kind()))
		{
			++position_;
			return true;
		}
		return false;
	}
	bool SkipBalanced(SimpleTokenKind open, SimpleTokenKind close)
	{
		if (!Match(open)) return false;
		std::size_t depth = 1;
		while (position_ < tokens_.size() && depth != 0)
		{
			if (At(open)) ++depth;
			else if (At(close)) --depth;
			if (AtEof()) return false;
			++position_;
		}
		return depth == 0;
	}
	bool SkipAttribute()
	{
		return SkipHostedAttributeSyntax();
	}
	void SkipAttributes()
	{
		while (SkipAttribute()) {}
	}
	NodeId ParseAlignmentSpecifier()
	{
		if (!At(KW_ALIGNAS)) return kNoNode;
		++position_;
		Expect(OP_LPAREN);
		const NodeId alignment = arena_.Make("alignment-specifier");
		arena_.AddFlags(alignment, SYNTAX_FLAG_SEMANTIC_ONLY);
		const Mark type_mark = Checkpoint();
		if (!ParseTypeId(alignment) || !At(OP_RPAREN))
		{
			Rollback(type_mark);
			const NodeId value = ParseExpression(2);
			if (value == kNoNode)
				throw Error("expected alignment type or expression");
			arena_.Add(alignment, value);
		}
		Expect(OP_RPAREN);
		Match(OP_DOTS);
		return alignment;
	}
	void ParseSemanticAttributes(std::vector<NodeId>* alignments)
	{
		while (true)
		{
			if (At(KW_ALIGNAS))
			{
				alignments->push_back(ParseAlignmentSpecifier());
				continue;
			}
			if (!ParseLeadingAttribute(alignments)) break;
		}
	}
	bool ParseConversionTypeName(NodeId* retained = 0)
	{
		NodeId conversion = kNoNode, specifiers = kNoNode,
			declarator = kNoNode;
		if (retained)
		{ *retained = kNoNode; conversion = arena_.Make("conversion-type-id");
			arena_.AddFlags(conversion, SYNTAX_FLAG_SEMANTIC_ONLY);
			specifiers = arena_.Make("decl-specifier-seq"); }
		while (At(KW_CONST) || At(KW_VOLATILE))
		{ const std::size_t qualifier = position_++;
			if (retained) arena_.Add(specifiers,
				MakeTokenNode("decl-specifier", qualifier)); }
		if (position_ < tokens_.size() &&
			IsFundamentalKind(tokens_[position_].Kind()))
		{
			do { const std::size_t fundamental = position_++;
				if (retained) arena_.Add(specifiers,
					MakeTokenNode("decl-specifier", fundamental)); }
			while (position_ < tokens_.size() &&
				IsFundamentalKind(tokens_[position_].Kind()));
		}
		else
		{
			std::string name;
			NodeId structure = kNoNode;
			if (!ParseName(&name, true, false, true,
				retained ? &structure : 0)) return false;
			if (retained)
			{
				const bool decorated = name.find("::") != std::string::npos ||
					name.find('<') != std::string::npos || name.find("decltype") == 0;
				const NodeId type = arena_.Make("decl-specifier",
					decorated ? name : "TT_IDENTIFIER:" + name);
				arena_.SetSemanticPayload(type, strings_.Intern(name));
				if (structure != kNoNode) arena_.Add(type, structure);
				arena_.Add(specifiers, type);
			}
		}
		while (At(KW_CONST) || At(KW_VOLATILE))
		{ const std::size_t qualifier = position_++;
			if (retained) arena_.Add(specifiers,
				MakeTokenNode("decl-specifier", qualifier)); }
		while (At(OP_STAR) || At(OP_AMP) || At(OP_LAND))
		{
			const std::size_t operation = position_++;
			if (retained)
			{
				if (declarator == kNoNode)
					declarator = arena_.Make("abstract-declarator");
				arena_.Add(declarator,
					MakeTokenNode("ptr-operator", operation));
			}
			while (At(KW_CONST) || At(KW_VOLATILE))
			{ const std::size_t qualifier = position_++;
				if (retained) arena_.Add(declarator,
					MakeTokenNode("cv-qualifier", qualifier)); }
		}
		if (retained)
		{ arena_.Add(conversion, specifiers);
			if (declarator != kNoNode) arena_.Add(conversion, declarator);
			*retained = conversion; }
		return true;
	}
	bool ParseName(std::string* text, bool allow_qualified = true, bool allow_operator = true,
		bool allow_template_arguments = true,
		NodeId* structure = 0, TextId* terminal_identifier = 0,
		NodeId* conversion_type = 0)
	{
		const Mark mark = Checkpoint();
		const std::size_t first = position_;
		const bool global = allow_qualified && Match(OP_COLON2);
		std::vector<TextId> component_names;
		std::vector<NodeId> component_arguments;
		bool retained_arguments = false;
		if (structure) *structure = kNoNode;
		if (terminal_identifier) *terminal_identifier = 0;
		if (conversion_type) *conversion_type = kNoNode;
		bool operator_component = false; NodeId operator_arguments = kNoNode;
		if (At(KW_OPERATOR) && allow_operator)
		{
			operator_component = true;
			++position_;
			if (AtLiteral())
			{
				++position_;
				if (!AtIdentifier())
				{
					Rollback(mark);
					return false;
				}
				++position_;
			}
			else if (!ParseOperatorFunctionSuffix())
			{
				if (!ParseConversionTypeName(conversion_type))
				{
					Rollback(mark);
					return false;
				}
			}
		}
		else
		{
			const bool destructor = Match(OP_COMPL);
			if (!AtIdentifier())
			{
				Rollback(mark);
				return false;
			}
			const TextId identifier = tokens_[position_++].spelling;
			if (terminal_identifier) *terminal_identifier = identifier;
			if (structure)
			{
				component_names.push_back(destructor ? strings_.Intern(
					"~" + strings_.Get(identifier)) : identifier);
				component_arguments.push_back(kNoNode);
			}
		}
		if (allow_template_arguments)
		{
			const NodeId arguments = TryConsumeTemplateArguments(structure != 0);
			if (structure && arguments != kNoNode)
			{
				if (operator_component) operator_arguments = arguments;
				else if (!component_arguments.empty()) component_arguments.back() = arguments;
				retained_arguments = true;
			}
		}
		if (allow_qualified)
		{
			while (Match(OP_COLON2))
			{
				Match(KW_TEMPLATE);
				const bool destructor = Match(OP_COMPL);
				if (At(KW_OPERATOR) && allow_operator)
				{
					operator_component = true;
					++position_;
					if (!ParseOperatorFunctionSuffix() &&
						!ParseConversionTypeName(conversion_type))
					{
						Rollback(mark);
						return false;
					}
				}
				else if (AtIdentifier())
				{
					const TextId identifier = tokens_[position_++].spelling;
					if (terminal_identifier) *terminal_identifier = identifier;
					if (structure)
					{
						component_names.push_back(destructor ? strings_.Intern(
							"~" + strings_.Get(identifier)) : identifier);
						component_arguments.push_back(kNoNode);
					}
				}
				else
				{
					Rollback(mark);
					return false;
				}
				if (allow_template_arguments)
				{
					const NodeId arguments =
						TryConsumeTemplateArguments(structure != 0);
					if (structure && arguments != kNoNode)
					{
						if (operator_component) operator_arguments = arguments;
						else if (!component_arguments.empty()) component_arguments.back() = arguments;
						retained_arguments = true;
					}
				}
			}
		}
		*text = JoinSpellings(first, position_);
		const std::size_t conversion = text->rfind("::operator");
		if (conversion != std::string::npos)
		{
			const std::size_t after = conversion +
				std::string("::operator").size();
			if (after < text->size() && (*text)[after] != ' ' &&
				(std::isalnum(static_cast<unsigned char>((*text)[after])) ||
				 (*text)[after] == '_'))
				text->insert(after, " ");
		}
		if (structure && operator_component)
		{
			const std::size_t operation = text->rfind("::operator");
			const std::string terminal = operation == std::string::npos ? *text : text->substr(operation + 2);
			std::string canonical;
			for (std::size_t i = 0; i < terminal.size() && (operator_arguments == kNoNode || terminal[i] != '<'); ++i)
				if (!std::isspace(static_cast<unsigned char>(terminal[i]))) canonical += terminal[i];
			component_names.push_back(strings_.Intern(canonical)); component_arguments.push_back(operator_arguments);
		}
		if (structure && (retained_arguments || global ||
			component_names.size() > 1))
		{
			const NodeId name = arena_.Make("structured-type-name");
			arena_.AddFlags(name, SYNTAX_FLAG_SEMANTIC_ONLY);
			if (global) arena_.Add(name, arena_.Make("global-qualifier"));
			for (std::size_t i = 0; i < component_names.size(); ++i)
			{
				const NodeId component = arena_.Make("name-component",
					strings_.Get(component_names[i]));
				arena_.SetSemanticPayload(component, component_names[i]);
				if (component_arguments[i] != kNoNode)
					arena_.Add(component, component_arguments[i]);
				arena_.Add(name, component);
			}
			*structure = name;
		}
		return true;
	}
	bool TemplateArgumentStartsType()
	{
		if (StartsHostedType(position_)) return true;
		if (At(KW_TYPENAME) || At(KW_DECLTYPE) || At(KW_CLASS) ||
			At(KW_STRUCT) || At(KW_UNION) || At(KW_ENUM) || At(KW_CONST) ||
			At(KW_VOLATILE)) return true;
		if (position_ < tokens_.size() &&
			IsFundamentalKind(tokens_[position_].Kind()))
			return !At(KW_BOOL) || !AtOffset(1, OP_LPAREN) ||
				AtOffset(2, OP_RPAREN) ||
				StartsParenthesizedMemberPointerDeclarator(1);
		if (At(OP_COLON2) ||
			(AtIdentifier() && AtOffset(1, OP_COLON2)))
			return !StartsQualifiedCallExpression() && QualifiedStartsType();
		if (!AtIdentifier()) return false;
		if (AtOffset(1, OP_LBRACE)) return false;
		const TextId name = tokens_[position_].spelling;
		if (HasNameFact(name, kActiveNonTypeParameter)) return false;
		if (!HasNameFact(name, kKnownType) && AtOffset(1, OP_LPAREN)) return false;
		if (HasNameFact(name, kKnownTemplate) &&
			!HasNameFact(name, kActiveNonTypeParameter)) return true;
		if (HasNameFact(name, kKnownNonTemplate) &&
			!HasNameFact(name, kKnownType)) return false;
		return HasNameFact(name, kKnownType) ||
			HasNameFact(name, kKnownTemplate) ||
			IsLikelyTypeIdentifier(position_);
	}
	NodeId ParseMatchedTemplateTypeArguments(std::size_t opener,
		std::size_t after)
	{
		const std::size_t saved = position_;
		position_ = opener + 1;
		const Mark mark = Checkpoint();
		const NodeId list = arena_.Make("template-type-argument-list");
		bool valid = true;
		if (!AtCloseAngle())
		{
			while (true)
			{
				bool parse_expression = !TemplateArgumentStartsType();
				if (!parse_expression)
				{
					const Mark argument_mark = Checkpoint();
					++retained_template_argument_depth_;
					const bool parsed_type = ParseTypeId(list);
					--retained_template_argument_depth_;
					if (!parsed_type ||
						(!At(OP_COMMA) && !AtCloseAngle()))
					{
						Rollback(argument_mark);
						parse_expression = true;
					}
				}
				if (parse_expression)
				{
					++angle_stop_depth_;
					const NodeId expression = ParseExpression(2);
					--angle_stop_depth_;
					if (expression == kNoNode) valid = false;
					else arena_.Add(list, ParsePackExpansion(expression));
				}
				if (!Match(OP_COMMA)) break;
			}
		}
		if (!MatchCloseAngle() || position_ != after || !valid)
		{
			Rollback(mark);
			position_ = saved;
			return kNoNode;
		}
		position_ = saved;
		return list;
	}
	void RecordTemplateArgumentScan(std::size_t scan_start, bool failed)
	{
		if (!stats_) return;
		const std::size_t scanned = position_ - scan_start;
		stats_->template_argument_scan_tokens += scanned;
		stats_->max_template_argument_scan_tokens = std::max(
		stats_->max_template_argument_scan_tokens, scanned);
		if (failed) ++stats_->failed_template_argument_scans;
	}
	NodeId ParseDeclaration(bool in_class);
	NodeId ParseDeclarationCore(bool in_class);
	bool ParseLeadingAttribute(std::vector<NodeId>* attributes);
	NodeId ParseNamespace();
	NodeId ParseUsing();
	NodeId ParseTemplate(bool in_class);
	NodeId ParseTemplateParameter();
	NodeId ParseTypeTemplateParameter();
	NodeId ParseNonTypeTemplateParameter();
	NodeId ParseNestedTemplateParameterClause();
	NodeId ParseClass(bool require_semicolon = true);
	NodeId ParseEnum(bool require_semicolon = true);
	NodeId ParseStaticAssert();
	NodeId ParseSimpleOrFunction(bool in_class, bool special_only = false);
	NodeId FinishSimpleOrFunction(const Mark& mark,
		std::size_t specifier_first, std::size_t specifier_last,
		NodeId specifiers);
	NodeId ParseSpecialMember(bool definition);
	NodeId ParseCompoundStatement();
	NodeId ParseStatement();
	NodeId ParseExpression(int minimum_precedence = 1);
	NodeId ParseUnaryExpression();
	NodeId ParsePostfixExpression();
	NodeId ParsePostfixSuffixes(NodeId value);
	NodeId ParsePrimaryExpression();
	NodeId ParseInitializer();
	NodeId ParseBracedInitList();
	NodeId ParsePackExpansion(NodeId value);
	NodeId ParseParameterClause();
	NodeId ParseDeclarator(bool abstract, std::string* name = 0);
	NodeId ParseDeclSpecifierSeq(bool for_type_id, std::string* first_type = 0);
	bool ParseTypeId(NodeId parent, bool attach = true);
	NodeId ParseCtorInitializer();
	NodeId ParseCondition(SimpleTokenKind terminator = OP_RPAREN);
	int BinaryPrecedence(std::uint16_t kind) const;
	bool StartsStandaloneEnumDeclaration() const;
	const std::vector<SyntaxToken>& tokens_;
	StringTable& strings_;
	SyntaxArena& arena_;
	SyntaxStats* stats_;
	std::size_t position_, angle_stop_depth_, compound_depth_, retained_template_argument_depth_;
	std::uint32_t name_fact_revision_;
	std::vector<std::uint8_t> name_facts_;
	std::vector<NameFactChange> name_fact_changes_;
	std::vector<AngleMatch> angle_matches_;
	std::vector<TextId> last_declared_names_, parameter_names_,
		active_non_type_parameter_names_, current_classes_;
};
NodeId Parser::ParseDeclSpecifierSeq(bool for_type_id, std::string* first_type)
{
	const Mark mark = Checkpoint();
	const NodeId sequence = arena_.Make(for_type_id ? "type-specifier-seq" : "decl-specifier-seq");
	bool consumed = false;
	bool saw_type = false;
	bool saw_user_type = false;
	bool saw_int128 = false;
	while (true)
	{
		if (AtHostedAttribute())
		{
			std::vector<NodeId> attributes;
			while (ParseLeadingAttribute(&attributes)) {}
			for (std::size_t i = 0; i < attributes.size(); ++i)
				arena_.Add(sequence, attributes[i]);
		}
		if (position_ >= tokens_.size()) break;
		const std::size_t token_position = position_;
		const std::uint16_t kind = tokens_[position_].Kind();
		if (TryParseHostedDeclSpecifier(sequence, for_type_id, &consumed,
			&saw_type, &saw_user_type, &saw_int128, first_type)) continue;
		if (!saw_type && TryParseBuiltinTransformSpecifier(sequence))
		{
			if (first_type && first_type->empty())
				*first_type = Spelling(token_position);
			consumed = true;
			saw_type = true;
			saw_user_type = true;
			continue;
		}
		if (kind < kSimpleTokenCount && IsFundamentalKind(kind))
		{
			if (saw_user_type && !(saw_int128 &&
				(kind == static_cast<std::uint16_t>(KW_SIGNED) ||
				 kind == static_cast<std::uint16_t>(KW_UNSIGNED)))) break;
			++position_;
			arena_.Add(sequence, MakeTokenNode(for_type_id ?
				"type-specifier" : "decl-specifier",
				token_position));
			if (first_type && first_type->empty())
				*first_type = Spelling(token_position);
			consumed = true;
			saw_type = true;
			continue;
		}
		if (kind < kSimpleTokenCount &&
			(kind == static_cast<std::uint16_t>(KW_CONST) ||
			 kind == static_cast<std::uint16_t>(KW_VOLATILE)))
		{
			++position_;
			arena_.Add(sequence, MakeTokenNode(for_type_id ?
				"cv-qualifier" : "decl-specifier",
				token_position));
			consumed = true;
			continue;
		}
		if (!for_type_id && IsDeclSpecifierKeyword(kind))
		{
			++position_;
			arena_.Add(sequence, MakeTokenNode("decl-specifier",
				token_position));
			consumed = true;
			if (kind == static_cast<std::uint16_t>(KW_AUTO)) saw_type = true;
			continue;
		}
		const bool typename_decltype = !saw_type && At(KW_TYPENAME) &&
			AtOffset(1, KW_DECLTYPE);
		if (typename_decltype) ++position_;
		if (!saw_type && At(KW_DECLTYPE))
		{
			const std::size_t first = position_++;
			Expect(OP_LPAREN);
			const NodeId expression = ParseExpression();
			if (expression == kNoNode) throw Error("expected decltype expression");
			Expect(OP_RPAREN);
			const NodeId qualified =
				ParseDecltypeQualifiedName("qualified-type-name");
			const std::string rendered = (typename_decltype ?
				"typename " : std::string()) +
				JoinSpellings(first, position_);
			const NodeId node = arena_.Make(for_type_id ?
				"decltype-specifier" : "decl-specifier", rendered);
			arena_.Add(node, expression);
			arena_.Add(node, qualified);
			arena_.Add(sequence, node);
			if (first_type && first_type->empty()) *first_type = rendered;
			consumed = true;
			saw_type = true;
			saw_user_type = true;
			continue;
		}
		if (!saw_type && Match(KW_TYPENAME))
		{
			std::string name;
			NodeId structure = kNoNode;
			if (!ParseName(&name, true, true, true, &structure))
			{
				Rollback(mark);
				return kNoNode;
			}
			const NodeId dependent_type = MakeStructuredNode(for_type_id ?
				"type-name" : "decl-specifier", name, structure);
			arena_.AddFlags(dependent_type, SYNTAX_FLAG_TYPENAME);
			arena_.Add(sequence, dependent_type);
			if (first_type && first_type->empty()) *first_type = name;
			consumed = true;
			saw_type = true;
			saw_user_type = true;
			continue;
		}
		if (!saw_type && At(KW_ENUM))
		{
			const NodeId node = ParseEnum(false);
			if (node == kNoNode) break;
			arena_.Add(sequence, node);
			consumed = true;
			saw_type = true;
			saw_user_type = true;
			continue;
		}
		if (!saw_type && (At(KW_STRUCT) || At(KW_CLASS) || At(KW_UNION)))
		{
			const NodeId node = ParseClass(false);
			if (node == kNoNode) break;
			arena_.Add(sequence, node);
			consumed = true;
			saw_type = true;
			saw_user_type = true;
			continue;
		}
		if (!saw_type && (AtIdentifier() || At(OP_COLON2)))
		{
			const Mark name_mark = Checkpoint();
			std::string name;
			NodeId structure = kNoNode;
			if (!ParseName(&name, true, true, true, &structure))
			{
				Rollback(name_mark);
				break;
			}
			const bool decorated = name.find("::") != std::string::npos ||
				name.find('<') != std::string::npos ||
				name.find("decltype") == 0;
			const NodeId name_node = arena_.Make(for_type_id ? "type-name" :
				"decl-specifier", for_type_id || decorated ? name :
				"TT_IDENTIFIER:" + name);
			arena_.SetSemanticPayload(name_node, strings_.Intern(name));
			if (structure != kNoNode) arena_.Add(name_node, structure);
			arena_.Add(sequence, name_node);
			if (first_type && first_type->empty()) *first_type = name;
			consumed = true;
			saw_type = true;
			saw_user_type = true;
			continue;
		}
		break;
	}
	if (!consumed || !saw_type)
	{
		Rollback(mark);
		return kNoNode;
	}
	return sequence;
}
bool Parser::ParseTypeId(NodeId parent, bool attach)
{ if (TryParseBuiltinTransformTypeId(parent, attach)) return true;
	const Mark mark = Checkpoint();
	const NodeId type_id = arena_.Make("type-id");
	std::string type_name;
	const NodeId specifiers = ParseDeclSpecifierSeq(true, &type_name);
	if (specifiers == kNoNode)
	{
		Rollback(mark);
		return false;
	}
	arena_.Add(type_id, specifiers);
	const Mark declarator_mark = Checkpoint();
	const NodeId declarator = ParseDeclarator(true);
	if (declarator != kNoNode) arena_.Add(type_id, declarator);
	else Rollback(declarator_mark);
	if (attach) arena_.Add(parent, type_id);
	return true;
}
NodeId Parser::ParseParameterClause()
{
	if (!Match(OP_LPAREN)) return kNoNode;
	const NodeId clause = arena_.Make("parameter-clause");
	if (Match(OP_RPAREN)) return clause;
	if (Match(OP_DOTS))
	{
		arena_.Add(clause, arena_.Make("parameter-pack", "..."));
		Expect(OP_RPAREN);
		return clause;
	}
	while (true)
	{
		const NodeId parameter = arena_.Make("parameter-declaration");
		std::string parameter_type;
		const NodeId specifiers = ParseDeclSpecifierSeq(false, &parameter_type);
		if (specifiers == kNoNode) throw Error("expected parameter declaration");
		arena_.Add(parameter, specifiers);
		bool pack_before_name = Match(OP_DOTS);
		const Mark declarator_mark = Checkpoint();
		std::string name;
		NodeId declarator = kNoNode;
		if (At(OP_LPAREN) && position_ + 2 < tokens_.size() &&
			tokens_[position_ + 1].Kind() == kIdentifierToken &&
			tokens_[position_ + 2].Kind() ==
				static_cast<std::uint16_t>(OP_RPAREN))
		{
			position_ += 1;
			const std::string inner_type = Spelling(position_++);
			Expect(OP_RPAREN);
			declarator = arena_.Make("declarator");
			const NodeId inner_clause = arena_.Make("parameter-clause");
			const NodeId inner_parameter = arena_.Make("parameter-declaration");
			const NodeId inner_specifiers = arena_.Make("decl-specifier-seq");
			const NodeId inner_specifier = arena_.Make("decl-specifier",
				"TT_IDENTIFIER:" + inner_type);
			arena_.SetSemanticPayload(inner_specifier,
				strings_.Intern(inner_type));
			arena_.Add(inner_specifiers, inner_specifier);
			arena_.Add(inner_parameter, inner_specifiers);
			arena_.Add(inner_clause, inner_parameter);
			arena_.Add(declarator, inner_clause);
		}
		else declarator = ParseDeclarator(false, &name);
		if (declarator == kNoNode)
		{
			Rollback(declarator_mark);
			declarator = ParseDeclarator(true, &name);
		}
		if (declarator != kNoNode)
		{
			if (pack_before_name)
				arena_.Prepend(declarator, arena_.Make("parameter-pack", "..."));
			arena_.Add(parameter, declarator);
			if (!name.empty()) arena_.SetSemanticPayload(
				parameter, strings_.Intern(name));
		}
		else if (pack_before_name)
		{
			const NodeId pack_declarator = arena_.Make("declarator");
			arena_.Add(pack_declarator, arena_.Make("parameter-pack", "..."));
			arena_.Add(parameter, pack_declarator);
		}
		SkipAttributes();
		if (Match(OP_ASS))
		{
			const NodeId default_argument = arena_.Make("default-argument");
			const NodeId initializer = arena_.Make("initializer");
			const NodeId value = ParseExpression(2);
			if (value == kNoNode) throw Error("expected default argument");
			arena_.Add(initializer, value);
			arena_.Add(default_argument, initializer);
			arena_.Add(parameter, default_argument);
		}
		arena_.Add(clause, parameter);
		if (!Match(OP_COMMA)) break;
		if (Match(OP_DOTS))
		{
			arena_.Add(clause, arena_.Make("parameter-pack", "..."));
			break;
		}
	}
	Expect(OP_RPAREN);
	return clause;
}
NodeId Parser::ParseDeclarator(bool abstract, std::string* name)
{
	const Mark mark = Checkpoint();
	const NodeId result = arena_.Make(abstract ?
		"abstract-declarator" : "declarator");
	bool consumed = false;
	while (true)
	{
		const std::size_t operator_position = position_;
		if (Match(OP_STAR) || Match(OP_AMP) || Match(OP_LAND) || Match(OP_XOR))
		{
			const NodeId pointer = MakeTokenNode("ptr-operator",
				operator_position);
			arena_.Add(result, pointer);
			while (At(KW_CONST) || At(KW_VOLATILE))
			{
				const std::size_t qualifier = position_++;
				arena_.Add(result, MakeTokenNode("cv-qualifier", qualifier));
			}
			while (SkipHostedTypeAnnotations() || SkipAttribute()) {}
			consumed = true;
			continue;
		}
		// A qualified member pointer is one pointer operation in the AST view.
		const NodeId member_pointer = ParseMemberPointerOperator();
		if (member_pointer != kNoNode)
		{
			arena_.Add(result, member_pointer);
			while (At(KW_CONST) || At(KW_VOLATILE))
			{
				const std::size_t qualifier = position_++;
				arena_.Add(result, MakeTokenNode("cv-qualifier", qualifier));
			}
			while (SkipHostedTypeAnnotations() || SkipAttribute()) {}
			consumed = true;
			continue;
		}
		break;
	}
	if (Match(OP_DOTS))
	{
		arena_.Add(result, arena_.Make("parameter-pack", "..."));
		consumed = true;
	}
	const bool abstract_function_suffix = abstract && At(OP_LPAREN) &&
		!StartsParenthesizedMemberPointerDeclarator() &&
		(AtOffset(1, OP_RPAREN) || AtOffset(1, OP_DOTS) ||
		 IsLikelyTypeIdentifier(position_ + 1) ||
		 QualifiedStartsTypeAt(position_ + 1) ||
		 (position_ + 1 < tokens_.size() &&
		  (IsTypeSpecifierStartKind(tokens_[position_ + 1].Kind()) ||
		   StartsHostedType(position_ + 1))));
	if (!abstract_function_suffix && Match(OP_LPAREN))
	{
		const NodeId nested_declarator = arena_.Make("nested-declarator");
		std::string nested_name;
		const NodeId nested = ParseDeclarator(abstract, &nested_name);
		if (nested == kNoNode)
		{
			Rollback(mark);
			return kNoNode;
		}
		if (!Match(OP_RPAREN))
		{
			Rollback(mark);
			return kNoNode;
		}
		arena_.Add(nested_declarator, nested);
		arena_.Add(result, nested_declarator);
		if (name) *name = nested_name;
		consumed = true;
	}
	else if (!abstract && ParseStructuredBindingDeclarator(result))
		consumed = true;
	else if (!abstract)
	{
		const Mark name_mark = Checkpoint();
		std::string parsed_name;
		NodeId name_structure = kNoNode;
		NodeId conversion_type = kNoNode;
		if (ParseName(&parsed_name, true, true, true, &name_structure, 0,
			&conversion_type))
		{
			if (conversion_type != kNoNode)
				arena_.Add(result, conversion_type);
			if (name) *name = parsed_name;
			arena_.Add(result, MakeStructuredNode(
				"identifier", parsed_name, name_structure));
			consumed = true;
		}
		else Rollback(name_mark);
	}
	while (true)
	{
		if (TryParseGnuAsmLabel(result)) { consumed = true; continue; }
		if (AtHostedAttribute())
		{
			std::vector<NodeId> attributes;
			while (ParseLeadingAttribute(&attributes)) {}
			for (std::size_t i = 0; i < attributes.size(); ++i)
				arena_.Add(result, attributes[i]);
			continue;
		}
		if (At(OP_LPAREN))
		{
			const bool parameter_like = AtOffset(1, OP_RPAREN) ||
				AtOffset(1, OP_DOTS) ||
				(position_ + 1 < tokens_.size() &&
					 (IsTypeSpecifierStartKind(
						tokens_[position_ + 1].Kind()) ||
					  StartsHostedType(position_ + 1) ||
					  (!StartsQualifiedCallArgument() && (IsLikelyTypeIdentifier(position_ + 1) ||
					   QualifiedStartsTypeAt(position_ + 1)))));
			if (!parameter_like) break;
			const Mark parameter_mark = Checkpoint(); NodeId parameters = kNoNode;
			try { parameters = ParseParameterClause(); }
			catch (const std::runtime_error&) { Rollback(parameter_mark); if (abstract || !consumed) throw; break; }
			arena_.Add(result, parameters);
			consumed = true;
			while (true)
			{
				std::vector<NodeId> attributes;
				while (ParseLeadingAttribute(&attributes)) {}
				for (std::size_t i = 0; i < attributes.size(); ++i)
					arena_.Add(result, attributes[i]);
				if (At(KW_CONST) || At(KW_VOLATILE))
				{
					const std::size_t qualifier = position_++;
					arena_.Add(result, MakeTokenNode("cv-qualifier", qualifier));
					continue;
				}
				if (At(OP_AMP) || At(OP_LAND))
				{
					const std::size_t qualifier = position_++;
					arena_.Add(result, MakeTokenNode("ref-qualifier", qualifier));
					continue;
				}
				if (Match(KW_NOEXCEPT))
				{
					const std::size_t first = position_ - 1;
					if (Match(OP_LPAREN))
					{
						const NodeId expression = ParseExpression();
						Expect(OP_RPAREN);
						const NodeId qualifier = arena_.Make(
							"function-qualifier",
							JoinSpellings(first, position_));
						arena_.Add(qualifier, expression);
						arena_.Add(result, qualifier);
					}
					else arena_.Add(result,
						arena_.Make("function-qualifier", "noexcept"));
					continue;
				}
				if (Match(KW_THROW))
				{
					const std::size_t first = position_ - 1; Expect(OP_LPAREN);
					const NodeId types = arena_.Make("exception-type-list"); arena_.AddFlags(types, SYNTAX_FLAG_SEMANTIC_ONLY);
					if (!Match(OP_RPAREN)) { do { if (!ParseTypeId(types)) throw Error("expected exception type-id"); } while (Match(OP_COMMA)); Expect(OP_RPAREN); }
					const NodeId qualifier = arena_.Make("function-qualifier", JoinSpellings(first, position_));
					arena_.Add(qualifier, types); arena_.Add(result, qualifier);
					continue;
				}
				if (AtIdentifier() &&
					(Spelling(position_) == "override" ||
					 Spelling(position_) == "final"))
				{
					const std::size_t specifier = position_++;
					arena_.Add(result, MakeTokenNode("virt-specifier", specifier));
					continue;
				}
				if (Match(OP_ARROW))
				{
					const std::size_t type_first = position_;
					const NodeId trailing = arena_.Make("trailing-return-type");
					if (!ParseTypeId(trailing))
						throw Error("expected trailing return type");
					arena_.SetPayload(trailing,
						JoinSpellings(type_first, position_));
					arena_.Add(result, trailing);
					continue;
				}
				break;
			}
			continue;
		}
		if (Match(OP_LSQUARE))
		{
			const NodeId suffix = arena_.Make("array-suffix");
			if (!At(OP_RSQUARE))
			{
				const NodeId bound = ParseExpression();
				if (bound == kNoNode) throw Error("expected array bound");
				arena_.Add(suffix, bound);
			}
			Expect(OP_RSQUARE);
			SkipAttributes();
			arena_.Add(result, suffix);
			consumed = true;
			continue;
		}
		break;
	}
	if (Match(OP_DOTS))
	{
		arena_.Add(result, arena_.Make("parameter-pack", "..."));
		consumed = true;
	}
	if (!consumed)
	{
		Rollback(mark);
		return kNoNode;
	}
	return result;
}
int Parser::BinaryPrecedence(std::uint16_t kind) const
{
	if (kind == kRShiftFirstToken) return 10;
	if (kind >= kSimpleTokenCount) return 0;
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case OP_COMMA: return 1;
	case OP_ASS: case OP_PLUSASS: case OP_MINUSASS: case OP_STARASS:
	case OP_DIVASS: case OP_MODASS: case OP_XORASS: case OP_BANDASS:
	case OP_BORASS: case OP_LSHIFTASS: case OP_RSHIFTASS: return 2;
	case OP_LOR: return 4;
	case OP_LAND: return 5;
	case OP_BOR: return 6;
	case OP_XOR: return 7;
	case OP_AMP: return 8;
	case OP_EQ: case OP_NE: return 9;
	case OP_LT: case OP_GT: case OP_LE: case OP_GE: return 10;
	case OP_LSHIFT: return 11;
	case OP_PLUS: case OP_MINUS: return 12;
	case OP_STAR: case OP_DIV: case OP_MOD: return 13;
	case OP_DOTSTAR: case OP_ARROWSTAR: return 14;
	default: return 0;
	}
}
NodeId Parser::ParseExpression(int minimum_precedence)
{
	if (Match(KW_THROW))
	{
		const NodeId result = arena_.Make("throw-expression");
		const NodeId operand = ParseExpression(2);
		if (operand != kNoNode) arena_.Add(result, operand);
		return result;
	}
	NodeId left = ParseUnaryExpression();
	if (left == kNoNode) return kNoNode;
	while (position_ < tokens_.size())
	{
		if (At(OP_QMARK) && minimum_precedence <= 3)
		{
			++position_;
			const NodeId middle = ParseExpression(1);
			if (middle == kNoNode) throw Error("expected conditional operand");
			Expect(OP_COLON);
			const NodeId right = ParseExpression(2);
			if (right == kNoNode) throw Error("expected conditional operand");
			const NodeId conditional = arena_.Make("conditional-expression");
			arena_.Add(conditional, left);
			arena_.Add(conditional, middle);
			arena_.Add(conditional, right);
			left = conditional;
			continue;
		}
		const std::uint16_t kind = tokens_[position_].Kind();
		if (angle_stop_depth_ != 0 &&
			(kind == static_cast<std::uint16_t>(OP_GT) ||
			 kind == kRShiftFirstToken || kind == kRShiftSecondToken)) break;
		const int precedence = BinaryPrecedence(kind);
		if (precedence == 0 || precedence < minimum_precedence) break;
		const std::size_t operator_position = position_;
		if (kind == kRShiftFirstToken)
		{
			if (position_ + 1 >= tokens_.size() ||
				tokens_[position_ + 1].Kind() != kRShiftSecondToken) break;
			position_ += 2;
		}
		else ++position_;
		if (At(OP_DOTS))
		{
			++position_; const bool binary_fold = kind == kRShiftFirstToken ? position_ + 1 < tokens_.size() && tokens_[position_].Kind() == kRShiftFirstToken && tokens_[position_ + 1].Kind() == kRShiftSecondToken : position_ < tokens_.size() && tokens_[position_].Kind() == kind;
			if (binary_fold) position_ += kind == kRShiftFirstToken ? 2 : 1;
			const NodeId right = binary_fold ? ParseExpression(precedence + 1) : kNoNode;
			if (binary_fold && right == kNoNode) throw Error("expected fold operand");
			const std::string operation = kind == kRShiftFirstToken ? ">>" : Spelling(operator_position); const NodeId fold = arena_.Make("fold-expression", operation); arena_.SetSemanticPayload(fold, strings_.Intern(operation));
			arena_.Add(fold, arena_.Make(binary_fold ? "fold-binary" : "fold-right")); arena_.Add(fold, left);
			if (binary_fold) arena_.Add(fold, right);
			left = fold; continue;
		}
		const bool right_associative = IsAssignmentOperator(kind);
		NodeId right = ParseExpression(right_associative ? precedence :
			precedence + 1);
		if (right == kNoNode) throw Error("expected binary operand");
		const std::string description = kind == kRShiftFirstToken ?
			"OP_RSHIFT:>>" : std::string(SimpleTokenKindName(
				static_cast<SimpleTokenKind>(kind))) + ":" +
				Spelling(operator_position);
		const NodeId expression = arena_.Make(right_associative ?
			"assignment-expression" : "binary-expression", description);
		arena_.SetSemanticPayload(expression, strings_.Intern(
			kind == kRShiftFirstToken ? ">>" : Spelling(operator_position)));
		arena_.Add(expression, left);
		arena_.Add(expression, right);
		left = expression;
	}
	return left;
}
NodeId Parser::ParseBracedInitList()
{
	if (!Match(OP_LBRACE)) return kNoNode;
	const NodeId list = arena_.Make("braced-init-list");
	if (Match(OP_RBRACE)) return list;
	while (true)
	{
		NodeId value = TryParseDesignatedInitializer();
		if (value == kNoNode) value = At(OP_LBRACE) ?
			ParseBracedInitList() : ParseExpression(2);
		if (value == kNoNode) throw Error("expected braced initializer");
		value = ParsePackExpansion(value);
		arena_.Add(list, value);
		if (!Match(OP_COMMA)) break;
		if (At(OP_RBRACE)) break;
	}
	Expect(OP_RBRACE);
	return list;
}
NodeId Parser::ParsePackExpansion(NodeId value)
{
	if (!Match(OP_DOTS)) return value;
	const NodeId pack = arena_.Make("pack-expansion-expression");
	arena_.Add(pack, value);
	return pack;
}
NodeId Parser::ParsePrimaryExpression()
{
	if (AtLiteral()) {
		const std::size_t token = position_++;
		const NodeId literal = arena_.Make("literal", Spelling(token));
		arena_.SetLiteralFact(literal, tokens_[token].LiteralFact()); return literal;
	}
	if (At(KW_TRUE) || At(KW_FALSE) || At(KW_NULLPTR) || At(KW_THIS))
	{
		const std::size_t token = position_++;
		return MakeTokenNode("keyword-literal", token);
	}
	if (At(OP_LBRACE)) return ParseBracedInitList();
	if (Match(KW_TYPENAME))
	{
		std::string name;
		NodeId structure = kNoNode;
		if (!ParseName(&name, true, true, true, &structure))
			throw Error("expected dependent type name");
		const NodeId result = MakeStructuredNode(
			"id-expression", name, structure);
		arena_.AddFlags(result, SYNTAX_FLAG_TYPENAME);
		return result;
	}
	if (At(OP_LSQUARE))
		return ParseLambdaExpression();
	if (At(OP_LPAREN))
	{
		const NodeId statement_expression = ParseStatementExpression();
		if (statement_expression != kNoNode) return statement_expression;
		const Mark cast_mark = Checkpoint();
		++position_;
		const NodeId cast = arena_.Make("cast-expression", "OP_LPAREN:");
		const bool type_like = At(KW_CONST) || At(KW_VOLATILE) ||
			At(KW_STRUCT) || At(KW_CLASS) || At(KW_UNION) ||
			(position_ < tokens_.size() &&
			 IsFundamentalKind(tokens_[position_].Kind())) ||
			StartsHostedType(position_) || IsLikelyTypeIdentifier(position_) ||
			QualifiedStartsType();
		if (type_like && ParseTypeId(cast) && Match(OP_RPAREN) &&
			!At(OP_SEMICOLON) && !At(OP_COMMA) && !At(OP_RPAREN) &&
			!At(OP_RSQUARE) && !At(OP_COLON))
		{
			const NodeId operand = ParseUnaryExpression();
			if (operand != kNoNode)
			{
				arena_.Add(cast, operand);
				return cast;
			}
		}
		Rollback(cast_mark);
		Expect(OP_LPAREN);
		if (Match(OP_DOTS))
		{
			const std::size_t operation_position = position_; const std::uint16_t operation_kind = tokens_[position_++].Kind();
			if (operation_kind == kRShiftFirstToken) { if (position_ >= tokens_.size() || tokens_[position_].Kind() != kRShiftSecondToken) throw Error("invalid fold operator"); ++position_; }
			const int precedence = BinaryPrecedence(operation_kind); if (precedence == 0) throw Error("expected fold operator");
			const NodeId pattern = ParseExpression(precedence + 1); if (pattern == kNoNode) throw Error("expected fold operand");
			Expect(OP_RPAREN); const std::string operation = operation_kind == kRShiftFirstToken ? ">>" : Spelling(operation_position); const NodeId fold = arena_.Make("fold-expression", operation); arena_.SetSemanticPayload(fold, strings_.Intern(operation));
			arena_.Add(fold, arena_.Make("fold-left")); arena_.Add(fold, pattern); return fold;
		}
		const bool nested_template_argument = angle_stop_depth_ != 0;
		if (nested_template_argument) --angle_stop_depth_;
		const NodeId expression = ParseExpression();
		if (nested_template_argument) ++angle_stop_depth_;
		if (expression == kNoNode) throw Error("expected parenthesized expression");
		Expect(OP_RPAREN);
		const NodeId parenthesized = arena_.Make("parenthesized-expression");
		arena_.Add(parenthesized, expression);
		return parenthesized;
	}
	if (At(KW_DECLTYPE))
		return ParseDecltypeValueName();
	if (AtIdentifier() && AtOffset(1, OP_LPAREN))
	{
		const NodeId trait = ParseBuiltinTypeTraitExpression();
		if (trait != kNoNode) return trait;
	}
	if (AtIdentifier() || At(OP_COLON2) || At(KW_OPERATOR) ||
		(position_ < tokens_.size() &&
		 IsFundamentalKind(tokens_[position_].Kind())))
	{
		std::string name;
		if (position_ < tokens_.size() &&
			IsFundamentalKind(tokens_[position_].Kind()))
		{
			const std::size_t first = position_++;
			while (position_ < tokens_.size() &&
				IsFundamentalKind(tokens_[position_].Kind())) ++position_;
			for (std::size_t i = first; i < position_; ++i)
			{
				if (i != first) name += ' ';
				name += Spelling(i);
			}
		}
		else
		{
			NodeId structure = kNoNode;
			if (!ParseName(&name, true, true, true, &structure)) return kNoNode;
			return MakeStructuredNode("id-expression", name, structure);
		}
		return arena_.Make("id-expression", name);
	}
	return kNoNode;
}
NodeId Parser::ParsePostfixExpression() {
	NodeId value = ParsePrimaryExpression();
	if (value == kNoNode) return kNoNode;
	return ParsePostfixSuffixes(value);
}
NodeId Parser::ParsePostfixSuffixes(NodeId value) {
	while (true) {
		if (At(OP_LBRACE) && arena_.IsTag(value, "id-expression") &&
			(HasNameFact(strings_.Intern(arena_.Payload(value)), kKnownType) ||
			 IsFundamentalTypeSpelling(arena_.Payload(value)) ||
			 arena_.FirstEdge(value) != kNoEdge ||
			 (arena_.Flags(value) & SYNTAX_FLAG_TYPENAME) != 0)) {
			const NodeId call = arena_.Make("call-expression");
			arena_.Add(call, value); arena_.Add(call, ParseBracedInitList());
			value = call; continue;
		}
		if (Match(OP_LPAREN)) {
			const NodeId call = arena_.Make("call-expression");
			arena_.Add(call, value);
			const std::string callee = arena_.IsTag(value, "id-expression") ?
				arena_.Payload(value) : std::string();
			bool function_style = false;
			for (std::uint16_t candidate = 0;
				candidate < kSimpleTokenCount; ++candidate)
				if (IsFundamentalKind(candidate) && callee ==
					SimpleTokenKindName(static_cast<SimpleTokenKind>(candidate)) +
					std::string()) function_style = true;
			// Fundamental token names are uppercase enum labels; compare the
			// source spelling for the function-style cast view as well.
			if (IsFundamentalTypeSpelling(callee)) function_style = true;
			const NodeId arguments = arena_.Make(function_style ?
				"paren-argument-list" : "argument-list");
			if (callee == "__builtin_va_arg") ParseBuiltinVaArgArguments(arguments);
			else if (!At(OP_RPAREN)) {
				while (true) {
					NodeId argument = At(OP_LBRACE) ?
						ParseBracedInitList() : ParseExpression(2);
					if (argument == kNoNode)
						throw Error("expected call argument");
					argument = ParsePackExpansion(argument);
					arena_.Add(arguments, argument);
					if (!Match(OP_COMMA)) break;
				}
			}
			Expect(OP_RPAREN);
			arena_.Add(call, arguments);
			value = call;
			continue;
		}
		if (Match(OP_LSQUARE))
		{
			const NodeId index = ParseExpression();
			if (index == kNoNode) throw Error("expected subscript");
			Expect(OP_RSQUARE);
			const NodeId subscript = arena_.Make("subscript-expression");
			arena_.Add(subscript, value);
			arena_.Add(subscript, index);
			value = subscript;
			continue;
		}
		if (At(OP_DOT) || At(OP_ARROW))
		{
			const std::size_t operation = position_++;
			const bool dependent_template = Match(KW_TEMPLATE);
			std::string member;
			NodeId structure = kNoNode;
			const bool qualified_member = AtIdentifier() && AtOffset(1, OP_COLON2);
			const bool known_template_member = StartsKnownTemplateId();
			if (!ParseName(&member, qualified_member || known_template_member, true,
				dependent_template || qualified_member || known_template_member,
				&structure))
				throw Error("expected member name");
			if (dependent_template) member = "template " + member;
			const NodeId expression = MakeTokenNode("member-expression", operation);
			arena_.Add(expression, value);
			arena_.Add(expression, MakeStructuredNode(
				"identifier", member, structure));
			value = expression;
			continue;
		}
		if (At(OP_INC) || At(OP_DEC))
		{
			const std::size_t operation = position_++;
			const NodeId expression = MakeTokenNode("postfix-expression", operation);
			arena_.Add(expression, value);
			value = expression;
			continue;
		}
		break;
	}
	return value;
}
NodeId Parser::ParseUnaryExpression()
{
	if (MatchHostedExtensionMarker()) return ParseUnaryExpression();
	if (AtIdentifier() && (Spelling(position_) == "__real__" ||
		Spelling(position_) == "__imag__"))
	{
		const std::size_t operation = position_++;
		const NodeId operand = ParseUnaryExpression();
		if (operand == kNoNode) throw Error("expected complex component operand");
		const NodeId unary = MakeTokenNode("unary-expression", operation);
		arena_.Add(unary, operand);
		return unary;
	}
	if (At(OP_INC) || At(OP_DEC) || At(OP_STAR) || At(OP_AMP) ||
		At(OP_PLUS) || At(OP_MINUS) || At(OP_LNOT) || At(OP_COMPL))
	{
		const std::size_t operation = position_++;
		const NodeId operand = ParseUnaryExpression();
		if (operand == kNoNode) throw Error("expected unary operand");
		const NodeId unary = MakeTokenNode("unary-expression", operation);
		arena_.Add(unary, operand);
		return unary;
	}
	if (At(KW_SIZEOF) || At(KW_ALIGNOF) || At(KW_TYPEID) || At(KW_NOEXCEPT))
	{
		const std::size_t keyword = position_++;
		const SimpleTokenKind kind = static_cast<SimpleTokenKind>(
			tokens_[keyword].Kind());
		if (kind == KW_SIZEOF && Match(OP_DOTS)) {
			Expect(OP_LPAREN); if (!AtIdentifier()) throw Error("expected parameter pack name");
			const TextId name = tokens_[position_++].spelling; Expect(OP_RPAREN);
			const NodeId result = arena_.Make("sizeof-pack-expression", strings_.Get(name)); arena_.SetSemanticPayload(result, name); return ParsePostfixSuffixes(result); }
		const NodeId trait = kind == KW_SIZEOF ?
			arena_.Make("sizeof-expression") :
			MakeTokenNode("type-trait-expression", keyword);
		if (kind == KW_SIZEOF && !At(OP_LPAREN))
		{
			const NodeId operand = ParseUnaryExpression();
			if (operand == kNoNode) throw Error("expected sizeof operand");
			arena_.Add(trait, operand);
			return trait;
		}
		Expect(OP_LPAREN);
		bool prefer_type = kind == KW_ALIGNOF;
		if (StartsHostedType(position_)) prefer_type = true;
		if (At(KW_CONST) || At(KW_VOLATILE) || At(KW_DECLTYPE)) prefer_type = true;
		if (At(KW_STRUCT) || At(KW_CLASS) || At(KW_UNION) ||
			(position_ < tokens_.size() && IsFundamentalKind(
				tokens_[position_].Kind()))) prefer_type = true;
		if (AtIdentifier() && (IsLikelyTypeIdentifier(position_) ||
			(AtOffset(1, OP_LT) && HasNameFact(tokens_[position_].spelling,
				kKnownTemplate))) &&
			!StartsKnownTemplateId(true) &&
			!StartsQualifiedCallExpression() &&
			!AtOffset(1, OP_LPAREN) && (!AtOffset(1, OP_COLON2) ||
				QualifiedStartsType())) prefer_type = true;
		if (kind == KW_NOEXCEPT) prefer_type = false;
		if (prefer_type) {
			if (!ParseTypeId(trait)) throw Error("expected trait type-id");
		}
		else {
			const NodeId operand = ParseExpression();
			if (operand == kNoNode) throw Error("expected trait operand");
			arena_.Add(trait, operand);
		}
		Expect(OP_RPAREN);
		return ParsePostfixSuffixes(trait);
	}
	if (At(KW_DYNAMIC_CAST) || At(KW_STATIC_CAST) ||
		At(KW_REINTERPET_CAST) || At(KW_CONST_CAST))
	{
		const std::size_t keyword = position_++;
		const NodeId cast = MakeTokenNode("cast-expression", keyword);
		Expect(OP_LT);
		++angle_stop_depth_;
		if (!ParseTypeId(cast)) throw Error("expected cast type-id");
		--angle_stop_depth_;
		ExpectCloseAngle();
		Expect(OP_LPAREN);
		const NodeId operand = ParseExpression();
		if (operand == kNoNode) throw Error("expected cast operand");
		Expect(OP_RPAREN);
		arena_.Add(cast, operand);
		return ParsePostfixSuffixes(cast);
	}
	bool global_scope = false;
	if (At(OP_COLON2) &&
		(AtOffset(1, KW_NEW) || AtOffset(1, KW_DELETE)))
	{
		global_scope = true;
		++position_;
	}
	if (At(KW_NEW))
	{
		++position_;
		const NodeId expression = arena_.Make("new-expression");
		if (global_scope) arena_.Add(expression, arena_.Make("global-scope"));
		bool parenthesized_type = false;
		if (At(OP_LPAREN))
		{
			std::size_t scan = position_ + 1;
			std::size_t depth = 1;
			while (scan < tokens_.size() && depth != 0)
			{
				if (tokens_[scan].Kind() == static_cast<std::uint16_t>(OP_LPAREN))
					++depth;
				else if (tokens_[scan].Kind() ==
					static_cast<std::uint16_t>(OP_RPAREN)) --depth;
				++scan;
			}
			if (depth != 0) throw Error("unterminated new parentheses");
			bool placement = scan < tokens_.size() &&
				(tokens_[scan].Kind() == kIdentifierToken ||
				 (tokens_[scan].Kind() < kSimpleTokenCount &&
				  (IsFundamentalKind(tokens_[scan].Kind()) ||
				   tokens_[scan].Kind() == static_cast<std::uint16_t>(KW_CONST) ||
				   tokens_[scan].Kind() == static_cast<std::uint16_t>(KW_VOLATILE))));
			if (placement)
			{
				const std::size_t first = position_;
				Expect(OP_LPAREN);
				const NodeId arguments = arena_.Make("paren-argument-list");
				while (!At(OP_RPAREN))
				{
					const NodeId argument = ParseExpression(2);
					if (argument == kNoNode)
						throw Error("expected placement argument");
					arena_.Add(arguments, argument);
					if (!Match(OP_COMMA)) break;
				}
				Expect(OP_RPAREN);
				const NodeId node = arena_.Make("placement",
					JoinSpellings(first, position_));
				arena_.Add(node, arguments);
				arena_.Add(expression, node);
			}
			else
			{
				parenthesized_type = true;
				Expect(OP_LPAREN);
			}
		}
		if (!ParseTypeId(expression)) throw Error("expected allocated type");
		if (parenthesized_type) Expect(OP_RPAREN);
		if (At(OP_LPAREN))
		{
			Expect(OP_LPAREN);
			const NodeId initializer = arena_.Make("initializer");
			const NodeId values = arena_.Make("paren-initializer");
			if (!At(OP_RPAREN))
			{
				while (true)
				{
					NodeId value = ParseExpression(2);
					if (value == kNoNode)
						throw Error("expected new initializer");
					value = ParsePackExpansion(value);
					arena_.Add(values, value);
					if (!Match(OP_COMMA)) break;
				}
			}
			Expect(OP_RPAREN);
			arena_.Add(initializer, values);
			arena_.Add(expression, initializer);
		}
		else if (At(OP_LBRACE))
		{
			const NodeId initializer = arena_.Make("initializer");
			arena_.Add(initializer, ParseBracedInitList());
			arena_.Add(expression, initializer);
		}
		return expression;
	}
	if (At(KW_DELETE))
	{
		++position_;
		const NodeId expression = arena_.Make("delete-expression");
		if (global_scope) arena_.Add(expression, arena_.Make("global-scope"));
		if (Match(OP_LSQUARE))
		{
			Expect(OP_RSQUARE);
			arena_.Add(expression, arena_.Make("array-delete"));
		}
		const NodeId operand = ParseUnaryExpression();
		if (operand == kNoNode) throw Error("expected delete operand");
		arena_.Add(expression, operand);
		return expression;
	}
	if (global_scope) --position_;
	return ParsePostfixExpression();
}
NodeId Parser::ParseInitializer()
{
	if (Match(OP_ASS))
	{
		const NodeId initializer = arena_.Make("initializer");
		arena_.SetSemanticPayload(initializer,
			arena_.SharedStrings().Intern("copy"));
		if (Match(KW_DEFAULT))
		{
			arena_.Add(initializer,
				arena_.Make("special-initializer", "default"));
			return initializer;
		}
		if (Match(KW_DELETE))
		{
			arena_.Add(initializer,
				arena_.Make("special-initializer", "delete"));
			return initializer;
		}
		NodeId value = At(OP_LBRACE) ? ParseBracedInitList() :
			ParseExpression(2);
		if (value == kNoNode) throw Error("expected initializer");
		arena_.Add(initializer, value);
		return initializer;
	}
	if (At(OP_LBRACE))
	{
		const NodeId initializer = arena_.Make("initializer");
		arena_.SetSemanticPayload(initializer,
			arena_.SharedStrings().Intern("direct-list"));
		arena_.Add(initializer, ParseBracedInitList());
		return initializer;
	}
	if (Match(OP_LPAREN))
	{
		const NodeId initializer = arena_.Make("initializer");
		arena_.SetSemanticPayload(initializer,
			arena_.SharedStrings().Intern("direct-paren"));
		const NodeId values = arena_.Make("paren-initializer");
		if (!At(OP_RPAREN))
		{
			while (true)
			{
				NodeId value = ParseExpression(2);
				if (value == kNoNode) throw Error("expected paren initializer");
				value = ParsePackExpansion(value);
				arena_.Add(values, value);
				if (!Match(OP_COMMA)) break;
			}
		}
		Expect(OP_RPAREN);
		arena_.Add(initializer, values);
		return initializer;
	}
	return kNoNode;
}
NodeId Parser::ParseCondition(SimpleTokenKind terminator)
{
	const NodeId condition = arena_.Make("condition");
	const Mark declaration_mark = Checkpoint();
	std::string type_name;
	const NodeId declaration = arena_.Make("condition-declaration");
	const NodeId specifiers = ParseDeclSpecifierSeq(false, &type_name);
	if (specifiers != kNoNode)
	{
		std::string name;
		const NodeId declarator = ParseDeclarator(false, &name);
		const NodeId initializer = declarator == kNoNode ? kNoNode :
			ParseInitializer();
		if (declarator != kNoNode && initializer != kNoNode && At(terminator))
		{
			arena_.Add(declaration, specifiers);
			arena_.Add(declaration, declarator);
			arena_.Add(declaration, initializer);
			arena_.Add(condition, declaration);
			return condition;
		}
	}
	Rollback(declaration_mark);
	const NodeId expression = ParseExpression();
	if (expression == kNoNode) throw Error("expected condition");
	arena_.Add(condition, expression);
	return condition;
}
NodeId Parser::ParseCompoundStatement()
{
	if (!Match(OP_LBRACE)) return kNoNode;
	++compound_depth_; const std::size_t fact_mark = name_fact_changes_.size();
	const NodeId compound = arena_.Make("compound-statement");
	while (!At(OP_RBRACE))
	{
		if (AtEof()) throw Error("unterminated compound statement");
		NodeId item = kNoNode;
		const bool declaration_start = At(KW_TEMPLATE) || At(KW_USING) ||
			At(KW_NAMESPACE) ||
			At(KW_TYPEDEF) || At(KW_TYPENAME) || At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION) ||
			At(KW_ENUM) || At(KW_DECLTYPE) || At(KW_STATIC_ASSERT) || At(KW_EXTERN) ||
			StartsHostedDeclaration(position_) ||
			(position_ < tokens_.size() &&
			 IsDeclSpecifierKeyword(tokens_[position_].Kind())) ||
			(IsLikelyTypeIdentifier(position_) && !AtOffset(1, OP_COLON2) &&
			 !AtOffset(1, OP_LSQUARE) && !StartsQualifiedCallExpression()) ||
			(((AtIdentifier() && AtOffset(1, OP_COLON2)) || At(OP_COLON2)) &&
			 QualifiedStartsType() && !StartsQualifiedCallExpression());
		if (declaration_start)
		{
			const Mark declaration_mark = Checkpoint();
			item = ParseDeclaration(false);
			if (item == kNoNode) Rollback(declaration_mark);
		}
		if (item == kNoNode) item = ParseStatement();
		if (item == kNoNode) throw Error("expected block item");
		arena_.Add(compound, item);
	}
	Expect(OP_RBRACE);
	RestoreNameFacts(fact_mark); --compound_depth_;
	return compound;
}
NodeId Parser::ParseStatement()
{
	SkipAttributes();
	const NodeId gnu_asm = TryParseGnuAsmStatement();
	if (gnu_asm != kNoNode) return gnu_asm;
	if (At(OP_LBRACE)) return ParseCompoundStatement();
	if (AtIdentifier() && AtOffset(1, OP_COLON))
	{
		const std::string name = Spelling(position_);
		position_ += 2;
		const NodeId statement = arena_.Make("labeled-statement", name);
		const NodeId child = ParseStatement();
		if (child == kNoNode) throw Error("expected labeled statement");
		arena_.Add(statement, child);
		return statement;
	}
	if (Match(KW_CASE))
	{
		const NodeId statement = arena_.Make("case-statement");
		const NodeId value = ParseExpression();
		if (value == kNoNode) throw Error("expected case value");
		Expect(OP_COLON);
		arena_.Add(statement, value);
		const NodeId child = ParseStatement();
		if (child == kNoNode) throw Error("expected case statement");
		arena_.Add(statement, child);
		return statement;
	}
	if (Match(KW_DEFAULT))
	{
		Expect(OP_COLON);
		const NodeId statement = arena_.Make("default-statement");
		const NodeId child = ParseStatement();
		if (child == kNoNode) throw Error("expected default statement");
		arena_.Add(statement, child);
		return statement;
	}
	if (Match(KW_IF))
	{
		const NodeId statement = arena_.Make("if-statement");
		Expect(OP_LPAREN);
		arena_.Add(statement, ParseCondition());
		Expect(OP_RPAREN);
		const NodeId then_node = arena_.Make("then");
		const NodeId then_statement = ParseStatement();
		if (then_statement == kNoNode) throw Error("expected if body");
		arena_.Add(then_node, then_statement);
		arena_.Add(statement, then_node);
		if (Match(KW_ELSE))
		{
			const NodeId else_node = arena_.Make("else");
			const NodeId else_statement = ParseStatement();
			if (else_statement == kNoNode) throw Error("expected else body");
			arena_.Add(else_node, else_statement);
			arena_.Add(statement, else_node);
		}
		return statement;
	}
	if (Match(KW_SWITCH))
	{
		const NodeId statement = arena_.Make("switch-statement");
		Expect(OP_LPAREN);
		arena_.Add(statement, ParseCondition());
		Expect(OP_RPAREN);
		const NodeId body = ParseStatement();
		if (body == kNoNode) throw Error("expected switch body");
		arena_.Add(statement, body);
		return statement;
	}
	if (Match(KW_WHILE))
	{
		const NodeId statement = arena_.Make("while-statement");
		Expect(OP_LPAREN);
		arena_.Add(statement, ParseCondition());
		Expect(OP_RPAREN);
		const NodeId body = ParseStatement();
		if (body == kNoNode) throw Error("expected while body");
		arena_.Add(statement, body);
		return statement;
	}
	if (Match(KW_DO))
	{
		const NodeId statement = arena_.Make("do-statement");
		const NodeId body = ParseStatement();
		if (body == kNoNode) throw Error("expected do body");
		arena_.Add(statement, body);
		if (!Match(KW_WHILE)) throw Error("expected while after do body");
		Expect(OP_LPAREN);
		arena_.Add(statement, ParseCondition());
		Expect(OP_RPAREN);
		Expect(OP_SEMICOLON);
		return statement;
	}
	if (Match(KW_FOR))
	{
		const std::size_t for_fact_mark = name_fact_changes_.size();
		Expect(OP_LPAREN);
		const NodeId range_statement = TryParseRangeForStatement(for_fact_mark);
		if (range_statement != kNoNode) return range_statement;
		const NodeId statement = arena_.Make("for-statement");
		const NodeId initial = arena_.Make("for-init-statement");
		NodeId initial_value = kNoNode;
		const Mark declaration_mark = Checkpoint();
		initial_value = ParseSimpleOrFunction(false);
		if (initial_value == kNoNode)
		{
			Rollback(declaration_mark);
			if (!At(OP_SEMICOLON)) initial_value = ParseExpression();
			Expect(OP_SEMICOLON);
		}
		arena_.Add(initial, initial_value);
		arena_.Add(statement, initial);
		if (!At(OP_SEMICOLON))
			arena_.Add(statement, ParseCondition(OP_SEMICOLON));
		Expect(OP_SEMICOLON);
		if (!At(OP_RPAREN))
		{
			const NodeId iteration = arena_.Make("iteration");
			const NodeId value = ParseExpression();
			if (value == kNoNode) throw Error("expected for iteration");
			arena_.Add(iteration, value);
			arena_.Add(statement, iteration);
		}
		Expect(OP_RPAREN);
		const NodeId body = ParseStatement();
		if (body == kNoNode) throw Error("expected for body");
		arena_.Add(statement, body);
		RestoreNameFacts(for_fact_mark);
		return statement;
	}
	if (At(KW_TRY)) return ParseTryStatement();
	if (Match(KW_BREAK))
	{
		Expect(OP_SEMICOLON);
		return arena_.Make("break-statement");
	}
	if (Match(KW_CONTINUE))
	{
		Expect(OP_SEMICOLON);
		return arena_.Make("continue-statement");
	}
	if (Match(KW_GOTO))
	{
		if (!AtIdentifier()) throw Error("expected goto label");
		const std::string name = Spelling(position_++);
		Expect(OP_SEMICOLON);
		return arena_.Make("goto-statement", name);
	}
	if (Match(KW_RETURN))
	{
		const NodeId statement = arena_.Make("return-statement");
		if (!At(OP_SEMICOLON))
		{
			const NodeId value = At(OP_LBRACE) ? ParseBracedInitList() :
				ParseExpression();
			if (value == kNoNode) throw Error("expected return value");
			arena_.Add(statement, value);
		}
		Expect(OP_SEMICOLON);
		return statement;
	}
	if (Match(KW_THROW))
	{
		const NodeId statement = arena_.Make("throw-statement");
		if (!At(OP_SEMICOLON))
		{
			const NodeId value = ParseExpression(2);
			if (value == kNoNode) throw Error("expected throw value");
			arena_.Add(statement, value);
		}
		Expect(OP_SEMICOLON);
		return statement;
	}
	const bool declaration_start = At(KW_USING) || At(KW_NAMESPACE) ||
		At(KW_TYPEDEF) || At(KW_TYPENAME) ||
		At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION) || At(KW_ENUM) ||
		At(KW_DECLTYPE) || At(KW_STATIC_ASSERT) || At(KW_EXTERN) ||
		StartsHostedDeclaration(position_) ||
		(position_ < tokens_.size() &&
		 IsDeclSpecifierKeyword(tokens_[position_].Kind())) ||
		(IsLikelyTypeIdentifier(position_) && !AtOffset(1, OP_COLON2) &&
		 !AtOffset(1, OP_LSQUARE) && !StartsQualifiedCallExpression()) ||
		(((AtIdentifier() && AtOffset(1, OP_COLON2)) || At(OP_COLON2)) &&
		 QualifiedStartsType() && !StartsQualifiedCallExpression());
	if (declaration_start)
	{
		const Mark declaration_mark = Checkpoint();
		const NodeId declaration = ParseDeclaration(false);
		if (declaration != kNoNode) return declaration;
		Rollback(declaration_mark);
	}
	if (Match(OP_SEMICOLON)) return arena_.Make("expression-statement");
	const NodeId expression = ParseExpression();
	if (expression == kNoNode) return kNoNode;
	Expect(OP_SEMICOLON);
	const NodeId statement = arena_.Make("expression-statement");
	arena_.Add(statement, expression);
	return statement;
}
NodeId Parser::ParseNamespace()
{
	bool is_inline = Match(KW_INLINE);
	if (!Match(KW_NAMESPACE)) return kNoNode;
	SkipAttributes();
	if (!is_inline && AtIdentifier() && AtOffset(1, OP_ASS))
	{
		const std::string alias = Spelling(position_++);
		Expect(OP_ASS);
		std::string target;
		NodeId structure = kNoNode;
		if (!ParseName(&target, true, true, true, &structure))
			throw Error("expected namespace alias target");
		Expect(OP_SEMICOLON);
		const NodeId declaration = arena_.Make(
			"namespace-alias-definition", alias);
		arena_.Add(declaration,
			MakeStructuredNode("target", target, structure));
		return declaration;
	}
	std::string name = "<unnamed>";
	if (AtIdentifier()) name = Spelling(position_++);
	SkipAttributes();
	const NodeId declaration = arena_.Make("namespace-definition", name);
	if (is_inline) arena_.Add(declaration, arena_.Make("inline"));
	Expect(OP_LBRACE);
	while (!At(OP_RBRACE))
	{
		if (AtEof()) throw Error("unterminated namespace");
		const NodeId child = ParseDeclaration(false);
		if (child == kNoNode) throw Error("expected namespace declaration");
		arena_.Add(declaration, child);
	}
	Expect(OP_RBRACE);
	return declaration;
}
NodeId Parser::ParseUsing()
{
	if (!Match(KW_USING)) return kNoNode;
	if (Match(KW_NAMESPACE))
	{
		std::string target;
		NodeId structure = kNoNode;
		if (!ParseName(&target, true, true, true, &structure))
			throw Error("expected using namespace target");
		Expect(OP_SEMICOLON);
		const NodeId declaration = arena_.Make("using-directive");
		arena_.Add(declaration,
			MakeStructuredNode("target", target, structure));
		return declaration;
	}
	Match(KW_TYPENAME);
	if (!AtIdentifier() && !At(OP_COLON2)) throw Error("expected using target");
	const Mark alias_mark = Checkpoint();
	std::string first;
	if (!ParseName(&first)) throw Error("expected using name");
	std::vector<NodeId> attributes;
	while (ParseLeadingAttribute(&attributes)) {}
	if (Match(OP_ASS))
	{
		const NodeId declaration = arena_.Make("alias-declaration", first);
		if (!ParseTypeId(declaration)) throw Error("expected alias type-id");
		Expect(OP_SEMICOLON);
		for (std::size_t i = 0; i < attributes.size(); ++i)
			arena_.Add(declaration, attributes[i]);
		SetNameFact(first, kKnownType);
		last_declared_names_.clear();
		last_declared_names_.push_back(strings_.Intern(first));
		return declaration;
	}
	Rollback(alias_mark);
	std::string target;
	NodeId structure = kNoNode;
	NodeId conversion_type = kNoNode;
	if (!ParseName(&target, true, true, true, &structure, 0,
		&conversion_type))
		throw Error("expected using target");
	attributes.clear();
	while (ParseLeadingAttribute(&attributes)) {}
	Expect(OP_SEMICOLON);
	const NodeId declaration = arena_.Make("using-declaration");
	const NodeId target_node =
		MakeStructuredNode("target", target, structure);
	if (conversion_type != kNoNode)
		arena_.Add(target_node, conversion_type);
	arena_.Add(declaration, target_node);
	for (std::size_t i = 0; i < attributes.size(); ++i)
		arena_.Add(declaration, attributes[i]);
	return declaration;
}
NodeId Parser::ParseNestedTemplateParameterClause()
{
	const NodeId clause = arena_.Make("template-parameter-clause");
	Expect(OP_LT);
	++angle_stop_depth_;
	if (!AtCloseAngle())
	{
		const NodeId list = arena_.Make("template-parameter-list");
		while (true)
		{
			const NodeId parameter = ParseTemplateParameter();
			arena_.Add(list, parameter);
			if (!Match(OP_COMMA)) break;
		}
		arena_.Add(clause, list);
	}
	--angle_stop_depth_;
	ExpectCloseAngle();
	return clause;
}
NodeId Parser::ParseTypeTemplateParameter()
{
	const NodeId parameter = arena_.Make("type-parameter");
	const bool template_template = Match(KW_TEMPLATE);
	if (template_template)
	{
		arena_.Add(parameter, arena_.Make("template-template-parameter"));
		arena_.Add(parameter, ParseNestedTemplateParameterClause());
		if (!At(KW_CLASS) && !At(KW_TYPENAME))
			throw Error("expected template parameter key");
	}
	const std::size_t key = position_++;
	arena_.Add(parameter, MakeTokenNode("parameter-key", key));
	if (Match(OP_DOTS))
		arena_.Add(parameter, arena_.Make("parameter-pack", "..."));
	if (AtIdentifier())
	{
		const std::string name = Spelling(position_++);
		arena_.Add(parameter, arena_.Make("identifier", name));
		SetNameFact(name, kKnownType);
		if (template_template) SetNameFact(name, kKnownTemplate);
	}
	if (Match(OP_ASS))
	{
		const NodeId argument = arena_.Make("default-template-argument");
		if (!ParseTypeId(argument))
			throw Error("expected default template type");
		arena_.Add(parameter, argument);
	}
	return parameter;
}
NodeId Parser::ParseNonTypeTemplateParameter()
{
	const NodeId parameter = arena_.Make("non-type-template-parameter");
	const bool bare_int_parameter = At(KW_INT);
	const NodeId specifiers = ParseDeclSpecifierSeq(false);
	if (specifiers == kNoNode)
		throw Error("expected non-type template parameter");
	arena_.Add(parameter, specifiers);
	if (Match(OP_DOTS))
		arena_.Add(parameter, arena_.Make("parameter-pack", "..."));
	const Mark declarator_mark = Checkpoint();
	std::string parameter_name;
	const NodeId declarator = ParseDeclarator(false, &parameter_name);
	if (declarator != kNoNode) arena_.Add(parameter, declarator);
	else Rollback(declarator_mark);
	if (!parameter_name.empty())
	{
		SetNameFact(parameter_name, kKnownNonTemplate);
		const TextId parameter_id = strings_.Intern(parameter_name);
		SetNameFact(parameter_id, kActiveNonTypeParameter);
		active_non_type_parameter_names_.push_back(parameter_id);
	}
	if (Match(OP_ASS))
	{
		const NodeId argument = arena_.Make("default-template-argument");
		NodeId value;
		if (bare_int_parameter && parameter_name.empty() && AtLiteral()) {
			const std::size_t token = position_++;
			value = arena_.Make("literal", "TT_LITERAL:" + Spelling(token));
			arena_.SetLiteralFact(value, tokens_[token].LiteralFact());
		}
		else value = ParseExpression(2);
		if (value == kNoNode) throw Error("expected non-type default");
		arena_.Add(argument, value);
		arena_.Add(parameter, argument);
	}
	return parameter;
}
NodeId Parser::ParseTemplateParameter()
{
	if (!StartsDependentNonTypeTemplateParameter() &&
		(At(KW_CLASS) || At(KW_TYPENAME) || At(KW_TEMPLATE)))
		return ParseTypeTemplateParameter();
	return ParseNonTypeTemplateParameter();
}
NodeId Parser::ParseTemplate(bool in_class)
{
	if (!Match(KW_TEMPLATE)) return kNoNode;
	const std::size_t parameter_mark = active_non_type_parameter_names_.size();
	const NodeId declaration = arena_.Make("template-declaration");
	const NodeId clause = arena_.Make("template-parameter-clause");
	Expect(OP_LT);
	++angle_stop_depth_;
	if (!AtCloseAngle())
	{
		const NodeId list = arena_.Make("template-parameter-list");
		while (true)
		{
			const NodeId parameter = ParseTemplateParameter();
			arena_.Add(list, parameter);
			if (!Match(OP_COMMA)) break;
		}
		arena_.Add(clause, list);
	}
	--angle_stop_depth_;
	ExpectCloseAngle();
	arena_.Add(declaration, clause);
	last_declared_names_.clear();
	const NodeId target = ParseDeclaration(in_class);
	if (target == kNoNode) throw Error("expected templated declaration");
	arena_.Add(declaration, target);
	while (active_non_type_parameter_names_.size() > parameter_mark)
	{
		SetNameFact(active_non_type_parameter_names_.back(),
			kActiveNonTypeParameter, false);
		active_non_type_parameter_names_.pop_back();
	}
	for (std::size_t i = 0; i < last_declared_names_.size(); ++i)
	{
		SetNameFact(last_declared_names_[i], kKnownTemplate);
		SetNameFact(last_declared_names_[i], kKnownNonTemplate, false);
	}
	return declaration;
}
NodeId Parser::ParseCtorInitializer()
{
	if (!Match(OP_COLON)) return kNoNode;
	const NodeId initializer = arena_.Make("ctor-initializer");
	while (true)
	{
		std::string name;
		NodeId structure = kNoNode;
		if (At(KW_DECLTYPE))
		{
			const std::size_t first = position_++;
			Expect(OP_LPAREN);
			const NodeId ignored = ParseExpression();
			if (ignored == kNoNode) throw Error("expected decltype expression");
			Expect(OP_RPAREN);
			name = JoinSpellings(first, position_);
		}
		else if (!ParseName(&name, true, true, true, &structure))
			throw Error("expected mem-initializer-id");
		const NodeId member = arena_.Make("mem-initializer");
		arena_.Add(member,
			MakeStructuredNode("mem-initializer-id", name, structure));
		if (Match(OP_LPAREN))
		{
			const NodeId arguments = arena_.Make("paren-argument-list");
			if (!At(OP_RPAREN))
			{
				while (true)
				{
					NodeId value = ParseExpression(2);
					if (value == kNoNode)
						throw Error("expected mem-initializer argument");
					value = ParsePackExpansion(value);
					arena_.Add(arguments, value);
					if (!Match(OP_COMMA)) break;
				}
			}
			Expect(OP_RPAREN);
			arena_.Add(member, arguments);
		}
		else if (At(OP_LBRACE)) arena_.Add(member, ParseBracedInitList());
		else throw Error("expected mem-initializer");
		if (Match(OP_DOTS))
			arena_.Add(member, arena_.Make(
				"pack-expansion", "OP_DOTS:..."));
		arena_.Add(initializer, member);
		if (!Match(OP_COMMA)) break;
	}
	return initializer;
}
NodeId Parser::ParseSpecialMember(bool)
{
	const Mark mark = Checkpoint();
	std::vector<std::size_t> specifiers;
	while (At(KW_INLINE) || At(KW_VIRTUAL) || At(KW_EXPLICIT) ||
		At(KW_CONSTEXPR) || At(KW_FRIEND) || At(KW_STATIC))
		specifiers.push_back(position_++);
	SkipAttributes();
	const std::size_t name_start = position_;
	std::string name;
	TextId terminal_identifier = 0;
	NodeId name_structure = kNoNode;
	if (!ParseName(&name, true, true, true, &name_structure,
		&terminal_identifier) ||
		!At(OP_LPAREN))
	{
		Rollback(mark);
		return kNoNode;
	}
	const bool qualified = name.find("::") != std::string::npos;
	bool special_name = name.find("operator") != std::string::npos;
	if (!special_name && qualified && name_structure != kNoNode)
	{
		std::vector<TextId> components;
		for (std::uint32_t edge = arena_.FirstEdge(name_structure);
			edge != kNoEdge; edge = arena_.NextEdge(edge))
		{
			const NodeId child = arena_.EdgeChild(edge);
			if (arena_.IsTag(child, "name-component"))
				components.push_back(arena_.SemanticPayloadId(child));
		}
		if (components.size() > 1)
		{
			const TextId owner = components[components.size() - 2];
			const TextId terminal = components.back();
			special_name = terminal == owner || terminal == strings_.Intern(
				"~" + strings_.Get(owner));
		}
	}
	else if (!special_name && !current_classes_.empty())
	{
		special_name = terminal_identifier == current_classes_.back();
	}
	if (!special_name)
	{
		Rollback(mark);
		return kNoNode;
	}
	if (qualified)
	{
		const std::size_t op = name.rfind("::operator");
		if (op != std::string::npos)
		{
			const std::size_t after = op + std::string("::operator").size();
			if (after < name.size() &&
				name[after] != ' ' &&
				!std::isalnum(static_cast<unsigned char>(name[after])) &&
				name[after] != '_')
			{
				Rollback(mark);
				return kNoNode;
			}
			if (after < name.size() && name[after] != ' ')
				name.insert(after, " ");
		}
	}
	position_ = name_start;
	std::string declarator_name;
	const NodeId declarator = ParseDeclarator(false, &declarator_name);
	if (declarator == kNoNode)
	{
		Rollback(mark);
		return kNoNode;
	}
	if (declarator_name.find("operator ") != std::string::npos)
		name = declarator_name;
	if (qualified)
	{
		const std::size_t op = declarator_name.rfind("::operator");
		if (op != std::string::npos)
		{
			const std::size_t after = op + std::string("::operator").size();
			if (after < declarator_name.size() &&
				std::isalnum(static_cast<unsigned char>(declarator_name[after])))
				declarator_name.insert(after, " ");
		}
		name = declarator_name;
	}
	NodeId ctor_initializer = kNoNode;
	const bool function_try = At(KW_TRY);
	if (!function_try && At(OP_COLON)) ctor_initializer = ParseCtorInitializer();
	const bool has_body = At(OP_LBRACE) || function_try;
	const bool is_declaration = At(OP_SEMICOLON) || At(OP_ASS);
	if (!has_body && !is_declaration)
	{
		Rollback(mark);
		return kNoNode;
	}
	const NodeId member = arena_.Make(has_body ?
		"special-member-definition" : "special-member-declaration", name);
	if (!specifiers.empty())
	{
		const NodeId set = arena_.Make("member-specifiers");
		for (std::size_t i = 0; i < specifiers.size(); ++i)
		{
			const std::size_t specifier = specifiers[i];
			if (tokens_[specifier].Kind() ==
				static_cast<std::uint16_t>(KW_EXPLICIT))
				arena_.Add(set, arena_.Make("specifier", "explicit"));
			else arena_.Add(set, MakeTokenNode("specifier", specifier));
		}
		arena_.Add(member, set);
	}
	arena_.Add(member, declarator);
	if (ctor_initializer != kNoNode) arena_.Add(member, ctor_initializer);
	if (Match(OP_ASS))
	{
		const NodeId initializer = arena_.Make("initializer");
		if (Match(KW_DEFAULT))
			arena_.Add(initializer,
				arena_.Make("special-initializer", "default"));
		else if (Match(KW_DELETE))
			arena_.Add(initializer,
				arena_.Make("special-initializer", "delete"));
		else
		{
			const NodeId value = ParseExpression(2);
			if (value == kNoNode)
				throw Error("expected special member initializer");
			arena_.Add(initializer, value);
		}
		arena_.Add(member, initializer);
		Expect(OP_SEMICOLON);
		return member;
	}
	if (Match(OP_SEMICOLON)) return member;
	const std::size_t parameter_fact_mark = name_fact_changes_.size();
	ApplyFunctionParameterFacts(declarator);
	const NodeId body = function_try ? ParseFunctionTryBlock(true) :
		ParseCompoundStatement();
	RestoreNameFacts(parameter_fact_mark);
	if (body == kNoNode) throw Error("expected special member body");
	arena_.Add(member, body);
	return member;
}
NodeId Parser::ParseClass(bool require_semicolon)
{
	if (!At(KW_CLASS) && !At(KW_STRUCT) && !At(KW_UNION)) return kNoNode;
	const std::size_t key = position_++;
	std::vector<NodeId> alignments;
	ParseSemanticAttributes(&alignments);
	std::string name;
	TextId class_identifier = 0;
	NodeId name_structure = kNoNode;
	const Mark name_mark = Checkpoint();
	if (!ParseName(&name, true, true, true, &name_structure,
		&class_identifier))
	{
		Rollback(name_mark);
		name.clear();
	}
	ParseSemanticAttributes(&alignments);
	NodeId class_virt_specifier = kNoNode;
	if (AtIdentifier() && Spelling(position_) == "final")
	{
		const std::size_t specifier = position_++;
		class_virt_specifier = MakeTokenNode(
			"class-virt-specifier", specifier);
	}
	if (name.empty() && !At(OP_LBRACE)) throw Error("expected class name");
	last_declared_names_.clear();
	if (!name.empty()) last_declared_names_.push_back(strings_.Intern(name));
	if (!name.empty()) SetNameFact(name, kKnownType);
	if ((require_semicolon && Match(OP_SEMICOLON)) ||
		(!require_semicolon && !At(OP_LBRACE) && !At(OP_COLON)))
	{
		last_declared_names_.clear();
		if (!name.empty()) last_declared_names_.push_back(strings_.Intern(name));
		const NodeId declaration = arena_.Make("class-forward-declaration", name);
		if (name_structure != kNoNode) arena_.Add(declaration, name_structure);
		arena_.Add(declaration, MakeTokenNode("class-key", key));
		if (class_virt_specifier != kNoNode)
			arena_.Add(declaration, class_virt_specifier);
		for (std::size_t i = 0; i < alignments.size(); ++i)
			arena_.Add(declaration, alignments[i]);
		arena_.SetTokenRange(declaration, key, position_);
		return declaration;
	}
	const NodeId declaration = arena_.Make("class-specifier", name);
	if (name_structure != kNoNode) arena_.Add(declaration, name_structure);
	const std::size_t class_fact_mark = name_fact_changes_.size();
	arena_.Add(declaration, MakeTokenNode("class-key", key));
	if (class_virt_specifier != kNoNode)
		arena_.Add(declaration, class_virt_specifier);
	for (std::size_t i = 0; i < alignments.size(); ++i)
		arena_.Add(declaration, alignments[i]);
	if (Match(OP_COLON))
	{
		const NodeId clause = arena_.Make("base-clause");
		while (true)
		{
			const NodeId base = arena_.Make("base-specifier");
			if (Match(KW_VIRTUAL))
				arena_.Add(base, arena_.Make("virtual", "KW_VIRTUAL:virtual"));
			if (At(KW_PUBLIC) || At(KW_PRIVATE) || At(KW_PROTECTED))
			{
				const std::size_t access = position_++;
				arena_.Add(base, MakeTokenNode("access-specifier", access));
			}
			if (Match(KW_VIRTUAL))
				arena_.Add(base, arena_.Make("virtual", "KW_VIRTUAL:virtual"));
			std::string base_name;
			NodeId base_structure = kNoNode;
			NodeId decltype_expression = kNoNode;
			if (At(KW_DECLTYPE))
			{
				const std::size_t first = position_++;
				Expect(OP_LPAREN);
				decltype_expression = ParseExpression();
				if (decltype_expression == kNoNode)
					throw Error("expected decltype base expression");
				Expect(OP_RPAREN);
				base_name = JoinSpellings(first, position_);
			}
			else if (!ParseName(&base_name, true, true, true, &base_structure))
				throw Error("expected base name");
			const NodeId base_name_node =
				MakeStructuredNode("base-name", base_name, base_structure);
			if (decltype_expression != kNoNode)
			{
				// Retain the parsed dependent expression for specialization replay
				// without changing the established source AST presentation.
				arena_.AddFlags(
					decltype_expression, SYNTAX_FLAG_SEMANTIC_ONLY);
				arena_.Add(base_name_node, decltype_expression);
			}
			arena_.Add(base, base_name_node);
			if (Match(OP_DOTS))
				arena_.Add(base, arena_.Make(
					"pack-expansion", "OP_DOTS:..."));
			arena_.Add(clause, base);
			if (!Match(OP_COMMA)) break;
		}
		arena_.Add(declaration, clause);
	}
	Expect(OP_LBRACE);
	current_classes_.push_back(class_identifier);
	while (!At(OP_RBRACE))
	{
		if (AtEof()) throw Error("unterminated class");
		std::vector<NodeId> member_alignments;
		ParseSemanticAttributes(&member_alignments);
		if ((At(KW_PUBLIC) || At(KW_PRIVATE) || At(KW_PROTECTED)) &&
			AtOffset(1, OP_COLON))
		{
			const std::size_t access = position_++;
			++position_;
			arena_.Add(declaration, MakeTokenNode("access-specifier", access));
			continue;
		}
		NodeId special_member = ParseSpecialMember(false);
		if (special_member != kNoNode)
		{
			for (std::size_t i = 0; i < member_alignments.size(); ++i)
				arena_.Add(special_member, member_alignments[i]);
			arena_.Add(declaration, special_member);
			continue;
		}
		if (At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION))
		{
			// A class type cannot be a bit-field type.  Route class-key
			// members directly to declaration parsing so a nested class body
			// is not parsed speculatively and then replayed after rollback.
			NodeId member = ParseDeclaration(true);
			if (member == kNoNode) throw Error("expected class member");
			for (std::size_t i = 0; i < member_alignments.size(); ++i)
				arena_.Add(member, member_alignments[i]);
			arena_.Add(declaration, member);
			continue;
		}
		const Mark bit_field_mark = Checkpoint();
		const NodeId bit_field = arena_.Make("bit-field-declaration");
		const NodeId bit_specifiers = ParseDeclSpecifierSeq(false);
		bool parsed_bit_field = false;
		if (bit_specifiers != kNoNode)
		{
			std::vector<NodeId> fields;
			while (true)
			{
				const NodeId field = arena_.Make("bit-field-declarator");
				const Mark declarator_mark = Checkpoint();
				const NodeId declarator = ParseDeclarator(false);
				if (declarator != kNoNode) arena_.Add(field, declarator);
				else Rollback(declarator_mark);
				if (!Match(OP_COLON)) break;
				const NodeId width = ParseExpression(2);
				if (width == kNoNode) throw Error("expected bit-field width");
				arena_.Add(field, width);
				fields.push_back(field);
				if (!Match(OP_COMMA)) break;
			}
			if (!fields.empty() && Match(OP_SEMICOLON))
			{
				arena_.Add(bit_field, bit_specifiers);
				for (std::size_t i = 0; i < fields.size(); ++i)
					arena_.Add(bit_field, fields[i]);
				for (std::size_t i = 0; i < member_alignments.size(); ++i)
					arena_.Add(bit_field, member_alignments[i]);
				arena_.Add(declaration, bit_field);
				parsed_bit_field = true;
			}
		}
		if (parsed_bit_field) continue;
		Rollback(bit_field_mark);
		NodeId member = ParseDeclaration(true);
		if (member == kNoNode) throw Error("expected class member");
		for (std::size_t i = 0; i < member_alignments.size(); ++i)
			arena_.Add(member, member_alignments[i]);
		arena_.Add(declaration, member);
	}
	current_classes_.pop_back();
	Expect(OP_RBRACE);
	if (require_semicolon) Expect(OP_SEMICOLON);
	PublishClassNameFacts(class_fact_mark);
	arena_.AddFlags(declaration, SYNTAX_FLAG_DEFINITION);
	arena_.SetTokenRange(declaration, key, position_);
	last_declared_names_.clear();
	if (!name.empty()) last_declared_names_.push_back(strings_.Intern(name));
	return declaration;
}
NodeId Parser::ParseEnum(bool require_semicolon)
{
	if (!At(KW_ENUM)) return kNoNode;
	const std::size_t first = position_++;
	std::size_t key = std::numeric_limits<std::size_t>::max();
	if (At(KW_CLASS) || At(KW_STRUCT)) key = position_++;
	std::string name;
	if (AtIdentifier())
	{
		if (!ParseName(&name, true, false)) throw Error("expected enum name");
	}
	SkipAttributes();
	NodeId underlying = kNoNode;
	if (Match(OP_COLON))
	{
		underlying = arena_.Make("type-id");
		const NodeId specifiers = ParseDeclSpecifierSeq(true);
		if (specifiers == kNoNode) throw Error("expected enum underlying type");
		arena_.Add(underlying, specifiers);
	}
	const NodeId declaration = arena_.Make("enum-specifier", name);
	if (key != std::numeric_limits<std::size_t>::max())
		arena_.Add(declaration, MakeTokenNode("enum-key", key));
	if (underlying != kNoNode) arena_.Add(declaration, underlying);
	if (Match(OP_LBRACE))
	{
		arena_.AddFlags(declaration, SYNTAX_FLAG_DEFINITION);
		if (!At(OP_RBRACE))
		{
			while (true)
			{
				if (!AtIdentifier()) throw Error("expected enumerator");
				const std::string enumerator_name = Spelling(position_++);
				const NodeId enumerator = arena_.Make("enumerator",
					enumerator_name);
				SkipAttributes();
				if (Match(OP_ASS))
				{
					const NodeId value = ParseExpression(2);
					if (value == kNoNode) throw Error("expected enumerator value");
					arena_.Add(enumerator, value);
				}
				arena_.Add(declaration, enumerator);
				if (!Match(OP_COMMA)) break;
				if (At(OP_RBRACE)) break;
			}
		}
		Expect(OP_RBRACE);
	}
	if (!name.empty()) SetNameFact(name, kKnownType);
	last_declared_names_.clear();
	if (!name.empty()) last_declared_names_.push_back(strings_.Intern(name));
	if (require_semicolon) Expect(OP_SEMICOLON);
	arena_.SetTokenRange(declaration, first, position_);
	return declaration;
}
bool Parser::StartsStandaloneEnumDeclaration() const
{
	std::size_t scan = position_ + 1;
	if (scan < tokens_.size() &&
		(tokens_[scan].Kind() == static_cast<std::uint16_t>(KW_CLASS) ||
		 tokens_[scan].Kind() == static_cast<std::uint16_t>(KW_STRUCT))) ++scan;
	if (scan >= tokens_.size()) return false;
	if (tokens_[scan].Kind() == static_cast<std::uint16_t>(OP_LBRACE))
		return true;
	if (tokens_[scan].Kind() != kIdentifierToken) return false;
	++scan;
	while (scan + 1 < tokens_.size() &&
		tokens_[scan].Kind() == static_cast<std::uint16_t>(OP_COLON2) &&
		tokens_[scan + 1].Kind() == kIdentifierToken)
		scan += 2;
	if (scan >= tokens_.size()) return false;
	const std::uint16_t kind = tokens_[scan].Kind();
	return kind == static_cast<std::uint16_t>(OP_LBRACE) ||
		kind == static_cast<std::uint16_t>(OP_COLON) ||
		kind == static_cast<std::uint16_t>(OP_SEMICOLON);
}
NodeId Parser::ParseStaticAssert()
{
	if (!Match(KW_STATIC_ASSERT)) return kNoNode;
	const NodeId declaration = arena_.Make("static-assert-declaration");
	Expect(OP_LPAREN);
	const NodeId expression = ParseExpression(2);
	if (expression == kNoNode) throw Error("expected static assertion");
	arena_.Add(declaration, expression);
	if (Match(OP_COMMA))
	{
		if (!AtLiteral()) throw Error("expected static assertion message");
		arena_.Add(declaration, arena_.Make("message",
			Spelling(position_++)));
	}
	Expect(OP_RPAREN);
	Expect(OP_SEMICOLON);
	return declaration;
}
NodeId Parser::ParseSimpleOrFunction(bool, bool)
{
	const Mark mark = Checkpoint();
	const std::size_t specifier_first = position_;
	const NodeId specifiers = ParseDeclSpecifierSeq(false);
	if (specifiers == kNoNode)
	{
		Rollback(mark);
		return kNoNode;
	}
	const std::size_t specifier_last = position_;
	return FinishSimpleOrFunction(
		mark, specifier_first, specifier_last, specifiers);
}

NodeId Parser::FinishSimpleOrFunction(const Mark& mark,
	std::size_t specifier_first, std::size_t specifier_last,
	NodeId specifiers)
{
	bool is_typedef = false;
	for (std::size_t i = specifier_first; i < specifier_last; ++i)
		if (tokens_[i].Kind() == static_cast<std::uint16_t>(KW_TYPEDEF))
			is_typedef = true;
	if (Match(OP_SEMICOLON))
	{
		const NodeId declaration = arena_.Make("simple-declaration");
		arena_.Add(declaration, specifiers);
		return declaration;
	}
	const NodeId list = arena_.Make("init-declarator-list");
	std::vector<std::string> names;
	while (true)
	{
		const std::size_t item_first = position_;
		const NodeId item = arena_.Make("init-declarator");
		std::string name;
		const NodeId declarator = ParseDeclarator(false, &name);
		if (declarator == kNoNode)
		{
			Rollback(mark);
			return kNoNode;
		}
		if ((At(OP_LBRACE) || At(KW_TRY)) && names.empty() &&
			arena_.HasDescendantTag(declarator, "parameter-clause"))
		{
			const NodeId declaration = arena_.Make("function-definition");
			arena_.Add(declaration, specifiers);
			arena_.Add(declaration, declarator);
			if (!name.empty()) { SetNameFact(name, kKnownType, false);
				SetNameFact(name, kKnownNonTemplate); }
			const std::size_t parameter_fact_mark = name_fact_changes_.size();
			ApplyFunctionParameterFacts(declarator);
			arena_.Add(declaration, At(KW_TRY) ?
				ParseFunctionTryBlock(false) : ParseCompoundStatement());
			RestoreNameFacts(parameter_fact_mark);
			last_declared_names_.clear();
			if (!name.empty())
				last_declared_names_.push_back(strings_.Intern(name));
			return declaration;
		}
		AppendDeclaratorNames(declarator, name, &names);
		arena_.Add(item, declarator);
		const NodeId initializer = ParseInitializer();
		if (initializer != kNoNode) arena_.Add(item, initializer);
		SkipAttributes();
		arena_.SetTokenRange(item, item_first, position_);
		arena_.Add(list, item);
		if (!Match(OP_COMMA)) break;
	}
	if (!Match(OP_SEMICOLON))
	{
		Rollback(mark);
		return kNoNode;
	}
	const NodeId declaration = arena_.Make("simple-declaration");
	arena_.Add(declaration, specifiers);
	arena_.Add(declaration, list);
	last_declared_names_.clear();
	last_declared_names_.reserve(names.size());
	for (std::size_t i = 0; i < names.size(); ++i)
		if (!names[i].empty())
			last_declared_names_.push_back(strings_.Intern(names[i]));
	if (is_typedef)
	{
		for (std::size_t i = 0; i < names.size(); ++i)
			if (!names[i].empty()) SetNameFact(names[i], kKnownType);
	}
	else
	{
		for (std::size_t i = 0; i < names.size(); ++i)
			if (!names[i].empty())
			{
				SetNameFact(names[i], kKnownType, false);
				if (compound_depth_) SetNameFact(names[i], kActiveNonTypeParameter);
				SetNameFact(names[i], kKnownNonTemplate);
			}
	}
	return declaration;
}
bool Parser::ParseLeadingAttribute(std::vector<NodeId>* attributes)
{
	return pa32_syntax_detail::ConsumeLeadingGnuObjectAttribute(
		tokens_, strings_, arena_, &position_, attributes) ||
		pa32_syntax_detail::ConsumeLeadingStandardObjectAttribute(
			tokens_, strings_, arena_, &position_, attributes) || SkipAttribute();
}
NodeId Parser::ParseDeclaration(bool in_class)
{
	std::vector<NodeId> attributes;
	while (ParseLeadingAttribute(&attributes)) {}
	const NodeId declaration = ParseDeclarationCore(in_class);
	if (declaration != kNoNode)
		for (std::size_t i = 0; i < attributes.size(); ++i)
			arena_.Add(declaration, attributes[i]);
	return declaration;
}

NodeId Parser::ParseDeclarationCore(bool in_class)
{
	if (position_ < tokens_.size() &&
		tokens_[position_].Kind() == kPragmaPackPushToken)
	{
		const NodeId directive = arena_.Make("layout-pack-push",
			Spelling(position_++));
		arena_.AddFlags(directive, SYNTAX_FLAG_SEMANTIC_ONLY);
		return directive;
	}
	if (position_ < tokens_.size() &&
		tokens_[position_].Kind() == kPragmaPackPopToken)
	{
		++position_;
		const NodeId directive = arena_.Make("layout-pack-pop");
		arena_.AddFlags(directive, SYNTAX_FLAG_SEMANTIC_ONLY);
		return directive;
	}
	if (Match(OP_SEMICOLON)) return arena_.Make("empty-declaration");
	if ((At(KW_INLINE) && AtOffset(1, KW_NAMESPACE)) || At(KW_NAMESPACE))
		return ParseNamespace();
	if (At(KW_USING)) return ParseUsing();
	const bool explicit_definition = At(KW_TEMPLATE) && !AtOffset(1, OP_LT);
	const bool explicit_declaration = At(KW_EXTERN) && AtOffset(1, KW_TEMPLATE);
	if (explicit_definition || explicit_declaration)
	{
		position_ += explicit_declaration ? 2 : 1;
		const NodeId result = arena_.Make(explicit_declaration ?
			"explicit-instantiation-declaration" :
			"explicit-instantiation-definition");
		NodeId target = At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION) ?
			ParseClass() : ParseSimpleOrFunction(in_class);
		if (target == kNoNode)
			target = ParseSpecialMember(false);
		if (target == kNoNode) throw Error("expected explicit instantiation");
		arena_.Add(result, target);
		return result;
	}
	if (At(KW_TEMPLATE)) return ParseTemplate(in_class);
	if (At(KW_EXTERN) && position_ + 1 < tokens_.size() &&
		tokens_[position_ + 1].Kind() == kLiteralToken)
	{
		position_ += 1;
		std::string language = Spelling(position_++);
		if (language.size() >= 2 && language[0] == '"' &&
			language[language.size() - 1] == '"')
			language = language.substr(1, language.size() - 2);
		const NodeId declaration = arena_.Make(
			"linkage-specification", language);
		if (Match(OP_LBRACE))
		{
			while (!At(OP_RBRACE))
			{
				const NodeId child = ParseDeclaration(in_class);
				if (child == kNoNode)
					throw Error("expected linkage declaration");
				arena_.Add(declaration, child);
			}
			Expect(OP_RBRACE);
		}
		else
		{
			arena_.AddFlags(
				declaration, SYNTAX_FLAG_DIRECT_LINKAGE_DECLARATION);
			const NodeId child = ParseDeclaration(in_class);
			if (child == kNoNode) throw Error("expected linkage declaration");
			arena_.Add(declaration, child);
		}
		return declaration;
	}
	if (At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION))
	{
		// Parse a class-specifier exactly once.  If no declarator follows,
		// preserve the established standalone AST shape; otherwise hand the
		// already-parsed specifier to the ordinary declaration tail.
		const Mark class_mark = Checkpoint();
		const std::size_t specifier_first = position_;
		const NodeId class_specifier = ParseClass(false);
		const NodeId specifiers = arena_.Make("decl-specifier-seq");
		arena_.Add(specifiers, class_specifier);
		while (At(KW_CONST) || At(KW_VOLATILE))
		{
			const std::size_t qualifier = position_++;
			arena_.Add(specifiers,
				MakeTokenNode("decl-specifier", qualifier));
		}
		const std::size_t specifier_last = position_;
		if (Match(OP_SEMICOLON))
		{
			arena_.SetTokenRange(
				class_specifier, specifier_first, position_);
			return class_specifier;
		}
		return FinishSimpleOrFunction(class_mark,
			specifier_first, specifier_last, specifiers);
	}
	if (At(KW_ENUM)) return in_class ?
		ParseSimpleOrFunction(in_class) :
		(StartsStandaloneEnumDeclaration() ? ParseEnum() :
		 ParseSimpleOrFunction(in_class));
	if (At(KW_STATIC_ASSERT)) return ParseStaticAssert();
	const Mark special_mark = Checkpoint();
	const NodeId special = ParseSpecialMember(true);
	if (special != kNoNode) return special;
	Rollback(special_mark);
	return ParseSimpleOrFunction(in_class);
}

}
namespace pa10_syntax_detail
{
void RunSyntaxTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream* output, SyntaxTreeConsumer* consumer, SyntaxStats* stats,
	InternedStringTable* retained_strings)
{
	const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	if (stats) *stats = SyntaxStats();
	StringTable local_strings; StringTable& strings = retained_strings ? *retained_strings : local_strings;
	SyntaxTokenSink sink(strings);
	PreprocessFile(path, source, sink, options,
		stats ? &stats->preprocessing : 0);
	SyntaxArena arena(strings, sink.Tokens(), sink.LiteralFacts());
	Parser parser(sink.Tokens(), strings, arena, stats);
	const std::chrono::steady_clock::time_point parse_started = std::chrono::steady_clock::now();
	const NodeId root = parser.ParseTranslationUnit();
	const std::size_t rollback_storage = arena.RollbackStorageBytes();
	arena.ReleaseRollbackStorage();
	const std::chrono::steady_clock::time_point boundary_started = std::chrono::steady_clock::now();
	if (output)
		arena.Write(*output, root, stats ? &stats->syntax_output_bytes : 0,
			stats ? &stats->max_syntax_depth : 0,
			stats ? &stats->render_stack_storage_bytes : 0);
	else consumer->Consume(arena, root);
	if (stats)
	{
		const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
		stats->tokens = sink.Tokens().size(); stats->interned_spellings = strings.Size();
		stats->spelling_bytes = strings.SpellingBytes(); stats->syntax_nodes = arena.Nodes();
		stats->syntax_edges = arena.Edges();
		stats->token_storage_bytes = sink.StorageBytes();
		stats->syntax_storage_bytes = arena.StorageBytes() +
			strings.StorageBytes();
		stats->parser_storage_bytes = parser.StorageBytes() + rollback_storage;
		stats->peak_stage_storage_bytes = source.size() +
			stats->token_storage_bytes + stats->syntax_storage_bytes +
			stats->parser_storage_bytes + stats->render_stack_storage_bytes;
		stats->parse_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				boundary_started - parse_started).count());
		stats->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				finished - boundary_started).count());
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				finished - started).count());
	}
}

}

}
