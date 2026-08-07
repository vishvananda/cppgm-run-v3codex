#include "pa10_syntax_model.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace cppgm
{
namespace pa10_syntax_detail
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
const NodeId kNoNode = std::numeric_limits<NodeId>::max();
const std::uint32_t kNoEdge = std::numeric_limits<std::uint32_t>::max();

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

void SyntaxTokenSink::EmitPragmaPackPush(std::size_t alignment)
{
	tokens_.push_back(SyntaxToken(kPragmaPackPushToken,
		strings_.Intern(std::to_string(alignment))));
}

void SyntaxTokenSink::EmitPragmaPackPop()
{
	tokens_.push_back(SyntaxToken(kPragmaPackPopToken, 0));
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
	: tag(tag_value), payload(payload_value), semantic_payload(0),
	  first_edge(kNoEdge),
	  last_edge(kNoEdge), token_first(0), token_last(0), flags(0)
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

bool SyntaxArena::IsTag(NodeId node, const char* tag) const
{
	return nodes_[node].tag == strings_.Intern(tag);
}

const std::string& SyntaxArena::Payload(NodeId node) const
{
	return strings_.Get(nodes_[node].payload);
}

const std::string& SyntaxArena::SemanticPayload(NodeId node) const
{
	const SyntaxNode& record = nodes_[node];
	return strings_.Get(record.semantic_payload == 0 ?
		record.payload : record.semantic_payload);
}

void SyntaxArena::SetSemanticPayload(NodeId node, TextId payload)
{
	nodes_[node].semantic_payload = payload;
}

void SyntaxArena::SetPayload(NodeId node, const std::string& payload)
{
	nodes_[node].payload = payload.empty() ? 0 : strings_.Intern(payload);
}

bool SyntaxArena::HasDirectChildTag(NodeId node, const char* tag) const
{
	const TextId identity = strings_.Intern(tag);
	for (std::uint32_t edge = nodes_[node].first_edge;
		edge != kNoEdge; edge = edges_[edge].next)
		if (nodes_[edges_[edge].child].tag == identity) return true;
	return false;
}

std::uint32_t SyntaxArena::FirstEdge(NodeId node) const
{
	return nodes_[node].first_edge;
}

std::uint32_t SyntaxArena::NextEdge(std::uint32_t edge) const
{
	return edges_[edge].next;
}

NodeId SyntaxArena::EdgeChild(std::uint32_t edge) const
{
	return edges_[edge].child;
}

void SyntaxArena::SetTokenRange(NodeId node, std::size_t first,
	std::size_t last)
{
	if (first > std::numeric_limits<std::uint32_t>::max() ||
		last > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("syntax token range is too large");
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
		edges_.capacity() * sizeof(SyntaxEdge);
}

}
}
