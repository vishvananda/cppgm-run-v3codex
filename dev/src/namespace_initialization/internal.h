#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "namespace_initialization/types.h"
#include "namespace_initialization/driver.h"

namespace cppgm
{
namespace namespace_initialization
{

class NameSequence
{
public:
	NameSequence();
	void push_back(NameId name);
	bool empty() const;
	std::size_t size() const;
	NameId operator[](std::size_t index) const;
	NameId back() const;
	std::size_t StorageBytes() const;

private:
	std::array<NameId, 4> inline_names_;
	std::vector<NameId> overflow_names_;
	std::size_t size_;
};

struct QualifiedName
{
	bool absolute;
	NameSequence segments;
	QualifiedName();
};

enum Linkage
{
	LINKAGE_NONE,
	LINKAGE_INTERNAL,
	LINKAGE_EXTERNAL
};

enum InitialKind
{
	INITIAL_ZERO,
	INITIAL_SCALAR,
	INITIAL_ADDRESS_ENTITY,
	INITIAL_ADDRESS_STRING,
	INITIAL_ADDRESS_TEMPORARY,
	INITIAL_ARRAY_BYTES,
	INITIAL_UNKNOWN
};

struct InitialValue
{
	InitialKind kind;
	FundamentalType scalar_type;
	std::array<unsigned char, 16> bytes;
	std::uint32_t target;
	std::uint32_t byte_offset;
	std::uint32_t byte_size;
	std::int64_t addend;

	InitialValue();
};

enum ValueCategory
{
	VALUE_LVALUE,
	VALUE_XVALUE,
	VALUE_PRVALUE
};

struct Expression
{
	TypeId type;
	ValueCategory category;
	InitialValue value;
	EntityId entity;
	bool constant_expression;
	bool null_pointer_constant;
	bool string_literal;
	StringId string_id;
	CandidateId first_function;
	CandidateId last_function;
	std::uint32_t translation_unit;

	Expression();
};

struct CandidateLink
{
	EntityId entity;
	CandidateId next;
	CandidateLink(EntityId value, CandidateId next_value);
};

struct Binding
{
	BindingId identity;
	ScopeId owner;
	NameId name;
	TypeId type;
	ScopeId name_space;
	EntityId variable;
	CandidateId first_function;
	CandidateId last_function;
	BindingId type_origin;
	BindingId namespace_origin;
	bool namespace_alias;

	Binding(BindingId identity_value = 0, ScopeId owner_value = 0,
		NameId name_value = 0);
};

struct ScopeRecord
{
	ScopeId parent;
	PathId path;
	NameId name;
	ScopeId unnamed_child;
	UsingEdgeId first_using_edge;
	UsingEdgeId last_using_edge;
	UsingEdgeId first_using_predecessor;
	UsingEdgeId last_using_predecessor;
	bool is_inline;
	bool internal_context;
	std::uint32_t translation_unit;

	ScopeRecord(ScopeId parent_value, PathId path_value, NameId name_value,
		bool inline_value, bool internal_value, std::uint32_t unit);
};

struct UsingEdgeRecord
{
	ScopeId owner;
	ScopeId target;
	UsingEdgeId next_from_owner;
	UsingEdgeId next_to_target;

	UsingEdgeRecord(ScopeId owner_value, ScopeId target_value);
};

enum LookupKind
{
	LOOKUP_TYPE,
	LOOKUP_NAMESPACE,
	LOOKUP_EXPRESSION,
	LOOKUP_USING_TARGET
};

struct LookupResult
{
	TypeId type;
	ScopeId name_space;
	EntityId variable;
	CandidateId first_function;
	CandidateId last_function;
	BindingId type_origin;
	BindingId namespace_origin;
	bool declarations_found;
	bool ambiguous;

	LookupResult();
	bool Found(LookupKind kind) const;
};

struct LookupCacheEntry
{
	ScopeId start;
	NameId name;
	LookupKind kind;
	std::uint32_t generation;
	LookupResult result;

	LookupCacheEntry(ScopeId start_value, NameId name_value,
		LookupKind kind_value, std::uint32_t generation_value,
		const LookupResult& result_value);
};

struct EntityRecord
{
	NameId name;
	TypeId type;
	ScopeId owner;
	Linkage linkage;
	std::uint32_t definition_unit;
	std::uint32_t declaration_count;
	InitialValue initial;
	std::uint64_t image_offset;
	bool function;
	bool defined;
	bool definitions_inline;
	bool has_thread_storage;
	bool constexpr_declared;
	bool constant_initialized;
	bool constant_usable;

	EntityRecord(NameId name_value, TypeId type_value, ScopeId owner_value,
		Linkage linkage_value, bool function_value);
};

struct StringRecord
{
	FundamentalType element_type;
	std::uint32_t byte_offset;
	std::uint32_t byte_size;
	std::uint64_t image_offset;
};

struct TemporaryRecord
{
	TypeId type;
	InitialValue initial;
	std::uint64_t image_offset;
};

struct DeclarationSpecifiers
{
	TypeId type;
	bool is_typedef;
	bool is_static;
	bool is_extern;
	bool is_thread_local;
	bool is_constexpr;
	bool is_inline;

	DeclarationSpecifiers();
};

struct DeclaratorOperation
{
	TypeKind kind;
	unsigned char cv;
	std::uint64_t bound;
	bool variadic;
	std::vector<TypeId> parameters;

	explicit DeclaratorOperation(TypeKind kind_value);
};

struct Declarator
{
	QualifiedName name;
	ScopeId resolved_owner;
	bool has_name;
	bool has_function_operation;
	std::vector<DeclaratorOperation> operations;

	Declarator();
};

bool IsIntegralFundamental(FundamentalType type);
bool IsFloatingFundamental(FundamentalType type);
bool IsUnsignedFundamental(FundamentalType type);
std::size_t FundamentalSize(FundamentalType type);
long double ReadArithmetic(const InitialValue& value);
InitialValue ConvertArithmetic(const InitialValue& source,
	FundamentalType destination);

}
}
