#include "recognition/recognizer.h"
#include "support/exception_types.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "recognition/grammar_definition.h"

namespace cppgm
{
namespace recognition
{
namespace
{

__attribute__((cold, noinline, noreturn))
void ThrowRecognitionSourceError(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::LEXICAL);
}

__attribute__((cold, noinline, noreturn))
void ThrowRecognitionResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::RECOGNITION);
}

__attribute__((cold, noinline, noreturn))
void ThrowRecognitionInternalError(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::RECOGNITION);
}

__attribute__((cold, noinline, noreturn))
void ThrowRecognitionInternalError(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::RECOGNITION);
}

const std::uint16_t kSimpleTokenCount =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kIdentifierToken = kSimpleTokenCount;
const std::uint16_t kLiteralToken = kSimpleTokenCount + 1;
const std::uint16_t kEofToken = kSimpleTokenCount + 2;
const std::uint16_t kRShift1Token = kSimpleTokenCount + 3;
const std::uint16_t kRShift2Token = kSimpleTokenCount + 4;
const std::uint32_t kMemoUnvisited =
	std::numeric_limits<std::uint32_t>::max();
const std::uint32_t kMemoInProgress = kMemoUnvisited - 1;
const std::uint32_t kParseFailure = kMemoUnvisited - 2;

enum IdentifierFlags
{
	IF_CLASS = 1,
	IF_TEMPLATE = 2,
	IF_TYPEDEF = 4,
	IF_ENUM = 8,
	IF_NAMESPACE = 16
};

enum TokenProperties
{
	TP_EMPTY_STRING = 1,
	TP_ZERO = 2
};

enum AngleRole
{
	AR_NORMAL,
	AR_OPEN,
	AR_CLOSE
};

struct IdentifierInfo
{
	std::string spelling;
	unsigned char flags;

	IdentifierInfo() : flags(0) {}
	IdentifierInfo(const std::string& text, unsigned char value)
		: spelling(text), flags(value) {}
};

std::size_t HashSpelling(const std::string& spelling)
{
	std::size_t value = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1469598103934665603ULL) :
		static_cast<std::size_t>(2166136261U);
	const std::size_t prime = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1099511628211ULL) :
		static_cast<std::size_t>(16777619U);
	for (std::size_t i = 0; i < spelling.size(); ++i)
	{
		value ^= static_cast<unsigned char>(spelling[i]);
		value *= prime;
	}
	return value;
}

unsigned char CategorizeIdentifier(const std::string& spelling)
{
	unsigned char flags = 0;
	if (spelling.find('C') != std::string::npos) flags |= IF_CLASS;
	if (spelling.find('T') != std::string::npos) flags |= IF_TEMPLATE;
	if (spelling.find('Y') != std::string::npos) flags |= IF_TYPEDEF;
	if (spelling.find('E') != std::string::npos) flags |= IF_ENUM;
	if (spelling.find('N') != std::string::npos) flags |= IF_NAMESPACE;
	return flags;
}

class IdentifierTable
{
public:
	IdentifierTable() : slots_(16, 0), spelling_bytes_(0)
	{
		identifiers_.push_back(IdentifierInfo());
	}

	std::uint32_t Intern(const std::string& spelling)
	{
		if ((identifiers_.size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = HashSpelling(spelling) & mask;
		while (slots_[slot] != 0)
		{
			const std::uint32_t id = slots_[slot];
			if (identifiers_[id].spelling == spelling)
				return id;
			slot = (slot + 1) & mask;
		}
		if (identifiers_.size() >
			std::numeric_limits<std::uint32_t>::max())
			ThrowRecognitionResourceLimit("too many identifiers");
		const std::uint32_t id =
			static_cast<std::uint32_t>(identifiers_.size());
		identifiers_.push_back(IdentifierInfo(spelling,
			CategorizeIdentifier(spelling)));
		spelling_bytes_ += spelling.size();
		slots_[slot] = id;
		return id;
	}

	const IdentifierInfo& Get(std::uint32_t id) const
	{
		return identifiers_[id];
	}

	std::size_t Size() const { return identifiers_.size() - 1; }
	std::size_t SpellingBytes() const { return spelling_bytes_; }
	std::size_t StorageBytes() const
	{
		std::size_t bytes = identifiers_.capacity() * sizeof(IdentifierInfo) +
			slots_.capacity() * sizeof(std::uint32_t);
		for (std::size_t i = 1; i < identifiers_.size(); ++i)
			bytes += identifiers_[i].spelling.capacity();
		return bytes;
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::uint32_t id = 1; id < identifiers_.size(); ++id)
		{
			std::size_t slot =
				HashSpelling(identifiers_[id].spelling) & mask;
			while (replacement[slot] != 0)
				slot = (slot + 1) & mask;
			replacement[slot] = id;
		}
		slots_.swap(replacement);
	}

	std::vector<IdentifierInfo> identifiers_;
	std::vector<std::uint32_t> slots_;
	std::size_t spelling_bytes_;
};

struct RecognitionToken
{
	std::uint16_t kind;
	unsigned char role;
	unsigned char properties;
	std::uint32_t identifier;

