#pragma once

#include "frontend_intern.h"
#include "hosted_builtin_registry.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa11
{

typedef std::uint32_t NameId;
typedef std::uint32_t TypeId;
typedef std::uint32_t ScopeId;
typedef std::uint32_t EntityId;
typedef std::uint32_t BindingId;
typedef std::uint32_t TemplateArgumentListId;

const TypeId kNoType = std::numeric_limits<TypeId>::max();
const ScopeId kNoScope = std::numeric_limits<ScopeId>::max();
const EntityId kNoEntity = std::numeric_limits<EntityId>::max();
const BindingId kNoBinding = std::numeric_limits<BindingId>::max();
const TemplateArgumentListId kNoTemplateArgumentList =
	std::numeric_limits<TemplateArgumentListId>::max();
const std::uint32_t kNoTemplateParameter =
	std::numeric_limits<std::uint32_t>::max();
const std::uint32_t kNondeducedTemplateParameter =
	std::numeric_limits<std::uint32_t>::max() - 1;

enum TemplateArgumentKind
{
	TEMPLATE_ARGUMENT_TYPE,
	TEMPLATE_ARGUMENT_INTEGRAL,
	TEMPLATE_ARGUMENT_TEMPLATE
};

struct TemplateArgument
{
	TemplateArgumentKind kind;
	// A dependent non-type argument keeps its deduction shape in `type`; a
	// directly written integral literal keeps its lexical type separately for
	// source ABI identity.
	TypeId type, source_value_type;
	std::int64_t value;
	BindingId value_binding;
	std::uint32_t dependent_parameter;
	bool pack_expansion;

	TemplateArgument()
		: kind(TEMPLATE_ARGUMENT_TYPE), type(kNoType),
		  source_value_type(kNoType), value(0),
		  value_binding(kNoBinding),
		  dependent_parameter(kNoTemplateParameter), pack_expansion(false) {}
	TemplateArgument(TemplateArgumentKind kind_value, TypeId type_value,
		std::int64_t integral_value = 0,
		std::uint32_t dependent_parameter_value = kNoTemplateParameter,
		bool pack_expansion_value = false,
		BindingId value_binding_value = kNoBinding)
		: kind(kind_value), type(type_value), source_value_type(kNoType),
		  value(integral_value),
		  value_binding(value_binding_value),
		  dependent_parameter(dependent_parameter_value),
		  pack_expansion(pack_expansion_value) {}
	bool IsDependent() const
		{ return dependent_parameter != kNoTemplateParameter; }
	bool IsNondeduced() const
		{ return dependent_parameter == kNondeducedTemplateParameter; }
	bool operator==(const TemplateArgument& other) const
	{
		return kind == other.kind && type == other.type &&
			source_value_type == other.source_value_type &&
			value == other.value && value_binding == other.value_binding &&
			dependent_parameter == other.dependent_parameter &&
			pack_expansion == other.pack_expansion;
	}
	bool operator!=(const TemplateArgument& other) const
		{ return !(*this == other); }
};

std::size_t MixHash(std::size_t seed, std::uint64_t value);

class NameTable
{
public:
	explicit NameTable(InternedStringTable& strings);
	NameId Intern(const std::string& spelling);
	NameId InternRange(const std::string& spelling,
		std::size_t first, std::size_t count);
	NameId UseInterned(NameId name);
	const std::string& Get(NameId name) const;
	std::size_t Size() const;
	std::size_t StorageBytes() const;

private:
	InternedStringTable& strings_;
	std::vector<std::uint8_t> used_;
	std::size_t size_;
};

struct NamePath
{
	bool global;

	NamePath();
	void Reserve(std::size_t count);
	void Push(NameId name);
	void Pop();
	bool Empty() const;
	std::size_t Size() const;
	NameId operator[](std::size_t index) const;
	NameId Last() const;

private:
	NameId inline_parts_[4];
	std::vector<NameId> overflow_parts_;
	std::size_t size_;
};

enum FundamentalKind
{
	FUND_BOOL,
	FUND_CHAR,
	FUND_SIGNED_CHAR,
	FUND_UNSIGNED_CHAR,
	FUND_SHORT_INT,
	FUND_UNSIGNED_SHORT_INT,
	FUND_INT,
	FUND_UNSIGNED_INT,
	FUND_LONG_INT,
	FUND_UNSIGNED_LONG_INT,
	FUND_LONG_LONG_INT,
	FUND_UNSIGNED_LONG_LONG_INT,
	FUND_FLOAT,
	FUND_DOUBLE,
	FUND_LONG_DOUBLE,
	FUND_VOID,
	FUND_NULLPTR_T,
	FUND_WCHAR_T,
	FUND_CHAR16_T,
	FUND_CHAR32_T,
	FUND_INT128,
	FUND_UINT128
};

enum TypeKind
{
	TYPE_INVALID,
	TYPE_FUNDAMENTAL,
	TYPE_NAMED,
	TYPE_QUALIFIED,
	TYPE_POINTER,
	TYPE_LVALUE_REFERENCE,
	TYPE_RVALUE_REFERENCE,
	TYPE_ARRAY,
	TYPE_FUNCTION,
	TYPE_MEMBER_POINTER
};

enum CvFlags
{
	CV_NONE = 0,
	CV_CONST = 1,
	CV_VOLATILE = 2,
	CV_ATOMIC = 4
};

enum FunctionRefQualifier
{
	FUNCTION_REF_NONE,
	FUNCTION_REF_LVALUE,
	FUNCTION_REF_RVALUE
};

struct TypeRecord
{
	TypeKind kind;
	TypeId child;
	EntityId entity;
	std::uint64_t bound;
	TypeId dependent_bound_type;
	std::uint32_t dependent_bound_parameter;
	std::uint32_t parameter_offset;
	std::uint32_t parameter_count;
	std::uint8_t cv;
	std::uint8_t ref_qualifier;
	bool variadic;
	FundamentalKind fundamental;

	TypeRecord();
};

class TypeTable
{
public:
	TypeTable();
	TypeId Fundamental(FundamentalKind kind);
	TypeId Named(EntityId entity);
	TypeId TryQualify(TypeId type, std::uint8_t cv);
	TypeId Qualify(TypeId type, std::uint8_t cv);
	TypeId TryPointer(TypeId type);
	TypeId Pointer(TypeId type);
	TypeId TryMemberPointer(TypeId owner, TypeId member);
	TypeId MemberPointer(TypeId owner, TypeId member);
	TypeId TryReference(TypeKind kind, TypeId type);
	TypeId Reference(TypeKind kind, TypeId type);
	TypeId TryArray(TypeId type, std::uint64_t bound);
	TypeId Array(TypeId type, std::uint64_t bound);
	TypeId TryDependentArray(TypeId type, TypeId bound_type,
		std::uint32_t parameter);
	TypeId DependentArray(TypeId type, TypeId bound_type,
		std::uint32_t parameter);
	TypeId TryFunction(TypeId result, const std::vector<TypeId>& parameters,
		bool variadic, std::uint8_t cv = CV_NONE,
		std::uint8_t ref_qualifier = FUNCTION_REF_NONE);
	TypeId Function(TypeId result, const std::vector<TypeId>& parameters,
		bool variadic, std::uint8_t cv = CV_NONE,
		std::uint8_t ref_qualifier = FUNCTION_REF_NONE);
	TypeId RemoveTopCv(TypeId type) const;
	bool IsAtomic(TypeId type) const;
	bool IsFunction(TypeId type) const;
	bool IsReference(TypeId type) const;
	bool IsNamed(TypeId type) const;
	const TypeRecord& Get(TypeId type) const;
	const TypeId* Parameters(TypeId function) const;
	std::size_t Size() const;
	std::size_t IndexProbes() const;
	std::size_t StorageBytes() const;

private:
	TypeId Unary(TypeKind kind, TypeId child);
	TypeId Intern(TypeRecord candidate, const TypeId* parameters,
		std::size_t count);
	std::size_t Hash(const TypeRecord& record, const TypeId* parameters,
		std::size_t count) const;
	bool Equal(const TypeRecord& existing, const TypeRecord& candidate,
		const TypeId* parameters, std::size_t count) const;
	void Rehash(std::size_t capacity);
	std::vector<TypeRecord> types_;
	std::vector<TypeId> parameters_;
	std::vector<TypeId> slots_;
	std::size_t index_probes_;
};

enum ScopeKind
{
	SCOPE_NAMESPACE,
	SCOPE_TEMPLATE_PARAMETERS,
	SCOPE_CLASS,
	SCOPE_ENUM,
	SCOPE_FUNCTION,
	SCOPE_BLOCK
};

enum NamedFlavor
{
	NAMED_NONE,
	NAMED_STRUCT,
	NAMED_CLASS,
	NAMED_UNION,
	NAMED_ENUM,
	NAMED_ENUM_CLASS,
	NAMED_TYPENAME_PARAMETER,
	NAMED_TEMPLATE_PARAMETER
};

enum BindingKind
{
	BIND_TYPE,
	BIND_TYPE_ALIAS,
	BIND_ENUMERATOR,
	BIND_FUNCTION,
	BIND_VARIABLE,
	BIND_PARAMETER
};

enum LanguageLinkage { LANGUAGE_LINKAGE_CPP, LANGUAGE_LINKAGE_C };

enum StorageClass { STORAGE_CLASS_NONE, STORAGE_CLASS_EXTERN,
	STORAGE_CLASS_STATIC };

enum AccessKind { ACCESS_PUBLIC, ACCESS_PROTECTED, ACCESS_PRIVATE };

enum FunctionExceptionBoundaryKind : std::uint8_t
{
	FUNCTION_EXCEPTION_BOUNDARY_NONE,
	FUNCTION_EXCEPTION_BOUNDARY_TERMINATE,
	FUNCTION_EXCEPTION_BOUNDARY_UNEXPECTED
};

enum OperatorKind
{
	OPERATOR_NONE,
	OPERATOR_PLUS,
	OPERATOR_MINUS,
	OPERATOR_STAR,
	OPERATOR_AMPERSAND,
	OPERATOR_DIVIDE,
	OPERATOR_REMAINDER,
	OPERATOR_BIT_OR,
	OPERATOR_BIT_XOR,
	OPERATOR_ASSIGN,
	OPERATOR_PLUS_ASSIGN,
	OPERATOR_MINUS_ASSIGN,
	OPERATOR_MULTIPLY_ASSIGN,
	OPERATOR_DIVIDE_ASSIGN,
	OPERATOR_REMAINDER_ASSIGN,
	OPERATOR_AND_ASSIGN,
	OPERATOR_OR_ASSIGN,
	OPERATOR_XOR_ASSIGN,
	OPERATOR_LEFT_SHIFT,
	OPERATOR_RIGHT_SHIFT,
	OPERATOR_LEFT_SHIFT_ASSIGN,
	OPERATOR_RIGHT_SHIFT_ASSIGN,
	OPERATOR_EQUAL,
	OPERATOR_NOT_EQUAL,
	OPERATOR_LESS,
	OPERATOR_GREATER,
	OPERATOR_LESS_EQUAL,
	OPERATOR_GREATER_EQUAL,
	OPERATOR_LOGICAL_NOT,
	OPERATOR_LOGICAL_AND,
	OPERATOR_LOGICAL_OR,
	OPERATOR_INCREMENT,
	OPERATOR_DECREMENT,
	OPERATOR_COMMA,
	OPERATOR_MEMBER_POINTER,
	OPERATOR_ARROW,
	OPERATOR_CALL,
	OPERATOR_INDEX,
	OPERATOR_NEW,
	OPERATOR_NEW_ARRAY,
	OPERATOR_DELETE,
	OPERATOR_DELETE_ARRAY,
	OPERATOR_LITERAL
};

enum BuiltinFunctionKind
{
	BUILTIN_FUNCTION_NONE,
	BUILTIN_FUNCTION_STRLEN,
	BUILTIN_FUNCTION_UNREACHABLE,
	BUILTIN_FUNCTION_MEMCPY,
	BUILTIN_FUNCTION_MEMMOVE,
	BUILTIN_FUNCTION_NANL,
	BUILTIN_FUNCTION_ISNAN,
	BUILTIN_FUNCTION_ALLOCA,
	BUILTIN_FUNCTION_VA_START,
	BUILTIN_FUNCTION_VA_END,
	BUILTIN_FUNCTION_VA_ARG,
	BUILTIN_FUNCTION_HOSTED_INTEGER_INTRINSIC,
	BUILTIN_FUNCTION_HOSTED_FLOATING_INTRINSIC,
	BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC,
	BUILTIN_FUNCTION_ABORT,
	BUILTIN_FUNCTION_OPERATOR_NEW,
	BUILTIN_FUNCTION_OPERATOR_DELETE,
	BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY,
	BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY
};

struct DirectBaseEdge
{
	EntityId entity;
	std::uint64_t offset;
	AccessKind access;
	bool virtual_base;

	DirectBaseEdge(EntityId entity_ = kNoEntity,
		AccessKind access_ = ACCESS_PUBLIC, bool virtual_base_ = false)
		: entity(entity_), offset(0), access(access_),
		  virtual_base(virtual_base_) {}
};

struct VirtualBaseLayout
{
	EntityId entity;
	std::uint64_t offset;

	VirtualBaseLayout(EntityId entity_ = kNoEntity,
		std::uint64_t offset_ = 0)
		: entity(entity_), offset(offset_) {}
};

struct EntityRecord
{
	NameId name, identity_name;
	ScopeId owner, member_scope;
	EntityId direct_base, enclosing_class;
	BindingId local_context, lambda_call_operator;
	TemplateArgumentListId template_argument_list;
	std::uint32_t template_argument_begin, template_argument_count,
		template_argument_pack_begin;
	std::uint32_t direct_base_begin, direct_base_count,
		virtual_base_begin, virtual_base_count;
	std::uint32_t abi_tag_begin, abi_tag_count;
	NamedFlavor flavor;
	TypeId type, underlying;
	BindingId declaration, union_default_member;
	std::uint64_t object_size, nonvirtual_size,
		object_alignment, nonvirtual_alignment, natural_alignment,
		requested_alignment, packing_alignment, direct_base_offset;
	AccessKind base_access;
	bool complete, layout_complete, has_user_declared_constructor,
		has_user_provided_constructor, default_constructible,
		trivial_default_constructor, has_user_declared_destructor,
		destructible, trivial_destructor,
		has_direct_base, is_aggregate, empty_class,
		indirect_class_value_abi, indirect_class_result_abi,
		indirect_class_parameter_abi,
		polymorphic_class, abstract_class;
	bool nonlinear_base_graph;
	bool has_nonzero_base_subobject_offset;
	bool deferred_template_completion;
	bool explicit_template_specialization;
	bool unnamed_class, lambda_closure;
	std::uint32_t local_name_ordinal, lambda_ordinal, lambda_capture_count;
	std::uint32_t template_parameter_ordinal;

	EntityRecord();
};

typedef std::uint32_t FunctionTemplateAbiRecipeId;
const FunctionTemplateAbiRecipeId kNoFunctionTemplateAbiRecipe =
	std::numeric_limits<FunctionTemplateAbiRecipeId>::max();

typedef std::uint32_t FunctionTemplateAbiTypeId;
const FunctionTemplateAbiTypeId kNoFunctionTemplateAbiType =
	std::numeric_limits<FunctionTemplateAbiTypeId>::max();

typedef std::uint32_t FunctionTemplateAbiExpressionId;
const FunctionTemplateAbiExpressionId kNoFunctionTemplateAbiExpression =
	std::numeric_limits<FunctionTemplateAbiExpressionId>::max();

enum FunctionTemplateAbiTypeKind
{
	FUNCTION_TEMPLATE_ABI_TYPE_CONCRETE,
	FUNCTION_TEMPLATE_ABI_TYPE_PARAMETER,
	FUNCTION_TEMPLATE_ABI_TYPE_MEMBER,
	FUNCTION_TEMPLATE_ABI_TYPE_TEMPLATE_SPECIALIZATION,
	FUNCTION_TEMPLATE_ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION,
	FUNCTION_TEMPLATE_ABI_TYPE_BUILTIN_TRANSFORM,
	FUNCTION_TEMPLATE_ABI_TYPE_DECLTYPE,
	FUNCTION_TEMPLATE_ABI_TYPE_QUALIFIED,
	FUNCTION_TEMPLATE_ABI_TYPE_POINTER,
	FUNCTION_TEMPLATE_ABI_TYPE_LVALUE_REFERENCE,
	FUNCTION_TEMPLATE_ABI_TYPE_RVALUE_REFERENCE,
	FUNCTION_TEMPLATE_ABI_TYPE_ARRAY
};

enum FunctionTemplateAbiArgumentKind
{
	FUNCTION_TEMPLATE_ABI_ARGUMENT_TYPE,
	FUNCTION_TEMPLATE_ABI_ARGUMENT_EXPRESSION
};

struct FunctionTemplateAbiArgument
{
	FunctionTemplateAbiArgumentKind kind;
	FunctionTemplateAbiTypeId type;
	FunctionTemplateAbiExpressionId expression;
	bool pack_expansion;

	FunctionTemplateAbiArgument(
		FunctionTemplateAbiArgumentKind kind_value,
		FunctionTemplateAbiTypeId type_value = kNoFunctionTemplateAbiType,
		FunctionTemplateAbiExpressionId expression_value =
			kNoFunctionTemplateAbiExpression,
		bool pack_expansion_value = false)
		: kind(kind_value), type(type_value), expression(expression_value),
		  pack_expansion(pack_expansion_value) {}
};

enum FunctionTemplateAbiExpressionKind
{
	FUNCTION_TEMPLATE_ABI_EXPRESSION_TEMPLATE_PARAMETER,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_FUNCTION_PARAMETER,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_TYPE_MEMBER,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_OBJECT_MEMBER,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_UNARY,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_CALL,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_BINARY,
	FUNCTION_TEMPLATE_ABI_EXPRESSION_TEMPLATE_ID
};

struct FunctionTemplateAbiExpression
{
	FunctionTemplateAbiExpressionKind kind;
	FunctionTemplateAbiExpressionId left, right;
	FunctionTemplateAbiTypeId type;
	NameId name;
	std::uint32_t parameter, argument_begin, argument_count;
	OperatorKind operation;
	bool indirect_member;

	FunctionTemplateAbiExpression(
		FunctionTemplateAbiExpressionKind kind_value,
		FunctionTemplateAbiExpressionId left_value =
			kNoFunctionTemplateAbiExpression,
		FunctionTemplateAbiExpressionId right_value =
			kNoFunctionTemplateAbiExpression,
		FunctionTemplateAbiTypeId type_value = kNoFunctionTemplateAbiType,
		NameId name_value = 0,
		std::uint32_t parameter_value = kNoTemplateParameter,
		OperatorKind operation_value = OPERATOR_NONE,
		bool indirect_member_value = false,
		std::uint32_t argument_begin_value = 0,
		std::uint32_t argument_count_value = 0)
		: kind(kind_value), left(left_value), right(right_value),
		  type(type_value), name(name_value), parameter(parameter_value),
		  argument_begin(argument_begin_value),
		  argument_count(argument_count_value), operation(operation_value),
		  indirect_member(indirect_member_value) {}
};

// Syntax-independent source-type structure retained for function-template ABI
// encoding. Nodes are immutable after publication and refer only to canonical
// parameter ordinals, interned names, and other nodes in this table.
struct FunctionTemplateAbiType
{
	FunctionTemplateAbiTypeKind kind;
	FunctionTemplateAbiTypeId child;
	FunctionTemplateAbiExpressionId expression;
	TypeId concrete_type;
	EntityId entity;
	NameId name;
	std::uint64_t bound;
	std::uint32_t parameter, argument_begin, argument_count;
	std::uint8_t cv;

	FunctionTemplateAbiType(FunctionTemplateAbiTypeKind kind_value,
		FunctionTemplateAbiTypeId child_value = kNoFunctionTemplateAbiType,
		NameId name_value = 0, std::uint64_t bound_value = 0,
		std::uint32_t parameter_value = kNoTemplateParameter,
		std::uint8_t cv_value = 0,
		TypeId concrete_type_value = kNoType,
		EntityId entity_value = kNoEntity,
		std::uint32_t argument_begin_value = 0,
		std::uint32_t argument_count_value = 0,
		FunctionTemplateAbiExpressionId expression_value =
			kNoFunctionTemplateAbiExpression)
		: kind(kind_value), child(child_value), expression(expression_value),
		  concrete_type(concrete_type_value), entity(entity_value),
		  name(name_value), bound(bound_value), parameter(parameter_value),
		  argument_begin(argument_begin_value),
		  argument_count(argument_count_value), cv(cv_value) {}
};

struct FunctionTemplateAbiRecipe
{
	TypeId function_type;
	FunctionTemplateAbiTypeId result_type;
	std::uint32_t parameter_shape_begin, template_parameter_type_begin,
		function_parameter_type_begin, template_parameter_count,
		function_parameter_count;
	bool template_parameter_pack;
	bool function_parameter_pack;
	bool overloaded_pattern;

	FunctionTemplateAbiRecipe(TypeId function_type_value = kNoType,
		std::uint32_t parameter_shape_begin_value = 0,
		std::uint32_t template_parameter_count_value = 0,
		bool template_parameter_pack_value = false,
		bool function_parameter_pack_value = false,
		bool overloaded_pattern_value = false)
		: function_type(function_type_value),
		  result_type(kNoFunctionTemplateAbiType),
		  parameter_shape_begin(parameter_shape_begin_value),
		  template_parameter_type_begin(0),
		  function_parameter_type_begin(0),
		  template_parameter_count(template_parameter_count_value),
		  function_parameter_count(0),
		  template_parameter_pack(template_parameter_pack_value),
		  function_parameter_pack(function_parameter_pack_value),
		  overloaded_pattern(overloaded_pattern_value) {}
};

struct BindingRecord
{
	ScopeId owner;
	NameId name, qualified_name, object_section_name;
	BindingKind kind;
	TypeId type, conversion_target;
	BindingId next;
	EntityId member_owner, access_owner;
	std::uint64_t member_offset, requested_alignment;
	std::uint32_t bit_offset, bit_width, bit_storage_bits;
	std::uint32_t overload_ordinal, member_ordinal;
	TemplateArgumentListId template_argument_list;
	std::uint32_t template_argument_begin, template_argument_count;
	std::uint32_t abi_tag_begin, abi_tag_count;
	std::uint32_t exception_type_begin, exception_type_count;
	FunctionTemplateAbiRecipeId function_template_abi_recipe;
	FunctionExceptionBoundaryKind exception_boundary;
	NamedFlavor display_flavor;
	NameId display_type_name;
	BindingId canonical;
	BindingId lifecycle_base_entry;
	std::int64_t value;
	OperatorKind operator_kind;
	BuiltinFunctionKind builtin_function;
	hosted_builtin::IntegerIntrinsicKind hosted_integer_intrinsic;
	hosted_builtin::FloatingIntrinsicKind hosted_floating_intrinsic;
	hosted_builtin::MemoryIntrinsicKind hosted_memory_intrinsic;
	NameId operator_literal_suffix;
	LanguageLinkage language_linkage;
	StorageClass storage_class;
	AccessKind access;
	bool constant, nonthrowing, unnamed_namespace_linkage,
		thread_local_storage, non_static_data_member,
		mutable_member, bit_field, anonymous_union_storage,
		static_member_function, has_default_member_initializer,
		conversion_function, constructor,
		constructor_base_entry, destructor, destructor_base_entry,
		inline_function, virtual_function, pure_virtual, final_virtual,
		override_specifier, weak_odr, weak_symbol, object_output_root,
		emission_demanded;
	bool explicit_instantiation_suppressed;
	bool template_parameter_constant;
	bool variable_template_specialization;
	// A canonical callable specialization owns boundary exceptions that cannot
	// be inferred from its result type alone.  Keeping this on the binding
	// prevents one call site from mutating the ABI of every function returning
	// the same class entity.
	bool force_indirect_class_result_abi;
	bool closure_template_specialization;

	BindingRecord();
};

struct LookupResult
{
	ScopeId name_space;
	TypeId type;
	BindingId type_declaration, type_declaration_canonical;
	BindingId ordinary, ordinary_declaration;
	EntityId naming_class;

	LookupResult();
	bool Empty() const;
	std::size_t OrdinaryCount() const;
	BindingId OrdinaryAt(std::size_t index) const;
	void AddOrdinary(BindingId binding);
	bool HasFunctionTemplateLookup() const;
	void BeginFunctionTemplateLookup();
	std::size_t FunctionTemplateOwnerCount() const;
	ScopeId FunctionTemplateOwnerAt(std::size_t index) const;
	void AddFunctionTemplateOwner(ScopeId owner);
	bool HasVariableTemplateLookup() const;
	void BeginVariableTemplateLookup();
	std::size_t VariableTemplateOwnerCount() const;
	ScopeId VariableTemplateOwnerAt(std::size_t index) const;
	void AddVariableTemplateOwner(ScopeId owner);
	std::size_t DynamicStorageBytes() const;

private:
	std::size_t TemplateOwnerCount() const;
	ScopeId TemplateOwnerAt(std::size_t index) const;
	void AddTemplateOwner(ScopeId owner);
	BindingId extra_ordinary_inline_[2];
	std::vector<BindingId> extra_ordinary_overflow_;
	std::size_t extra_ordinary_count_;
	ScopeId template_owner_;
	ScopeId extra_template_owner_inline_[2];
	std::vector<ScopeId> extra_template_owner_overflow_;
	std::size_t extra_template_owner_count_;
	bool function_template_lookup_;
	bool variable_template_lookup_;
};

enum LookupKind
{
	LOOKUP_NAMESPACE,
	LOOKUP_TYPE,
	LOOKUP_ORDINARY,
	LOOKUP_SCOPE_CARRIER,
	LOOKUP_FUNCTION_TEMPLATE,
	LOOKUP_VARIABLE_TEMPLATE
};

class Program
{
public:
	explicit Program(InternedStringTable& strings);
	~Program();
	ScopeId GlobalScope() const;
	ScopeId NewScope(ScopeId parent, ScopeKind kind, NameId name,
		EntityId entity = kNoEntity, ScopeId output_parent = kNoScope);
	ScopeId OpenNamespace(ScopeId parent, NameId name, bool is_inline);
	void SetScopeEmissionName(ScopeId scope, NameId name);
	void AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target);
	void AddUsingEdge(ScopeId owner, ScopeId target);
	void PublishFunctionTemplateName(ScopeId owner, NameId name);
	void PublishVariableTemplateName(ScopeId owner, NameId name);
	TemplateArgumentListId InternTemplateArgumentList(
		const std::vector<TemplateArgument>& arguments,
		std::uint32_t* first = 0, std::uint32_t* count = 0);
	EntityId NewEntity(NameId name, NamedFlavor flavor, bool complete,
		TypeId underlying = kNoType, ScopeId owner = kNoScope,
		NameId identity_name = 0);
	BindingId AddBinding(ScopeId owner, BindingKind kind, NameId name,
		TypeId type, bool constant = false, std::int64_t value = 0,
		NamedFlavor display = NAMED_NONE, NameId display_type_name = 0,
		BindingId canonical = kNoBinding, bool merge_redeclaration = true);
	BindingId AddUnindexedBinding(ScopeId owner, BindingKind kind, NameId name,
		TypeId type, BindingId canonical = kNoBinding);
	bool IsStaticDataMember(BindingId binding) const;
	BindingId AddOutputTypeBinding(ScopeId owner, NameId display_name,
		TypeId type, NamedFlavor display);
	void SetTypeName(ScopeId owner, NameId name, TypeId type);
	void SetEntityScope(EntityId entity, ScopeId scope);
	void ResetClassDefinition(EntityId entity);
	void SetDirectBase(EntityId derived, EntityId base, AccessKind access);
	void SetDirectBases(EntityId derived,
		const std::vector<DirectBaseEdge>& bases);
	const DirectBaseEdge& DirectBase(EntityId derived,
		std::size_t ordinal) const;
	DirectBaseEdge& MutableDirectBase(EntityId derived,
		std::size_t ordinal);
	void SetVirtualBaseLayouts(EntityId derived,
		const std::vector<VirtualBaseLayout>& layouts);
	const VirtualBaseLayout& VirtualBase(EntityId derived,
		std::size_t ordinal) const;
	bool FindVirtualBase(EntityId derived, EntityId base,
		std::uint64_t* offset = 0, std::uint32_t* ordinal = 0) const;
	bool IsBaseOf(EntityId base, EntityId derived) const;
	bool HasVirtualBasePath(EntityId derived, EntityId base) const;
	bool QueryBasePath(EntityId derived, EntityId base,
		std::size_t* distance, bool* all_public,
		std::uint64_t* offset = 0, bool* ambiguous = 0,
		std::vector<std::uint32_t>* direct_base_ordinals = 0) const;
	LookupResult Lookup(ScopeId current, const NamePath& name,
		LookupKind kind);
	LookupResult LookupName(ScopeId current, NameId name, LookupKind kind);
	LookupResult LookupNameCandidate(ScopeId current, NameId name,
		LookupKind kind, bool* ambiguous);
	LookupResult LookupDirect(ScopeId scope, NameId name,
		LookupKind kind);
	LookupResult LookupMember(EntityId entity, NameId name,
		LookupKind kind);
	LookupResult LookupQualified(ScopeId owner, const NamePath& name,
		LookupKind kind);
	LookupResult LookupQualifiedCandidate(ScopeId owner, const NamePath& name,
		LookupKind kind, bool* ambiguous);
	ScopeId ResolveScope(ScopeId current, const NamePath& name);
	ScopeId ScopeForType(TypeId type) const;
	ScopeId ParentScope(ScopeId scope) const;
	ScopeKind KindOfScope(ScopeId scope) const;
	bool IsInlineNamespace(ScopeId scope) const;
	NameId NameOfScope(ScopeId scope) const;
	EntityId EntityForScope(ScopeId scope) const;
	std::size_t SizeOf(TypeId type) const;
	std::size_t AlignOf(TypeId type) const;
	std::string RenderType(TypeId type) const;
	void BuildEmissionPath(ScopeId owner, NameId terminal,
		std::vector<NameId>* path) const;
	void Render(std::ostream& output, std::size_t* max_depth = 0,
		std::size_t* stack_storage_bytes = 0,
		std::size_t* rendered_type_nodes = 0) const;
	std::size_t ScopeCount() const;
	std::size_t StorageBytes() const;

	NameTable names;
	TypeTable types;
	std::vector<EntityRecord> entities;
	std::vector<DirectBaseEdge> direct_bases;
	std::vector<VirtualBaseLayout> virtual_bases;
	std::vector<NameId> abi_tags;
	std::vector<BindingRecord> bindings;
	std::vector<TypeId> function_exception_types;
	std::vector<TypeId> template_arguments;
	std::vector<TemplateArgument> canonical_template_arguments;
	std::vector<TypeId> function_template_parameter_shapes;
	std::vector<FunctionTemplateAbiType> function_template_abi_types;
	std::vector<FunctionTemplateAbiArgument> function_template_abi_arguments;
	std::vector<FunctionTemplateAbiExpression>
		function_template_abi_expressions;
	std::vector<FunctionTemplateAbiTypeId>
		function_template_abi_template_parameter_types;
	std::vector<FunctionTemplateAbiTypeId>
		function_template_abi_function_parameter_types;
	std::vector<FunctionTemplateAbiRecipe> function_template_abi_recipes;
	std::size_t lookup_queries, lookup_scope_visits, lookup_edge_visits;
	std::size_t lookup_cache_hits, lookup_cache_misses;
	std::size_t lookup_cache_invalidations, lookup_cache_dependency_edges;
	std::size_t lookup_cache_invalidation_pushes;
	mutable std::size_t base_path_queries, base_path_cache_hits,
		base_path_cache_misses;
	mutable std::size_t base_path_edge_visits;
	mutable std::size_t virtual_base_path_visits;
	mutable std::size_t virtual_base_layout_lookups;
	mutable std::size_t virtual_base_layout_probes;
	std::size_t direct_base_validation_visits;
	mutable std::size_t name_index_probes;
	std::size_t using_index_probes;
	std::size_t template_argument_list_requests;
	std::size_t template_argument_list_cache_hits;
	std::size_t template_argument_list_index_probes;

private:
	struct ScopeRecord; struct NameEntry;
	struct UsingEdge; struct UsingNameRelation; struct ScopeVisibleName;
	struct ChildEdge;
	struct LookupCacheEntry; struct LookupCache;
	struct TemplateArgumentListRecord;
	struct BasePathState
	{
		std::uint32_t generation;
		std::size_t distance;
		std::uint64_t offset;
		std::uint32_t first_base;
		std::uint8_t path_count;
		bool complete, all_public;
		BasePathState();
	};
	struct BasePathFrame
	{
		EntityId entity;
		std::uint32_t next_base;
		BasePathFrame(EntityId entity_value = kNoEntity,
			std::uint32_t next_base_value = 0);
	};
	struct BasePathCacheEntry
	{
		EntityId derived, base;
		std::uint32_t version;
		std::size_t distance;
		std::uint64_t offset;
		bool found, all_public, ambiguous, complete;
		BasePathCacheEntry(EntityId derived_value = kNoEntity,
			EntityId base_value = kNoEntity,
			std::uint32_t version_value = 0);
	};
	struct VirtualBaseIndexEntry
	{
		EntityId derived, base;
		std::uint32_t ordinal;
		VirtualBaseIndexEntry(EntityId derived_value = kNoEntity,
			EntityId base_value = kNoEntity, std::uint32_t ordinal_value = 0)
			: derived(derived_value), base(base_value), ordinal(ordinal_value) {}
	};
	NameEntry* EnsureEntry(ScopeId scope, NameId name);
	const NameEntry* FindEntry(ScopeId scope, NameId name) const;
	void RehashEntries(std::size_t capacity);
	void RehashUsingEdges(std::size_t capacity);
	void RehashVisibleNames(std::size_t capacity);
	void RehashUsingNameRelations(std::size_t capacity);
	std::uint32_t FindVisibleName(ScopeId scope, NameId name) const;
	std::uint32_t EnsureVisibleName(ScopeId scope, NameId name,
		bool* created);
	std::uint32_t FindUsingNameRelation(std::uint32_t edge,
		NameId name) const;
	bool AddUsingNameRelation(std::uint32_t edge, NameId name,
		bool* owner_became_visible);
	void PublishUsingName(ScopeId scope, NameId name);
	void PropagateUsingName(ScopeId scope, NameId name);
	void BeginLookupDependencies();
	void RecordLookupDependency(ScopeId scope);
	void InvalidateLookupName(ScopeId scope, NameId name);
	void InvalidateLookupScope(ScopeId scope);
	LookupResult DirectLookup(ScopeId scope, NameId name,
		LookupKind kind) const;
	bool MergeLookup(LookupResult* result,
		const LookupResult& candidate, bool tolerate_ambiguity = false) const;
	LookupResult LookupGraph(ScopeId scope, NameId name, LookupKind kind);
	LookupResult LookupGraphCandidate(ScopeId scope, NameId name,
		LookupKind kind, bool* ambiguous);
	LookupResult LookupUnqualified(ScopeId scope, NameId name,
		LookupKind kind);
	LookupResult LookupUnqualifiedCandidate(ScopeId scope, NameId name,
		LookupKind kind, bool* ambiguous);
	ScopeId CarrierScope(const LookupResult& result) const;
	void AppendType(std::string& output, TypeId type,
		std::size_t* rendered_type_nodes,
		std::size_t* stack_storage_bytes) const;
	void WriteType(std::ostream& output, TypeId type,
		std::size_t* rendered_type_nodes,
		std::size_t* stack_storage_bytes) const;
	void WriteScope(std::ostream& output, ScopeId scope, std::size_t depth,
		std::size_t* max_depth, std::size_t* stack_storage_bytes,
		std::size_t* rendered_type_nodes) const;
	std::size_t FundamentalSize(FundamentalKind kind) const;
	void RehashTemplateArgumentLists(std::size_t capacity);
	void RehashBasePathCache(std::size_t capacity) const;
	bool FindBasePathCache(EntityId derived, EntityId base,
		bool* found, std::size_t* distance, bool* all_public,
		std::uint64_t* offset, bool* ambiguous,
		bool require_complete) const;
	void StoreBasePathCache(EntityId derived, EntityId base, bool found,
		std::size_t distance, bool all_public, std::uint64_t offset,
		bool ambiguous, bool complete) const;
	void RehashVirtualBaseIndex(std::size_t capacity);
	void IndexVirtualBase(EntityId derived, EntityId base,
		std::uint32_t ordinal);

	std::vector<ScopeRecord> scopes_;
	std::vector<ChildEdge> child_edges_;
	std::vector<UsingEdge> using_edges_;
	std::vector<std::uint32_t> using_edge_slots_;
	std::vector<ScopeVisibleName> visible_names_;
	std::vector<std::uint32_t> visible_name_slots_;
	std::vector<UsingNameRelation> using_name_relations_;
	std::vector<std::uint32_t> using_name_relation_slots_;
	std::vector<ScopeId> using_name_worklist_;
	std::vector<std::uint32_t> using_name_invalidation_marks_;
	std::uint32_t using_name_invalidation_generation_;
	std::vector<NameEntry> entries_;
	std::vector<std::uint32_t> entry_slots_;
	std::vector<TemplateArgumentListRecord> template_argument_lists_;
	std::vector<std::uint32_t> template_argument_list_slots_;
	std::vector<std::uint32_t> lookup_marks_;
	std::vector<ScopeId> lookup_worklist_;
	std::uint32_t lookup_generation_;
	std::vector<std::uint32_t> lookup_dependency_marks_;
	std::vector<ScopeId> lookup_dependencies_;
	std::vector<std::uint32_t> lookup_pending_heads_;
	std::vector<std::uint32_t> lookup_pending_head_marks_;
	std::vector<ScopeId> lookup_pending_targets_;
	std::vector<std::uint32_t> lookup_pending_next_;
	std::vector<std::uint32_t> lookup_pending_target_marks_;
	std::vector<EntityId> base_jumps_;
	std::vector<std::size_t> base_jump_offsets_;
	std::vector<std::uint8_t> base_jump_counts_;
	std::vector<std::uint32_t> base_depths_;
	std::vector<std::uint32_t> deepest_nonpublic_base_depths_;
	std::vector<std::uint32_t> direct_base_input_marks_;
	std::uint32_t direct_base_input_generation_;
	mutable std::vector<BasePathState> base_path_states_;
	mutable std::vector<BasePathFrame> base_path_scratch_;
	mutable std::uint32_t base_path_generation_;
	mutable std::vector<BasePathCacheEntry> base_path_cache_entries_;
	mutable std::vector<std::uint32_t> base_path_cache_slots_;
	std::vector<VirtualBaseIndexEntry> virtual_base_index_entries_;
	std::vector<std::uint32_t> virtual_base_index_slots_;
	std::vector<std::uint32_t> base_graph_versions_;
	std::uint32_t lookup_dependency_generation_;
	std::uint32_t lookup_pending_generation_;
	bool collecting_lookup_dependencies_;
	std::unique_ptr<LookupCache> lookup_cache_;
};

}
}
