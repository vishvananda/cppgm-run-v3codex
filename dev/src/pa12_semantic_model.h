#pragma once

#include "pa10_syntax_model.h"
#include "pa11_model.h"

#include <cstdint>
#include <deque>
#include <functional>
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

enum LogicalOperation : std::uint8_t
{
	LOGICAL_OPERATION_NONE,
	LOGICAL_OPERATION_AND,
	LOGICAL_OPERATION_OR
};

enum GnuAsmOperation : std::uint8_t
{
	GNU_ASM_NONE,
	GNU_ASM_NOP,
	GNU_ASM_PAUSE,
	GNU_ASM_BSWAP,
	GNU_ASM_LOCK_NOT,
	GNU_ASM_LOCK_INCREMENT,
	GNU_ASM_COMPILER_FENCE
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
	DUMP_STATEMENT_EXPRESSION,
	DUMP_STATEMENT_EXPRESSION_RESULT,
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
	DUMP_GNU_ASM_STATEMENT,
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
	DUMP_TYPEID_EXPRESSION,
	DUMP_DYNAMIC_CAST_EXPRESSION,
	DUMP_THROW_EXPRESSION,
	DUMP_TRY_STATEMENT,
	DUMP_HANDLER,
	DUMP_INITIALIZER_LIST,
	DUMP_INITIALIZER_LIST_BEGIN,
	DUMP_INITIALIZER_LIST_SIZE,
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
	DUMP_DESTRUCTOR_ACTION,
	DUMP_COMPLEX_CONSTRUCTION,
	DUMP_COMPLEX_COMPONENT
};

enum FunctionTryBodyKind : std::uint8_t
{
	FUNCTION_TRY_BODY_NONE,
	FUNCTION_TRY_BODY_ORDINARY,
	FUNCTION_TRY_BODY_CONSTRUCTOR,
	FUNCTION_TRY_BODY_DESTRUCTOR
};

const std::uint32_t kNoDumpEdge =
	std::numeric_limits<std::uint32_t>::max();

struct DumpNode
{
	DumpKind kind;
	TypeId type, operand_type;
	ValueCategory category;
	LogicalOperation logical_operation;
	GnuAsmOperation gnu_asm_operation;
	hosted_builtin::AtomicIntrinsicKind hosted_atomic_intrinsic;
	NameId text;
	BindingId binding, object_binding, selected_binding;
	std::int64_t constant_value;
	std::int64_t dynamic_cast_hint;
	std::uint64_t array_count;
	std::uint64_t storage_size;
	std::uint64_t direct_base_offset;
	std::uint64_t base_projection_offset;
	std::uint32_t first_edge;
	std::uint32_t last_edge;
	std::uint32_t base_projection_count;
	std::uint32_t aggregate_helper;
	std::uint32_t value_constructor;
	std::uint32_t lifetime_object;
	std::uint32_t lifetime_branch_owner;
	std::uint32_t lifetime_branch_child;
	std::uint32_t virtual_slot;
	std::uint32_t storage_alignment;
	bool constant;
	bool integer_literal_zero;
	bool null_member_pointer_constant;
	bool target_typed_scalar_immediate;
	bool integer_narrowing_conversion;
	bool enum_arithmetic_conversion;
	bool template_layout_constant;
	bool template_parameter_constant;
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
	bool range_for_materialization;
	bool initializer_list_backing;
	bool initializer_list_lifetime_observation;
	bool contains_temporary_object;
	bool temporary_implicit_object;
	bool pending_constructor_demand;
	bool pending_runtime_call_demand;
	bool runtime_call_demand_scanned;
	bool class_argument_staging;
	bool elided_temporary_storage;
	bool variadic_class_argument;
	bool direct_return_slot;
	bool declaration_only;
	bool unwind_only;
	bool exception_handler_exit;
	bool exception_cleanup_region_exit;
	bool full_expression_staging;
	bool managed_full_expression_cleanup;
	bool eager_full_expression_cleanup;
	bool enclosing_lifetime_cleanup;
	bool conditionally_constructed;
	bool lifetime_branch_statically_unreachable;
	bool default_argument;
	bool control_dependent_temporary;
	bool projected_subobject_temporary;
	bool virtual_call;
	bool member_pointer_conversion;
	bool has_base_projection_offset;
	bool inverse_base_projection;
	bool has_direct_base_offset;
	bool pseudo_destructor_call;
	bool complete_object_destruction;
	bool reverse_pointer_compound_assignment;
	bool dynamic_type_query;
	bool dynamic_cast_reference;
	FunctionTryBodyKind function_try_body;
	std::uint32_t exception_control_exit_count;