	explicit RecognitionToken(std::uint16_t token_kind,
		std::uint32_t token_identifier = 0,
		unsigned char token_properties = 0)
		: kind(token_kind), role(AR_NORMAL),
		  properties(token_properties), identifier(token_identifier) {}
};

bool IsSimple(const RecognitionToken& token, SimpleTokenKind kind)
{
	return token.kind == static_cast<std::uint16_t>(kind);
}

class RecognitionTokenSink : public IPostTokenStream
{
public:
	void EmitInvalid(const std::string& source)
	{
		ThrowRecognitionSourceError("invalid phase-7 token: " + source);
	}

	void EmitSimple(const std::string&, SimpleTokenKind kind)
	{
		if (kind == OP_RSHIFT)
		{
			tokens_.push_back(RecognitionToken(kRShift1Token));
			tokens_.push_back(RecognitionToken(kRShift2Token));
		}
		else
		{
			tokens_.push_back(RecognitionToken(
				static_cast<std::uint16_t>(kind)));
		}
	}

	void EmitIdentifier(const std::string& source)
	{
		tokens_.push_back(RecognitionToken(kIdentifierToken,
			identifiers_.Intern(source)));
	}

	void EmitLiteral(const std::string& source, FundamentalType,
		const void*, std::size_t)
	{
		EmitOrdinaryLiteral(source);
	}

	void EmitLiteralArray(const std::string& source, std::size_t,
		FundamentalType, const void*, std::size_t)
	{
		EmitOrdinaryLiteral(source);
	}

	void EmitUserDefinedCharacter(const std::string&, const std::string&,
		FundamentalType, const void*, std::size_t)
	{
		tokens_.push_back(RecognitionToken(kLiteralToken));
	}

	void EmitUserDefinedString(const std::string&, const std::string&,
		std::size_t, FundamentalType, const void*, std::size_t)
	{
		tokens_.push_back(RecognitionToken(kLiteralToken));
	}

	void EmitUserDefinedInteger(const std::string&, const std::string&,
		const std::string&)
	{
		tokens_.push_back(RecognitionToken(kLiteralToken));
	}

	void EmitUserDefinedFloating(const std::string&, const std::string&,
		const std::string&)
	{
		tokens_.push_back(RecognitionToken(kLiteralToken));
	}

	void EmitEof()
	{
		tokens_.push_back(RecognitionToken(kEofToken));
	}

	void ClassifyAngles(std::size_t* opening_count,
		std::size_t* closing_count, std::size_t* scratch_bytes);

	const std::vector<RecognitionToken>& Tokens() const { return tokens_; }
	const IdentifierTable& Identifiers() const { return identifiers_; }
	std::size_t TokenStorageBytes() const
	{
		return tokens_.capacity() * sizeof(RecognitionToken);
	}

private:
	void EmitOrdinaryLiteral(const std::string& source)
	{
		unsigned char properties = 0;
		if (source == "\"\"") properties |= TP_EMPTY_STRING;
		if (source == "0") properties |= TP_ZERO;
		tokens_.push_back(RecognitionToken(kLiteralToken, 0, properties));
	}

	bool EndsOperatorTemplateName(std::size_t position) const;
	bool BeginsAngle(std::size_t position) const;

