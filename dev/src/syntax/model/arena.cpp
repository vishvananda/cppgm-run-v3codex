#include "syntax/model/arena.h"

#include "support/driver_errors.h"
#include "support/exceptions.h"
#include "preprocess/tokens/post_tokenizer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ostream>

namespace cppgm
{
namespace syntax
{
const std::uint16_t kSimpleTokenCount =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kIdentifierToken = kSimpleTokenCount;
const std::uint16_t kLiteralToken = kSimpleTokenCount + 1;
const std::uint16_t kEofToken = kSimpleTokenCount + 2;
const std::uint16_t kRShiftFirstToken = kSimpleTokenCount + 3;
const std::uint16_t kRShiftSecondToken = kSimpleTokenCount + 4;
const std::uint16_t kPragmaPackPushToken = kSimpleTokenCount + 5;
const std::uint16_t kPragmaPackPopToken = kSimpleTokenCount + 6;
const std::uint32_t kNoLiteralFact =
	std::numeric_limits<std::uint32_t>::max();
const NodeId kNoNode = std::numeric_limits<NodeId>::max();
const std::uint32_t kNoEdge = std::numeric_limits<std::uint32_t>::max();
const std::size_t kSyntaxTagCacheEntries = 4096;

static_assert(static_cast<unsigned int>(OP_ARROW) + 7 <= 0xffU,
	"syntax token kinds must fit the packed identity byte");
static_assert(sizeof(SyntaxToken) == 16,
	"syntax token provenance must remain four compact 32-bit words");
static_assert(sizeof(SyntaxLiteralFact) == 16,
	"scalar literal facts must remain compact");
static_assert(sizeof(SyntaxNode) == 32,
	"compact syntax tag codes must occupy existing node padding");

SyntaxToken::SyntaxToken(std::uint16_t kind_value, TextId spelling_value,
	std::uint32_t literal_fact, TextId source_file_value,
	std::uint32_t source_line_value, std::uint32_t source_column_value)
	: kind_and_literal_fact(0), spelling(spelling_value),
	  source_line(0), source_file(0), source_column(0)
{
	if (kind_value > 0xffU)
		ThrowSyntaxInternal("syntax token kind exceeds packed identity");
	if (source_file_value <= std::numeric_limits<std::uint16_t>::max() &&
		source_column_value <= std::numeric_limits<std::uint16_t>::max())
	{
		source_line = source_line_value;
		source_file = static_cast<std::uint16_t>(source_file_value);
		source_column = static_cast<std::uint16_t>(source_column_value);
	}
	const std::uint32_t encoded_fact = literal_fact == kNoLiteralFact ? 0 :
		literal_fact + 1;
	if (encoded_fact > 0xffffffU)
		ThrowSyntaxResourceLimit("too many scalar literal facts");
	kind_and_literal_fact = kind_value | (encoded_fact << 8);
}

std::uint16_t SyntaxToken::Kind() const
{
	return static_cast<std::uint16_t>(kind_and_literal_fact & 0xffU);
}

std::uint32_t SyntaxToken::LiteralFact() const
{
	const std::uint32_t encoded = kind_and_literal_fact >> 8;
	return encoded == 0 ? kNoLiteralFact : encoded - 1;
}

SyntaxTokenSink::SyntaxTokenSink(StringTable& strings,
	InterningStats* stats)
	: strings_(strings), stats_(stats), source_file_(0),
	  cached_source_file_(0), has_cached_source_file_(false), source_line_(0),
	  source_column_(0) {}

void SyntaxTokenSink::SetSourceLocation(const std::string& file,
	std::size_t line, std::size_t column)
{
	if (stats_) ++stats_->source_location_calls;
	TextId source_file = cached_source_file_;
	if (has_cached_source_file_ && cached_source_file_spelling_ == file)
	{
		if (stats_) ++stats_->source_file_cache_hits;
	}
	else
	{
		if (stats_) ++stats_->source_file_cache_misses;
		source_file = strings_.Intern(file);
		cached_source_file_spelling_ = file;
		cached_source_file_ = source_file;
		has_cached_source_file_ = true;
	}
	if (line > std::numeric_limits<std::uint32_t>::max() ||
		column > std::numeric_limits<std::uint16_t>::max() ||
		source_file > std::numeric_limits<std::uint16_t>::max())
	{
		source_file_ = 0;
		source_line_ = source_column_ = 0;
		return;
	}
	source_file_ = source_file;
	source_line_ = static_cast<std::uint32_t>(line);
	source_column_ = static_cast<std::uint32_t>(column);
}

TextId SyntaxTokenSink::InternTokenSpelling(const std::string& source)
{
	if (stats_) ++stats_->token_spelling_calls;
	return strings_.Intern(source);
}

SyntaxToken SyntaxTokenSink::LocatedToken(std::uint16_t kind,
	TextId spelling, std::uint32_t literal_fact) const
{
	return SyntaxToken(kind, spelling, literal_fact,
		source_file_, source_line_, source_column_);
}

void SyntaxTokenSink::EmitInvalid(const std::string& source)
{
	std::uint32_t multicharacter = 0;
	if (DecodeOrdinaryMulticharacterLiteral(source, &multicharacter))
	{
		EmitScalarLiteral(source, FT_INT, &multicharacter,
			sizeof(multicharacter));
		return;
	}
	driver_errors::ThrowLexicalSource("invalid phase-7 token: " + source);
}

void SyntaxTokenSink::EmitSimple(const std::string& source,
	SimpleTokenKind kind)
{
	if (kind == OP_RSHIFT)
	{
		const TextId close = InternTokenSpelling(">");
		tokens_.push_back(LocatedToken(kRShiftFirstToken, close));
		tokens_.push_back(LocatedToken(kRShiftSecondToken, close));
		return;
	}
	tokens_.push_back(LocatedToken(static_cast<std::uint16_t>(kind),
		InternTokenSpelling(source)));
}

void SyntaxTokenSink::EmitIdentifier(const std::string& source)
{
	tokens_.push_back(LocatedToken(kIdentifierToken,
		InternTokenSpelling(source)));
}

TextId SyntaxTokenSink::RemappedTokenSpelling(
	std::uint32_t producer_spelling, const std::string& source)
{
	// One intern per distinct emitted producer spelling; repeats reuse the
	// remembered frontend TextId without hashing the bytes again.
	if (producer_spelling == 0) return InternTokenSpelling(source);
	if (spelling_remap_.size() <= producer_spelling)
		spelling_remap_.resize(
			static_cast<std::size_t>(producer_spelling) + 1, 0);
	TextId& slot = spelling_remap_[producer_spelling];
	if (slot == 0) slot = InternTokenSpelling(source);
	return slot;
}

void SyntaxTokenSink::EmitSimpleId(std::uint32_t producer_spelling,
	const std::string& source, SimpleTokenKind kind)
{
	if (kind == OP_RSHIFT)
	{
		EmitSimple(source, kind);
		return;
	}
	tokens_.push_back(LocatedToken(static_cast<std::uint16_t>(kind),
		RemappedTokenSpelling(producer_spelling, source)));
}

void SyntaxTokenSink::EmitIdentifierId(std::uint32_t producer_spelling,
	const std::string& source)
{
	tokens_.push_back(LocatedToken(kIdentifierToken,
		RemappedTokenSpelling(producer_spelling, source)));
}

void SyntaxTokenSink::EmitLiteral(const std::string& source, FundamentalType type,
	const void* data, std::size_t size)
{
	EmitScalarLiteral(source, type, data, size);
}

void SyntaxTokenSink::EmitLiteralArray(const std::string& source, std::size_t,
	FundamentalType, const void*, std::size_t)
{
	EmitLiteralSpelling(source);
}

void SyntaxTokenSink::EmitUserDefinedCharacter(const std::string& source,
	const std::string&, FundamentalType, const void*, std::size_t)
{
	EmitLiteralSpelling(source);
}

void SyntaxTokenSink::EmitUserDefinedString(const std::string& source,
	const std::string&, std::size_t, FundamentalType,
	const void*, std::size_t)
{
	EmitLiteralSpelling(source);
}

void SyntaxTokenSink::EmitUserDefinedInteger(const std::string& source,
	const std::string&, const std::string&)
{
	EmitLiteralSpelling(source);
}

void SyntaxTokenSink::EmitUserDefinedFloating(const std::string& source,
	const std::string&, const std::string&)
{
	EmitLiteralSpelling(source);
}

void SyntaxTokenSink::EmitPragmaPackPush(std::size_t alignment)
{
	tokens_.push_back(LocatedToken(kPragmaPackPushToken,
		InternTokenSpelling(std::to_string(alignment))));
}

void SyntaxTokenSink::EmitPragmaPackPop()
{
	tokens_.push_back(LocatedToken(kPragmaPackPopToken, 0));
}

void SyntaxTokenSink::EmitEof()
{
	tokens_.push_back(LocatedToken(kEofToken,
		InternTokenSpelling(std::string())));
}

const std::vector<SyntaxToken>& SyntaxTokenSink::Tokens() const
{
	return tokens_;
}

const std::vector<SyntaxLiteralFact>& SyntaxTokenSink::LiteralFacts() const
{
	return literal_facts_;
}

std::size_t SyntaxTokenSink::StorageBytes() const
{
	return tokens_.capacity() * sizeof(SyntaxToken) +
		literal_facts_.capacity() * sizeof(SyntaxLiteralFact) +
		cached_source_file_spelling_.capacity();
}

void SyntaxTokenSink::EmitLiteralSpelling(const std::string& source)
{
	tokens_.push_back(LocatedToken(kLiteralToken,
		InternTokenSpelling(source)));
}

void SyntaxTokenSink::EmitScalarLiteral(const std::string& source,
	FundamentalType type, const void* data, std::size_t size)
{
	std::uint64_t value = 0;
	const bool value_valid = data != 0 && size <= sizeof(value);
	if (value_valid)
	{
		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		for (std::size_t i = 0; i < size; ++i)
			value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
	}
	if (literal_facts_.size() >= 0xffffffU)
		ThrowSyntaxResourceLimit("too many scalar literal facts");
	const std::uint32_t fact =
		static_cast<std::uint32_t>(literal_facts_.size());
	literal_facts_.push_back(SyntaxLiteralFact(type, value, value_valid));
	tokens_.push_back(LocatedToken(
		kLiteralToken, InternTokenSpelling(source), fact));
}

SyntaxNode::SyntaxNode(TextId tag_value, TextId payload_value,
	SyntaxTagCode tag_code_value)
	: tag(tag_value), payload(payload_value), semantic_payload(0),
	  first_edge(kNoEdge),
	  last_edge(kNoEdge), token_first(0), token_last(0), flags(0),
	  tag_code(tag_code_value)
{
}

SyntaxEdge::SyntaxEdge(NodeId child_value) : child(child_value), next(kNoEdge)
{
}

SyntaxArena::SyntaxArena(StringTable& strings,
	const std::vector<SyntaxToken>& tokens,
	const std::vector<SyntaxLiteralFact>& literal_facts,
	InterningStats* stats)
	: strings_(strings), stats_(stats), tokens_(tokens),
	  literal_facts_(literal_facts), tag_cache_(kSyntaxTagCacheEntries),
	  rollback_edge_base_(0) {}

TextId SyntaxArena::InternTag(const char* tag, SyntaxTagCode* code) const
{
	// Syntax tags are compiler-owned static spellings. Cache their stable
	// pointers only after the ordinary interner establishes the identity, so
	// first-use ordering visible in AST dumps remains unchanged.
	const std::uintptr_t pointer = reinterpret_cast<std::uintptr_t>(tag);
	std::size_t mixed = static_cast<std::size_t>(pointer >> 4);
	mixed ^= mixed >> 17;
	mixed *= static_cast<std::size_t>(0xed5ad4bbU);
	mixed ^= mixed >> 11;
	std::size_t slot = mixed & (kSyntaxTagCacheEntries - 1);
	for (std::size_t probes = 0; probes != kSyntaxTagCacheEntries; ++probes)
	{
		TagCacheEntry& cached = tag_cache_[slot];
		if (cached.spelling == tag)
		{
			if (stats_) ++stats_->syntax_tag_cache_hits;
			if (code) *code = cached.code;
			return cached.identity;
		}
		if (cached.spelling == 0)
		{
			if (stats_) ++stats_->syntax_tag_cache_misses;
			const TextId identity = strings_.Intern(tag);
			cached.spelling = tag;
			cached.identity = identity;
			cached.code = ClassifySyntaxTag(tag);
			if (code) *code = cached.code;
			return identity;
		}
		slot = (slot + 1) & (kSyntaxTagCacheEntries - 1);
	}
	// The source tree currently has far fewer static tag sites than entries,
	// but retain the ordinary correct path if that invariant ever changes.
	if (stats_) ++stats_->syntax_tag_cache_misses;
	if (code) *code = ClassifySyntaxTag(tag);
	return strings_.Intern(tag);
}

NodeId SyntaxArena::Make(const char* tag)
{
	return Make(tag, std::string());
}

NodeId SyntaxArena::Make(const char* tag, const std::string& payload)
{
	if (nodes_.size() >= kNoNode)
		ThrowSyntaxResourceLimit("too many syntax nodes");
	const NodeId id = static_cast<NodeId>(nodes_.size());
	// The payload may refer to a spelling already owned by strings_.  Interning
	// the tag can grow that table and invalidate the reference, so capture the
	// payload identity before performing any other interning operation.
	if (stats_)
	{
		++stats_->syntax_tag_calls;
		if (!payload.empty()) ++stats_->syntax_payload_calls;
	}
	const TextId payload_id = payload.empty() ? 0 : strings_.Intern(payload);
	SyntaxTagCode tag_code = STAG_NONE;
	const TextId tag_id = InternTag(tag, &tag_code);
	nodes_.push_back(SyntaxNode(tag_id, payload_id, tag_code));
	return id;
}

void SyntaxArena::Add(NodeId parent, NodeId child)
{
	if (parent == kNoNode || child == kNoNode) return;
	const std::uint32_t edge = PrepareEdgeMutation(parent, child);
	SyntaxNode& node = nodes_[parent];
	if (node.first_edge == kNoEdge) node.first_edge = edge;
	else edges_[node.last_edge].next = edge;
	node.last_edge = edge;
}

std::uint32_t SyntaxArena::PrepareEdgeMutation(NodeId parent, NodeId child)
{
	if (edges_.size() >= kNoEdge)
		ThrowSyntaxResourceLimit("too many syntax edges");
	const std::uint32_t edge = static_cast<std::uint32_t>(edges_.size());
	SyntaxNode& node = nodes_[parent];
	edge_mutations_.push_back(
		EdgeMutation(parent, node.first_edge, node.last_edge));
	edges_.push_back(SyntaxEdge(child));
	return edge;
}

void SyntaxArena::Prepend(NodeId parent, NodeId child)
{
	if (parent == kNoNode || child == kNoNode) return;
	const std::uint32_t edge = PrepareEdgeMutation(parent, child);
	SyntaxNode& node = nodes_[parent];
	edges_[edge].next = node.first_edge;
	node.first_edge = edge;
	if (node.last_edge == kNoEdge) node.last_edge = edge;
}

std::size_t SyntaxArena::NodeMark() const { return nodes_.size(); }
std::size_t SyntaxArena::EdgeMark() const { return edges_.size(); }

void SyntaxArena::Rollback(std::size_t node_mark, std::size_t edge_mark)
{
	if (node_mark > nodes_.size() || edge_mark > edges_.size() ||
		edge_mark < rollback_edge_base_ ||
		edge_mutations_.size() != edges_.size() - rollback_edge_base_)
		ThrowSyntaxInternal("invalid syntax rollback checkpoint");
	for (std::size_t edge = edges_.size(); edge != edge_mark; --edge)
	{
		const EdgeMutation& mutation =
			edge_mutations_[edge - rollback_edge_base_ - 1];
		if (mutation.parent >= node_mark) continue;
		SyntaxNode& parent = nodes_[mutation.parent];
		if (mutation.last_edge != kNoEdge)
			edges_[mutation.last_edge].next = kNoEdge;
		parent.first_edge = mutation.first_edge;
		parent.last_edge = mutation.last_edge;
	}
	edge_mutations_.erase(
		edge_mutations_.begin() + (edge_mark - rollback_edge_base_),
		edge_mutations_.end());
	edges_.erase(edges_.begin() + edge_mark, edges_.end());
	nodes_.erase(nodes_.begin() + node_mark, nodes_.end());
}

std::size_t SyntaxArena::RollbackStorageBytes() const
{
	return edge_mutations_.capacity() * sizeof(EdgeMutation);
}

void SyntaxArena::ReleaseRollbackStorage()
{
	std::vector<EdgeMutation>().swap(edge_mutations_);
	rollback_edge_base_ = edges_.size();
}

void SyntaxArena::Write(std::ostream& output, NodeId root,
	std::size_t* output_bytes, std::size_t* max_depth,
	std::size_t* stack_storage_bytes) const
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
	std::size_t bytes = 0;
	std::size_t deepest = 0;
	const bool measure = output_bytes != 0;
	stack.push_back(Frame(root, 0));
	while (!stack.empty())
	{
		Frame& frame = stack.back();
		const SyntaxNode& node = nodes_[frame.node];
		if (!frame.entered &&
			(node.flags & SYNTAX_FLAG_SEMANTIC_ONLY) != 0)
		{
			stack.pop_back();
			continue;
		}
		if (!frame.entered)
		{
			if (measure) deepest = std::max(deepest, frame.depth);
			for (std::size_t i = 0; i < frame.depth; ++i) output << "  ";
			output << strings_.Get(node.tag);
			if (measure)
				bytes += frame.depth * 2 + strings_.Get(node.tag).size() + 1;
			if (node.payload != 0)
			{
				output << ' ' << strings_.Get(node.payload);
				if (measure) bytes += strings_.Get(node.payload).size() + 1;
			}
			output << '\n';
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
	if (output_bytes) *output_bytes = bytes;
	if (max_depth) *max_depth = deepest;
	if (stack_storage_bytes)
		*stack_storage_bytes = stack.capacity() * sizeof(Frame);
}

std::size_t SyntaxArena::Nodes() const { return nodes_.size(); }
std::size_t SyntaxArena::Edges() const { return edges_.size(); }

const std::string& SyntaxArena::Tag(NodeId node) const
{
	return strings_.Get(nodes_[node].tag);
}

TextId SyntaxArena::TagId(NodeId node) const
{
	return nodes_[node].tag;
}

bool SyntaxArena::IsTag(NodeId node, const char* tag) const
{
	if (stats_) ++stats_->syntax_tag_query_calls;
	return nodes_[node].tag == InternTag(tag);
}

const std::string& SyntaxArena::Payload(NodeId node) const
{
	return strings_.Get(nodes_[node].payload);
}

TextId SyntaxArena::PayloadId(NodeId node) const
{
	return nodes_[node].payload;
}

const std::string& SyntaxArena::SemanticPayload(NodeId node) const
{
	const SyntaxNode& record = nodes_[node];
	return strings_.Get(record.semantic_payload == 0 ?
		record.payload : record.semantic_payload);
}

TextId SyntaxArena::SemanticPayloadId(NodeId node) const
{
	const SyntaxNode& record = nodes_[node];
	return record.semantic_payload == 0 ?
		record.payload : record.semantic_payload;
}

bool SyntaxArena::HasSemanticPayload(NodeId node) const
{
	return nodes_[node].semantic_payload != 0;
}

void SyntaxArena::SetSemanticPayload(NodeId node, TextId payload)
{
	nodes_[node].semantic_payload = payload;
}

void SyntaxArena::SetLiteralFact(NodeId node, std::uint32_t fact)
{
	if (fact == kNoLiteralFact) return;
	if (fact >= literal_facts_.size())
		ThrowSyntaxInternal("invalid scalar literal fact");
	nodes_[node].token_first = fact;
	nodes_[node].flags |= SYNTAX_FLAG_LITERAL_FACT;
}

bool SyntaxArena::ScalarLiteralFact(NodeId node, FundamentalType* type,
	std::uint64_t* value) const
{
	const SyntaxNode& syntax = nodes_[node];
	if ((syntax.flags & SYNTAX_FLAG_LITERAL_FACT) == 0) return false;
	if (syntax.token_first >= literal_facts_.size())
		ThrowSyntaxInternal("scalar literal fact is out of range");
	const SyntaxLiteralFact& fact = literal_facts_[syntax.token_first];
	if (!fact.value_valid) return false;
	if (type) *type = fact.type;
	if (value) *value = fact.value;
	return true;
}

void SyntaxArena::AppendImmediateParameterNames(NodeId declarator,
	std::vector<TextId>* result) const
{
	std::vector<NodeId> pending(1, declarator);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		for (std::uint32_t edge = FirstEdge(current); edge != kNoEdge;
			edge = NextEdge(edge))
		{
			const NodeId child = EdgeChild(edge);
			if (!IsTag(child, ::cppgm::syntax::STAG_PARAMETER_CLAUSE))
			{
				pending.push_back(child);
				continue;
			}
			for (std::uint32_t item = FirstEdge(child); item != kNoEdge;
				item = NextEdge(item))
			{
				const NodeId parameter = EdgeChild(item);
				if (!IsTag(parameter, ::cppgm::syntax::STAG_PARAMETER_DECLARATION)) continue;
				const TextId name = nodes_[parameter].semantic_payload;
				if (name != 0) result->push_back(name);
			}
			return;
		}
	}
}

void SyntaxArena::SetPayload(NodeId node, const std::string& payload)
{
	if (stats_ && !payload.empty()) ++stats_->syntax_payload_update_calls;
	nodes_[node].payload = payload.empty() ? 0 : strings_.Intern(payload);
}

NodeId SyntaxArena::FindDirectChildTag(NodeId node, const char* tag) const
{
	if (stats_) ++stats_->syntax_tag_query_calls;
	const TextId identity = InternTag(tag);
	for (std::uint32_t edge = nodes_[node].first_edge;
		edge != kNoEdge; edge = edges_[edge].next)
	{
		const NodeId child = edges_[edge].child;
		if (nodes_[child].tag == identity) return child;
	}
	return kNoNode;
}

bool SyntaxArena::HasDirectChildTag(NodeId node, const char* tag) const
{
	if (stats_) ++stats_->syntax_tag_query_calls;
	const TextId identity = InternTag(tag);
	for (std::uint32_t edge = nodes_[node].first_edge;
		edge != kNoEdge; edge = edges_[edge].next)
		if (nodes_[edges_[edge].child].tag == identity) return true;
	return false;
}

bool SyntaxArena::HasDirectChildTag(NodeId node, SyntaxTagCode tag) const
{
	if (stats_) ++stats_->syntax_tag_query_calls;
	for (std::uint32_t edge = nodes_[node].first_edge;
		edge != kNoEdge; edge = edges_[edge].next)
		if (nodes_[edges_[edge].child].tag_code == tag) return true;
	return false;
}

bool SyntaxArena::HasDescendantTag(NodeId node, const char* tag) const
{
	if (stats_) ++stats_->syntax_tag_query_calls;
	const TextId identity = InternTag(tag);
	std::vector<NodeId> pending(1, node);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		for (std::uint32_t edge = nodes_[current].first_edge;
			edge != kNoEdge; edge = edges_[edge].next)
		{
			const NodeId child = edges_[edge].child;
			if (nodes_[child].tag == identity) return true;
			pending.push_back(child);
		}
	}
	return false;
}

bool SyntaxArena::HasDescendantTag(NodeId node, SyntaxTagCode tag) const
{
	if (stats_) ++stats_->syntax_tag_query_calls;
	std::vector<NodeId> pending(1, node);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		for (std::uint32_t edge = nodes_[current].first_edge;
			edge != kNoEdge; edge = edges_[edge].next)
		{
			const NodeId child = edges_[edge].child;
			if (nodes_[child].tag_code == tag) return true;
			pending.push_back(child);
		}
	}
	return false;
}

