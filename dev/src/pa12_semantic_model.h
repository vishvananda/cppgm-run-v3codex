#pragma once

#include "pa10_syntax_model.h"
#include "pa11_model.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa10_syntax_detail;
using namespace pa11;

enum ValueCategory
{
	VALUE_NONE,
	VALUE_LVALUE,
	VALUE_PRVALUE,
	VALUE_XVALUE
};

enum DumpKind
{
	DUMP_TRANSLATION_UNIT,
	DUMP_NAMESPACE,
	DUMP_TYPE_ALIAS,
	DUMP_VARIABLE,
	DUMP_FUNCTION_DECLARATION,
	DUMP_FUNCTION_DEFINITION,
	DUMP_PARAMETER,
	DUMP_COMPOUND_STATEMENT,
	DUMP_SIMPLE_DECLARATION,
	DUMP_RETURN_STATEMENT,
	DUMP_EXPRESSION_STATEMENT,
	DUMP_IF_STATEMENT,
	DUMP_SWITCH_STATEMENT,
	DUMP_WHILE_STATEMENT,
	DUMP_DO_STATEMENT,
	DUMP_FOR_STATEMENT,
	DUMP_BREAK_STATEMENT,
	DUMP_CONTINUE_STATEMENT,
	DUMP_CONDITION,
	DUMP_CONDITION_DECLARATION,
	DUMP_FOR_INIT_STATEMENT,
	DUMP_ITERATION,
	DUMP_THEN,
	DUMP_ELSE,
	DUMP_CASE_STATEMENT,
	DUMP_DEFAULT_STATEMENT,
	DUMP_CALL_EXPRESSION,
	DUMP_CALLEE,
	DUMP_ID_EXPRESSION,
	DUMP_LITERAL,
	DUMP_UNARY_EXPRESSION,
	DUMP_POSTFIX_EXPRESSION,
	DUMP_BINARY_EXPRESSION,
	DUMP_SUBSCRIPT_EXPRESSION,
	DUMP_CONDITIONAL_EXPRESSION,
	DUMP_SIZEOF_EXPRESSION,
	DUMP_ASSIGNMENT_EXPRESSION,
	DUMP_CAST_EXPRESSION,
	DUMP_BRACED_INIT_LIST,
	DUMP_MEMBER_EXPRESSION,
	DUMP_CONSTRUCTOR_ACTION
};

const std::uint32_t kNoDumpEdge =
	std::numeric_limits<std::uint32_t>::max();

struct DumpNode
{
	DumpKind kind;
	TypeId type;
	TypeId operand_type;
	ValueCategory category;
	NameId text;
	BindingId binding;
	std::int64_t constant_value;
	std::uint32_t first_edge;
	std::uint32_t last_edge;
	bool constant;

	explicit DumpNode(DumpKind value)
		: kind(value), type(kNoType), operand_type(kNoType),
		  category(VALUE_NONE), text(0), binding(kNoBinding),
		  constant_value(0), first_edge(kNoDumpEdge),
		  last_edge(kNoDumpEdge), constant(false) {}
};

struct DumpEdge
{
	std::uint32_t child;
	std::uint32_t next;
	explicit DumpEdge(std::uint32_t value)
		: child(value), next(kNoDumpEdge) {}
};

class DumpArena
{
public:
	std::uint32_t Make(DumpKind kind)
	{
		if (nodes.size() >= kNoDumpEdge)
			throw std::runtime_error("too many PA12 semantic nodes");
		nodes.push_back(DumpNode(kind));
		return static_cast<std::uint32_t>(nodes.size() - 1);
	}

	void Add(std::uint32_t parent, std::uint32_t child)
	{
		if (edges.size() >= kNoDumpEdge)
			throw std::runtime_error("too many PA12 semantic edges");
		const std::uint32_t edge = static_cast<std::uint32_t>(edges.size());
		edges.push_back(DumpEdge(child));
		DumpNode& owner = nodes[parent];
		if (owner.first_edge == kNoDumpEdge) owner.first_edge = edge;
		else edges[owner.last_edge].next = edge;
		owner.last_edge = edge;
	}

	std::size_t StorageBytes() const
	{
		return nodes.capacity() * sizeof(DumpNode) +
			edges.capacity() * sizeof(DumpEdge);
	}

	std::vector<DumpNode> nodes;
	std::vector<DumpEdge> edges;
};

struct SpecInfo
{
	TypeId type;
	StorageClass storage_class;
	bool is_typedef;
	bool is_constexpr;
	SpecInfo() : type(kNoType), storage_class(STORAGE_CLASS_NONE),
		is_typedef(false), is_constexpr(false) {}
};

struct ParameterInfo
{
	NameId name;
	TypeId declared_type;
	TypeId function_type;
	ParameterInfo(NameId name_value, TypeId declared_value,
		TypeId function_value)
		: name(name_value), declared_type(declared_value),
		  function_type(function_value) {}
};

struct DeclaratorInfo
{
	NameId name;
	TypeId type;
	std::vector<ParameterInfo> parameters;
	DeclaratorInfo() : name(0), type(kNoType) {}
};

struct ExpressionInfo
{
	std::uint32_t node;
	TypeId type;
	ValueCategory category;
	BindingId binding;
	bool constant;
	std::int64_t value;
	bool integer_literal_zero;

	ExpressionInfo()
		: node(kNoDumpEdge), type(kNoType), category(VALUE_PRVALUE),
		  binding(kNoBinding), constant(false), value(0),
		  integer_literal_zero(false) {}
};

enum ConversionRank
{
	CONVERSION_EXACT = 0,
	CONVERSION_PROMOTION = 1,
	CONVERSION_STANDARD = 2,
	CONVERSION_BOOLEAN = 3,
	CONVERSION_ELLIPSIS = 4,
	CONVERSION_INVALID = 100
};

struct FunctionInfo
{
	BindingId binding;
	ScopeId owner;
	TypeId type;
	NameId display_name;
	TypeId member_owner;
	std::vector<ParameterInfo> parameters;
	NodeId definition_body;
	bool defined;
	bool deferred;
	bool template_specialization;
	std::uint8_t demand_state;
	FunctionInfo()
		: binding(kNoBinding), owner(kNoScope), type(kNoType), display_name(0),
		  member_owner(kNoType),
		  definition_body(kNoNode), defined(false), deferred(false),
		  template_specialization(false),
		  demand_state(0) {}
};

struct FunctionTemplatePattern
{
	ScopeId owner;
	NameId name;
	NodeId specifiers;
	NodeId declarator;
	std::vector<NameId> type_parameters;

	FunctionTemplatePattern()
		: owner(kNoScope), name(0), specifiers(kNoNode),
		  declarator(kNoNode) {}
};

struct InjectedMemberInfo
{
	BindingId storage;
	NameId member;
	InjectedMemberInfo() : storage(kNoBinding), member(0) {}
	InjectedMemberInfo(BindingId storage_value, NameId member_value)
		: storage(storage_value), member(member_value) {}
};

// Borrowed, translation-unit-local view of the canonical PA12 graph.  The
// owner invokes consumers synchronously before releasing Program and DumpArena;
// consumers must copy only the typed facts needed by their next phase.
struct SemanticGraphView
{
	const Program& program;
	const DumpArena& arena;
	std::uint32_t root;

	SemanticGraphView(const Program& program_value,
		const DumpArena& arena_value, std::uint32_t root_value)
		: program(program_value), arena(arena_value), root(root_value) {}
};

class SemanticGraphConsumer
{
public:
	virtual ~SemanticGraphConsumer() {}
	virtual void Consume(const SemanticGraphView& graph) = 0;
};

}
}
