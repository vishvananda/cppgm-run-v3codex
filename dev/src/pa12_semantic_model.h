#pragma once

#include "pa10_syntax_model.h"
#include "pa11_model.h"

#include <cstdint>
#include <deque>
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
	DUMP_CONDITIONAL_ARM,
	DUMP_SIZEOF_EXPRESSION,
	DUMP_ASSIGNMENT_EXPRESSION,
	DUMP_CAST_EXPRESSION,
	DUMP_BRACED_INIT_LIST,
	DUMP_AGGREGATE_CONSTRUCTION_ACTION,
	DUMP_CLASS_VALUE_TRANSFER,
	DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION,
	DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION,
	DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION,
	DUMP_INITIALIZER_ACTION,
	DUMP_BASE_INITIALIZER_ACTION,
	DUMP_VPTR_INITIALIZATION_ACTION,
	DUMP_DELEGATING_INITIALIZER_ACTION,
	DUMP_MEMBER_EXPRESSION,
	DUMP_NEW_EXPRESSION,
	DUMP_DELETE_EXPRESSION,
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
	BindingId binding, object_binding, selected_binding;
	std::int64_t constant_value;
	std::uint64_t array_count;
	std::uint64_t storage_transfer_size;
	std::uint32_t first_edge;
	std::uint32_t last_edge;
	std::uint32_t base_projection_count;
	std::uint32_t aggregate_helper;
	std::uint32_t value_constructor;
	std::uint32_t lifetime_object;
	std::uint32_t virtual_slot;
	std::uint32_t storage_transfer_alignment;
	bool constant;
	bool integer_narrowing_conversion;
	bool enum_arithmetic_conversion;
	bool template_layout_constant;
	bool boolean_conversion;
	bool user_conversion_call;
	bool explicit_user_conversion_call;
	bool allocation_may_return_null;
	bool array_action;
	bool array_cookie;
	bool array_count_constant;
	bool value_initialization;
	bool elide_empty_constructor;
	bool trivial_special_member_action;
	bool storage_unit_transfer;
	bool argument_materialization;
	bool discarded_materialization;
	bool reference_call_materialization;
	bool class_argument_staging;
	bool direct_return_slot;
	bool declaration_only;
	bool unwind_only;
	bool full_expression_staging;
	bool conditionally_constructed;
	bool control_dependent_temporary;
	bool virtual_call;

	explicit DumpNode(DumpKind value)
		: kind(value), type(kNoType), operand_type(kNoType),
		  category(VALUE_NONE), text(0), binding(kNoBinding),
		  object_binding(kNoBinding), selected_binding(kNoBinding),
		  constant_value(0), array_count(0), storage_transfer_size(0),
		  first_edge(kNoDumpEdge),
		  last_edge(kNoDumpEdge), base_projection_count(0),
		  aggregate_helper(kNoDumpEdge), value_constructor(kNoDumpEdge),
		  lifetime_object(kNoDumpEdge),
		  virtual_slot(kNoDumpEdge),
		  storage_transfer_alignment(0),
		  constant(false), integer_narrowing_conversion(false),
		  enum_arithmetic_conversion(false), template_layout_constant(false),
		  boolean_conversion(false), user_conversion_call(false),
		  explicit_user_conversion_call(false),
		  allocation_may_return_null(false),
		  array_action(false), array_cookie(false),
		  array_count_constant(false),
		  value_initialization(false), elide_empty_constructor(false),
		  trivial_special_member_action(false), storage_unit_transfer(false),
		  argument_materialization(false), discarded_materialization(false),
		  reference_call_materialization(false), class_argument_staging(false),
		  direct_return_slot(false), declaration_only(false),
		  unwind_only(false), full_expression_staging(false),
		  conditionally_constructed(false),
		  control_dependent_temporary(false), virtual_call(false) {}
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
		owner.template_layout_constant = owner.template_layout_constant ||
			nodes[child].template_layout_constant;
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
	bool inline_specifier;
	bool placeholder_auto;
	bool thread_local_storage;
	bool mutable_member;
	bool virtual_specifier;
	SpecInfo() : type(kNoType), storage_class(STORAGE_CLASS_NONE),
		is_typedef(false), is_constexpr(false), is_friend(false),
		inline_specifier(false),
		placeholder_auto(false),
		thread_local_storage(false), mutable_member(false),
		virtual_specifier(false) {}
};