	std::vector<RecognitionToken> tokens_;
	IdentifierTable identifiers_;
};

bool IsSingleTokenOperator(const RecognitionToken& token)
{
	if (token.kind >= kSimpleTokenCount)
		return false;
	const SimpleTokenKind kind = static_cast<SimpleTokenKind>(token.kind);
	switch (kind)
	{
	case KW_NEW: case KW_DELETE:
	case OP_PLUS: case OP_MINUS: case OP_STAR: case OP_DIV: case OP_MOD:
	case OP_XOR: case OP_AMP: case OP_BOR: case OP_COMPL: case OP_LNOT:
	case OP_ASS: case OP_LT: case OP_GT: case OP_PLUSASS: case OP_MINUSASS:
	case OP_STARASS: case OP_DIVASS: case OP_MODASS: case OP_XORASS:
	case OP_BANDASS: case OP_BORASS: case OP_LSHIFT: case OP_RSHIFTASS:
	case OP_LSHIFTASS: case OP_EQ: case OP_NE: case OP_LE: case OP_GE:
	case OP_LAND: case OP_LOR: case OP_INC: case OP_DEC: case OP_COMMA:
	case OP_ARROWSTAR: case OP_ARROW:
		return true;
	default:
		return false;
	}
}

bool RecognitionTokenSink::EndsOperatorTemplateName(
	std::size_t position) const
{
	if (position >= 1 &&
		IsSimple(tokens_[position - 1], KW_OPERATOR) &&
		IsSingleTokenOperator(tokens_[position]))
		return true;
	if (tokens_[position].kind == kRShift2Token && position >= 2 &&
		tokens_[position - 1].kind == kRShift1Token &&
		IsSimple(tokens_[position - 2], KW_OPERATOR))
		return true;
	if (IsSimple(tokens_[position], OP_RPAREN) && position >= 2 &&
		IsSimple(tokens_[position - 1], OP_LPAREN) &&
		IsSimple(tokens_[position - 2], KW_OPERATOR))
		return true;
	if (!IsSimple(tokens_[position], OP_RSQUARE) || position < 2 ||
		!IsSimple(tokens_[position - 1], OP_LSQUARE))
		return false;
	if (IsSimple(tokens_[position - 2], KW_OPERATOR))
		return true;
	return position >= 3 &&
		(IsSimple(tokens_[position - 2], KW_NEW) ||
		 IsSimple(tokens_[position - 2], KW_DELETE)) &&
		IsSimple(tokens_[position - 3], KW_OPERATOR);
}

bool RecognitionTokenSink::BeginsAngle(std::size_t position) const
{
	if (position == 0 || !IsSimple(tokens_[position], OP_LT))
		return false;
	const RecognitionToken& previous = tokens_[position - 1];
	if (previous.kind == kIdentifierToken)
	{
		if ((identifiers_.Get(previous.identifier).flags & IF_TEMPLATE) != 0)
			return true;
		if (position >= 3 && tokens_[position - 2].kind == kLiteralToken &&
			(tokens_[position - 2].properties & TP_EMPTY_STRING) != 0 &&
			IsSimple(tokens_[position - 3], KW_OPERATOR))
			return true;
	}
	if (previous.kind < kSimpleTokenCount)
	{
		const SimpleTokenKind kind =
			static_cast<SimpleTokenKind>(previous.kind);
		if (kind == KW_TEMPLATE || kind == KW_DYNAMIC_CAST ||
			kind == KW_STATIC_CAST || kind == KW_REINTERPET_CAST ||
			kind == KW_CONST_CAST)
			return true;
	}
	return EndsOperatorTemplateName(position - 1);
}

bool IsOpeningDelimiter(const RecognitionToken& token)
{
	return IsSimple(token, OP_LPAREN) || IsSimple(token, OP_LSQUARE) ||
		IsSimple(token, OP_LBRACE);
}

bool ClosesDelimiter(const RecognitionToken& token,
	SimpleTokenKind opening)
{
	return (opening == OP_LPAREN && IsSimple(token, OP_RPAREN)) ||
		(opening == OP_LSQUARE && IsSimple(token, OP_RSQUARE)) ||
		(opening == OP_LBRACE && IsSimple(token, OP_RBRACE));
}

void RecognitionTokenSink::ClassifyAngles(std::size_t* opening_count,
	std::size_t* closing_count, std::size_t* scratch_bytes)
{
	// Give each non-angle delimiter region a stable identity. Equal numeric
	// depths are insufficient: an unclosed angle in one sibling pair must not
	// consume a greater-than token from a later sibling pair.
	std::vector<std::uint32_t> contexts(tokens_.size(), 0);
	std::vector<std::uint32_t> context_stack(1, 0);
	std::vector<SimpleTokenKind> delimiters;
	std::uint32_t next_context = 1;
	for (std::size_t i = 0; i < tokens_.size(); ++i)
	{
		const RecognitionToken& token = tokens_[i];
		contexts[i] = context_stack.back();
		if (IsOpeningDelimiter(token))
		{
			delimiters.push_back(static_cast<SimpleTokenKind>(token.kind));
			context_stack.push_back(next_context++);
		}
		else if (!delimiters.empty() &&
			ClosesDelimiter(token, delimiters.back()))
		{
			delimiters.pop_back();
			context_stack.pop_back();
		}
	}

	// A possible template-name nested below an existing angle is committed
	// only when that exact (), [], or {} region contains its own closing angle.
	// Compute this predicate once so malformed and relational-heavy inputs stay
	// linear rather than rescanning the suffix for every '<'.
	std::vector<unsigned char> has_later_close(tokens_.size(), 0);
	std::vector<unsigned char> context_has_close(next_context, 0);
	for (std::size_t i = tokens_.size(); i-- != 0;)
	{
		has_later_close[i] = context_has_close[contexts[i]];
		const RecognitionToken& token = tokens_[i];
		if (IsSimple(token, OP_GT) || token.kind == kRShift1Token ||
			token.kind == kRShift2Token)
			context_has_close[contexts[i]] = 1;
	}

	std::vector<std::uint32_t> angles;
	for (std::size_t i = 0; i < tokens_.size(); ++i)
	{
		RecognitionToken& token = tokens_[i];
		const bool can_close = IsSimple(token, OP_GT) ||
			token.kind == kRShift1Token || token.kind == kRShift2Token;
		if (can_close && !angles.empty() && angles.back() == contexts[i])
		{
			token.role = AR_CLOSE;
			angles.pop_back();
			if (closing_count) ++*closing_count;
		}
		else if (BeginsAngle(i) &&
			(angles.empty() || angles.back() == contexts[i] ||
			 has_later_close[i]))
		{
			token.role = AR_OPEN;
			angles.push_back(contexts[i]);
			if (opening_count) ++*opening_count;
		}
	}
	if (scratch_bytes)
	{
		*scratch_bytes = contexts.capacity() * sizeof(std::uint32_t) +
			context_stack.capacity() * sizeof(std::uint32_t) +
			delimiters.capacity() * sizeof(SimpleTokenKind) +
			has_later_close.capacity() * sizeof(unsigned char) +
			context_has_close.capacity() * sizeof(unsigned char) +
			angles.capacity() * sizeof(std::uint32_t);
	}
}

enum GrammarNodeKind
{
	GN_TERMINAL,
	GN_NONTERMINAL,
	GN_SEQUENCE,
	GN_OPTIONAL,
	GN_ZERO_MORE,
	GN_ONE_MORE
};

enum TerminalPredicate
{
	TP_EXACT,
	TP_ANY_IDENTIFIER,
	TP_ANY_LITERAL,
	TP_EMPTY_LITERAL,
	TP_ZERO_LITERAL,
	TP_FINAL_IDENTIFIER,
	TP_OVERRIDE_IDENTIFIER,
	TP_NONPAREN
};

enum RequiredAngleRole
{
	RR_ANY,
	RR_NORMAL,
	RR_OPEN,
	RR_CLOSE
};

struct GrammarNode
{
	unsigned char kind;
	unsigned char predicate;
	unsigned char required_role;
	std::uint32_t value;
	std::uint32_t first_child;
	std::uint32_t child_count;

