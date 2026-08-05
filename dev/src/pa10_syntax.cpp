#include "pa10_syntax.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppgm
{
namespace
{

typedef std::uint32_t TextId;
typedef std::uint32_t NodeId;

const std::uint16_t kSimpleTokenCount =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kIdentifierToken = kSimpleTokenCount;
const std::uint16_t kLiteralToken = kSimpleTokenCount + 1;
const std::uint16_t kEofToken = kSimpleTokenCount + 2;
const std::uint16_t kRShiftFirstToken = kSimpleTokenCount + 3;
const std::uint16_t kRShiftSecondToken = kSimpleTokenCount + 4;
const NodeId kNoNode = std::numeric_limits<NodeId>::max();
const std::uint32_t kNoEdge = std::numeric_limits<std::uint32_t>::max();

std::size_t HashText(const std::string& text)
{
	std::size_t value = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1469598103934665603ULL) :
		static_cast<std::size_t>(2166136261U);
	const std::size_t prime = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1099511628211ULL) :
		static_cast<std::size_t>(16777619U);
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		value ^= static_cast<unsigned char>(text[i]);
		value *= prime;
	}
	return value;
}

class StringTable
{
public:
	StringTable() : slots_(32, 0), spelling_bytes_(0)
	{
		texts_.push_back(std::string());
	}

	TextId Intern(const std::string& text)
	{
		if ((texts_.size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = HashText(text) & mask;
		while (slots_[slot] != 0)
		{
			const TextId id = slots_[slot];
			if (texts_[id] == text) return id;
			slot = (slot + 1) & mask;
		}
		if (texts_.size() > std::numeric_limits<TextId>::max())
			throw std::runtime_error("too many syntax spellings");
		const TextId id = static_cast<TextId>(texts_.size());
		texts_.push_back(text);
		spelling_bytes_ += text.size();
		slots_[slot] = id;
		return id;
	}

	const std::string& Get(TextId id) const { return texts_[id]; }
	std::size_t Size() const { return texts_.size() - 1; }
	std::size_t SpellingBytes() const { return spelling_bytes_; }
	std::size_t StorageBytes() const
	{
		std::size_t bytes = texts_.capacity() * sizeof(std::string) +
			slots_.capacity() * sizeof(TextId);
		for (std::size_t i = 1; i < texts_.size(); ++i)
			bytes += texts_[i].capacity();
		return bytes;
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<TextId> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (TextId id = 1; id < texts_.size(); ++id)
		{
			std::size_t slot = HashText(texts_[id]) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = id;
		}
		slots_.swap(replacement);
	}

	std::vector<std::string> texts_;
	std::vector<TextId> slots_;
	std::size_t spelling_bytes_;
};

struct SyntaxToken
{
	std::uint16_t kind;
	TextId spelling;

	SyntaxToken(std::uint16_t kind_value, TextId spelling_value)
		: kind(kind_value), spelling(spelling_value) {}
};

class SyntaxTokenSink : public IPostTokenStream
{
public:
	explicit SyntaxTokenSink(StringTable& strings) : strings_(strings) {}

	void EmitInvalid(const std::string& source)
	{
		throw std::runtime_error("invalid phase-7 token: " + source);
	}

	void EmitSimple(const std::string& source, SimpleTokenKind kind)
	{
		if (kind == OP_RSHIFT)
		{
			const TextId close = strings_.Intern(">");
			tokens_.push_back(SyntaxToken(kRShiftFirstToken, close));
			tokens_.push_back(SyntaxToken(kRShiftSecondToken, close));
			return;
		}
		tokens_.push_back(SyntaxToken(static_cast<std::uint16_t>(kind),
			strings_.Intern(source)));
	}

	void EmitIdentifier(const std::string& source)
	{
		tokens_.push_back(SyntaxToken(kIdentifierToken,
			strings_.Intern(source)));
	}

	void EmitLiteral(const std::string& source, FundamentalType,
		const void*, std::size_t)
	{
		EmitLiteralSpelling(source);
	}

	void EmitLiteralArray(const std::string& source, std::size_t,
		FundamentalType, const void*, std::size_t)
	{
		EmitLiteralSpelling(source);
	}

	void EmitUserDefinedCharacter(const std::string& source,
		const std::string&, FundamentalType, const void*, std::size_t)
	{
		EmitLiteralSpelling(source);
	}

	void EmitUserDefinedString(const std::string& source,
		const std::string&, std::size_t, FundamentalType,
		const void*, std::size_t)
	{
		EmitLiteralSpelling(source);
	}

	void EmitUserDefinedInteger(const std::string& source,
		const std::string&, const std::string&)
	{
		EmitLiteralSpelling(source);
	}

	void EmitUserDefinedFloating(const std::string& source,
		const std::string&, const std::string&)
	{
		EmitLiteralSpelling(source);
	}

	void EmitEof()
	{
		tokens_.push_back(SyntaxToken(kEofToken,
			strings_.Intern(std::string())));
	}

	const std::vector<SyntaxToken>& Tokens() const { return tokens_; }
	std::size_t StorageBytes() const
	{
		return tokens_.capacity() * sizeof(SyntaxToken);
	}

private:
	void EmitLiteralSpelling(const std::string& source)
	{
		tokens_.push_back(SyntaxToken(kLiteralToken,
			strings_.Intern(source)));
	}

	StringTable& strings_;
	std::vector<SyntaxToken> tokens_;
};

struct SyntaxNode
{
	TextId label;
	std::uint32_t first_edge;
	std::uint32_t last_edge;

	explicit SyntaxNode(TextId label_value)
		: label(label_value), first_edge(kNoEdge), last_edge(kNoEdge) {}
};

struct SyntaxEdge
{
	NodeId child;
	std::uint32_t next;

	explicit SyntaxEdge(NodeId child_value)
		: child(child_value), next(kNoEdge) {}
};

class SyntaxArena
{
public:
	explicit SyntaxArena(StringTable& strings) : strings_(strings) {}

	NodeId Make(const std::string& label)
	{
		if (nodes_.size() >= kNoNode)
			throw std::runtime_error("too many syntax nodes");
		const NodeId id = static_cast<NodeId>(nodes_.size());
		nodes_.push_back(SyntaxNode(strings_.Intern(label)));
		return id;
	}

	void Add(NodeId parent, NodeId child)
	{
		if (parent == kNoNode || child == kNoNode) return;
		if (edges_.size() >= kNoEdge)
			throw std::runtime_error("too many syntax edges");
		const std::uint32_t edge = static_cast<std::uint32_t>(edges_.size());
		edges_.push_back(SyntaxEdge(child));
		SyntaxNode& node = nodes_[parent];
		if (node.first_edge == kNoEdge) node.first_edge = edge;
		else edges_[node.last_edge].next = edge;
		node.last_edge = edge;
	}
	void Prepend(NodeId parent, NodeId child)
	{
		if (parent == kNoNode || child == kNoNode) return;
		if (edges_.size() >= kNoEdge)
			throw std::runtime_error("too many syntax edges");
		const std::uint32_t edge = static_cast<std::uint32_t>(edges_.size());
		edges_.push_back(SyntaxEdge(child));
		SyntaxNode& node = nodes_[parent];
		edges_[edge].next = node.first_edge;
		node.first_edge = edge;
		if (node.last_edge == kNoEdge) node.last_edge = edge;
	}

	std::size_t NodeMark() const { return nodes_.size(); }
	std::size_t EdgeMark() const { return edges_.size(); }

	void Rollback(std::size_t node_mark, std::size_t edge_mark)
	{
		nodes_.erase(nodes_.begin() + node_mark, nodes_.end());
		edges_.erase(edges_.begin() + edge_mark, edges_.end());
		// Any surviving node can only have edges below edge_mark: callers mark
		// before attaching speculative children to an existing parent.
	}

	void Write(std::ostream& output, NodeId root) const
	{
		struct Frame
		{
			NodeId node;
			std::uint32_t edge;
			std::size_t depth;
			bool entered;
			Frame(NodeId node_value, std::size_t depth_value)
				: node(node_value), edge(kNoEdge), depth(depth_value),
				  entered(false) {}
		};
		std::vector<Frame> stack;
		stack.push_back(Frame(root, 0));
		while (!stack.empty())
		{
			Frame& frame = stack.back();
			const SyntaxNode& node = nodes_[frame.node];
			if (!frame.entered)
			{
				for (std::size_t i = 0; i < frame.depth; ++i) output << "  ";
				output << strings_.Get(node.label) << '\n';
				frame.entered = true;
				frame.edge = node.first_edge;
			}
			if (frame.edge == kNoEdge)
			{
				stack.pop_back();
				continue;
			}
			const std::uint32_t edge = frame.edge;
			frame.edge = edges_[edge].next;
			stack.push_back(Frame(edges_[edge].child, frame.depth + 1));
		}
	}

	std::size_t Nodes() const { return nodes_.size(); }
	std::size_t Edges() const { return edges_.size(); }
	const std::string& Label(NodeId node) const
	{
		return strings_.Get(nodes_[node].label);
	}
	void SetLabel(NodeId node, const std::string& label)
	{
		nodes_[node].label = strings_.Intern(label);
	}
	bool HasDirectChildPrefix(NodeId node, const std::string& prefix) const
	{
		for (std::uint32_t edge = nodes_[node].first_edge;
			edge != kNoEdge; edge = edges_[edge].next)
			if (Label(edges_[edge].child).compare(0, prefix.size(), prefix) == 0)
				return true;
		return false;
	}
	std::size_t StorageBytes() const
	{
		return nodes_.capacity() * sizeof(SyntaxNode) +
			edges_.capacity() * sizeof(SyntaxEdge);
	}

private:
	StringTable& strings_;
	std::vector<SyntaxNode> nodes_;
	std::vector<SyntaxEdge> edges_;
};

bool IsFundamentalKind(std::uint16_t kind)
{
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case KW_BOOL: case KW_CHAR: case KW_CHAR16_T: case KW_CHAR32_T:
	case KW_DOUBLE: case KW_FLOAT: case KW_INT: case KW_LONG: case KW_SHORT:
	case KW_SIGNED: case KW_UNSIGNED: case KW_VOID: case KW_WCHAR_T:
		return true;
	default:
		return false;
	}
}

bool IsDeclSpecifierKeyword(std::uint16_t kind)
{
	if (kind < kSimpleTokenCount && IsFundamentalKind(kind)) return true;
	if (kind >= kSimpleTokenCount) return false;
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case KW_CONST: case KW_VOLATILE: case KW_TYPEDEF: case KW_EXTERN:
	case KW_STATIC: case KW_INLINE: case KW_VIRTUAL: case KW_CONSTEXPR:
	case KW_THREAD_LOCAL: case KW_AUTO: case KW_FRIEND: case KW_MUTABLE:
	case KW_REGISTER:
		return true;
	default:
		return false;
	}
}