void SyntaxArena::SetTokenRange(NodeId node, std::size_t first,
	std::size_t last)
{
	if (first > std::numeric_limits<std::uint32_t>::max() ||
		last > std::numeric_limits<std::uint32_t>::max())
		ThrowSyntaxResourceLimit("syntax token range is too large");
	nodes_[node].token_first = static_cast<std::uint32_t>(first);
	nodes_[node].token_last = static_cast<std::uint32_t>(last);
}

std::size_t SyntaxArena::TokenFirst(NodeId node) const
{
	return nodes_[node].token_first;
}

std::size_t SyntaxArena::TokenLast(NodeId node) const
{
	return nodes_[node].token_last;
}

const std::string& SyntaxArena::SourceFile(NodeId node) const
{
	const std::size_t token = nodes_[node].token_first;
	return strings_.Get(token < tokens_.size() ? tokens_[token].source_file : 0);
}

std::size_t SyntaxArena::SourceLine(NodeId node) const
{
	const std::size_t token = nodes_[node].token_first;
	return token < tokens_.size() ? tokens_[token].source_line : 0;
}

std::size_t SyntaxArena::SourceColumn(NodeId node) const
{
	const std::size_t token = nodes_[node].token_first;
	return token < tokens_.size() ? tokens_[token].source_column : 0;
}

void SyntaxArena::AddFlags(NodeId node, std::uint16_t flags)
{
	nodes_[node].flags = static_cast<std::uint16_t>(nodes_[node].flags | flags);
}

std::uint16_t SyntaxArena::Flags(NodeId node) const
{
	return nodes_[node].flags;
}

StringTable& SyntaxArena::SharedStrings() const
{
	return strings_;
}

std::size_t SyntaxArena::StorageBytes() const
{
	return nodes_.capacity() * sizeof(SyntaxNode) +
		edges_.capacity() * sizeof(SyntaxEdge) +
		tag_cache_.capacity() * sizeof(TagCacheEntry);
}

}
}