	GrammarNode(GrammarNodeKind node_kind, std::uint32_t node_value = 0)
		: kind(node_kind), predicate(TP_EXACT), required_role(RR_ANY),
		  value(node_value), first_child(0), child_count(0) {}
};

struct GrammarRule
{
	std::string name;
	std::vector<std::uint32_t> alternatives;
	unsigned char required_name_flag;

	explicit GrammarRule(const std::string& rule_name)
		: name(rule_name), required_name_flag(0) {}
};

struct RawRule
{
	std::string name;
	std::vector<std::string> alternatives;
};

std::string Trim(const std::string& value)
{
	std::size_t first = 0;
	while (first < value.size() &&
		std::isspace(static_cast<unsigned char>(value[first])))
		++first;
	std::size_t last = value.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(value[last - 1])))
		--last;
	return value.substr(first, last - first);
}

std::vector<RawRule> ReadRawGrammar()
{
	std::vector<RawRule> rules;
	std::string line;
	std::string continued;
	const std::string grammar(kCppGrammarDefinition);
	std::size_t start = 0;
	while (start <= grammar.size())
	{
		const std::size_t newline = grammar.find('\n', start);
		line = grammar.substr(start, newline == std::string::npos ?
			std::string::npos : newline - start);
		if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
		if (!Trim(line).empty())
		{
			if (!std::isspace(static_cast<unsigned char>(line[0])))
			{
				if (!continued.empty() || line[line.size() - 1] != ':')
					ThrowRecognitionInternalError("invalid embedded PA6 grammar");
				rules.push_back(RawRule());
				rules.back().name = Trim(line.substr(0, line.size() - 1));
			}
			else
			{
				if (rules.empty())
					ThrowRecognitionInternalError("grammar body without rule");
				std::string body = Trim(line);
				if (!continued.empty()) body = continued + " " + body;
				if (!body.empty() && body[body.size() - 1] == '\\')
				{
					continued = Trim(body.substr(0, body.size() - 1));
				}
				else
				{
					rules.back().alternatives.push_back(body);
					continued.clear();
				}
			}
		}
		if (newline == std::string::npos) break;
		start = newline + 1;
	}
	if (!continued.empty())
		ThrowRecognitionInternalError("unterminated grammar line");
	return rules;
}

class Grammar;

class GrammarExpressionReader
{
public:
	GrammarExpressionReader(Grammar& grammar, const std::string& lhs,
		const std::string& expression);
	std::uint32_t Parse();

private:
	std::uint32_t ParseSequence();
	std::string Peek() const;
	std::string Take();

	Grammar& grammar_;
	std::string lhs_;
	std::vector<std::string> tokens_;
	std::size_t position_;
};