bool IsAssignmentOperator(std::uint16_t kind)
{
	if (kind >= kSimpleTokenCount) return false;
	switch (static_cast<SimpleTokenKind>(kind))
	{
	case OP_ASS: case OP_PLUSASS: case OP_MINUSASS: case OP_STARASS:
	case OP_DIVASS: case OP_MODASS: case OP_XORASS: case OP_BANDASS:
	case OP_BORASS: case OP_LSHIFTASS: case OP_RSHIFTASS:
		return true;
	default:
		return false;
	}
}

class Parser
{
public:
	Parser(const std::vector<SyntaxToken>& tokens, StringTable& strings,
		SyntaxArena& arena, SyntaxStats* stats)
		: tokens_(tokens), strings_(strings), arena_(arena), stats_(stats),
		  position_(0), angle_stop_depth_(0)
	{
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

private:
	struct Mark
	{
		std::size_t position;
		std::size_t nodes;
		std::size_t edges;
	};

	Mark Checkpoint()
	{
		if (stats_) ++stats_->parser_checkpoints;
		Mark mark = {position_, arena_.NodeMark(), arena_.EdgeMark()};
		return mark;
	}

	void Rollback(const Mark& mark)
	{
		if (stats_) ++stats_->parser_rollbacks;
		position_ = mark.position;
		arena_.Rollback(mark.nodes, mark.edges);
	}

	std::runtime_error Error(const std::string& message) const
	{
		return std::runtime_error(message + " at token " +
			std::to_string(position_) +
			(position_ < tokens_.size() ? " (`" + Spelling(position_) + "`)" :
			 std::string()));
	}

	bool At(SimpleTokenKind kind) const
	{
		return position_ < tokens_.size() && tokens_[position_].kind ==
			static_cast<std::uint16_t>(kind);
	}

	bool AtOffset(std::size_t offset, SimpleTokenKind kind) const
	{
		return position_ + offset < tokens_.size() &&
			tokens_[position_ + offset].kind ==
				static_cast<std::uint16_t>(kind);
	}

	bool AtIdentifier() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == kIdentifierToken;
	}