	explicit DumpNode(DumpKind value)
		: kind(value), type(kNoType), operand_type(kNoType),
		  category(VALUE_NONE), logical_operation(LOGICAL_OPERATION_NONE),
		  gnu_asm_operation(GNU_ASM_NONE),
		  hosted_atomic_intrinsic(hosted_builtin::ATOMIC_INTRINSIC_NONE),
		  text(0), binding(kNoBinding),
		  object_binding(kNoBinding), selected_binding(kNoBinding),
		  constant_value(0), dynamic_cast_hint(-1), array_count(0), storage_size(0),
		  direct_base_offset(0), base_projection_offset(0),
		  first_edge(kNoDumpEdge),
		  last_edge(kNoDumpEdge), base_projection_count(0),
		  aggregate_helper(kNoDumpEdge), value_constructor(kNoDumpEdge),
		  lifetime_object(kNoDumpEdge),
		  lifetime_branch_owner(kNoDumpEdge),
		  lifetime_branch_child(kNoDumpEdge),
		  virtual_slot(kNoDumpEdge),
		  storage_alignment(0),
		  constant(false), integer_literal_zero(false),
		  null_member_pointer_constant(false),
		  target_typed_scalar_immediate(false),
		  integer_narrowing_conversion(false),
		  enum_arithmetic_conversion(false), template_layout_constant(false),
		  template_parameter_constant(false),
		  boolean_conversion(false), user_conversion_call(false),
		  explicit_user_conversion_call(false),
		  allocation_may_return_null(false),
		  array_action(false), array_cookie(false),
		  array_count_constant(false),
		  value_initialization(false), elide_empty_constructor(false),
		  trivial_special_member_action(false), storage_unit_transfer(false),
		  argument_materialization(false), discarded_materialization(false),
		  reference_call_materialization(false), range_for_materialization(false),
		  initializer_list_backing(false),
		  initializer_list_lifetime_observation(false),
		  contains_temporary_object(value == DUMP_TEMPORARY_OBJECT),
		  temporary_implicit_object(false), pending_constructor_demand(false),
		  pending_runtime_call_demand(false),
		  runtime_call_demand_scanned(false),
		  class_argument_staging(false), elided_temporary_storage(false),
		  variadic_class_argument(false),
		  direct_return_slot(false), declaration_only(false),
		  unwind_only(false), exception_handler_exit(false),
		  exception_cleanup_region_exit(false), full_expression_staging(false),
		  managed_full_expression_cleanup(false),
		  eager_full_expression_cleanup(false),
		  enclosing_lifetime_cleanup(false),
		  conditionally_constructed(false),
		  lifetime_branch_statically_unreachable(false),
		  default_argument(false),
		  control_dependent_temporary(false),
		  projected_subobject_temporary(false), virtual_call(false),
		  member_pointer_conversion(false),
		  has_base_projection_offset(false),
		  inverse_base_projection(false),
		  has_direct_base_offset(false), pseudo_destructor_call(false),
		  complete_object_destruction(false),
		  reverse_pointer_compound_assignment(false),
		  dynamic_type_query(false), dynamic_cast_reference(false),
		  function_try_body(FUNCTION_TRY_BODY_NONE),
		  exception_control_exit_count(0) {}
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
		owner.template_parameter_constant = owner.template_parameter_constant ||
			nodes[child].template_parameter_constant;
		owner.contains_temporary_object = owner.contains_temporary_object ||
			nodes[child].contains_temporary_object;
		owner.eager_full_expression_cleanup =
			owner.eager_full_expression_cleanup ||
			nodes[child].eager_full_expression_cleanup;
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
	std::uint8_t placeholder_cv;
	bool thread_local_storage;
	bool mutable_member;
	bool virtual_specifier;
	SpecInfo() : type(kNoType), storage_class(STORAGE_CLASS_NONE),
		is_typedef(false), is_constexpr(false), is_friend(false),
		inline_specifier(false),
		placeholder_auto(false), placeholder_cv(CV_NONE),
		thread_local_storage(false), mutable_member(false),
		virtual_specifier(false) {}
};

enum PlaceholderDeclaratorKind
{
	PLACEHOLDER_DECLARATOR_NONE,
	PLACEHOLDER_DECLARATOR_VALUE,
	PLACEHOLDER_DECLARATOR_POINTER,
	PLACEHOLDER_DECLARATOR_LVALUE_REFERENCE,
	PLACEHOLDER_DECLARATOR_RVALUE_REFERENCE
};

enum PlaceholderBodyState
{
	PLACEHOLDER_BODY_NOT_STARTED,
	PLACEHOLDER_BODY_IN_PROGRESS,
	PLACEHOLDER_BODY_SUCCEEDED,
	PLACEHOLDER_BODY_FAILED
};

struct ParameterInfo
{
	NameId name;
	NameId pack_name;
	TypeId declared_type;
	TypeId function_type;
	NodeId type_syntax;
	NodeId default_argument;
	NodeId nondeduced_type_syntax;
	ScopeId default_scope;
	bool nondeduced;
	ParameterInfo(NameId name_value, TypeId declared_value,
		TypeId function_value)
		: name(name_value), pack_name(0), declared_type(declared_value),
		  function_type(function_value), type_syntax(kNoNode),
		  default_argument(kNoNode),
		  nondeduced_type_syntax(kNoNode), default_scope(kNoScope),
		  nondeduced(false) {}
};

struct DeclaratorInfo
{
	NameId name;
	TypeId type;
	ScopeId trailing_return_scope;
	PlaceholderDeclaratorKind placeholder_return_kind;
	std::uint8_t placeholder_return_cv;
	std::vector<ParameterInfo> parameters;
	DeclaratorInfo()
		: name(0), type(kNoType), trailing_return_scope(kNoScope),
		  placeholder_return_kind(PLACEHOLDER_DECLARATOR_NONE),
		  placeholder_return_cv(CV_NONE) {}
};

enum ConstexprScalarKind
{
	CONSTEXPR_SCALAR_INTEGRAL,
	CONSTEXPR_SCALAR_FLOATING,
	CONSTEXPR_SCALAR_MEMBER_POINTER
};

struct ConstexprScalarValue
{
	ConstexprScalarKind kind;
	std::int64_t integral;
	long double floating;
	BindingId member_pointer;

	ConstexprScalarValue()
		: kind(CONSTEXPR_SCALAR_INTEGRAL), integral(0), floating(0.0L),
		  member_pointer(kNoBinding) {}
	explicit ConstexprScalarValue(std::int64_t value)
		: kind(CONSTEXPR_SCALAR_INTEGRAL), integral(value), floating(0.0L),
		  member_pointer(kNoBinding) {}
	explicit ConstexprScalarValue(long double value)
		: kind(CONSTEXPR_SCALAR_FLOATING), integral(0), floating(value),
		  member_pointer(kNoBinding) {}
	ConstexprScalarValue(BindingId member, std::int64_t value)
		: kind(CONSTEXPR_SCALAR_MEMBER_POINTER), integral(value), floating(0.0L),
		  member_pointer(member) {}

	bool operator==(const ConstexprScalarValue& other) const
	{
		return kind == other.kind && (kind == CONSTEXPR_SCALAR_FLOATING ?
			floating == other.floating : integral == other.integral &&
			(kind != CONSTEXPR_SCALAR_MEMBER_POINTER ||
			 member_pointer == other.member_pointer));
	}
};

const std::uint32_t kNoConstexprObject =
	std::numeric_limits<std::uint32_t>::max();
const std::uint32_t kNoConstexprAddress =
	std::numeric_limits<std::uint32_t>::max();
const std::size_t kNoConstexprLocal =
	std::numeric_limits<std::size_t>::max();