class Grammar
{
public:
	Grammar()
		: translation_unit_(0), decl_specifier_seq_(0),
		  decl_specifier_(0), attribute_specifier_(0)
	{
		const std::vector<RawRule> raw = ReadRawGrammar();
		for (std::size_t i = 0; i < raw.size(); ++i)
		{
			if (rule_ids_.count(raw[i].name) != 0)
				ThrowRecognitionInternalError("duplicate grammar rule");
			const std::uint32_t id = static_cast<std::uint32_t>(rules_.size());
			rule_ids_[raw[i].name] = id;
			rules_.push_back(GrammarRule(raw[i].name));
		}
		BuildSimpleTokenNames();
		for (std::size_t i = 0; i < raw.size(); ++i)
		{
			for (std::size_t j = 0; j < raw[i].alternatives.size(); ++j)
			{
				GrammarExpressionReader reader(*this, raw[i].name,
					raw[i].alternatives[j]);
				rules_[i].alternatives.push_back(reader.Parse());
			}
		}
		SetNameCategory("class-name", IF_CLASS);
		SetNameCategory("template-name", IF_TEMPLATE);
		SetNameCategory("typedef-name", IF_TYPEDEF);
		SetNameCategory("enum-name", IF_ENUM);
		SetNameCategory("namespace-name", IF_NAMESPACE);
		translation_unit_ = RuleId("translation-unit");
		decl_specifier_seq_ = RuleId("decl-specifier-seq");
		decl_specifier_ = RuleId("decl-specifier");
		attribute_specifier_ = RuleId("attribute-specifier");
	}

	std::uint32_t AddSequence(const std::vector<std::uint32_t>& children)
	{
		GrammarNode node(GN_SEQUENCE);
		node.first_child = static_cast<std::uint32_t>(children_.size());
		node.child_count = static_cast<std::uint32_t>(children.size());
		children_.insert(children_.end(), children.begin(), children.end());
		return AddNode(node);
	}

	std::uint32_t AddUnary(GrammarNodeKind kind, std::uint32_t child)
	{
		return AddNode(GrammarNode(kind, child));
	}

	std::uint32_t AddSymbol(const std::string& symbol,
		const std::string& lhs)
	{
		std::unordered_map<std::string, std::uint32_t>::const_iterator rule =
			rule_ids_.find(symbol);
		if (rule != rule_ids_.end())
			return AddNode(GrammarNode(GN_NONTERMINAL, rule->second));
		GrammarNode node(GN_TERMINAL);
		ResolveTerminal(symbol, lhs, &node);
		return AddNode(node);
	}

	const GrammarRule& Rule(std::uint32_t id) const { return rules_[id]; }
	const GrammarNode& Node(std::uint32_t id) const { return nodes_[id]; }
	std::uint32_t Child(std::uint32_t id) const { return children_[id]; }
	std::size_t RuleCount() const { return rules_.size(); }
	std::uint32_t TranslationUnit() const { return translation_unit_; }
	std::uint32_t DeclSpecifierSeq() const { return decl_specifier_seq_; }
	std::uint32_t DeclSpecifier() const { return decl_specifier_; }
	std::uint32_t AttributeSpecifier() const { return attribute_specifier_; }

private:
	std::uint32_t AddNode(const GrammarNode& node)
	{
		nodes_.push_back(node);
		return static_cast<std::uint32_t>(nodes_.size() - 1);
	}

	std::uint32_t RuleId(const std::string& name) const
	{
		std::unordered_map<std::string, std::uint32_t>::const_iterator found =
			rule_ids_.find(name);
		if (found == rule_ids_.end())
			ThrowRecognitionInternalError("missing grammar rule");
		return found->second;
	}

	void BuildSimpleTokenNames()
	{
		for (std::uint16_t i = 0; i < kSimpleTokenCount; ++i)
			simple_tokens_[SimpleTokenKindName(static_cast<SimpleTokenKind>(i))] = i;
	}

	void SetNameCategory(const std::string& name, unsigned char flag)
	{
		rules_[RuleId(name)].required_name_flag = flag;
	}

	void ResolveTerminal(const std::string& symbol, const std::string& lhs,
		GrammarNode* node) const;

	std::vector<GrammarRule> rules_;
	std::vector<GrammarNode> nodes_;
	std::vector<std::uint32_t> children_;
	std::unordered_map<std::string, std::uint32_t> rule_ids_;
	std::unordered_map<std::string, std::uint32_t> simple_tokens_;
	std::uint32_t translation_unit_;
	std::uint32_t decl_specifier_seq_;
	std::uint32_t decl_specifier_;
	std::uint32_t attribute_specifier_;
};