	bool AtLiteral() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == kLiteralToken;
	}

	bool AtEof() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == kEofToken;
	}

	bool AtCloseAngle() const
	{
		if (position_ >= tokens_.size()) return false;
		const std::uint16_t kind = tokens_[position_].kind;
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
		if (token.kind == kIdentifierToken) return "TT_IDENTIFIER:" +
			Spelling(position);
		if (token.kind == kLiteralToken) return Spelling(position);
		if (token.kind == kRShiftFirstToken ||
			token.kind == kRShiftSecondToken) return "OP_RSHIFT:>>";
		return std::string(SimpleTokenKindName(
			static_cast<SimpleTokenKind>(token.kind))) + ":" +
			Spelling(position);
	}

	std::string JoinSpellings(std::size_t first, std::size_t last) const
	{
		std::string result;
		for (std::size_t i = first; i < last; ++i)
		{
			if (i != first)
			{
				const std::uint16_t previous = tokens_[i - 1].kind;
				if (previous == static_cast<std::uint16_t>(KW_CONST) ||
					previous == static_cast<std::uint16_t>(KW_VOLATILE) ||
					previous == static_cast<std::uint16_t>(KW_TYPENAME) ||
					previous == static_cast<std::uint16_t>(KW_TEMPLATE))
					result += ' ';
			}
			result += Spelling(i);
		}
		return result;
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
		const Mark mark = Checkpoint();
		if (AtIdentifier() && Spelling(position_) == "__attribute__")
		{
			++position_;
			if (SkipBalanced(OP_LPAREN, OP_RPAREN)) return true;
			Rollback(mark);
			return false;
		}
		if (At(KW_ALIGNAS))
		{
			++position_;
			if (SkipBalanced(OP_LPAREN, OP_RPAREN)) return true;
			Rollback(mark);
			return false;
		}
		if (At(OP_LSQUARE) && AtOffset(1, OP_LSQUARE))
		{
			position_ += 2;
			while (!AtEof())
			{
				if (At(OP_RSQUARE) && AtOffset(1, OP_RSQUARE))
				{
					position_ += 2;
					return true;
				}
				++position_;
			}
		}
		Rollback(mark);
		return false;
	}

	void SkipAttributes()
	{
		while (SkipAttribute()) {}
	}

	bool IsLikelyTypeIdentifier(std::size_t position) const
	{
		if (position >= tokens_.size() ||
			tokens_[position].kind != kIdentifierToken) return false;
		const std::string& name = Spelling(position);
		if (known_types_.find(name) != known_types_.end()) return true;
		return name.find('C') != std::string::npos ||
			name.find('T') != std::string::npos ||
			name.find('Y') != std::string::npos ||
			name.find('E') != std::string::npos;
	}
	bool QualifiedStartsType() const
	{
		std::size_t scan = position_;
		if (scan < tokens_.size() && tokens_[scan].kind ==
			static_cast<std::uint16_t>(OP_COLON2)) ++scan;
		std::size_t last = tokens_.size();
		while (scan < tokens_.size() && tokens_[scan].kind == kIdentifierToken)
		{
			last = scan++;
			if (scan >= tokens_.size() || tokens_[scan].kind !=
				static_cast<std::uint16_t>(OP_COLON2)) break;
			++scan;
		}
		return last != tokens_.size() &&
			(known_types_.find(Spelling(last)) != known_types_.end() ||
			 IsLikelyTypeIdentifier(last));
	}

	bool ParseName(std::string* text, bool allow_qualified = true,
		bool allow_operator = true, bool allow_template_arguments = true)
	{
		const Mark mark = Checkpoint();
		const std::size_t first = position_;
		if (allow_qualified) Match(OP_COLON2);
		if (At(KW_OPERATOR) && allow_operator)
		{
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
			else if (At(KW_NEW) || At(KW_DELETE))
			{
				++position_;
				if (Match(OP_LSQUARE)) Expect(OP_RSQUARE);
			}
			else if (Match(OP_LPAREN)) Expect(OP_RPAREN);
			else if (Match(OP_LSQUARE)) Expect(OP_RSQUARE);
			else if (position_ < tokens_.size() &&
				tokens_[position_].kind < kSimpleTokenCount &&
				tokens_[position_].kind !=
					static_cast<std::uint16_t>(OP_COLON2))
				++position_;
			else
			{
				while (At(KW_CONST) || At(KW_VOLATILE)) ++position_;
				std::string conversion_type;
				if (position_ < tokens_.size() &&
					IsFundamentalKind(tokens_[position_].kind))
					++position_;
				else if (!ParseName(&conversion_type, true, false, true))
				{
					Rollback(mark);
					return false;
				}
				while (Match(OP_STAR) || Match(OP_AMP) || Match(OP_LAND))
					while (At(KW_CONST) || At(KW_VOLATILE)) ++position_;
			}
		}
		else
		{
			Match(OP_COMPL);
			if (!AtIdentifier())
			{
				Rollback(mark);
				return false;
			}
			++position_;
		}
		if (allow_template_arguments) TryConsumeTemplateArguments();
		if (allow_qualified)
		{
			while (Match(OP_COLON2))
			{
				Match(KW_TEMPLATE);
				Match(OP_COMPL);
				if (At(KW_OPERATOR) && allow_operator)
				{
					++position_;
					if (AtIdentifier() || At(KW_NEW) || At(KW_DELETE) ||
						(position_ < tokens_.size() &&
						 tokens_[position_].kind < kSimpleTokenCount))
						++position_;
					else
					{
						Rollback(mark);
						return false;
					}
				}
				else if (AtIdentifier()) ++position_;
				else
				{
					Rollback(mark);
					return false;
				}
				if (allow_template_arguments) TryConsumeTemplateArguments();
			}
		}
		*text = JoinSpellings(first, position_);
		const std::size_t conversion = text->rfind("::operator");
		if (conversion != std::string::npos)
		{
			const std::size_t after = conversion +
				std::string("::operator").size();
			if (after < text->size() &&
				std::isalnum(static_cast<unsigned char>((*text)[after])))
				text->insert(after, " ");
		}
		return true;
	}

	void TryConsumeTemplateArguments()
	{
		if (!At(OP_LT)) return;
		const std::string candidate = position_ == 0 ? std::string() :
			Spelling(position_ - 1);
		const bool qualified_candidate = position_ >= 2 &&
			tokens_[position_ - 2].kind ==
				static_cast<std::uint16_t>(OP_COLON2);
		const bool trusted =
			(qualified_candidate || non_template_names_.find(candidate) ==
			 non_template_names_.end()) &&
			(known_templates_.find(candidate) != known_templates_.end() ||
			 candidate.find('T') != std::string::npos);
		const Mark mark = Checkpoint();
		++position_;
		std::size_t paren = 0;
		std::size_t square = 0;
		std::size_t brace = 0;
		std::size_t angle = 1;
		bool saw_expression_operator = false;
		while (position_ < tokens_.size() && !AtEof())
		{
			if (At(OP_LPAREN)) ++paren;
			else if (At(OP_RPAREN))
			{
				if (paren == 0) break;
				--paren;
			}
			else if (At(OP_LSQUARE)) ++square;
			else if (At(OP_RSQUARE))
			{
				if (square == 0) break;
				--square;
			}
			else if (At(OP_LBRACE)) ++brace;
			else if (At(OP_RBRACE))
			{
				if (brace == 0) break;
				--brace;
			}
			else if (paren == 0 && square == 0 && brace == 0)
			{
				if (At(OP_LOR) || At(OP_LAND) || At(OP_QMARK) ||
					At(OP_PLUSASS) || At(OP_MINUSASS) || At(OP_ASS))
					saw_expression_operator = true;
				if (At(OP_LT))
				{
					const std::string nested_candidate = position_ == 0 ?
						std::string() : Spelling(position_ - 1);
					const bool explicitly_templated = position_ >= 2 &&
						tokens_[position_ - 2].kind ==
							static_cast<std::uint16_t>(KW_TEMPLATE);
					const bool qualified_nested = position_ >= 2 &&
						tokens_[position_ - 2].kind ==
							static_cast<std::uint16_t>(OP_COLON2);
					if (explicitly_templated ||
						(qualified_nested && known_templates_.find(
							nested_candidate) != known_templates_.end()) ||
						non_template_names_.find(nested_candidate) ==
							non_template_names_.end())
						++angle;
					else saw_expression_operator = true;
				}
				else if (AtCloseAngle())
				{
					--angle;
					++position_;
					if (angle == 0)
					{
						if (!trusted && saw_expression_operator)
						{
							Rollback(mark);
							return;
						}
						return;
					}
					continue;
				}
			}
			++position_;
		}
		Rollback(mark);
	}

	NodeId ParseDeclaration(bool in_class);
	NodeId ParseNamespace();
	NodeId ParseUsing();
	NodeId ParseTemplate(bool in_class);
	NodeId ParseClass(bool require_semicolon = true);
	NodeId ParseEnum(bool require_semicolon = true);
	NodeId ParseStaticAssert();
	NodeId ParseSimpleOrFunction(bool in_class, bool special_only = false);
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
	NodeId ParseParameterClause();
	NodeId ParseDeclarator(bool abstract, std::string* name = 0);
	NodeId ParseDeclSpecifierSeq(bool for_type_id, std::string* first_type = 0);
	bool ParseTypeId(NodeId parent, bool attach = true);
	NodeId ParseCtorInitializer();
	NodeId ParseCondition();
	int BinaryPrecedence(std::uint16_t kind) const;

	const std::vector<SyntaxToken>& tokens_;
	StringTable& strings_;
	SyntaxArena& arena_;
	SyntaxStats* stats_;
	std::size_t position_;
	std::size_t angle_stop_depth_;
	std::unordered_map<std::string, bool> known_types_;
	std::unordered_map<std::string, bool> known_templates_;
	std::unordered_map<std::string, bool> non_template_names_;
	std::vector<std::string> last_declared_names_;
	std::vector<std::string> current_classes_;
};