enum ConstexprAddressKind
{
	CONSTEXPR_ADDRESS_NULL,
	CONSTEXPR_ADDRESS_BINDING,
	CONSTEXPR_ADDRESS_STRING,
	CONSTEXPR_ADDRESS_LOCAL,
	CONSTEXPR_ADDRESS_FUNCTION
};

struct ConstexprAddressValue
{
	ConstexprAddressKind kind;
	std::uint64_t identity;
	std::int64_t offset, lower_bound, upper_bound;

	ConstexprAddressValue()
		: kind(CONSTEXPR_ADDRESS_NULL), identity(0), offset(0),
		  lower_bound(0), upper_bound(0) {}
	ConstexprAddressValue(ConstexprAddressKind kind_value,
		std::uint64_t identity_value, std::int64_t offset_value,
		std::int64_t lower, std::int64_t upper)
		: kind(kind_value), identity(identity_value), offset(offset_value),
		  lower_bound(lower), upper_bound(upper) {}

	bool operator==(const ConstexprAddressValue& other) const
	{
		return kind == other.kind && identity == other.identity &&
			offset == other.offset && lower_bound == other.lower_bound &&
			upper_bound == other.upper_bound;
	}
};

struct ConstexprAddressValueHash
{
	std::size_t operator()(const ConstexprAddressValue& value) const
	{
		std::size_t hash = std::hash<int>()(static_cast<int>(value.kind));
		hash ^= std::hash<std::uint64_t>()(value.identity) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::int64_t>()(value.offset) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::int64_t>()(value.lower_bound) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::int64_t>()(value.upper_bound) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		return hash;
	}
};

struct ConstexprObjectElement
{
	BindingId member;
	ConstexprScalarValue scalar;
	std::uint32_t object, address;
	bool object_value, address_value;

	ConstexprObjectElement(BindingId member_value,
		const ConstexprScalarValue& scalar_value)
		: member(member_value), scalar(scalar_value),
		  object(kNoConstexprObject), address(kNoConstexprAddress),
		  object_value(false), address_value(false) {}
	ConstexprObjectElement(BindingId member_value, std::uint32_t object_id)
		: member(member_value), scalar(), object(object_id),
		  address(kNoConstexprAddress), object_value(true),
		  address_value(false) {}
	ConstexprObjectElement(BindingId member_value, std::uint32_t address_id,
		bool)
		: member(member_value), scalar(), object(kNoConstexprObject),
		  address(address_id), object_value(false), address_value(true) {}

	bool operator==(const ConstexprObjectElement& other) const
	{
		return member == other.member && object_value == other.object_value &&
			address_value == other.address_value &&
			(object_value ? object == other.object :
			 address_value ? address == other.address : scalar == other.scalar);
	}
};

struct ConstexprObjectValue
{
	TypeId type;
	std::uint32_t first_element, element_count;
	std::size_t hash;
	std::uint64_t newest_local_storage_identity;

	ConstexprObjectValue(TypeId type_value, std::uint32_t first,
		std::uint32_t count, std::size_t hash_value,
		std::uint64_t newest_local_storage)
		: type(type_value), first_element(first), element_count(count),
		  hash(hash_value),
		  newest_local_storage_identity(newest_local_storage) {}
};

struct ExpressionInfo
{
	std::uint32_t node;
	TypeId type, converted_scalar_target;
	ValueCategory category;
	BindingId binding;
	std::size_t constexpr_local;
	bool constant;
	std::int64_t value;
	bool floating_constant;
	long double floating_value;
	std::uint32_t constexpr_object, constexpr_complete_object;
	std::uint32_t constexpr_address, constexpr_lvalue_address;
	bool integer_literal_zero;
	bool indirect_constant_designator;
	std::uint32_t string_unit_begin;
	std::uint32_t string_unit_count;

	ExpressionInfo()
		: node(kNoDumpEdge), type(kNoType),
		  converted_scalar_target(kNoType), category(VALUE_PRVALUE),
		  binding(kNoBinding),
		  constexpr_local(std::numeric_limits<std::size_t>::max()),
		  constant(false), value(0), floating_constant(false),
		  floating_value(0.0L), constexpr_object(kNoConstexprObject),
		  constexpr_complete_object(kNoConstexprObject),
		  constexpr_address(kNoConstexprAddress),
		  constexpr_lvalue_address(kNoConstexprAddress),
		  integer_literal_zero(false), indirect_constant_designator(false),
		  string_unit_begin(kNoDumpEdge),
		  string_unit_count(0) {}
};

struct ConstexprLocalValue
{
	NameId name, pack_name;
	TypeId type;
	ConstexprScalarValue value;
	std::uint32_t object, complete_object, address;
	std::uint64_t storage_identity;
	std::size_t previous_same_name, previous_same_pack;

	ConstexprLocalValue(NameId name_value, NameId pack_name_value,
		TypeId type_value, const ConstexprScalarValue& value_value)
		: name(name_value), pack_name(pack_name_value), type(type_value),
		  value(value_value), object(kNoConstexprObject),
		  complete_object(kNoConstexprObject),
		  address(kNoConstexprAddress), storage_identity(0),
		  previous_same_name(kNoConstexprLocal),
		  previous_same_pack(kNoConstexprLocal) {}
	ConstexprLocalValue(NameId name_value, NameId pack_name_value,
		TypeId type_value, std::uint32_t object_value)
		: name(name_value), pack_name(pack_name_value), type(type_value),
		  value(), object(object_value), complete_object(object_value),
		  address(kNoConstexprAddress),
		  storage_identity(0), previous_same_name(kNoConstexprLocal),
		  previous_same_pack(kNoConstexprLocal) {}
};

struct ConstexprFrame
{
	BindingId function;
	std::size_t first_local, first_scope_fact, first_block;
	std::uint64_t first_storage_identity;
	std::uint32_t receiver_object, receiver_complete_object;
	std::uint32_t receiver_address;
	ConstexprFrame(BindingId function_value, std::size_t local,
		std::size_t scope_fact, std::size_t block, std::uint64_t storage_identity,
		std::uint32_t receiver = kNoConstexprObject,
		std::uint32_t complete_receiver = kNoConstexprObject,
		std::uint32_t address = kNoConstexprAddress)
		: function(function_value), first_local(local),
		  first_scope_fact(scope_fact), first_block(block),
		  first_storage_identity(storage_identity),
		  receiver_object(receiver),
		  receiver_complete_object(complete_receiver),
		  receiver_address(address) {}
};