void Grammar::ResolveTerminal(const std::string& symbol,
	const std::string& lhs, GrammarNode* node) const
{
	node->predicate = TP_EXACT;
	node->required_role = RR_ANY;
	if (symbol == "TT_IDENTIFIER")
	{
		node->value = kIdentifierToken;
		node->predicate = TP_ANY_IDENTIFIER;
	}
	else if (symbol == "TT_LITERAL")
	{
		node->value = kLiteralToken;
		node->predicate = TP_ANY_LITERAL;
	}
	else if (symbol == "ST_EMPTYSTR")
	{
		node->value = kLiteralToken;
		node->predicate = TP_EMPTY_LITERAL;
	}
	else if (symbol == "ST_ZERO")
	{
		node->value = kLiteralToken;
		node->predicate = TP_ZERO_LITERAL;
	}
	else if (symbol == "ST_FINAL" || symbol == "ST_OVERRIDE")
	{
		node->value = kIdentifierToken;
		node->predicate = symbol == "ST_FINAL" ?
			TP_FINAL_IDENTIFIER : TP_OVERRIDE_IDENTIFIER;
	}
	else if (symbol == "ST_NONPAREN")
	{
		node->value = 0;
		node->predicate = TP_NONPAREN;
	}
	else if (symbol == "ST_EOF") node->value = kEofToken;
	else if (symbol == "ST_RSHIFT_1") node->value = kRShift1Token;
	else if (symbol == "ST_RSHIFT_2") node->value = kRShift2Token;
	else
	{
		std::unordered_map<std::string, std::uint32_t>::const_iterator found =
			simple_tokens_.find(symbol);
		if (found == simple_tokens_.end())
			ThrowRecognitionInternalError("unknown grammar terminal: " + symbol);
		node->value = found->second;
	}
	const bool angle_terminal = symbol == "OP_LT" || symbol == "OP_GT" ||
		symbol == "ST_RSHIFT_1" || symbol == "ST_RSHIFT_2";
	if (!angle_terminal) return;
	if (lhs == "close-angle-bracket") node->required_role = RR_CLOSE;
	else if (lhs == "relational-operator" || lhs == "shift-operator" ||
		lhs == "operator-function-id") node->required_role = RR_NORMAL;
	else node->required_role = RR_OPEN;
}

GrammarExpressionReader::GrammarExpressionReader(Grammar& grammar,
	const std::string& lhs, const std::string& expression)
	: grammar_(grammar), lhs_(lhs), position_(0)
{
	std::size_t offset = 0;
	while (offset < expression.size())
	{
		if (std::isspace(static_cast<unsigned char>(expression[offset])))
		{
			++offset;
			continue;
		}
		const char c = expression[offset];
		if (c == '(' || c == ')' || c == '?' || c == '*' || c == '+')
		{
			tokens_.push_back(std::string(1, c));
			++offset;
			continue;
		}
		std::size_t end = offset;
		while (end < expression.size() &&
			!std::isspace(static_cast<unsigned char>(expression[end])) &&
			expression[end] != '(' && expression[end] != ')' &&
			expression[end] != '?' && expression[end] != '*' &&
			expression[end] != '+')
			++end;
		tokens_.push_back(expression.substr(offset, end - offset));
		offset = end;
	}
}

std::string GrammarExpressionReader::Peek() const
{
	return position_ == tokens_.size() ? std::string() : tokens_[position_];
}

std::string GrammarExpressionReader::Take()
{
	if (position_ == tokens_.size())
		ThrowRecognitionInternalError("incomplete EBNF");
	return tokens_[position_++];
}

std::uint32_t GrammarExpressionReader::ParseSequence()
{
	std::vector<std::uint32_t> children;
	while (!Peek().empty() && Peek() != ")")
	{
		std::uint32_t child;
		if (Peek() == "(")
		{
			Take();
			child = ParseSequence();
			if (Take() != ")")
				ThrowRecognitionInternalError("unclosed EBNF group");
		}
		else
		{
			child = grammar_.AddSymbol(Take(), lhs_);
		}
		const std::string modifier = Peek();
		if (modifier == "?" || modifier == "*" || modifier == "+")
		{
			Take();
			child = grammar_.AddUnary(modifier == "?" ? GN_OPTIONAL :
				(modifier == "*" ? GN_ZERO_MORE : GN_ONE_MORE), child);
		}
		children.push_back(child);
	}
	return grammar_.AddSequence(children);
}

std::uint32_t GrammarExpressionReader::Parse()
{
	const std::uint32_t root = ParseSequence();
	if (position_ != tokens_.size())
		ThrowRecognitionInternalError("extra EBNF token");
	return root;
}

const Grammar& CppGrammar()
{
	static const Grammar grammar;
	return grammar;
}

class Recognizer
{
public:
	Recognizer(const std::vector<RecognitionToken>& tokens,
		const IdentifierTable& identifiers, Stats* stats)
		: grammar_(CppGrammar()), tokens_(tokens), identifiers_(identifiers),
		  stats_(stats), stride_(tokens.size() + 1),
		  memo_(grammar_.RuleCount() * stride_, kMemoUnvisited)
	{
		if (tokens.size() >= kParseFailure)
			ThrowRecognitionResourceLimit("too many recognition tokens");
		if (stats_)
		{
			stats_->memo_entries = memo_.size();
			stats_->memo_storage_bytes =
				memo_.capacity() * sizeof(std::uint32_t);
			stats_->peak_stage_storage_bytes = std::max(
				stats_->peak_stage_storage_bytes,
				stats_->token_storage_bytes + stats_->identifier_storage_bytes +
				stats_->memo_storage_bytes);
		}
	}

