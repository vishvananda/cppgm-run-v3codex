#pragma once

#include "pa10_syntax.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa10_syntax_detail
{

typedef std::uint32_t TextId;
typedef std::uint32_t NodeId;

extern const std::uint16_t kSimpleTokenCount;
extern const std::uint16_t kIdentifierToken;
extern const std::uint16_t kLiteralToken;
extern const std::uint16_t kEofToken;
extern const std::uint16_t kRShiftFirstToken;
extern const std::uint16_t kRShiftSecondToken;
extern const NodeId kNoNode;
extern const std::uint32_t kNoEdge;

class StringTable
{
public:
	StringTable();
	TextId Intern(const std::string& text);
	const std::string& Get(TextId id) const;
	std::size_t Size() const;
	std::size_t SpellingBytes() const;
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);

	std::vector<std::string> texts_;
	std::vector<TextId> slots_;
	std::size_t spelling_bytes_;
};

struct SyntaxToken
{
	std::uint16_t kind;
	TextId spelling;

	SyntaxToken(std::uint16_t kind_value, TextId spelling_value);
};

class SyntaxTokenSink : public IPostTokenStream
{
public:
	explicit SyntaxTokenSink(StringTable& strings);
	void EmitInvalid(const std::string& source);
	void EmitSimple(const std::string& source, SimpleTokenKind kind);
	void EmitIdentifier(const std::string& source);
	void EmitLiteral(const std::string& source, FundamentalType,
		const void*, std::size_t);
	void EmitLiteralArray(const std::string& source, std::size_t,
		FundamentalType, const void*, std::size_t);
	void EmitUserDefinedCharacter(const std::string& source,
		const std::string&, FundamentalType, const void*, std::size_t);
	void EmitUserDefinedString(const std::string& source,
		const std::string&, std::size_t, FundamentalType,
		const void*, std::size_t);
	void EmitUserDefinedInteger(const std::string& source,
		const std::string&, const std::string&);
	void EmitUserDefinedFloating(const std::string& source,
		const std::string&, const std::string&);
	void EmitEof();
	const std::vector<SyntaxToken>& Tokens() const;
	std::size_t StorageBytes() const;

private:
	void EmitLiteralSpelling(const std::string& source);

	StringTable& strings_;
	std::vector<SyntaxToken> tokens_;
};

struct SyntaxNode
{
	TextId tag;
	TextId payload;
	std::uint32_t first_edge;
	std::uint32_t last_edge;

	SyntaxNode(TextId tag_value, TextId payload_value);
};

struct SyntaxEdge
{
	NodeId child;
	std::uint32_t next;

	explicit SyntaxEdge(NodeId child_value);
};

class SyntaxArena
{
public:
	explicit SyntaxArena(StringTable& strings);
	NodeId Make(const char* tag);
	NodeId Make(const char* tag, const std::string& payload);
	void Add(NodeId parent, NodeId child);
	void Prepend(NodeId parent, NodeId child);
	std::size_t NodeMark() const;
	std::size_t EdgeMark() const;
	void Rollback(std::size_t node_mark, std::size_t edge_mark);
	void Write(std::ostream& output, NodeId root, std::size_t* output_bytes,
		std::size_t* max_depth, std::size_t* stack_storage_bytes) const;
	std::size_t Nodes() const;
	std::size_t Edges() const;
	const std::string& Tag(NodeId node) const;
	const std::string& Payload(NodeId node) const;
	void SetPayload(NodeId node, const std::string& payload);
	bool HasDirectChildTag(NodeId node, const char* tag) const;
	std::size_t StorageBytes() const;

private:
	StringTable& strings_;
	std::vector<SyntaxNode> nodes_;
	std::vector<SyntaxEdge> edges_;
};

}
}