struct ConstexprScopeFact
{
	NameId name;
	TypeId type;
	ScopeId name_space;
	std::size_t previous_same_name;
	ConstexprScopeFact(NameId name_value, TypeId type_value,
		ScopeId namespace_value)
		: name(name_value), type(type_value), name_space(namespace_value),
		  previous_same_name(kNoConstexprLocal) {}
};

struct ConstexprBlockOffset
{
	std::size_t first_local, first_scope_fact;
	ConstexprBlockOffset(std::size_t local, std::size_t scope_fact)
		: first_local(local), first_scope_fact(scope_fact) {}
};

enum ConstexprFlow
{
	CONSTEXPR_FLOW_NORMAL,
	CONSTEXPR_FLOW_RETURN,
	CONSTEXPR_FLOW_BREAK,
	CONSTEXPR_FLOW_CONTINUE,
	CONSTEXPR_FLOW_INVALID
};

enum ConstexprCallArgumentKind
{
	CONSTEXPR_CALL_ARGUMENT_SCALAR = 1,
	CONSTEXPR_CALL_ARGUMENT_OBJECT = 2,
	CONSTEXPR_CALL_ARGUMENT_ADDRESS = 4
};

struct ConstexprCallArgument
{
	TypeId type;
	std::uint8_t kind;
	ConstexprScalarValue scalar;
	std::uint32_t object, complete_object, address;

	ConstexprCallArgument()
		: type(kNoType), kind(0), scalar(), object(kNoConstexprObject),
		  complete_object(kNoConstexprObject),
		  address(kNoConstexprAddress) {}

	bool operator==(const ConstexprCallArgument& other) const
	{
		return type == other.type && kind == other.kind &&
			(!(kind & CONSTEXPR_CALL_ARGUMENT_SCALAR) || scalar == other.scalar) &&
			(!(kind & CONSTEXPR_CALL_ARGUMENT_OBJECT) || object == other.object) &&
			(!(kind & CONSTEXPR_CALL_ARGUMENT_OBJECT) ||
			 complete_object == other.complete_object) &&
			(!(kind & CONSTEXPR_CALL_ARGUMENT_ADDRESS) || address == other.address);
	}
};

struct ConstexprCallKey
{
	BindingId function;
	std::uint32_t receiver_object, receiver_complete_object, receiver_address;
	std::vector<ConstexprCallArgument> arguments;

	ConstexprCallKey()
		: function(kNoBinding), receiver_object(kNoConstexprObject),
		  receiver_complete_object(kNoConstexprObject),
		  receiver_address(kNoConstexprAddress) {}

	bool operator==(const ConstexprCallKey& other) const
	{
		return function == other.function &&
			receiver_object == other.receiver_object &&
			receiver_complete_object == other.receiver_complete_object &&
			receiver_address == other.receiver_address &&
			arguments == other.arguments;
	}
};

