#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
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
	NameTable();
	NameId Intern(const std::string& spelling);
	const std::string& Get(NameId name) const;
	std::size_t Size() const;
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);
	std::vector<std::string> spellings_;
	std::vector<NameId> slots_;
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
	TYPE_FUNCTION
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
	TypeId Reference(TypeKind kind, TypeId type);
	TypeId Array(TypeId type, std::uint64_t bound);
	TypeId Function(TypeId result, const std::vector<TypeId>& parameters,
		bool variadic);
	TypeId RemoveTopCv(TypeId type) const;
	bool IsFunction(TypeId type) const;
	bool IsReference(TypeId type) const;
	bool IsNamed(TypeId type) const;
	const TypeRecord& Get(TypeId type) const;
	const TypeId* Parameters(TypeId function) const;
	std::size_t Size() const;
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

struct EntityRecord
{
	NameId name;
	NamedFlavor flavor;
	ScopeId member_scope;
	TypeId type;
	TypeId underlying;
	bool complete;

	EntityRecord();
};

struct BindingRecord
{
	ScopeId owner;
	NameId name;
	BindingKind kind;
	TypeId type;
	BindingId next;
	NamedFlavor display_flavor;
	std::int64_t value;
	bool constant;

	BindingRecord();
};

struct LookupResult
{
	ScopeId name_space;
	TypeId type;
	BindingId ordinary;

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
	Program();
	~Program();
	ScopeId GlobalScope() const;
	ScopeId NewScope(ScopeId parent, ScopeKind kind, NameId name,
		EntityId entity = kNoEntity);
	ScopeId OpenNamespace(ScopeId parent, NameId name, bool is_inline);
	void AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target);
	void AddUsingEdge(ScopeId owner, ScopeId target);
	EntityId NewEntity(NameId name, NamedFlavor flavor, bool complete,
		TypeId underlying = kNoType);
	BindingId AddBinding(ScopeId owner, BindingKind kind, NameId name,
		TypeId type, bool constant = false, std::int64_t value = 0,
		NamedFlavor display = NAMED_NONE);
	void SetTypeName(ScopeId owner, NameId name, TypeId type);
	void SetEntityScope(EntityId entity, ScopeId scope);
	LookupResult Lookup(ScopeId current, const std::string& spelling,
		LookupKind kind);
	LookupResult LookupDirect(ScopeId scope, const std::string& spelling,
		LookupKind kind);
	ScopeId ResolveScope(ScopeId current, const std::string& spelling);
	ScopeId ScopeForType(TypeId type) const;
	std::size_t SizeOf(TypeId type) const;
	std::size_t AlignOf(TypeId type) const;
	std::string RenderType(TypeId type) const;
	void Render(std::ostream& output) const;
	std::size_t ScopeCount() const;
	std::size_t StorageBytes() const;

	NameTable names;
	TypeTable types;
	std::vector<EntityRecord> entities;
	std::vector<BindingRecord> bindings;
	std::size_t lookup_queries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;

private:
	struct ScopeRecord;
	struct NameEntry;
	struct UsingEdge;
	struct ChildEdge;
	NameEntry* EnsureEntry(ScopeId scope, NameId name);
	const NameEntry* FindEntry(ScopeId scope, NameId name) const;
	void RehashEntries(std::size_t capacity);
	LookupResult DirectLookup(ScopeId scope, NameId name,
		LookupKind kind) const;
	LookupResult LookupGraph(ScopeId scope, NameId name, LookupKind kind);
	LookupResult LookupUnqualified(ScopeId scope, NameId name,
		LookupKind kind);
	ScopeId CarrierScope(const LookupResult& result) const;
	std::vector<NameId> SplitName(const std::string& spelling,
		bool* global);
	std::string RenderTypeInner(TypeId type) const;
	void RenderScope(std::ostream& output, ScopeId scope,
		std::size_t depth) const;
	std::size_t FundamentalSize(FundamentalKind kind) const;

	std::vector<ScopeRecord> scopes_;
	std::vector<ChildEdge> child_edges_;
	std::vector<UsingEdge> using_edges_;
	std::vector<NameEntry> entries_;
	std::vector<std::uint32_t> entry_slots_;
	std::vector<std::uint32_t> lookup_marks_;
	std::uint32_t lookup_generation_;
};

}
}