NodeId Parser::ParseDeclSpecifierSeq(bool for_type_id,
	std::string* first_type)
{
	const Mark mark = Checkpoint();
	const NodeId sequence = arena_.Make(for_type_id ?
		"type-specifier-seq" : "decl-specifier-seq");
	bool consumed = false;
	bool saw_type = false;
	bool saw_user_type = false;
	while (true)
	{
		SkipAttributes();
		if (position_ >= tokens_.size()) break;
		const std::size_t token_position = position_;
		const std::uint16_t kind = tokens_[position_].kind;
		if (kind < kSimpleTokenCount && IsFundamentalKind(kind))
		{
			if (saw_user_type) break;
			++position_;
			arena_.Add(sequence, arena_.Make(std::string(for_type_id ?
				"type-specifier " : "decl-specifier ") +
				TokenDescription(token_position)));
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
			arena_.Add(sequence, arena_.Make(std::string(for_type_id ?
				"cv-qualifier " : "decl-specifier ") +
				TokenDescription(token_position)));
			consumed = true;
			continue;
		}
		if (!for_type_id && IsDeclSpecifierKeyword(kind))
		{
			++position_;
			arena_.Add(sequence, arena_.Make("decl-specifier " +
				TokenDescription(token_position)));
			consumed = true;
			if (kind == static_cast<std::uint16_t>(KW_AUTO)) saw_type = true;
			continue;
		}
		if (!saw_type && At(KW_DECLTYPE))
		{
			const std::size_t first = position_++;
			Expect(OP_LPAREN);
			const NodeId expression = ParseExpression();
			if (expression == kNoNode) throw Error("expected decltype expression");
			Expect(OP_RPAREN);
			const std::string rendered = JoinSpellings(first, position_);
			const NodeId node = arena_.Make(for_type_id ?
				"decltype-specifier " + rendered :
				"decl-specifier " + rendered);
			arena_.Add(node, expression);
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
			if (!ParseName(&name))
			{
				Rollback(mark);
				return kNoNode;
			}
			arena_.Add(sequence, arena_.Make(for_type_id ?
				"type-name " + name : "decl-specifier " + name));
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
			if (!ParseName(&name))
			{
				Rollback(name_mark);
				break;
			}
			const bool decorated = name.find("::") != std::string::npos ||
				name.find('<') != std::string::npos ||
				name.find("decltype") == 0;
			arena_.Add(sequence, arena_.Make(for_type_id ?
				"type-name " + name : decorated ?
				"decl-specifier " + name :
				"decl-specifier TT_IDENTIFIER:" + name));
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
{
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
		arena_.Add(clause, arena_.Make("parameter-pack ..."));
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
			tokens_[position_ + 1].kind == kIdentifierToken &&
			tokens_[position_ + 2].kind ==
				static_cast<std::uint16_t>(OP_RPAREN))
		{
			position_ += 1;
			const std::string inner_type = Spelling(position_++);
			Expect(OP_RPAREN);
			declarator = arena_.Make("declarator");
			const NodeId inner_clause = arena_.Make("parameter-clause");
			const NodeId inner_parameter = arena_.Make("parameter-declaration");
			const NodeId inner_specifiers = arena_.Make("decl-specifier-seq");
			arena_.Add(inner_specifiers, arena_.Make(
				"decl-specifier TT_IDENTIFIER:" + inner_type));
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
				arena_.Prepend(declarator, arena_.Make("parameter-pack ..."));
			arena_.Add(parameter, declarator);
		}
		else if (pack_before_name)
		{
			const NodeId pack_declarator = arena_.Make("declarator");
			arena_.Add(pack_declarator, arena_.Make("parameter-pack ..."));
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
			arena_.Add(clause, arena_.Make("parameter-pack ..."));
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
		if (Match(OP_STAR) || Match(OP_AMP) || Match(OP_LAND))
		{
			const NodeId pointer = arena_.Make("ptr-operator " +
				TokenDescription(operator_position));
			arena_.Add(result, pointer);
			while (At(KW_CONST) || At(KW_VOLATILE))
			{
				const std::size_t qualifier = position_++;
				arena_.Add(result, arena_.Make("cv-qualifier " +
					TokenDescription(qualifier)));
			}
			consumed = true;
			continue;
		}
		// A qualified member pointer is one pointer operation in the AST view.
		if (AtIdentifier())
		{
			const Mark member_mark = Checkpoint();
			const std::size_t owner_first = position_;
			++position_;
			TryConsumeTemplateArguments();
			while (At(OP_COLON2) && !AtOffset(1, OP_STAR))
			{
				++position_;
				if (!AtIdentifier()) break;
				++position_;
				TryConsumeTemplateArguments();
			}
			if (Match(OP_COLON2) && Match(OP_STAR))
			{
				const std::string owner = JoinSpellings(owner_first,
					position_ - 2);
				arena_.Add(result, arena_.Make("ptr-operator " + owner + "::*"));
				consumed = true;
				continue;
			}
			Rollback(member_mark);
		}
		break;
	}
	if (Match(OP_DOTS))
	{
		arena_.Add(result, arena_.Make("parameter-pack ..."));
		consumed = true;
	}
	const bool abstract_function_suffix = abstract && At(OP_LPAREN) &&
		(AtOffset(1, OP_RPAREN) || AtOffset(1, OP_DOTS) ||
		 IsLikelyTypeIdentifier(position_ + 1) ||
		 (position_ + 1 < tokens_.size() &&
		  IsDeclSpecifierKeyword(tokens_[position_ + 1].kind)));
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
		Expect(OP_RPAREN);
		arena_.Add(nested_declarator, nested);
		arena_.Add(result, nested_declarator);
		if (name) *name = nested_name;
		consumed = true;
	}
	else if (!abstract)
	{
		const Mark name_mark = Checkpoint();
		std::string parsed_name;
		if (ParseName(&parsed_name))
		{
			if (name) *name = parsed_name;
			arena_.Add(result, arena_.Make("identifier " + parsed_name));
			consumed = true;
		}
		else Rollback(name_mark);
	}
	while (true)
	{
		if ((At(OP_LSQUARE) && AtOffset(1, OP_LSQUARE)) ||
			(AtIdentifier() && Spelling(position_) == "__attribute__"))
		{
			SkipAttributes();
			continue;
		}
		if (At(OP_LPAREN))
		{
			const bool parameter_like = AtOffset(1, OP_RPAREN) ||
				AtOffset(1, OP_DOTS) ||
				(position_ + 1 < tokens_.size() &&
					 (IsDeclSpecifierKeyword(tokens_[position_ + 1].kind) ||
					  IsFundamentalKind(tokens_[position_ + 1].kind) ||
					  tokens_[position_ + 1].kind ==
						static_cast<std::uint16_t>(KW_DECLTYPE) ||
					  tokens_[position_ + 1].kind ==
						static_cast<std::uint16_t>(KW_STRUCT) ||
					  tokens_[position_ + 1].kind ==
						static_cast<std::uint16_t>(KW_ENUM) ||
					  IsLikelyTypeIdentifier(position_ + 1) ||
					  (tokens_[position_ + 1].kind == kIdentifierToken &&
					   position_ + 2 < tokens_.size() &&
					   tokens_[position_ + 2].kind ==
						static_cast<std::uint16_t>(OP_COLON2))));
			if (!parameter_like) break;
			const NodeId parameters = ParseParameterClause();
			arena_.Add(result, parameters);
			consumed = true;
			while (true)
			{
				SkipAttributes();
				if (At(KW_CONST) || At(KW_VOLATILE))
				{
					const std::size_t qualifier = position_++;
					arena_.Add(result, arena_.Make("cv-qualifier " +
						TokenDescription(qualifier)));
					continue;
				}
				if (At(OP_AMP) || At(OP_LAND))
				{
					const std::size_t qualifier = position_++;
					arena_.Add(result, arena_.Make("ref-qualifier " +
						TokenDescription(qualifier)));
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
							"function-qualifier " +
							JoinSpellings(first, position_));
						arena_.Add(qualifier, expression);
						arena_.Add(result, qualifier);
					}
					else arena_.Add(result,
						arena_.Make("function-qualifier noexcept"));
					continue;
				}
				if (Match(KW_THROW))
				{
					const std::size_t first = position_ - 1;
					if (!SkipBalanced(OP_LPAREN, OP_RPAREN))
						throw Error("expected throw specification");
					arena_.Add(result, arena_.Make("function-qualifier " +
						JoinSpellings(first, position_)));
					continue;
				}
				if (AtIdentifier() &&
					(Spelling(position_) == "override" ||
					 Spelling(position_) == "final"))
				{
					const std::size_t specifier = position_++;
					arena_.Add(result, arena_.Make("virt-specifier " +
						TokenDescription(specifier)));
					continue;
				}
				if (Match(OP_ARROW))
				{
					const std::size_t type_first = position_;
					const NodeId trailing = arena_.Make("trailing-return-type");
					if (!ParseTypeId(trailing))
						throw Error("expected trailing return type");
					arena_.SetLabel(trailing, "trailing-return-type " +
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
		arena_.Add(result, arena_.Make("parameter-pack ..."));
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
			const NodeId right = ParseExpression(3);
			if (right == kNoNode) throw Error("expected conditional operand");
			const NodeId conditional = arena_.Make("conditional-expression");
			arena_.Add(conditional, left);
			arena_.Add(conditional, middle);
			arena_.Add(conditional, right);
			left = conditional;
			continue;
		}
		const std::uint16_t kind = tokens_[position_].kind;
		if (angle_stop_depth_ != 0 &&
			(kind == static_cast<std::uint16_t>(OP_GT) ||
			 kind == kRShiftFirstToken || kind == kRShiftSecondToken)) break;
		const int precedence = BinaryPrecedence(kind);
		if (precedence == 0 || precedence < minimum_precedence) break;
		const std::size_t operator_position = position_;
		if (kind == kRShiftFirstToken)
		{
			if (position_ + 1 >= tokens_.size() ||
				tokens_[position_ + 1].kind != kRShiftSecondToken) break;
			position_ += 2;
		}
		else ++position_;
		const bool right_associative = IsAssignmentOperator(kind);
		NodeId right = ParseExpression(right_associative ? precedence :
			precedence + 1);
		if (right == kNoNode) throw Error("expected binary operand");
		const std::string description = kind == kRShiftFirstToken ?
			"OP_RSHIFT:>>" : std::string(SimpleTokenKindName(
				static_cast<SimpleTokenKind>(kind))) + ":" +
				Spelling(operator_position);
		const NodeId expression = arena_.Make(std::string(
			right_associative ? "assignment-expression " :
			"binary-expression ") + description);
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
		NodeId value;
		if (At(OP_LBRACE)) value = ParseBracedInitList();
		else value = ParseExpression(2);
		if (value == kNoNode) throw Error("expected braced initializer");
		if (Match(OP_DOTS))
		{
			const NodeId pack = arena_.Make("pack-expansion-expression");
			arena_.Add(pack, value);
			value = pack;
		}
		arena_.Add(list, value);
		if (!Match(OP_COMMA)) break;
		if (At(OP_RBRACE)) break;
	}
	Expect(OP_RBRACE);
	return list;
}

NodeId Parser::ParsePrimaryExpression()
{
	if (AtLiteral())
	{
		const std::string value = Spelling(position_++);
		return arena_.Make("literal " + value);
	}
	if (At(KW_TRUE) || At(KW_FALSE) || At(KW_NULLPTR) || At(KW_THIS))
	{
		const std::size_t token = position_++;
		return arena_.Make("keyword-literal " + TokenDescription(token));
	}
	if (At(OP_LBRACE)) return ParseBracedInitList();
	if (At(OP_LSQUARE))
	{
		const std::size_t capture_first = position_;
		++position_;
		std::size_t depth = 1;
		while (position_ < tokens_.size() && depth != 0)
		{
			if (At(OP_LSQUARE)) ++depth;
			else if (At(OP_RSQUARE)) --depth;
			++position_;
		}
		if (depth != 0) throw Error("unterminated lambda capture");
		const NodeId lambda = arena_.Make("lambda-expression");
		arena_.Add(lambda, arena_.Make("lambda-introducer " +
			JoinSpellings(capture_first, position_)));
		if (At(OP_LPAREN))
		{
			const NodeId declarator = arena_.Make("lambda-declarator");
			arena_.Add(declarator, ParseParameterClause());
			if (Match(KW_MUTABLE))
				arena_.Add(declarator,
					arena_.Make("lambda-specifier KW_MUTABLE:mutable"));
			if (Match(KW_NOEXCEPT))
			{
				const NodeId specification = arena_.Make(
					"noexcept-specification");
				if (Match(OP_LPAREN))
				{
					const NodeId value = ParseExpression();
					if (value == kNoNode) throw Error("expected noexcept value");
					Expect(OP_RPAREN);
					arena_.Add(specification, value);
				}
				arena_.Add(declarator, specification);
			}
			if (Match(OP_ARROW))
			{
				const NodeId trailing = arena_.Make("trailing-return-type");
				if (!ParseTypeId(trailing))
					throw Error("expected lambda result type");
				arena_.Add(declarator, trailing);
			}
			arena_.Add(lambda, declarator);
		}
		const NodeId body = ParseCompoundStatement();
		if (body == kNoNode) throw Error("expected lambda body");
		arena_.Add(lambda, body);
		return lambda;
	}
	if (At(OP_LPAREN))
	{
		const Mark cast_mark = Checkpoint();
		++position_;
		const NodeId cast = arena_.Make("cast-expression OP_LPAREN:");
		const bool type_like = At(KW_CONST) || At(KW_VOLATILE) ||
			At(KW_STRUCT) || At(KW_CLASS) || At(KW_UNION) ||
			(position_ < tokens_.size() &&
			 IsFundamentalKind(tokens_[position_].kind)) ||
			IsLikelyTypeIdentifier(position_);
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
		const NodeId expression = ParseExpression();
		if (expression == kNoNode) throw Error("expected parenthesized expression");
		Expect(OP_RPAREN);
		const NodeId parenthesized = arena_.Make("parenthesized-expression");
		arena_.Add(parenthesized, expression);
		return parenthesized;
	}
	if (At(KW_DECLTYPE))
	{
		const std::size_t first = position_++;
		Expect(OP_LPAREN);
		const NodeId ignored = ParseExpression();
		if (ignored == kNoNode) throw Error("expected decltype expression");
		Expect(OP_RPAREN);
		while (Match(OP_COLON2))
		{
			Match(KW_TEMPLATE);
			if (!AtIdentifier()) throw Error("expected qualified decltype name");
			++position_;
			TryConsumeTemplateArguments();
		}
		return arena_.Make("id-expression " +
			JoinSpellings(first, position_));
	}
	if (AtIdentifier() || At(OP_COLON2) || At(KW_OPERATOR) ||
		(position_ < tokens_.size() &&
		 IsFundamentalKind(tokens_[position_].kind)))
	{
		std::string name;
		if (position_ < tokens_.size() &&
			IsFundamentalKind(tokens_[position_].kind))
			name = Spelling(position_++);
		else if (!ParseName(&name)) return kNoNode;
		return arena_.Make("id-expression " + name);
	}
	return kNoNode;
}

NodeId Parser::ParsePostfixExpression()
{
	NodeId value = ParsePrimaryExpression();
	if (value == kNoNode) return kNoNode;
	return ParsePostfixSuffixes(value);
}

NodeId Parser::ParsePostfixSuffixes(NodeId value)
{
	while (true)
	{
		if (Match(OP_LPAREN))
		{
			const NodeId call = arena_.Make("call-expression");
			arena_.Add(call, value);
			const std::string& callee_label = arena_.Label(value);
			const std::string callee = callee_label.compare(0, 14,
				"id-expression ") == 0 ? callee_label.substr(14) : std::string();
			bool function_style = false;
			for (std::uint16_t candidate = 0;
				candidate < kSimpleTokenCount; ++candidate)
				if (IsFundamentalKind(candidate) && callee ==
					SimpleTokenKindName(static_cast<SimpleTokenKind>(candidate)) +
					std::string()) function_style = true;
			// Fundamental token names are uppercase enum labels; compare the
			// source spellings directly for the function-style cast view.
			static const char* const fundamental_names[] = {
				"bool", "char", "char16_t", "char32_t", "double", "float",
				"int", "long", "short", "signed", "unsigned", "void",
				"wchar_t"
			};
			for (std::size_t i = 0; i < sizeof(fundamental_names) /
				sizeof(fundamental_names[0]); ++i)
				if (callee == fundamental_names[i]) function_style = true;
			const NodeId arguments = arena_.Make(function_style ?
				"paren-argument-list" : "argument-list");
			if (!At(OP_RPAREN))
			{
				while (true)
				{
					NodeId argument = At(OP_LBRACE) ?
						ParseBracedInitList() : ParseExpression(2);
					if (argument == kNoNode)
						throw Error("expected call argument");
					if (Match(OP_DOTS))
					{
						const NodeId pack = arena_.Make(
							"pack-expansion-expression");
						arena_.Add(pack, argument);
						argument = pack;
					}
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
			const bool qualified_member = AtIdentifier() && AtOffset(1, OP_COLON2);
			if (!ParseName(&member, qualified_member, true,
				dependent_template || qualified_member))
				throw Error("expected member name");
			if (dependent_template) member = "template " + member;
			const NodeId expression = arena_.Make("member-expression " +
				TokenDescription(operation));
			arena_.Add(expression, value);
			arena_.Add(expression, arena_.Make("identifier " + member));
			value = expression;
			continue;
		}
		if (At(OP_INC) || At(OP_DEC))
		{
			const std::size_t operation = position_++;
			const NodeId expression = arena_.Make("postfix-expression " +
				TokenDescription(operation));
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
	if (At(OP_INC) || At(OP_DEC) || At(OP_STAR) || At(OP_AMP) ||
		At(OP_PLUS) || At(OP_MINUS) || At(OP_LNOT) || At(OP_COMPL))
	{
		const std::size_t operation = position_++;
		const NodeId operand = ParseUnaryExpression();
		if (operand == kNoNode) throw Error("expected unary operand");
		const NodeId unary = arena_.Make("unary-expression " +
			TokenDescription(operation));
		arena_.Add(unary, operand);
		return unary;
	}
	if (At(KW_SIZEOF) || At(KW_ALIGNOF) || At(KW_TYPEID) || At(KW_NOEXCEPT))
	{
		const std::size_t keyword = position_++;
		const SimpleTokenKind kind = static_cast<SimpleTokenKind>(
			tokens_[keyword].kind);
		const std::string label = kind == KW_SIZEOF ? "sizeof-expression" :
			"type-trait-expression " + TokenDescription(keyword);
		const NodeId trait = arena_.Make(label);
		if (kind == KW_SIZEOF && !At(OP_LPAREN))
		{
			const NodeId operand = ParseUnaryExpression();
			if (operand == kNoNode) throw Error("expected sizeof operand");
			arena_.Add(trait, operand);
			return trait;
		}
		Expect(OP_LPAREN);
		bool prefer_type = kind == KW_ALIGNOF;
		if (At(KW_CONST) || At(KW_VOLATILE) || At(KW_DECLTYPE))
			prefer_type = true;
		if (At(KW_STRUCT) || At(KW_CLASS) || At(KW_UNION) ||
			(position_ < tokens_.size() &&
			 IsFundamentalKind(tokens_[position_].kind))) prefer_type = true;
		if (AtIdentifier() && IsLikelyTypeIdentifier(position_) &&
			!AtOffset(1, OP_LPAREN)) prefer_type = true;
		if (kind == KW_NOEXCEPT) prefer_type = false;
		if (prefer_type)
		{
			if (!ParseTypeId(trait)) throw Error("expected trait type-id");
		}
		else
		{
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
		const NodeId cast = arena_.Make("cast-expression " +
			TokenDescription(keyword));
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
				if (tokens_[scan].kind == static_cast<std::uint16_t>(OP_LPAREN))
					++depth;
				else if (tokens_[scan].kind ==
					static_cast<std::uint16_t>(OP_RPAREN)) --depth;
				++scan;
			}
			if (depth != 0) throw Error("unterminated new parentheses");
			bool placement = scan < tokens_.size() &&
				(tokens_[scan].kind == kIdentifierToken ||
				 (tokens_[scan].kind < kSimpleTokenCount &&
				  (IsFundamentalKind(tokens_[scan].kind) ||
				   tokens_[scan].kind == static_cast<std::uint16_t>(KW_CONST) ||
				   tokens_[scan].kind == static_cast<std::uint16_t>(KW_VOLATILE))));
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
				const NodeId node = arena_.Make("placement " +
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
					if (Match(OP_DOTS))
					{
						const NodeId pack = arena_.Make(
							"pack-expansion-expression");
						arena_.Add(pack, value);
						value = pack;
					}
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
		if (Match(KW_DEFAULT))
		{
			arena_.Add(initializer,
				arena_.Make("special-initializer default"));
			return initializer;
		}
		if (Match(KW_DELETE))
		{
			arena_.Add(initializer,
				arena_.Make("special-initializer delete"));
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
		arena_.Add(initializer, ParseBracedInitList());
		return initializer;
	}
	if (Match(OP_LPAREN))
	{
		const NodeId initializer = arena_.Make("initializer");
		const NodeId values = arena_.Make("paren-initializer");
		if (!At(OP_RPAREN))
		{
			while (true)
			{
				NodeId value = ParseExpression(2);
				if (value == kNoNode) throw Error("expected paren initializer");
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

NodeId Parser::ParseCondition()
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
		if (declarator != kNoNode && initializer != kNoNode && At(OP_RPAREN))
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
	const NodeId compound = arena_.Make("compound-statement");
	while (!At(OP_RBRACE))
	{
		if (AtEof()) throw Error("unterminated compound statement");
		NodeId item = kNoNode;
		const bool declaration_start = At(KW_TEMPLATE) || At(KW_USING) ||
			At(KW_TYPEDEF) || At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION) ||
			At(KW_ENUM) || At(KW_STATIC_ASSERT) || At(KW_EXTERN) ||
			(position_ < tokens_.size() &&
			 IsDeclSpecifierKeyword(tokens_[position_].kind)) ||
			IsLikelyTypeIdentifier(position_) ||
			(((AtIdentifier() && AtOffset(1, OP_COLON2)) || At(OP_COLON2)) &&
			 QualifiedStartsType());
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
	return compound;
}

NodeId Parser::ParseStatement()
{
	SkipAttributes();
	if (At(OP_LBRACE)) return ParseCompoundStatement();
	if (AtIdentifier() && AtOffset(1, OP_COLON))
	{
		const std::string name = Spelling(position_);
		position_ += 2;
		const NodeId statement = arena_.Make("labeled-statement " + name);
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
		const NodeId statement = arena_.Make("for-statement");
		Expect(OP_LPAREN);
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
		if (!At(OP_SEMICOLON)) arena_.Add(statement, ParseCondition());
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
		return statement;
	}
	if (Match(KW_TRY))
	{
		const NodeId statement = arena_.Make("try-block");
		const NodeId body = ParseCompoundStatement();
		if (body == kNoNode) throw Error("expected try body");
		arena_.Add(statement, body);
		if (!At(KW_CATCH)) throw Error("expected catch handler");
		while (Match(KW_CATCH))
		{
			const NodeId handler = arena_.Make("handler");
			Expect(OP_LPAREN);
			const NodeId declaration = arena_.Make("exception-declaration");
			if (Match(OP_DOTS)) arena_.Add(declaration,
				arena_.Make("ellipsis ..."));
			else
			{
				const NodeId specifiers = ParseDeclSpecifierSeq(false);
				if (specifiers == kNoNode)
					throw Error("expected exception declaration");
				arena_.Add(declaration, specifiers);
				const Mark declarator_mark = Checkpoint();
				const NodeId declarator = ParseDeclarator(false);
				if (declarator != kNoNode) arena_.Add(declaration, declarator);
				else Rollback(declarator_mark);
			}
			Expect(OP_RPAREN);
			arena_.Add(handler, declaration);
			const NodeId handler_body = ParseCompoundStatement();
			if (handler_body == kNoNode) throw Error("expected handler body");
			arena_.Add(handler, handler_body);
			arena_.Add(statement, handler);
		}
		return statement;
	}
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
		return arena_.Make("goto-statement " + name);
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
	if (!is_inline && AtIdentifier() && AtOffset(1, OP_ASS))
	{
		const std::string alias = Spelling(position_++);
		Expect(OP_ASS);
		std::string target;
		if (!ParseName(&target)) throw Error("expected namespace alias target");
		Expect(OP_SEMICOLON);
		const NodeId declaration = arena_.Make(
			"namespace-alias-definition " + alias);
		arena_.Add(declaration, arena_.Make("target " + target));
		return declaration;
	}
	std::string name = "<unnamed>";
	if (AtIdentifier()) name = Spelling(position_++);
	const NodeId declaration = arena_.Make("namespace-definition " + name);
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
		if (!ParseName(&target)) throw Error("expected using namespace target");
		Expect(OP_SEMICOLON);
		const NodeId declaration = arena_.Make("using-directive");
		arena_.Add(declaration, arena_.Make("target " + target));
		return declaration;
	}
	Match(KW_TYPENAME);
	if (!AtIdentifier() && !At(OP_COLON2)) throw Error("expected using target");
	const Mark alias_mark = Checkpoint();
	std::string first;
	if (!ParseName(&first)) throw Error("expected using name");
	if (Match(OP_ASS))
	{
		const NodeId declaration = arena_.Make("alias-declaration " + first);
		if (!ParseTypeId(declaration)) throw Error("expected alias type-id");
		Expect(OP_SEMICOLON);
		known_types_[first] = true;
		last_declared_names_.clear();
		last_declared_names_.push_back(first);
		return declaration;
	}
	Rollback(alias_mark);
	std::string target;
	if (!ParseName(&target)) throw Error("expected using target");
	Expect(OP_SEMICOLON);
	const NodeId declaration = arena_.Make("using-declaration");
	arena_.Add(declaration, arena_.Make("target " + target));
	return declaration;
}

NodeId Parser::ParseTemplate(bool in_class)
{
	if (!Match(KW_TEMPLATE)) return kNoNode;
	const NodeId declaration = arena_.Make("template-declaration");
	const NodeId clause = arena_.Make("template-parameter-clause");
	Expect(OP_LT);
	++angle_stop_depth_;
	if (!AtCloseAngle())
	{
		const NodeId list = arena_.Make("template-parameter-list");
		while (true)
		{
			NodeId parameter;
			if (At(KW_CLASS) || At(KW_TYPENAME) || At(KW_TEMPLATE))
			{
				parameter = arena_.Make("type-parameter");
				if (Match(KW_TEMPLATE))
				{
					arena_.Add(parameter,
						arena_.Make("template-template-parameter"));
					const NodeId nested_clause = arena_.Make(
						"template-parameter-clause");
					Expect(OP_LT);
					if (!AtCloseAngle())
					{
						const NodeId nested_list = arena_.Make(
							"template-parameter-list");
						while (true)
						{
							if (!At(KW_CLASS) && !At(KW_TYPENAME))
								throw Error("expected nested type parameter");
							const std::size_t key = position_++;
							const NodeId nested_parameter = arena_.Make(
								"type-parameter");
							arena_.Add(nested_parameter, arena_.Make(
								"parameter-key " + TokenDescription(key)));
							if (Match(OP_DOTS)) arena_.Add(nested_parameter,
								arena_.Make("parameter-pack ..."));
							if (AtIdentifier())
							{
								const std::string name = Spelling(position_++);
								arena_.Add(nested_parameter,
									arena_.Make("identifier " + name));
								known_types_[name] = true;
							}
							arena_.Add(nested_list, nested_parameter);
							if (!Match(OP_COMMA)) break;
						}
						arena_.Add(nested_clause, nested_list);
					}
					ExpectCloseAngle();
					arena_.Add(parameter, nested_clause);
					if (!At(KW_CLASS) && !At(KW_TYPENAME))
						throw Error("expected template parameter key");
				}
				const std::size_t key = position_++;
				arena_.Add(parameter, arena_.Make("parameter-key " +
					TokenDescription(key)));
				if (Match(OP_DOTS))
					arena_.Add(parameter, arena_.Make("parameter-pack ..."));
				if (AtIdentifier())
				{
					const std::string name = Spelling(position_++);
					arena_.Add(parameter, arena_.Make("identifier " + name));
					known_types_[name] = true;
					if (arena_.HasDirectChildPrefix(parameter,
						"template-template-parameter"))
						known_templates_[name] = true;
				}
				if (Match(OP_ASS))
				{
					const NodeId argument = arena_.Make(
						"default-template-argument");
					if (!ParseTypeId(argument))
						throw Error("expected default template type");
					arena_.Add(parameter, argument);
				}
			}
			else
			{
				parameter = arena_.Make("non-type-template-parameter");
				const bool bare_int_parameter = At(KW_INT);
				const NodeId specifiers = ParseDeclSpecifierSeq(false);
				if (specifiers == kNoNode)
					throw Error("expected non-type template parameter");
				arena_.Add(parameter, specifiers);
				if (Match(OP_DOTS))
					arena_.Add(parameter, arena_.Make("parameter-pack ..."));
				const Mark declarator_mark = Checkpoint();
				std::string parameter_name;
				const NodeId declarator = ParseDeclarator(false, &parameter_name);
				if (declarator != kNoNode) arena_.Add(parameter, declarator);
				else Rollback(declarator_mark);
				if (!parameter_name.empty())
					non_template_names_[parameter_name] = true;
				if (Match(OP_ASS))
				{
					const NodeId argument = arena_.Make(
						"default-template-argument");
					NodeId value;
					if (bare_int_parameter && parameter_name.empty() && AtLiteral())
						value = arena_.Make("literal TT_LITERAL:" +
							Spelling(position_++));
					else value = ParseExpression(2);
					if (value == kNoNode)
						throw Error("expected non-type default");
					arena_.Add(argument, value);
					arena_.Add(parameter, argument);
				}
			}
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
	for (std::size_t i = 0; i < last_declared_names_.size(); ++i)
	{
		known_templates_[last_declared_names_[i]] = true;
		non_template_names_.erase(last_declared_names_[i]);
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
		if (At(KW_DECLTYPE))
		{
			const std::size_t first = position_++;
			Expect(OP_LPAREN);
			const NodeId ignored = ParseExpression();
			if (ignored == kNoNode) throw Error("expected decltype expression");
			Expect(OP_RPAREN);
			name = JoinSpellings(first, position_);
		}
		else if (!ParseName(&name))
			throw Error("expected mem-initializer-id");
		const NodeId member = arena_.Make("mem-initializer");
		arena_.Add(member, arena_.Make("mem-initializer-id " + name));
		if (Match(OP_LPAREN))
		{
			const NodeId arguments = arena_.Make("paren-argument-list");
			if (!At(OP_RPAREN))
			{
				while (true)
				{
					const NodeId value = ParseExpression(2);
					if (value == kNoNode)
						throw Error("expected mem-initializer argument");
					arena_.Add(arguments, value);
					if (!Match(OP_COMMA)) break;
				}
			}
			Expect(OP_RPAREN);
			arena_.Add(member, arguments);
		}
		else if (At(OP_LBRACE)) arena_.Add(member, ParseBracedInitList());
		else throw Error("expected mem-initializer");
		Match(OP_DOTS);
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
	if (!ParseName(&name) || !At(OP_LPAREN))
	{
		Rollback(mark);
		return kNoNode;
	}
	const bool qualified = name.find("::") != std::string::npos;
	bool special_name = name.find("operator") != std::string::npos;
	if (!special_name && qualified)
	{
		const std::size_t separator = name.rfind("::");
		const std::string tail = name.substr(separator + 2);
		std::string owner = name.substr(0, separator);
		const std::size_t owner_separator = owner.rfind("::");
		if (owner_separator != std::string::npos)
			owner.erase(0, owner_separator + 2);
		const std::size_t arguments = owner.find('<');
		if (arguments != std::string::npos) owner.erase(arguments);
		special_name = tail == owner || tail == "~" + owner;
	}
	else if (!special_name && !current_classes_.empty())
	{
		std::string owner = current_classes_.back();
		const std::size_t arguments = owner.find('<');
		if (arguments != std::string::npos) owner.erase(arguments);
		const std::size_t separator = owner.rfind("::");
		if (separator != std::string::npos) owner.erase(0, separator + 2);
		special_name = name == owner || name == "~" + owner;
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
	if (At(OP_COLON)) ctor_initializer = ParseCtorInitializer();
	const bool has_body = At(OP_LBRACE);
	const bool is_declaration = At(OP_SEMICOLON) || At(OP_ASS);
	if (!has_body && !is_declaration)
	{
		Rollback(mark);
		return kNoNode;
	}
	const NodeId member = arena_.Make(std::string(has_body ?
		"special-member-definition " : "special-member-declaration ") + name);
	if (!specifiers.empty())
	{
		const NodeId set = arena_.Make("member-specifiers");
		for (std::size_t i = 0; i < specifiers.size(); ++i)
			arena_.Add(set, arena_.Make(tokens_[specifiers[i]].kind ==
				static_cast<std::uint16_t>(KW_EXPLICIT) ?
				"specifier explicit" :
				"specifier " + TokenDescription(specifiers[i])));
		arena_.Add(member, set);
	}
	arena_.Add(member, declarator);
	if (ctor_initializer != kNoNode) arena_.Add(member, ctor_initializer);
	if (Match(OP_ASS))
	{
		const NodeId initializer = arena_.Make("initializer");
		if (Match(KW_DEFAULT))
			arena_.Add(initializer, arena_.Make("special-initializer default"));
		else if (Match(KW_DELETE))
			arena_.Add(initializer, arena_.Make("special-initializer delete"));
		else throw Error("expected default or delete");
		arena_.Add(member, initializer);
		Expect(OP_SEMICOLON);
		return member;
	}
	if (Match(OP_SEMICOLON)) return member;
	const NodeId body = ParseCompoundStatement();
	if (body == kNoNode) throw Error("expected special member body");
	arena_.Add(member, body);
	return member;
}

NodeId Parser::ParseClass(bool require_semicolon)
{
	if (!At(KW_CLASS) && !At(KW_STRUCT) && !At(KW_UNION)) return kNoNode;
	const std::size_t key = position_++;
	SkipAttributes();
	std::string name;
	const Mark name_mark = Checkpoint();
	if (!ParseName(&name, false))
	{
		Rollback(name_mark);
		name.clear();
	}
	SkipAttributes();
	if (name.empty() && !At(OP_LBRACE)) throw Error("expected class name");
	const std::string visible_name = name.empty() ? std::string() : name;
	last_declared_names_.clear();
	if (!name.empty()) last_declared_names_.push_back(name);
	if (!name.empty()) known_types_[name] = true;
	if ((require_semicolon && Match(OP_SEMICOLON)) ||
		(!require_semicolon && !At(OP_LBRACE)))
	{
		last_declared_names_.clear();
		if (!name.empty()) last_declared_names_.push_back(name);
		const NodeId declaration = arena_.Make("class-forward-declaration" +
			(name.empty() ? std::string() : " " + name));
		arena_.Add(declaration, arena_.Make("class-key " +
			TokenDescription(key)));
		return declaration;
	}
	const NodeId declaration = arena_.Make("class-specifier" +
		(name.empty() ? std::string() : " " + name));
	arena_.Add(declaration, arena_.Make("class-key " + TokenDescription(key)));
	if (Match(OP_COLON))
	{
		const NodeId clause = arena_.Make("base-clause");
		while (true)
		{
			const NodeId base = arena_.Make("base-specifier");
			if (Match(KW_VIRTUAL))
				arena_.Add(base, arena_.Make("virtual KW_VIRTUAL:virtual"));
			if (At(KW_PUBLIC) || At(KW_PRIVATE) || At(KW_PROTECTED))
			{
				const std::size_t access = position_++;
				arena_.Add(base, arena_.Make("access-specifier " +
					TokenDescription(access)));
			}
			if (Match(KW_VIRTUAL))
				arena_.Add(base, arena_.Make("virtual KW_VIRTUAL:virtual"));
			std::string base_name;
			if (At(KW_DECLTYPE))
			{
				const std::size_t first = position_++;
				Expect(OP_LPAREN);
				const NodeId ignored = ParseExpression();
				if (ignored == kNoNode)
					throw Error("expected decltype base expression");
				Expect(OP_RPAREN);
				base_name = JoinSpellings(first, position_);
			}
			else if (!ParseName(&base_name)) throw Error("expected base name");
			arena_.Add(base, arena_.Make("base-name " + base_name));
			Match(OP_DOTS);
			arena_.Add(clause, base);
			if (!Match(OP_COMMA)) break;
		}
		arena_.Add(declaration, clause);
	}
	Expect(OP_LBRACE);
	current_classes_.push_back(visible_name);
	while (!At(OP_RBRACE))
	{
		if (AtEof()) throw Error("unterminated class");
		SkipAttributes();
		if ((At(KW_PUBLIC) || At(KW_PRIVATE) || At(KW_PROTECTED)) &&
			AtOffset(1, OP_COLON))
		{
			const std::size_t access = position_++;
			++position_;
			arena_.Add(declaration, arena_.Make("access-specifier " +
				TokenDescription(access)));
			continue;
		}
		NodeId special_member = ParseSpecialMember(false);
		if (special_member != kNoNode)
		{
			arena_.Add(declaration, special_member);
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
				arena_.Add(declaration, bit_field);
				parsed_bit_field = true;
			}
		}
		if (parsed_bit_field) continue;
		Rollback(bit_field_mark);
		NodeId member = ParseDeclaration(true);
		if (member == kNoNode) throw Error("expected class member");
		arena_.Add(declaration, member);
	}
	current_classes_.pop_back();
	Expect(OP_RBRACE);
	if (require_semicolon) Expect(OP_SEMICOLON);
	last_declared_names_.clear();
	if (!name.empty()) last_declared_names_.push_back(name);
	return declaration;
}

NodeId Parser::ParseEnum(bool require_semicolon)
{
	if (!Match(KW_ENUM)) return kNoNode;
	std::size_t key = std::numeric_limits<std::size_t>::max();
	if (At(KW_CLASS) || At(KW_STRUCT)) key = position_++;
	std::string name;
	if (AtIdentifier()) name = Spelling(position_++);
	NodeId underlying = kNoNode;
	if (Match(OP_COLON))
	{
		underlying = arena_.Make("type-id");
		const NodeId specifiers = ParseDeclSpecifierSeq(true);
		if (specifiers == kNoNode) throw Error("expected enum underlying type");
		arena_.Add(underlying, specifiers);
	}
	const NodeId declaration = arena_.Make("enum-specifier" +
		(name.empty() ? std::string() : " " + name));
	if (key != std::numeric_limits<std::size_t>::max())
		arena_.Add(declaration, arena_.Make("enum-key " +
			TokenDescription(key)));
	if (underlying != kNoNode) arena_.Add(declaration, underlying);
	if (Match(OP_LBRACE))
	{
		if (!At(OP_RBRACE))
		{
			while (true)
			{
				if (!AtIdentifier()) throw Error("expected enumerator");
				const std::string enumerator_name = Spelling(position_++);
				const NodeId enumerator = arena_.Make("enumerator " +
					enumerator_name);
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
	if (!name.empty()) known_types_[name] = true;
	last_declared_names_.clear();
	if (!name.empty()) last_declared_names_.push_back(name);
	if (require_semicolon) Expect(OP_SEMICOLON);
	return declaration;
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
		arena_.Add(declaration, arena_.Make("message " +
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
	std::string first_type;
	const NodeId specifiers = ParseDeclSpecifierSeq(false, &first_type);
	if (specifiers == kNoNode)
	{
		Rollback(mark);
		return kNoNode;
	}
	const std::size_t specifier_last = position_;
	bool is_typedef = false;
	for (std::size_t i = specifier_first; i < specifier_last; ++i)
		if (tokens_[i].kind == static_cast<std::uint16_t>(KW_TYPEDEF))
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
		const NodeId item = arena_.Make("init-declarator");
		std::string name;
		const NodeId declarator = ParseDeclarator(false, &name);
		if (declarator == kNoNode)
		{
			Rollback(mark);
			return kNoNode;
		}
		if (At(OP_LBRACE) && names.empty() &&
			arena_.HasDirectChildPrefix(declarator, "parameter-clause"))
		{
			const NodeId declaration = arena_.Make("function-definition");
			arena_.Add(declaration, specifiers);
			arena_.Add(declaration, declarator);
			arena_.Add(declaration, ParseCompoundStatement());
			last_declared_names_.clear();
			if (!name.empty()) last_declared_names_.push_back(name);
			return declaration;
		}
		names.push_back(name);
		arena_.Add(item, declarator);
		const NodeId initializer = ParseInitializer();
		if (initializer != kNoNode) arena_.Add(item, initializer);
		SkipAttributes();
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
	last_declared_names_ = names;
	if (is_typedef)
	{
		for (std::size_t i = 0; i < names.size(); ++i)
			if (!names[i].empty()) known_types_[names[i]] = true;
	}
	else
	{
		for (std::size_t i = 0; i < names.size(); ++i)
			if (!names[i].empty())
			{
				known_types_.erase(names[i]);
				non_template_names_[names[i]] = true;
			}
	}
	return declaration;
}

NodeId Parser::ParseDeclaration(bool in_class)
{
	SkipAttributes();
	if (Match(OP_SEMICOLON)) return arena_.Make("empty-declaration");
	if ((At(KW_INLINE) && AtOffset(1, KW_NAMESPACE)) || At(KW_NAMESPACE))
		return ParseNamespace();
	if (At(KW_USING)) return ParseUsing();
	if (At(KW_TEMPLATE)) return ParseTemplate(in_class);
	if (At(KW_EXTERN) && AtOffset(1, KW_TEMPLATE))
	{
		position_ += 2;
		const NodeId declaration = arena_.Make(
			"explicit-instantiation-declaration");
		const NodeId target = At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION) ?
			ParseClass() : ParseSimpleOrFunction(in_class);
		if (target == kNoNode) throw Error("expected explicit instantiation");
		arena_.Add(declaration, target);
		return declaration;
	}
	if (At(KW_EXTERN) && position_ + 1 < tokens_.size() &&
		tokens_[position_ + 1].kind == kLiteralToken)
	{
		position_ += 1;
		std::string language = Spelling(position_++);
		if (language.size() >= 2 && language[0] == '"' &&
			language[language.size() - 1] == '"')
			language = language.substr(1, language.size() - 2);
		const NodeId declaration = arena_.Make(
			"linkage-specification " + language);
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
			const NodeId child = ParseDeclaration(in_class);
			if (child == kNoNode) throw Error("expected linkage declaration");
			arena_.Add(declaration, child);
		}
		return declaration;
	}
	if (At(KW_CLASS) || At(KW_STRUCT) || At(KW_UNION)) return ParseClass();
	if (At(KW_ENUM)) return in_class ?
		ParseSimpleOrFunction(in_class) : ParseEnum();
	if (At(KW_STATIC_ASSERT)) return ParseStaticAssert();
	const Mark special_mark = Checkpoint();
	const NodeId special = ParseSpecialMember(true);
	if (special != kNoNode) return special;
	Rollback(special_mark);
	return ParseSimpleOrFunction(in_class);
}

}

SyntaxStats::SyntaxStats()
	: tokens(0), interned_spellings(0), spelling_bytes(0), syntax_nodes(0),
	  syntax_edges(0), parser_checkpoints(0), parser_rollbacks(0),
	  token_storage_bytes(0), syntax_storage_bytes(0),
	  peak_stage_storage_bytes(0), elapsed_nanoseconds(0)
{
}

void WriteSyntaxTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, SyntaxStats* stats)
{
	std::chrono::steady_clock::time_point started;
	if (stats)
	{
		*stats = SyntaxStats();
		started = std::chrono::steady_clock::now();
	}
	StringTable strings;
	SyntaxTokenSink sink(strings);
	PreprocessFile(path, source, sink, options,
		stats ? &stats->preprocessing : 0);
	SyntaxArena arena(strings);
	Parser parser(sink.Tokens(), strings, arena, stats);
	const NodeId root = parser.ParseTranslationUnit();
	arena.Write(output, root);
	if (stats)
	{
		stats->tokens = sink.Tokens().size();
		stats->interned_spellings = strings.Size();
		stats->spelling_bytes = strings.SpellingBytes();
		stats->syntax_nodes = arena.Nodes();
		stats->syntax_edges = arena.Edges();
		stats->token_storage_bytes = sink.StorageBytes();
		stats->syntax_storage_bytes = arena.StorageBytes() +
			strings.StorageBytes();
		stats->peak_stage_storage_bytes = stats->token_storage_bytes +
			stats->syntax_storage_bytes;
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
}

}