struct ConstexprCallKeyHash
{
	std::size_t operator()(const ConstexprCallKey& key) const
	{
		std::size_t hash = static_cast<std::size_t>(key.function) + 1;
		hash ^= std::hash<std::uint32_t>()(key.receiver_object) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::uint32_t>()(key.receiver_complete_object) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		hash ^= std::hash<std::uint32_t>()(key.receiver_address) +
			0x9e3779b9u + (hash << 6) + (hash >> 2);
		for (std::size_t i = 0; i < key.arguments.size(); ++i)
		{
			const ConstexprCallArgument& argument = key.arguments[i];
			hash ^= static_cast<std::size_t>(argument.type) +
				0x9e3779b9u + (hash << 6) + (hash >> 2);
			hash ^= std::hash<std::uint8_t>()(argument.kind) +
				0x9e3779b9u + (hash << 6) + (hash >> 2);
			if (argument.kind & CONSTEXPR_CALL_ARGUMENT_SCALAR)
			{
				hash ^= std::hash<int>()(
					static_cast<int>(argument.scalar.kind)) +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
				const std::size_t value_hash =
					argument.scalar.kind == CONSTEXPR_SCALAR_FLOATING ?
					std::hash<long double>()(argument.scalar.floating) :
					std::hash<std::int64_t>()(argument.scalar.integral);
				hash ^= value_hash +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
				if (argument.scalar.kind ==
					CONSTEXPR_SCALAR_MEMBER_POINTER)
					hash ^= std::hash<BindingId>()(
						argument.scalar.member_pointer) +
						0x9e3779b9u + (hash << 6) + (hash >> 2);
			}
			if (argument.kind & CONSTEXPR_CALL_ARGUMENT_OBJECT)
			{
				hash ^= std::hash<std::uint32_t>()(argument.object) +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
				hash ^= std::hash<std::uint32_t>()(argument.complete_object) +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
			}
			if (argument.kind & CONSTEXPR_CALL_ARGUMENT_ADDRESS)
				hash ^= std::hash<std::uint32_t>()(argument.address) +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

struct ConstexprCallFact
{
	// 1=in progress, 2=success, 3=expected failure.
	std::uint8_t state;
	ConstexprScalarValue value;
	std::uint32_t address, object, complete_object;
	bool has_scalar;
	ConstexprCallFact()
		: state(1), value(), address(kNoConstexprAddress),
		  object(kNoConstexprObject), complete_object(kNoConstexprObject),
		  has_scalar(false) {}
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
	std::uint64_t base_projection_offset;

	ObjectConversionFact()
		: rank(CONVERSION_INVALID), base_projection_count(0),
		  base_projection_offset(0) {}
};

struct CallConversionFact
{
	ConversionRank rank;
	BindingId constructor;
	BindingId conversion_function;
	// A converting constructor normally owns a standard first-argument
	// conversion.  The captureless-lambda conversion is the one PA25 path
	// where that selected argument conversion is itself represented by a
	// callable semantic fact; retain it instead of repeating class lookup
	// while constructing the argument.
	BindingId constructor_argument_conversion_function;
	ConversionRank constructor_argument_rank;
	ConversionRank constructor_argument_conversion_result_rank;
	ConversionRank constructor_argument_conversion_object_rank;
	ConversionRank conversion_result_rank;
	ConversionRank conversion_object_rank;
	std::uint32_t constructor_argument_conversion_base_projection_count;
	std::uint32_t conversion_base_projection_count;
	ConversionRank initializer_list_element_rank;
	bool initializer_list_conversion;

	CallConversionFact()
		: rank(CONVERSION_INVALID), constructor(kNoBinding),
		  conversion_function(kNoBinding),
		  constructor_argument_conversion_function(kNoBinding),
		  constructor_argument_rank(CONVERSION_INVALID),
		  constructor_argument_conversion_result_rank(CONVERSION_INVALID),
		  constructor_argument_conversion_object_rank(CONVERSION_INVALID),
		  conversion_result_rank(CONVERSION_INVALID),
		  conversion_object_rank(CONVERSION_INVALID),
		  constructor_argument_conversion_base_projection_count(0),
		  conversion_base_projection_count(0),
		  initializer_list_element_rank(CONVERSION_INVALID),
		  initializer_list_conversion(false) {}
};

enum SpecialMemberKind
{
	SPECIAL_MEMBER_NONE,
	SPECIAL_MEMBER_COPY_CONSTRUCTOR,
	SPECIAL_MEMBER_MOVE_CONSTRUCTOR,
	SPECIAL_MEMBER_COPY_ASSIGNMENT,
	SPECIAL_MEMBER_MOVE_ASSIGNMENT
};

enum ExceptionSpecificationState
{
	EXCEPTION_SPECIFICATION_FIXED,
	EXCEPTION_SPECIFICATION_DEFERRED,
	EXCEPTION_SPECIFICATION_IN_PROGRESS,
	EXCEPTION_SPECIFICATION_SUCCEEDED,
	EXCEPTION_SPECIFICATION_FAILED
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
	NameId parameter_pack_name;
	TypeId member_owner;
	EntityId friend_of;
	// The implicit conversion of an eligible captureless closure owns the
	// corresponding static invocation function.  Calls and lowering consume
	// this binding directly instead of reconstructing it from a lambda name.
	BindingId lambda_invocation_function;
	// A lambda inherits access privileges through this lexical edge without
	// inheriting the enclosing function's implicit object.
	BindingId lexical_access_function;
	// Captures are canonical closure-owned members.  A call operator borrows a
	// contiguous range and, when present, the member storing the enclosing
	// object pointer.
	std::uint32_t lambda_capture_begin, lambda_capture_count;
	BindingId lambda_this_capture_member;
	// A dependent exception specification is a specialization-owned demand
	// fact, separate from declaration formation and body/emission demand.
	ScopeId lexical_scope, exception_specification_scope;
	std::vector<ParameterInfo> parameters;
	NodeId definition_body, constructor_initializer, function_try_block;
	std::uint32_t retained_definition_semantics;
	std::uint32_t template_pattern;
	TypeId placeholder_return_type;
	PlaceholderDeclaratorKind placeholder_return_kind;
	std::uint8_t placeholder_return_cv;
	bool placeholder_return_deduced;
	PlaceholderBodyState placeholder_body_state;
	bool defined;
	bool deferred;
	bool definition_in_class;
	bool template_specialization;
	bool explicit_specialization;
	bool deleted_function;
	bool constructor;
	bool implicit_constructor;
	bool defaulted_constructor;
	bool deleted_constructor;
	bool explicit_constructor;
	bool conversion_function;
	bool constexpr_function;
	bool explicit_conversion;
	bool destructor;
	bool implicit_destructor;
	bool defaulted_destructor;
	bool deleted_destructor;
	SpecialMemberKind special_member;
	bool implicit_special_member;
	bool defaulted_special_member;
	bool user_provided_special_member;
	bool deleted_special_member;
	bool trivial_special_member;
	bool synthesized_storage_copy;
	bool synthesized_memberwise_copy;
	std::uint64_t synthesized_prefix_size;
	std::uint32_t synthesized_prefix_alignment;
	std::uint32_t synthesized_prefix_members;
	bool ordinary_visible;
	bool exception_specification_configured;
	ExceptionSpecificationState exception_specification_state;
	std::uint8_t demand_state;
	FunctionInfo()
		: binding(kNoBinding), inherited_constructor_source(kNoBinding),
		  complete_constructor(kNoBinding), delegated_constructor(kNoBinding),
		  owner(kNoScope), type(kNoType), signature(kNoType),
		  conversion_target(kNoType), display_name(0), parameter_pack_name(0),
		  member_owner(kNoType),
		  friend_of(kNoEntity), lambda_invocation_function(kNoBinding),
		  lexical_access_function(kNoBinding),
		  lambda_capture_begin(0), lambda_capture_count(0),
		  lambda_this_capture_member(kNoBinding),
		  lexical_scope(kNoScope),
		  exception_specification_scope(kNoScope),
		  definition_body(kNoNode), constructor_initializer(kNoNode),
		  function_try_block(kNoNode),
		  retained_definition_semantics(kNoDumpEdge),
		  template_pattern(kNoDumpEdge), placeholder_return_type(kNoType),
		  placeholder_return_kind(PLACEHOLDER_DECLARATOR_NONE),
		  placeholder_return_cv(CV_NONE), placeholder_return_deduced(false),
		  placeholder_body_state(PLACEHOLDER_BODY_NOT_STARTED),
		  defined(false), deferred(false), definition_in_class(false),
		  template_specialization(false), explicit_specialization(false),
		  deleted_function(false),
		  constructor(false), implicit_constructor(false),
		  defaulted_constructor(false), deleted_constructor(false),
		  explicit_constructor(false), conversion_function(false),
		  constexpr_function(false), explicit_conversion(false), destructor(false),
		  implicit_destructor(false), defaulted_destructor(false),
		  deleted_destructor(false), special_member(SPECIAL_MEMBER_NONE),
		  implicit_special_member(false), defaulted_special_member(false),
		  user_provided_special_member(false),
		  deleted_special_member(false), trivial_special_member(false),
		  synthesized_storage_copy(false), synthesized_memberwise_copy(false),
		  synthesized_prefix_size(0),
		  synthesized_prefix_alignment(0), synthesized_prefix_members(0),
		  ordinary_visible(true), exception_specification_configured(false),
		  exception_specification_state(EXCEPTION_SPECIFICATION_FIXED),
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
	std::int64_t this_adjustment;
	std::int64_t return_adjustment;
	std::int64_t return_vtable_offset;
	bool return_adjustment_virtual;

	VirtualSlotFact()
		: root(kNoBinding), function(kNoBinding), this_adjustment(0),
		  return_adjustment(0), return_vtable_offset(0),
		  return_adjustment_virtual(false) {}
	VirtualSlotFact(BindingId root_value, BindingId function_value)
		: root(root_value), function(function_value), this_adjustment(0),
		  return_adjustment(0), return_vtable_offset(0),
		  return_adjustment_virtual(false) {}
};

struct PolymorphicViewFact
{
	EntityId entity;
	std::uint32_t direct_base_ordinal;
	std::uint32_t virtual_base_ordinal;
	std::uint64_t relative_offset, offset;
	std::uint64_t address_point;
	bool stores_vptr;
	bool virtual_base;
	bool contributes_primary_override;
	std::vector<std::int64_t> virtual_base_offsets;
	std::vector<std::int64_t> virtual_call_offsets;
	std::vector<VirtualSlotFact> slots;

	PolymorphicViewFact(EntityId entity_value = kNoEntity,
		std::uint32_t direct_base_ordinal_value = 0,
		std::uint64_t relative_offset_value = 0, bool stores_vptr_value = true)
		: entity(entity_value),
		  direct_base_ordinal(direct_base_ordinal_value),
		  virtual_base_ordinal(kNoDumpEdge),
		  relative_offset(relative_offset_value), offset(0), address_point(16),
		  stores_vptr(stores_vptr_value), virtual_base(false),
		  contributes_primary_override(false) {}
};

struct ClassPolymorphismFacts
{
	std::vector<VirtualSlotFact> slots;
	std::vector<EntityId> primary_ancestors;
	std::vector<PolymorphicViewFact> views;
	std::vector<std::int64_t> virtual_base_offsets;
	std::vector<std::int64_t> virtual_call_offsets;
	std::uint64_t address_point;
	bool complete;
	bool vtable_demanded;

	ClassPolymorphismFacts()
		: address_point(16), complete(false), vtable_demanded(false) {}
};

struct TemplateParameter
{
	TemplateArgumentKind kind;
	NameId name;
	TypeId value_type;
	NodeId specifiers;
	NodeId declarator;
	NodeId default_argument;
	std::vector<TemplateParameter> template_parameters;
	bool dependent_type;
	bool pack;

	TemplateParameter()
		: kind(TEMPLATE_ARGUMENT_TYPE), name(0), value_type(kNoType),
		  specifiers(kNoNode), declarator(kNoNode),
		  default_argument(kNoNode), dependent_type(false), pack(false) {}
};

struct FunctionTemplateDefaultContext
{
	ScopeId lexical_scope;
	std::vector<TemplateParameter> parameters;

	FunctionTemplateDefaultContext() : lexical_scope(kNoScope) {}
};

struct FunctionTemplateResultLookupFact
{
	NodeId syntax;
	TypeId type;
	BindingId declaration;
	ScopeId name_space;
	EntityId naming_class;

	FunctionTemplateResultLookupFact(NodeId syntax_value,
		const LookupResult& result)
		: syntax(syntax_value), type(result.type),
		  declaration(result.type_declaration),
		  name_space(result.name_space), naming_class(result.naming_class) {}
};

typedef std::uint32_t FunctionTemplateResultIdentityId;
const FunctionTemplateResultIdentityId kNoFunctionTemplateResultIdentity =
	std::numeric_limits<FunctionTemplateResultIdentityId>::max();

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
	NodeId trailing_return_syntax;
	NodeId definition_body;
	NodeId constructor_initializer;
	TypeId shape_type;
	std::size_t required_parameter_count;
	std::vector<TemplateParameter> parameters;
	std::vector<std::uint32_t> default_context_by_parameter;
	std::vector<FunctionTemplateDefaultContext> default_contexts;
	std::vector<NameId> function_parameter_names;
	std::vector<NodeId> function_parameter_defaults;
	std::vector<NodeId> function_parameter_nondeduced_syntax;
	std::vector<std::uint8_t> function_parameter_nondeduced;
	std::vector<FunctionTemplateResultLookupFact> result_lookup_facts;
	NodeId result_root_structure;
	NameId result_root_name;
	BindingId result_root_declaration;
	ScopeId result_root_namespace;
	FunctionTemplateResultIdentityId expanded_result_identity;
	FunctionTemplateAbiTypeId abi_result_type;
	std::vector<FunctionTemplateAbiTypeId> abi_template_parameter_types;
	std::vector<FunctionTemplateAbiTypeId> abi_function_parameter_types;
	FunctionTemplateAbiRecipeId abi_recipe;
	bool result_root_global;
	bool expanded_result_has_alias;
	std::vector<BindingId> specialization_bindings;
	std::vector<TemplateArgument> specialization_arguments;
	std::vector<std::uint32_t> specialization_argument_offsets;
	std::vector<std::uint32_t> specialization_parameter_offsets;
	std::vector<EntityId> friend_owners;
	BindingId lambda_lexical_access_function;
	std::uint32_t lambda_capture_begin, lambda_capture_count;
	BindingId lambda_this_capture_member;
	LanguageLinkage language_linkage;
	AccessKind member_access;
	bool defined;
	bool ordinary_visible;
	bool definition_in_class;
	bool nonthrowing;
	bool dependent_exception_specification;
	bool function_parameter_pack;
	bool static_member;
	bool constructor_template;
	bool conversion_template;
	bool constexpr_specifier;
	bool explicit_specifier;
	bool inline_specifier;
	bool explicit_member_definition;
	bool deferred_result_formation;
	bool result_type_dependent;
	bool deleted_function;

	FunctionTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope), name(0),
		  specifiers(kNoNode),
		  declarator(kNoNode), trailing_return_syntax(kNoNode),
		  definition_body(kNoNode),
		  constructor_initializer(kNoNode),
		  shape_type(kNoType), required_parameter_count(0),
		  result_root_structure(kNoNode), result_root_name(0),
		  result_root_declaration(kNoBinding),
		  result_root_namespace(kNoScope),
		  expanded_result_identity(kNoFunctionTemplateResultIdentity),
		  abi_result_type(kNoFunctionTemplateAbiType),
		  abi_recipe(kNoFunctionTemplateAbiRecipe),
		  result_root_global(false), expanded_result_has_alias(false),
		  lambda_lexical_access_function(kNoBinding),
		  lambda_capture_begin(0), lambda_capture_count(0),
		  lambda_this_capture_member(kNoBinding),
		  language_linkage(LANGUAGE_LINKAGE_CPP), member_access(ACCESS_PUBLIC),
		  defined(false), ordinary_visible(true), definition_in_class(false),
		  nonthrowing(false), dependent_exception_specification(false),
		  function_parameter_pack(false), static_member(false),
		  constructor_template(false), conversion_template(false),
		  constexpr_specifier(false), explicit_specifier(false),
		  inline_specifier(false),
		  explicit_member_definition(false), deferred_result_formation(false),
		  result_type_dependent(false),
		  deleted_function(false) {}
};

struct FunctionTemplateDeduction
{
	std::vector<TemplateArgument> fixed_arguments;
	std::vector<std::vector<TemplateArgument> > pack_arguments;
	std::vector<std::size_t> pack_deduction_positions;
	std::vector<std::uint8_t> pack_deduction_started;