struct ParameterInfo
{
	NameId name;
	NameId pack_name;
	TypeId declared_type;
	TypeId function_type;
	NodeId default_argument;
	ScopeId default_scope;
	ParameterInfo(NameId name_value, TypeId declared_value,
		TypeId function_value)
		: name(name_value), pack_name(0), declared_type(declared_value),
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
	CONVERSION_USER_DEFINED = 5,
	CONVERSION_ELLIPSIS = 6,
	CONVERSION_INVALID = 100
};

struct ObjectConversionFact
{
	ConversionRank rank;
	std::uint32_t base_projection_count;

	ObjectConversionFact()
		: rank(CONVERSION_INVALID), base_projection_count(0) {}
};

struct CallConversionFact
{
	ConversionRank rank;
	BindingId constructor;
	BindingId conversion_function;
	ConversionRank constructor_argument_rank;
	ConversionRank conversion_result_rank;
	ConversionRank conversion_object_rank;
	std::uint32_t conversion_base_projection_count;

	CallConversionFact()
		: rank(CONVERSION_INVALID), constructor(kNoBinding),
		  conversion_function(kNoBinding),
		  constructor_argument_rank(CONVERSION_INVALID),
		  conversion_result_rank(CONVERSION_INVALID),
		  conversion_object_rank(CONVERSION_INVALID),
		  conversion_base_projection_count(0) {}
};

enum SpecialMemberKind
{
	SPECIAL_MEMBER_NONE,
	SPECIAL_MEMBER_COPY_CONSTRUCTOR,
	SPECIAL_MEMBER_MOVE_CONSTRUCTOR,
	SPECIAL_MEMBER_COPY_ASSIGNMENT,
	SPECIAL_MEMBER_MOVE_ASSIGNMENT
};

struct FunctionInfo
{
	BindingId binding;
	BindingId inherited_constructor_source;
	BindingId complete_constructor, delegated_constructor;
	ScopeId owner;
	TypeId type, signature;
	TypeId conversion_target;
	NameId display_name;
	TypeId member_owner;
	EntityId friend_of;
	ScopeId lexical_scope;
	std::vector<ParameterInfo> parameters;
	NodeId definition_body, constructor_initializer;
	std::uint32_t template_pattern;
	bool defined;
	bool deferred;
	bool template_specialization;
	bool constructor;
	bool implicit_constructor;
	bool defaulted_constructor;
	bool deleted_constructor;
	bool explicit_constructor;
	bool conversion_function;
	bool explicit_conversion;
	bool destructor;
	bool implicit_destructor;
	bool defaulted_destructor;
	bool deleted_destructor;
	SpecialMemberKind special_member;
	bool implicit_special_member;
	bool defaulted_special_member;
	bool deleted_special_member;
	bool trivial_special_member;
	bool synthesized_storage_copy;
	bool synthesized_memberwise_copy;
	std::uint64_t synthesized_prefix_size;
	std::uint32_t synthesized_prefix_alignment;
	std::uint32_t synthesized_prefix_members;
	bool ordinary_visible;
	std::uint8_t demand_state;
	FunctionInfo()
		: binding(kNoBinding), inherited_constructor_source(kNoBinding),
		  complete_constructor(kNoBinding), delegated_constructor(kNoBinding),
		  owner(kNoScope), type(kNoType), signature(kNoType),
		  conversion_target(kNoType), member_owner(kNoType),
		  friend_of(kNoEntity), lexical_scope(kNoScope),
		  definition_body(kNoNode), constructor_initializer(kNoNode),
		  template_pattern(kNoDumpEdge),
		  defined(false), deferred(false), template_specialization(false),
		  constructor(false), implicit_constructor(false),
		  defaulted_constructor(false), deleted_constructor(false),
		  explicit_constructor(false), conversion_function(false),
		  explicit_conversion(false), destructor(false),
		  implicit_destructor(false), defaulted_destructor(false),
		  deleted_destructor(false), special_member(SPECIAL_MEMBER_NONE),
		  implicit_special_member(false), defaulted_special_member(false),
		  deleted_special_member(false), trivial_special_member(false),
		  synthesized_storage_copy(false), synthesized_memberwise_copy(false),
		  synthesized_prefix_size(0),
		  synthesized_prefix_alignment(0), synthesized_prefix_members(0),
		  ordinary_visible(true),
		  demand_state(0) {}
};

