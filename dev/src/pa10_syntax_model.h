#pragma once

#include "frontend_intern.h"
#include "pa10_syntax.h"
#include "pa10_syntax_tags.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa10_syntax_detail
{

typedef InternedStringId TextId;
typedef std::uint32_t NodeId;

extern const std::uint16_t kSimpleTokenCount;
extern const std::uint16_t kIdentifierToken;
extern const std::uint16_t kLiteralToken;
extern const std::uint16_t kEofToken;
extern const std::uint16_t kRShiftFirstToken;
extern const std::uint16_t kRShiftSecondToken;
extern const std::uint16_t kPragmaPackPushToken;
extern const std::uint16_t kPragmaPackPopToken;
extern const std::uint32_t kNoLiteralFact;
extern const NodeId kNoNode;
extern const std::uint32_t kNoEdge;

typedef InternedStringTable StringTable;

struct SyntaxLiteralFact
{
	std::uint64_t value;
	FundamentalType type;
	bool value_valid;

	SyntaxLiteralFact(FundamentalType type_value, std::uint64_t value_value,
		bool value_valid_value)
		: value(value_value), type(type_value),
		  value_valid(value_valid_value) {}
};

struct SyntaxToken
{
	std::uint32_t kind_and_literal_fact;
	TextId spelling;
	std::uint32_t source_line;
	std::uint16_t source_file, source_column;

	SyntaxToken(std::uint16_t kind_value, TextId spelling_value,
		std::uint32_t literal_fact = kNoLiteralFact,
		TextId source_file_value = 0, std::uint32_t source_line_value = 0,
		std::uint32_t source_column_value = 0);
	std::uint16_t Kind() const;
	std::uint32_t LiteralFact() const;
};

class SyntaxTokenSink : public IPostTokenStream
{
public:
	SyntaxTokenSink(StringTable& strings, SyntaxInterningStats* stats);
	void SetSourceLocation(const std::string& file,
		std::size_t line, std::size_t column);
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
	void EmitPragmaPackPush(std::size_t alignment);
	void EmitPragmaPackPop();
	void EmitEof();
	const std::vector<SyntaxToken>& Tokens() const;
	const std::vector<SyntaxLiteralFact>& LiteralFacts() const;
	std::size_t StorageBytes() const;

private:
	TextId InternTokenSpelling(const std::string& source);
	SyntaxToken LocatedToken(std::uint16_t kind, TextId spelling,
		std::uint32_t literal_fact = kNoLiteralFact) const;
	void EmitLiteralSpelling(const std::string& source);
	void EmitScalarLiteral(const std::string& source, FundamentalType type,
		const void* data, std::size_t size);

	StringTable& strings_;
	SyntaxInterningStats* stats_;
	TextId source_file_;
	TextId cached_source_file_;
	std::string cached_source_file_spelling_;
	bool has_cached_source_file_;
	std::uint32_t source_line_, source_column_;
	std::vector<SyntaxToken> tokens_;
	std::vector<SyntaxLiteralFact> literal_facts_;
};

struct SyntaxNode
{
	TextId tag;
	TextId payload;
	TextId semantic_payload;
	std::uint32_t first_edge;
	std::uint32_t last_edge;
	std::uint32_t token_first;
	std::uint32_t token_last;
	std::uint16_t flags;
	SyntaxTagCode tag_code;

	SyntaxNode(TextId tag_value, TextId payload_value,
		SyntaxTagCode tag_code_value);
};

enum SyntaxNodeFlags
{
	SYNTAX_FLAG_NONE = 0,
	SYNTAX_FLAG_DEFINITION = 1,
	SYNTAX_FLAG_SEMANTIC_ONLY = 2,
	SYNTAX_FLAG_LITERAL_FACT = 4,
	SYNTAX_FLAG_TYPENAME = 8,
	SYNTAX_FLAG_DIRECT_LINKAGE_DECLARATION = 16
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
	SyntaxArena(StringTable& strings, const std::vector<SyntaxToken>& tokens,
		const std::vector<SyntaxLiteralFact>& literal_facts,
		SyntaxInterningStats* stats);
	NodeId Make(const char* tag);
	NodeId Make(const char* tag, const std::string& payload);
	void Add(NodeId parent, NodeId child);
	void Prepend(NodeId parent, NodeId child);
	std::size_t NodeMark() const;
	std::size_t EdgeMark() const;
	void Rollback(std::size_t node_mark, std::size_t edge_mark);
	std::size_t RollbackStorageBytes() const;
	void ReleaseRollbackStorage();
	void Write(std::ostream& output, NodeId root, std::size_t* output_bytes,
		std::size_t* max_depth, std::size_t* stack_storage_bytes) const;
	std::size_t Nodes() const;
	std::size_t Edges() const;
	TextId TagId(NodeId node) const;
	const std::string& Tag(NodeId node) const;
	bool IsTag(NodeId node, const char* tag) const;
	bool IsTag(NodeId node, SyntaxTagCode tag) const
	{
		if (stats_) ++stats_->syntax_tag_query_calls;
		return nodes_[node].tag_code == tag;
	}
	TextId PayloadId(NodeId node) const;
	const std::string& Payload(NodeId node) const;
	const std::string& SemanticPayload(NodeId node) const;
	TextId SemanticPayloadId(NodeId node) const;
	bool HasSemanticPayload(NodeId node) const;
	void SetSemanticPayload(NodeId node, TextId payload);
	void SetLiteralFact(NodeId node, std::uint32_t fact);
	bool ScalarLiteralFact(NodeId node, FundamentalType* type,
		std::uint64_t* value) const;
	void AppendImmediateParameterNames(NodeId declarator,
		std::vector<TextId>* result) const;
	void SetPayload(NodeId node, const std::string& payload);
	NodeId FindDirectChildTag(NodeId node, const char* tag) const;
	NodeId FindDirectChildTag(NodeId node, SyntaxTagCode tag) const
	{
		if (stats_) ++stats_->syntax_tag_query_calls;
		for (std::uint32_t edge = nodes_[node].first_edge;
			edge != kNoEdge; edge = edges_[edge].next)
		{
			const NodeId child = edges_[edge].child;
			if (nodes_[child].tag_code == tag) return child;
		}
		return kNoNode;
	}
	bool HasDirectChildTag(NodeId node, const char* tag) const;
	bool HasDirectChildTag(NodeId node, SyntaxTagCode tag) const;
	bool HasDescendantTag(NodeId node, const char* tag) const;
	bool HasDescendantTag(NodeId node, SyntaxTagCode tag) const;
	std::uint32_t FirstEdge(NodeId node) const
	{
		return nodes_[node].first_edge;
	}
	std::uint32_t NextEdge(std::uint32_t edge) const
	{
		return edges_[edge].next;
	}
	NodeId EdgeChild(std::uint32_t edge) const
	{
		return edges_[edge].child;
	}
	void SetTokenRange(NodeId node, std::size_t first, std::size_t last);
	std::size_t TokenFirst(NodeId node) const;
	std::size_t TokenLast(NodeId node) const;
	const std::string& SourceFile(NodeId node) const;
	std::size_t SourceLine(NodeId node) const;
	std::size_t SourceColumn(NodeId node) const;
	void AddFlags(NodeId node, std::uint16_t flags);
	std::uint16_t Flags(NodeId node) const;
	StringTable& SharedStrings() const;
	std::size_t StorageBytes() const;

private:
	struct TagCacheEntry
	{
		const char* spelling;
		TextId identity;
		SyntaxTagCode code;
		TagCacheEntry() : spelling(0), identity(0), code(STAG_NONE) {}
	};

	struct EdgeMutation
	{
		NodeId parent;
		std::uint32_t first_edge, last_edge;
		EdgeMutation(NodeId parent_value, std::uint32_t first,
			std::uint32_t last)
			: parent(parent_value), first_edge(first), last_edge(last) {}
	};
	TextId InternTag(const char* tag, SyntaxTagCode* code = 0) const;

	StringTable& strings_;
	SyntaxInterningStats* stats_;
	const std::vector<SyntaxToken>& tokens_;
	const std::vector<SyntaxLiteralFact>& literal_facts_;
	std::vector<SyntaxNode> nodes_;
	std::vector<SyntaxEdge> edges_;
	mutable std::vector<TagCacheEntry> tag_cache_;
	std::vector<EdgeMutation> edge_mutations_;
	std::size_t rollback_edge_base_;
};

class SyntaxTreeConsumer
{
public:
	virtual ~SyntaxTreeConsumer() {}
	virtual void Consume(const SyntaxArena& arena, NodeId root) = 0;
};

}
}