	FunctionTemplateDeduction() {}
	explicit FunctionTemplateDeduction(
		const std::vector<TemplateParameter>& parameters)
		: fixed_arguments(parameters.size()), pack_arguments(parameters.size()),
		  pack_deduction_positions(parameters.size(), 0),
		  pack_deduction_started(parameters.size(), 0)
	{
		for (std::size_t i = 0; i < parameters.size(); ++i)
			fixed_arguments[i].kind = parameters[i].kind;
	}
};

struct ClassTemplateMemberPattern
{
	ScopeId lexical_scope;
	NodeId declaration;
	BindingId concrete_owner;
	std::uint32_t owner_partial_pattern;
	std::vector<TemplateParameter> parameters;
	std::vector<TemplateArgument> canonical_owner_arguments;
	std::vector<NameId> nested_owner_path;
	std::vector<NodeId> nested_owner_argument_lists;
	// Captured when the retained definition is first classified.  Value-use
	// demand must not reopen retained syntax to distinguish const from constexpr.
	bool value_use_requires_storage;

	ClassTemplateMemberPattern()
		: lexical_scope(kNoScope), declaration(kNoNode),
		  concrete_owner(kNoBinding),
		  owner_partial_pattern(kNoDumpEdge),
		  value_use_requires_storage(false) {}
};

struct ClassTemplatePartialPattern
{
	ScopeId lexical_scope;
	NodeId declaration;
	std::vector<TemplateParameter> parameters;
	std::vector<NodeId> arguments;
	std::vector<TemplateArgument> canonical_arguments;
	std::uint8_t canonical_argument_state;
	std::uint32_t revision;
	bool concrete_replay_required;