	bool Recognize()
	{
		return ParseNonterminal(grammar_.TranslationUnit(), 0) == tokens_.size();
	}

private:
	std::uint32_t ParseNonterminal(std::uint32_t rule, std::uint32_t position);
	std::uint32_t ParseNode(std::uint32_t node, std::uint32_t position);
	std::uint32_t ParseDeclSpecifierSeq(std::uint32_t position);
	bool MatchesTerminal(const GrammarNode& node,
		const RecognitionToken& token) const;
	bool MeetsNameCategory(const GrammarRule& rule,
		std::uint32_t position) const;
	bool StartsTypeName(std::uint32_t position) const;
	bool StartsNonCvTypeSpecifier(std::uint32_t position) const;

	const Grammar& grammar_;
	const std::vector<RecognitionToken>& tokens_;
	const IdentifierTable& identifiers_;
	Stats* stats_;
	std::size_t stride_;
	std::vector<std::uint32_t> memo_;
};

bool Recognizer::MeetsNameCategory(const GrammarRule& rule,
	std::uint32_t position) const
{
	if (rule.required_name_flag == 0) return true;
	if (position >= tokens_.size() ||
		tokens_[position].kind != kIdentifierToken) return false;
	return (identifiers_.Get(tokens_[position].identifier).flags &
		rule.required_name_flag) != 0;
}

bool Recognizer::MatchesTerminal(const GrammarNode& node,
	const RecognitionToken& token) const
{
	if (node.required_role == RR_NORMAL && token.role != AR_NORMAL) return false;
	if (node.required_role == RR_OPEN && token.role != AR_OPEN) return false;
	if (node.required_role == RR_CLOSE && token.role != AR_CLOSE) return false;
	switch (node.predicate)
	{
	case TP_ANY_IDENTIFIER: return token.kind == kIdentifierToken;
	case TP_ANY_LITERAL: return token.kind == kLiteralToken;
	case TP_EMPTY_LITERAL:
		return token.kind == kLiteralToken &&
			(token.properties & TP_EMPTY_STRING) != 0;
	case TP_ZERO_LITERAL:
		return token.kind == kLiteralToken && (token.properties & TP_ZERO) != 0;
	case TP_FINAL_IDENTIFIER: case TP_OVERRIDE_IDENTIFIER:
		if (token.kind != kIdentifierToken) return false;
		return identifiers_.Get(token.identifier).spelling ==
			(node.predicate == TP_FINAL_IDENTIFIER ? "final" : "override");
	case TP_NONPAREN:
		return token.kind != kEofToken && !IsSimple(token, OP_LPAREN) &&
			!IsSimple(token, OP_RPAREN) && !IsSimple(token, OP_LSQUARE) &&
			!IsSimple(token, OP_RSQUARE) && !IsSimple(token, OP_LBRACE) &&
			!IsSimple(token, OP_RBRACE);
	default:
		return token.kind == node.value;
	}
}

std::uint32_t Recognizer::ParseNode(std::uint32_t node_id,
	std::uint32_t position)
{
	if (stats_) ++stats_->expression_evaluations;
	const GrammarNode& node = grammar_.Node(node_id);
	if (node.kind == GN_TERMINAL)
	{
		if (position >= tokens_.size() ||
			!MatchesTerminal(node, tokens_[position])) return kParseFailure;
		return position + 1;
	}
	if (node.kind == GN_NONTERMINAL)
		return ParseNonterminal(node.value, position);
	if (node.kind == GN_SEQUENCE)
	{
		std::uint32_t end = position;
		for (std::uint32_t i = 0; i < node.child_count; ++i)
		{
			end = ParseNode(grammar_.Child(node.first_child + i), end);
			if (end == kParseFailure) return end;
		}
		return end;
	}
	std::uint32_t end = ParseNode(node.value, position);
	if (node.kind == GN_OPTIONAL)
		return end == kParseFailure ? position : end;
	if (end == kParseFailure) return node.kind == GN_ZERO_MORE ? position : end;
	if (end == position) return end;
	while (true)
	{
		const std::uint32_t next = ParseNode(node.value, end);
		if (next == kParseFailure || next == end) return end;
		end = next;
	}
}