struct ClassSpecialMemberFacts
{
	BindingId copy_constructor, move_constructor;
	BindingId copy_assignment, move_assignment;
	bool user_copy_constructor, user_move_constructor;
	bool user_copy_assignment, user_move_assignment;

	ClassSpecialMemberFacts()
		: copy_constructor(kNoBinding), move_constructor(kNoBinding),
		  copy_assignment(kNoBinding), move_assignment(kNoBinding),
		  user_copy_constructor(false), user_move_constructor(false),
		  user_copy_assignment(false), user_move_assignment(false) {}
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

struct VirtualSlotFact
{
	BindingId root;
	BindingId function;

	VirtualSlotFact() : root(kNoBinding), function(kNoBinding) {}
	VirtualSlotFact(BindingId root_value, BindingId function_value)
		: root(root_value), function(function_value) {}
};

struct ClassPolymorphismFacts
{
	std::vector<VirtualSlotFact> slots;
	bool complete;
	bool vtable_demanded;

	ClassPolymorphismFacts() : complete(false), vtable_demanded(false) {}
};

struct TemplateParameter
{
	TemplateArgumentKind kind;
	NameId name;
	TypeId value_type;
	NodeId specifiers;
	NodeId declarator;
	NodeId default_argument;
	bool dependent_type;
	bool pack;

	TemplateParameter()
		: kind(TEMPLATE_ARGUMENT_TYPE), name(0), value_type(kNoType),
		  specifiers(kNoNode), declarator(kNoNode),
		  default_argument(kNoNode), dependent_type(false), pack(false) {}
};

inline bool HasTrailingTemplateParameterPack(
	const std::vector<TemplateParameter>& parameters)
{
	return !parameters.empty() && parameters.back().pack;
}

inline std::size_t FixedTemplateParameterCount(
	const std::vector<TemplateParameter>& parameters)
{
	return parameters.size() -
		(HasTrailingTemplateParameterPack(parameters) ? 1 : 0);
}

inline const TemplateParameter& TemplateParameterForArgument(
	const std::vector<TemplateParameter>& parameters, std::size_t argument)
{
	const std::size_t fixed = FixedTemplateParameterCount(parameters);
	return parameters[argument < fixed ? argument : parameters.size() - 1];
}

struct FunctionTemplatePattern
{
	ScopeId owner;
	ScopeId lexical_scope;
	NameId name;
	NodeId specifiers;
	NodeId declarator;
	NodeId definition_body;
	TypeId shape_type;
	std::size_t required_parameter_count;
	std::vector<TemplateParameter> parameters;
	std::vector<BindingId> specialization_bindings;
	std::vector<TemplateArgument> specialization_arguments;
	std::vector<std::uint32_t> specialization_argument_offsets;
	LanguageLinkage language_linkage;
	AccessKind member_access;
	bool defined;
	bool nonthrowing;
	bool function_parameter_pack;

	FunctionTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope), name(0),
		  specifiers(kNoNode),
		  declarator(kNoNode), definition_body(kNoNode),
		  shape_type(kNoType), required_parameter_count(0),
		  language_linkage(LANGUAGE_LINKAGE_CPP), member_access(ACCESS_PUBLIC),
		  defined(false),
		  nonthrowing(false), function_parameter_pack(false) {}
};

struct ClassTemplateMemberPattern
{
	ScopeId lexical_scope;
	NodeId declaration;
	std::vector<TemplateParameter> parameters;
	std::vector<std::uint32_t> owner_parameter_indices;
	std::vector<TemplateArgument> owner_fixed_arguments;
	std::vector<NameId> nested_owner_path;

	ClassTemplateMemberPattern()
		: lexical_scope(kNoScope), declaration(kNoNode) {}
};

struct VariableTemplatePattern
{
	ScopeId owner, lexical_scope;
	NameId name;
	NodeId declaration, specifiers, declarator, initializer;
	std::vector<NameId> type_parameters;
	std::vector<NodeId> default_arguments;
	bool partial_specialization;

	VariableTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope), name(0),
		  declaration(kNoNode), specifiers(kNoNode), declarator(kNoNode),
		  initializer(kNoNode), partial_specialization(false) {}
};

struct ClassTemplatePattern
{
	ScopeId owner;
	ScopeId lexical_scope;
	NameId name;
	NodeId declaration;
	std::vector<TemplateParameter> parameters;
	std::vector<BindingId> specialization_bindings;
	// Retained member patterns are published once and borrowed during
	// specialization replay. Keep their addresses stable if replay discovers
	// another retained definition.
	std::deque<ClassTemplateMemberPattern> member_definitions;
	EntityId marker_entity;
	bool defined;

	ClassTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope), name(0),
		  declaration(kNoNode), marker_entity(kNoEntity), defined(false) {}
};

struct InjectedMemberInfo
{
	BindingId storage, member;
	InjectedMemberInfo() : storage(kNoBinding), member(kNoBinding) {}
	InjectedMemberInfo(BindingId storage_value, BindingId member_value)
		: storage(storage_value), member(member_value) {}
};

struct LifetimeObligation
{
	BindingId object, destructor;
	TypeId type;
	std::uint32_t temporary;
	LifetimeObligation(BindingId object_value, BindingId destructor_value,
		TypeId type_value, std::uint32_t temporary_value = kNoDumpEdge)
		: object(object_value), destructor(destructor_value), type(type_value),
		  temporary(temporary_value) {}
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
	std::vector<BindingId> member_constructors;
	std::vector<std::uint8_t> trivial_member_constructors;

	AggregateHelperInfo(EntityId entity_value, TypeId object_type_value,
		TypeId function_type_value, const std::vector<BindingId>& members_value,
		const std::vector<BindingId>& member_constructors_value,
		const std::vector<std::uint8_t>& trivial_member_constructors_value)
		: entity(entity_value), object_type(object_type_value),
		  function_type(function_type_value), members(members_value),
		  member_constructors(member_constructors_value),
		  trivial_member_constructors(trivial_member_constructors_value) {}
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
	const std::vector<ClassPolymorphismFacts>& class_polymorphism;
	std::uint32_t root;

	SemanticGraphView(const Program& program_value,
		const DumpArena& arena_value,
		const std::vector<NamespaceObjectAction>& namespace_objects_value,
		const std::vector<AggregateHelperInfo>& aggregate_helpers_value,
		const std::vector<ClassPolymorphismFacts>& class_polymorphism_value,
		std::uint32_t root_value)
		: program(program_value), arena(arena_value),
		  namespace_objects(namespace_objects_value),
		  aggregate_helpers(aggregate_helpers_value),
		  class_polymorphism(class_polymorphism_value), root(root_value) {}
};

class SemanticGraphConsumer
{
public:
	virtual ~SemanticGraphConsumer() {}
	virtual void Consume(const SemanticGraphView& graph) = 0;
};

}
}