	ClassTemplatePartialPattern()
		: lexical_scope(kNoScope), declaration(kNoNode),
		  canonical_argument_state(0), revision(1),
		  concrete_replay_required(false) {}
};

struct ClassTemplatePartialSelection
{
	std::uint32_t pattern;
	std::uint32_t revision;
	FunctionTemplateDeduction bindings;

	ClassTemplatePartialSelection()
		: pattern(kNoDumpEdge), revision(0) {}
};

enum HostedTraitTemplateKind
{
	HOSTED_TRAIT_TEMPLATE_NONE,
	HOSTED_TRAIT_TEMPLATE_NOTHROW_DEFAULT_CONSTRUCTIBLE,
	HOSTED_TRAIT_TEMPLATE_NOTHROW_COPY_CONSTRUCTIBLE,
	HOSTED_TRAIT_TEMPLATE_NOTHROW_MOVE_CONSTRUCTIBLE,
	HOSTED_TRAIT_TEMPLATE_NOTHROW_INVOCABLE
};

struct VariableTemplatePattern
{
	ScopeId owner, lexical_scope;
	NameId name;
	NodeId declaration, specifiers, declarator, initializer;
	std::vector<TemplateParameter> parameters;
	std::vector<NodeId> specialization_arguments;
	std::vector<TemplateArgument> canonical_specialization_arguments;
	std::uint8_t canonical_argument_state;
	bool partial_specialization;

	VariableTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope), name(0),
		  declaration(kNoNode), specifiers(kNoNode), declarator(kNoNode),
		  initializer(kNoNode), canonical_argument_state(0),
		  partial_specialization(false) {}
};

struct AliasTemplatePattern
{
	ScopeId owner, lexical_scope, specialization_scope;
	NameId name;
	NodeId declaration, type_id;
	std::vector<TemplateParameter> parameters;
	EntityId marker_entity;

	AliasTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope),
		  specialization_scope(kNoScope), name(0), declaration(kNoNode),
		  type_id(kNoNode), marker_entity(kNoEntity) {}
};

struct LambdaCaptureFact
{
	NameId name, pack_name;
	BindingId source, member;
	TypeId value_type;
	bool captures_this, by_reference;

	LambdaCaptureFact(NameId name_value, NameId pack_name_value,
		BindingId source_value, BindingId member_value, TypeId value_type_value,
		bool captures_this_value, bool by_reference_value)
		: name(name_value), pack_name(pack_name_value), source(source_value),
		  member(member_value), value_type(value_type_value),
		  captures_this(captures_this_value),
		  by_reference(by_reference_value) {}
};

// A closure expression is canonical only within its concrete enclosing
// function. Retained syntax can be replayed for more than one function-template
// specialization, so syntax identity alone is not a complete semantic key.
struct LambdaClosureFact
{
	NodeId syntax;
	BindingId function, call_operator, invocation_function,
		conversion_function;
	ScopeId namespace_owner;
	EntityId entity;
	std::uint32_t ordinal, capture_begin, capture_count;

