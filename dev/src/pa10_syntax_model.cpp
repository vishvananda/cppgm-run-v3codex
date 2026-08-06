#include "pa10_syntax_model.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace cppgm
{
namespace pa10_syntax_detail
{
namespace
{

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

}

const std::uint16_t kSimpleTokenCount =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kIdentifierToken = kSimpleTokenCount;
const std::uint16_t kLiteralToken = kSimpleTokenCount + 1;
const std::uint16_t kEofToken = kSimpleTokenCount + 2;
const std::uint16_t kRShiftFirstToken = kSimpleTokenCount + 3;
const std::uint16_t kRShiftSecondToken = kSimpleTokenCount + 4;
const NodeId kNoNode = std::numeric_limits<NodeId>::max();
const std::uint32_t kNoEdge = std::numeric_limits<std::uint32_t>::max();

StringTable::StringTable() : slots_(32, 0), spelling_bytes_(0)
{
	texts_.push_back(std::string());
}

TextId StringTable::Intern(const std::string& text)
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

const std::string& StringTable::Get(TextId id) const { return texts_[id]; }
std::size_t StringTable::Size() const { return texts_.size() - 1; }
std::size_t StringTable::SpellingBytes() const { return spelling_bytes_; }

std::size_t StringTable::StorageBytes() const
{
	std::size_t bytes = texts_.capacity() * sizeof(std::string) +
		slots_.capacity() * sizeof(TextId);
	for (std::size_t i = 1; i < texts_.size(); ++i)
		bytes += texts_[i].capacity();
	return bytes;
}

void StringTable::Rehash(std::size_t capacity)
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

SyntaxToken::SyntaxToken(std::uint16_t kind_value, TextId spelling_value)
	: kind(kind_value), spelling(spelling_value)
{
}

SyntaxTokenSink::SyntaxTokenSink(StringTable& strings) : strings_(strings) {}

void SyntaxTokenSink::EmitInvalid(const std::string& source)
{
	throw std::runtime_error("invalid phase-7 token: " + source);
}

void SyntaxTokenSink::EmitSimple(const std::string& source,
	SimpleTokenKind kind)
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

void SyntaxTokenSink::EmitIdentifier(const std::string& source)
{
	tokens_.push_back(SyntaxToken(kIdentifierToken, strings_.Intern(source)));
}

void SyntaxTokenSink::EmitLiteral(const std::string& source, FundamentalType,
	const void*, std::size_t)
{
	EmitLiteralSpelling(source);
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

void SyntaxTokenSink::EmitEof()
{
	tokens_.push_back(SyntaxToken(kEofToken, strings_.Intern(std::string())));
}

const std::vector<SyntaxToken>& SyntaxTokenSink::Tokens() const
{
	return tokens_;
}

std::size_t SyntaxTokenSink::StorageBytes() const
{
	return tokens_.capacity() * sizeof(SyntaxToken);
}

void SyntaxTokenSink::EmitLiteralSpelling(const std::string& source)
{
	tokens_.push_back(SyntaxToken(kLiteralToken, strings_.Intern(source)));
}

SyntaxNode::SyntaxNode(TextId tag_value, TextId payload_value)
	: tag(tag_value), payload(payload_value), first_edge(kNoEdge),
	  last_edge(kNoEdge)
{
}

SyntaxEdge::SyntaxEdge(NodeId child_value) : child(child_value), next(kNoEdge)
{
}

SyntaxArena::SyntaxArena(StringTable& strings) : strings_(strings) {}

NodeId SyntaxArena::Make(const char* tag)
{
	return Make(tag, std::string());
}

NodeId SyntaxArena::Make(const char* tag, const std::string& payload)
{
	if (nodes_.size() >= kNoNode)
		throw std::runtime_error("too many syntax nodes");
	const NodeId id = static_cast<NodeId>(nodes_.size());
	nodes_.push_back(SyntaxNode(strings_.Intern(tag),
		payload.empty() ? 0 : strings_.Intern(payload)));
	return id;
}

void SyntaxArena::Add(NodeId parent, NodeId child)
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

void SyntaxArena::Prepend(NodeId parent, NodeId child)
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

std::size_t SyntaxArena::NodeMark() const { return nodes_.size(); }
std::size_t SyntaxArena::EdgeMark() const { return edges_.size(); }

void SyntaxArena::Rollback(std::size_t node_mark, std::size_t edge_mark)
{
	nodes_.erase(nodes_.begin() + node_mark, nodes_.end());
	edges_.erase(edges_.begin() + edge_mark, edges_.end());
	// Surviving nodes only have edges below edge_mark: speculative callers mark
	// before attaching children to an existing parent.
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

const std::string& SyntaxArena::Payload(NodeId node) const
{
	return strings_.Get(nodes_[node].payload);
}

void SyntaxArena::SetPayload(NodeId node, const std::string& payload)
{
	nodes_[node].payload = payload.empty() ? 0 : strings_.Intern(payload);
}

bool SyntaxArena::HasDirectChildTag(NodeId node, const char* tag) const
{
	for (std::uint32_t edge = nodes_[node].first_edge;
		edge != kNoEdge; edge = edges_[edge].next)
		if (Tag(edges_[edge].child) == tag) return true;
	return false;
}

std::size_t SyntaxArena::StorageBytes() const
{
	return nodes_.capacity() * sizeof(SyntaxNode) +
		edges_.capacity() * sizeof(SyntaxEdge);
}

}
}