bool Recognizer::StartsTypeName(std::uint32_t position) const
{
	if (position >= tokens_.size() || tokens_[position].kind != kIdentifierToken)
		return false;
	const unsigned char flags = identifiers_.Get(
		tokens_[position].identifier).flags;
	if ((flags & (IF_CLASS | IF_TYPEDEF | IF_ENUM)) != 0) return true;
	return (flags & IF_TEMPLATE) != 0 && position + 1 < tokens_.size() &&
		IsSimple(tokens_[position + 1], OP_LT) &&
		tokens_[position + 1].role == AR_OPEN;
}

bool Recognizer::StartsNonCvTypeSpecifier(std::uint32_t position) const
{
	if (position >= tokens_.size()) return false;
	const RecognitionToken& token = tokens_[position];
	if (token.kind >= kSimpleTokenCount) return true;
	switch (static_cast<SimpleTokenKind>(token.kind))
	{
	case KW_REGISTER: case KW_STATIC: case KW_THREAD_LOCAL: case KW_EXTERN:
	case KW_MUTABLE: case KW_INLINE: case KW_VIRTUAL: case KW_EXPLICIT:
	case KW_FRIEND: case KW_TYPEDEF: case KW_CONSTEXPR:
	case KW_CONST: case KW_VOLATILE:
		return false;
	default:
		return true;
	}
}

std::uint32_t Recognizer::ParseDeclSpecifierSeq(std::uint32_t position)
{
	std::uint32_t end = position;
	std::size_t count = 0;
	bool saw_non_cv_type = false;
	while (!saw_non_cv_type || !StartsTypeName(end))
	{
		const std::uint32_t next =
			ParseNonterminal(grammar_.DeclSpecifier(), end);
		if (next == kParseFailure || next == end) break;
		if (StartsNonCvTypeSpecifier(end)) saw_non_cv_type = true;
		end = next;
		++count;
	}
	if (count == 0) return kParseFailure;
	while (true)
	{
		const std::uint32_t next =
			ParseNonterminal(grammar_.AttributeSpecifier(), end);
		if (next == kParseFailure || next == end) return end;
		end = next;
	}
}

std::uint32_t Recognizer::ParseNonterminal(std::uint32_t rule_id,
	std::uint32_t position)
{
	if (stats_) ++stats_->memo_queries;
	if (position > tokens_.size()) return kParseFailure;
	std::uint32_t& memo = memo_[rule_id * stride_ + position];
	if (memo != kMemoUnvisited && memo != kMemoInProgress)
	{
		if (stats_) ++stats_->memo_hits;
		return memo;
	}
	if (memo == kMemoInProgress) return kParseFailure;
	memo = kMemoInProgress;
	if (stats_) ++stats_->rule_evaluations;
	const GrammarRule& rule = grammar_.Rule(rule_id);
	std::uint32_t best = kParseFailure;
	if (MeetsNameCategory(rule, position))
	{
		if (rule_id == grammar_.DeclSpecifierSeq())
		{
			best = ParseDeclSpecifierSeq(position);
		}
		else
		{
			for (std::size_t i = 0; i < rule.alternatives.size(); ++i)
			{
				const std::uint32_t end =
					ParseNode(rule.alternatives[i], position);
				if (end != kParseFailure &&
					(best == kParseFailure || end > best)) best = end;
			}
		}
	}
	memo = best;
	return best;
}

}

Stats::Stats()
	: tokens(0), interned_identifiers(0), interned_identifier_bytes(0),
	  token_storage_bytes(0), identifier_storage_bytes(0),
	  angle_scratch_bytes(0), angle_openings(0), angle_closings(0),
	  memo_queries(0), memo_hits(0), rule_evaluations(0),
	  expression_evaluations(0), memo_entries(0), memo_storage_bytes(0),
	  peak_stage_storage_bytes(0), elapsed_nanoseconds(0)
{
}

bool RecognizeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	Stats* stats)
{
	std::chrono::steady_clock::time_point started;
	if (stats)
	{
		*stats = Stats();
		started = std::chrono::steady_clock::now();
	}
	RecognitionTokenSink sink;
	PreprocessFile(path, source, sink, options,
		stats ? &stats->preprocessing : 0);
	sink.ClassifyAngles(stats ? &stats->angle_openings : 0,
		stats ? &stats->angle_closings : 0,
		stats ? &stats->angle_scratch_bytes : 0);
	if (stats)
	{
		stats->tokens = sink.Tokens().size();
		stats->interned_identifiers = sink.Identifiers().Size();
		stats->interned_identifier_bytes = sink.Identifiers().SpellingBytes();
		stats->token_storage_bytes = sink.TokenStorageBytes();
		stats->identifier_storage_bytes = sink.Identifiers().StorageBytes();
		stats->peak_stage_storage_bytes = stats->token_storage_bytes +
			stats->identifier_storage_bytes + stats->angle_scratch_bytes;
	}
	Recognizer parser(sink.Tokens(), sink.Identifiers(), stats);
	const bool accepted = parser.Recognize();
	if (stats)
	{
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
	return accepted;
}

}
}