	LambdaClosureFact(NodeId syntax_value, BindingId function_value,
		ScopeId namespace_owner_value, EntityId entity_value,
		BindingId call_operator_value, BindingId invocation_function_value,
		BindingId conversion_function_value,
		std::uint32_t ordinal_value, std::uint32_t capture_begin_value,
		std::uint32_t capture_count_value)
		: syntax(syntax_value), function(function_value),
		  call_operator(call_operator_value),
		  invocation_function(invocation_function_value),
		  conversion_function(conversion_function_value),
		  namespace_owner(namespace_owner_value), entity(entity_value),
		  ordinal(ordinal_value), capture_begin(capture_begin_value),
		  capture_count(capture_count_value) {}
};

struct ClassTemplatePattern
{
	ScopeId owner;
	ScopeId lexical_scope;
	NameId name;
	NodeId declaration;
	std::vector<TemplateParameter> parameters;
	std::vector<BindingId> specialization_bindings;
	std::vector<EntityId> friend_owners;
	// Retained member patterns are published once and borrowed during
	// specialization replay. Keep their addresses stable if replay discovers
	// another retained definition.
	std::deque<ClassTemplateMemberPattern> member_definitions;
	std::deque<ClassTemplateMemberPattern> demanded_member_definitions;
	std::deque<ClassTemplatePartialPattern> partial_specializations;
	EntityId marker_entity;
	std::uint32_t template_parameter_ordinal;
	HostedTraitTemplateKind hosted_trait_template;
	bool defined;
	bool template_parameter_proxy;
	bool initializer_list_template;

	ClassTemplatePattern()
		: owner(kNoScope), lexical_scope(kNoScope), name(0),
		  declaration(kNoNode), marker_entity(kNoEntity),
		  template_parameter_ordinal(kNoTemplateParameter),
		  hosted_trait_template(HOSTED_TRAIT_TEMPLATE_NONE), defined(false),
		  template_parameter_proxy(false), initializer_list_template(false) {}
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
	std::uint32_t initializer_list_backing;

	NamespaceObjectAction(BindingId object_value, TypeId type_value,
		std::uint32_t variable_value, std::uint32_t initializer_value,
		std::uint32_t destructor_value,
		std::uint32_t initializer_list_backing_value = kNoDumpEdge)
		: object(object_value), type(type_value), variable(variable_value),
		  initializer(initializer_value), destructor(destructor_value),
		  initializer_list_backing(initializer_list_backing_value) {}
};

// A block-scope static owns persistent storage independently of an invocation.
// Its declaration ordinal is scoped by the canonical function identity and is
// the stable emission identity. Compact source provenance is presentation data
// only; it must not participate in semantic or ABI identity.
struct LocalStaticObjectAction
{
	BindingId object, function;
	TypeId type;
	std::uint32_t variable, initializer, destructor;
	std::uint32_t declaration_ordinal;
	NameId source_file;
	std::uint32_t source_line, source_column;
	std::uint32_t source_token_first, source_token_last;
	bool constant_initialized, specialization_owned_recipe;
	bool source_identity_presentation;

	LocalStaticObjectAction(BindingId object_value, BindingId function_value,
		TypeId type_value, std::uint32_t variable_value,
		std::uint32_t initializer_value, std::uint32_t destructor_value,
		std::uint32_t declaration_ordinal_value,
		NameId source_file_value, std::uint32_t source_line_value,
		std::uint32_t source_column_value,
		std::uint32_t source_token_first_value,
		std::uint32_t source_token_last_value,
		bool constant_initialized_value,
		bool specialization_owned_recipe_value,
		bool source_identity_presentation_value)
		: object(object_value), function(function_value), type(type_value),
		  variable(variable_value), initializer(initializer_value),
		  destructor(destructor_value),
		  declaration_ordinal(declaration_ordinal_value),
		  source_file(source_file_value), source_line(source_line_value),
		  source_column(source_column_value),
		  source_token_first(source_token_first_value),
		  source_token_last(source_token_last_value),
		  constant_initialized(constant_initialized_value),
		  specialization_owned_recipe(specialization_owned_recipe_value),
		  source_identity_presentation(source_identity_presentation_value) {}
};

// A lowering-only aggregate helper has a canonical typed identity but is not a
// C++ constructor declaration and therefore never participates in lookup.
// The explicit parameters correspond to the member prefix ending at the last
// non-omitted initializer.  `members` retains the complete declaration-order
// plan so lowering can zero-initialize an omitted trailing scalar/array tail;
// the hidden object parameter is already present in function_type.
struct AggregateHelperInfo
{
	EntityId entity;
	TypeId object_type;
	TypeId function_type;
	std::uint32_t parameter_member_count;
	std::vector<BindingId> members;
	std::vector<BindingId> member_constructors;
	std::vector<BindingId> member_destructors;
	std::vector<std::uint8_t> trivial_member_constructors;

	AggregateHelperInfo(EntityId entity_value, TypeId object_type_value,
		TypeId function_type_value, std::uint32_t parameter_member_count_value,
		const std::vector<BindingId>& members_value,
		const std::vector<BindingId>& member_constructors_value,
		const std::vector<BindingId>& member_destructors_value,
		const std::vector<std::uint8_t>& trivial_member_constructors_value)
		: entity(entity_value), object_type(object_type_value),
		  function_type(function_type_value),
		  parameter_member_count(parameter_member_count_value),
		  members(members_value),
		  member_constructors(member_constructors_value),
		  member_destructors(member_destructors_value),
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
	const std::vector<LocalStaticObjectAction>& local_static_objects;
	const std::vector<AggregateHelperInfo>& aggregate_helpers;
	const std::vector<ClassPolymorphismFacts>& class_polymorphism;
	std::uint32_t root;

	SemanticGraphView(const Program& program_value,
		const DumpArena& arena_value,
		const std::vector<NamespaceObjectAction>& namespace_objects_value,
		const std::vector<LocalStaticObjectAction>& local_static_objects_value,
		const std::vector<AggregateHelperInfo>& aggregate_helpers_value,
		const std::vector<ClassPolymorphismFacts>& class_polymorphism_value,
		std::uint32_t root_value)
		: program(program_value), arena(arena_value),
		  namespace_objects(namespace_objects_value),
		  local_static_objects(local_static_objects_value),
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
