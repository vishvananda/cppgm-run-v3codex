#pragma once

#include "frontend_intern.h"

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

const TypeId kNoType = std::numeric_limits<TypeId>::max();
const ScopeId kNoScope = std::numeric_limits<ScopeId>::max();
const EntityId kNoEntity = std::numeric_limits<EntityId>::max();
const BindingId kNoBinding = std::numeric_limits<BindingId>::max();

std::size_t MixHash(std::size_t seed, std::uint64_t value);

class NameTable
{
public:
	explicit NameTable(InternedStringTable& strings);
	NameId Intern(const std::string& spelling);
	NameId InternRange(const std::string& spelling,
		std::size_t first, std::size_t count);
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
	FUND_CHAR32_T
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
	CV_VOLATILE = 2
};

struct TypeRecord
{
	TypeKind kind;
	TypeId child;
	EntityId entity;
	std::uint64_t bound;
	std::uint32_t parameter_offset;
	std::uint32_t parameter_count;
	std::uint8_t cv;
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
	TypeId Qualify(TypeId type, std::uint8_t cv);
	TypeId Pointer(TypeId type);
	TypeId MemberPointer(TypeId owner, TypeId member);
	TypeId Reference(TypeKind kind, TypeId type);
	TypeId Array(TypeId type, std::uint64_t bound);
	TypeId Function(TypeId result, const std::vector<TypeId>& parameters,
		bool variadic, std::uint8_t cv = CV_NONE);
	TypeId RemoveTopCv(TypeId type) const;
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
	STORAGE_CLASS_STATIC, STORAGE_CLASS_THREAD_LOCAL };

enum AccessKind { ACCESS_PUBLIC, ACCESS_PROTECTED, ACCESS_PRIVATE };

struct EntityRecord
{
	NameId name, identity_name;
	ScopeId owner, member_scope;
	EntityId direct_base;
	NamedFlavor flavor;
	TypeId type, underlying;
	BindingId declaration;
	std::uint64_t object_size, object_alignment;
	AccessKind base_access;
	bool complete, layout_complete, has_user_declared_constructor,
		has_user_provided_constructor, default_constructible,
		trivial_default_constructor, has_user_declared_destructor,
		destructible, trivial_destructor,
		has_direct_base, is_aggregate;

	EntityRecord();
};

struct BindingRecord
{
	ScopeId owner;
	NameId name, qualified_name;
	BindingKind kind;
	TypeId type;
	BindingId next;
	EntityId member_owner;
	std::uint64_t member_offset;
	std::uint32_t overload_ordinal, member_ordinal;
	NamedFlavor display_flavor;
	NameId display_type_name;
	BindingId canonical;
	std::int64_t value;
	LanguageLinkage language_linkage;
	StorageClass storage_class;
	AccessKind access;
	bool constant, nonthrowing, non_static_data_member, static_member_function,
		has_default_member_initializer, constructor, destructor;

	BindingRecord();
};

struct LookupResult
{
	ScopeId name_space;
	TypeId type;
	BindingId type_declaration, ordinary, ordinary_declaration;
	EntityId naming_class;

	LookupResult();
	bool Empty() const;
};

enum LookupKind
{
	LOOKUP_NAMESPACE,
	LOOKUP_TYPE,
	LOOKUP_ORDINARY,
	LOOKUP_SCOPE_CARRIER
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
	void AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target);
	void AddUsingEdge(ScopeId owner, ScopeId target);
	EntityId NewEntity(NameId name, NamedFlavor flavor, bool complete,
		TypeId underlying = kNoType, ScopeId owner = kNoScope,
		NameId identity_name = 0);
	BindingId AddBinding(ScopeId owner, BindingKind kind, NameId name,
		TypeId type, bool constant = false, std::int64_t value = 0,
		NamedFlavor display = NAMED_NONE, NameId display_type_name = 0,
		BindingId canonical = kNoBinding, bool merge_redeclaration = true);
	BindingId AddOutputTypeBinding(ScopeId owner, NameId display_name,
		TypeId type, NamedFlavor display);
	void SetTypeName(ScopeId owner, NameId name, TypeId type);
	void SetEntityScope(EntityId entity, ScopeId scope);
	void SetDirectBase(EntityId derived, EntityId base, AccessKind access);
	bool IsBaseOf(EntityId base, EntityId derived) const;
	LookupResult Lookup(ScopeId current, const NamePath& name,
		LookupKind kind);
	LookupResult LookupName(ScopeId current, NameId name, LookupKind kind);
	LookupResult LookupDirect(ScopeId scope, NameId name,
		LookupKind kind);
	LookupResult LookupMember(EntityId entity, NameId name,
		LookupKind kind);
	LookupResult LookupQualified(ScopeId owner, const NamePath& name,
		LookupKind kind);
	ScopeId ResolveScope(ScopeId current, const NamePath& name);
	ScopeId ScopeForType(TypeId type) const;
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
	std::vector<BindingRecord> bindings;
	std::size_t lookup_queries, lookup_scope_visits, lookup_edge_visits;
	mutable std::size_t name_index_probes;
	std::size_t using_index_probes;

private:
	struct ScopeRecord; struct NameEntry;
	struct UsingEdge; struct ChildEdge;
	struct LookupCacheEntry; struct LookupCache;
	NameEntry* EnsureEntry(ScopeId scope, NameId name);
	const NameEntry* FindEntry(ScopeId scope, NameId name) const;
	void RehashEntries(std::size_t capacity);
	void RehashUsingEdges(std::size_t capacity);
	LookupResult DirectLookup(ScopeId scope, NameId name,
		LookupKind kind) const;
	void MergeLookup(LookupResult* result,
		const LookupResult& candidate) const;
	LookupResult LookupGraph(ScopeId scope, NameId name, LookupKind kind);
	LookupResult LookupUnqualified(ScopeId scope, NameId name,
		LookupKind kind);
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

	std::vector<ScopeRecord> scopes_;
	std::vector<ChildEdge> child_edges_;
	std::vector<UsingEdge> using_edges_;
	std::vector<std::uint32_t> using_edge_slots_;
	std::vector<NameEntry> entries_;
	std::vector<std::uint32_t> entry_slots_;
	std::vector<std::uint32_t> lookup_marks_;
	std::vector<ScopeId> lookup_worklist_;
	std::uint32_t lookup_generation_;
	std::unique_ptr<LookupCache> lookup_cache_;
};

}
}
