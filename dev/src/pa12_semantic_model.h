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
	DUMP_LABELED_STATEMENT,
	DUMP_GOTO_STATEMENT,
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
	DUMP_AGGREGATE_CONSTRUCTION_ACTION,
	DUMP_INITIALIZER_ACTION,
	DUMP_BASE_INITIALIZER_ACTION,
	DUMP_MEMBER_EXPRESSION,
	DUMP_NEW_EXPRESSION,
	DUMP_TEMPORARY_OBJECT,
	DUMP_CONSTRUCTOR_ACTION,
	DUMP_CONSTRUCTOR_ARRAY_ACTION,
	DUMP_DESTRUCTOR_ACTION
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
	BindingId binding, object_binding;
	std::int64_t constant_value;
	std::uint32_t first_edge;
	std::uint32_t last_edge;
	std::uint32_t base_projection_count;
	std::uint32_t aggregate_helper;
	bool constant;
	bool integer_narrowing_conversion;
	bool value_initialization;
	bool elide_empty_constructor;

	explicit DumpNode(DumpKind value)
		: kind(value), type(kNoType), operand_type(kNoType),
		  category(VALUE_NONE), text(0), binding(kNoBinding),
		  object_binding(kNoBinding),
		  constant_value(0), first_edge(kNoDumpEdge),
		  last_edge(kNoDumpEdge), base_projection_count(0),
		  aggregate_helper(kNoDumpEdge),
		  constant(false), integer_narrowing_conversion(false),
		  value_initialization(false), elide_empty_constructor(false) {}
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
	bool is_friend;
	bool placeholder_auto;
	bool thread_local_storage;
	SpecInfo() : type(kNoType), storage_class(STORAGE_CLASS_NONE),
		is_typedef(false), is_constexpr(false), is_friend(false),
		placeholder_auto(false),
		thread_local_storage(false) {}
};

struct ParameterInfo
{
	NameId name;
	TypeId declared_type;
	TypeId function_type;
	NodeId default_argument;
	ScopeId default_scope;
	ParameterInfo(NameId name_value, TypeId declared_value,
		TypeId function_value)
		: name(name_value), declared_type(declared_value),
		  function_type(function_value), default_argument(kNoNode),
		  default_scope(kNoScope) {}
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
	CONVERSION_DERIVED_TO_BASE = 2,
	CONVERSION_STANDARD = 3,
	CONVERSION_BOOLEAN = 4,
	CONVERSION_ELLIPSIS = 5,
	CONVERSION_INVALID = 100
};

struct ObjectConversionFact
{
	ConversionRank rank;
	std::uint32_t base_projection_count;

	ObjectConversionFact()
		: rank(CONVERSION_INVALID), base_projection_count(0) {}
};

struct FunctionInfo
{
	BindingId binding;
	BindingId inherited_constructor_source;
	ScopeId owner;
	TypeId type, signature;
	NameId display_name;
	TypeId member_owner;
	EntityId friend_of;
	ScopeId lexical_scope;
	std::vector<ParameterInfo> parameters;
	NodeId definition_body, constructor_initializer;
	bool defined;
	bool deferred;
	bool template_specialization;
	bool constructor;
	bool implicit_constructor;
	bool defaulted_constructor;
	bool deleted_constructor;
	bool explicit_constructor;
	bool destructor;
	bool implicit_destructor;
	bool defaulted_destructor;
	bool deleted_destructor;
	bool ordinary_visible;
	std::uint8_t demand_state;
	FunctionInfo()
		: binding(kNoBinding), inherited_constructor_source(kNoBinding),
		  owner(kNoScope), type(kNoType), signature(kNoType),
		  member_owner(kNoType), friend_of(kNoEntity), lexical_scope(kNoScope),
		  definition_body(kNoNode), constructor_initializer(kNoNode),
		  defined(false), deferred(false), template_specialization(false),
		  constructor(false), implicit_constructor(false),
		  defaulted_constructor(false), deleted_constructor(false),
		  explicit_constructor(false), destructor(false),
		  implicit_destructor(false), defaulted_destructor(false),
		  deleted_destructor(false), ordinary_visible(true),
		  demand_state(0) {}
};

struct ClassLayoutMember
{
	BindingId binding;
	TypeId type;
	std::uint32_t bit_width;
	bool bit_field;

	ClassLayoutMember(BindingId binding_value, TypeId type_value,
		std::uint32_t bit_width_value = 0, bool bit_field_value = false)
		: binding(binding_value), type(type_value), bit_width(bit_width_value),
		  bit_field(bit_field_value) {}
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

struct LifetimeObligation
{
	BindingId object, destructor;
	TypeId type;
	LifetimeObligation(BindingId object_value, BindingId destructor_value,
		TypeId type_value)
		: object(object_value), destructor(destructor_value), type(type_value) {}
};

struct NamespaceObjectAction
{
	BindingId object;
	TypeId type;
	std::uint32_t variable, initializer, destructor;

	NamespaceObjectAction(BindingId object_value, TypeId type_value,
		std::uint32_t variable_value, std::uint32_t initializer_value,
		std::uint32_t destructor_value)
		: object(object_value), type(type_value), variable(variable_value),
		  initializer(initializer_value), destructor(destructor_value) {}
};

// A lowering-only aggregate helper has a canonical typed identity but is not a
// C++ constructor declaration and therefore never participates in lookup.
// The explicit parameters correspond one-for-one with members; the hidden
// object parameter is already present in function_type.
struct AggregateHelperInfo
{
	EntityId entity;
	TypeId object_type;
	TypeId function_type;
	std::vector<BindingId> members;

	AggregateHelperInfo(EntityId entity_value, TypeId object_type_value,
		TypeId function_type_value, const std::vector<BindingId>& members_value)
		: entity(entity_value), object_type(object_type_value),
		  function_type(function_type_value), members(members_value) {}
};

// Borrowed, translation-unit-local view of the canonical PA12 graph.  The
// owner invokes consumers synchronously before releasing Program and DumpArena;
// consumers must copy only the typed facts needed by their next phase.
struct SemanticGraphView
{
	const Program& program;
	const DumpArena& arena;
	const std::vector<NamespaceObjectAction>& namespace_objects;
	const std::vector<AggregateHelperInfo>& aggregate_helpers;
	std::uint32_t root;

	SemanticGraphView(const Program& program_value,
		const DumpArena& arena_value,
		const std::vector<NamespaceObjectAction>& namespace_objects_value,
		const std::vector<AggregateHelperInfo>& aggregate_helpers_value,
		std::uint32_t root_value)
		: program(program_value), arena(arena_value),
		  namespace_objects(namespace_objects_value),
		  aggregate_helpers(aggregate_helpers_value), root(root_value) {}
};

class SemanticGraphConsumer
{
public:
	virtual ~SemanticGraphConsumer() {}
	virtual void Consume(const SemanticGraphView& graph) = 0;
};

}
}
