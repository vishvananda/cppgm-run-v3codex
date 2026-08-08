#pragma once

#include "pa15_lowir_types.h"

namespace cppgm
{
namespace pa15_lowir_detail
{

using namespace pa11;

typedef std::uint32_t IdentityNameId;
typedef std::uint32_t IdentityPathId;
typedef std::uint32_t IdentityTypeId;

struct IdentityPathKey
{
	IdentityPathId parent;
	IdentityNameId name;

	bool operator==(const IdentityPathKey& other) const
	{
		return parent == other.parent && name == other.name;
	}
};

struct IdentityPathHash
{
	std::size_t operator()(const IdentityPathKey& key) const
	{
		return static_cast<std::size_t>(key.parent) * 16777619U ^ key.name;
	}
};

struct IdentityTypeKey
{
	TypeKind kind;
	FundamentalKind fundamental;
	IdentityTypeId child;
	IdentityPathId named;
	std::uint64_t bound;
	std::uint8_t cv;
	std::uint8_t ref_qualifier;
	bool variadic;
	std::vector<IdentityTypeId> parameters;

	IdentityTypeKey()
		: kind(TYPE_FUNDAMENTAL), fundamental(FUND_VOID), child(kNoLowId),
		  named(kNoLowId), bound(0), cv(0),
		  ref_qualifier(FUNCTION_REF_NONE), variadic(false) {}

	bool operator==(const IdentityTypeKey& other) const
	{
		return kind == other.kind && fundamental == other.fundamental &&
			child == other.child && named == other.named && bound == other.bound &&
			cv == other.cv && ref_qualifier == other.ref_qualifier &&
			variadic == other.variadic &&
			parameters == other.parameters;
	}
};

struct IdentityTypeHash
{
	std::size_t operator()(const IdentityTypeKey& key) const
	{
		std::size_t hash = static_cast<std::size_t>(key.kind) * 16777619U ^
			static_cast<std::size_t>(key.fundamental);
		hash = hash * 16777619U ^ key.child;
		hash = hash * 16777619U ^ key.named;
		hash = hash * 16777619U ^ static_cast<std::size_t>(key.bound);
		hash = hash * 16777619U ^ key.cv;
		hash = hash * 16777619U ^ key.ref_qualifier;
		hash = hash * 16777619U ^ key.variadic;
		for (std::size_t i = 0; i < key.parameters.size(); ++i)
			hash = hash * 16777619U ^ key.parameters[i];
		return hash;
	}
};

class EmissionIdentityTable
{
private:
	struct PendingType
	{
		TypeId type;
		bool expanded;
		PendingType(TypeId type_value, bool expanded_value)
			: type(type_value), expanded(expanded_value) {}
	};

public:
	EmissionIdentityTable();

	IdentityPathId InternPath(const Program& program, ScopeId owner,
		NameId terminal);
	IdentityPathId InternClassMemberPath(const Program& program,
		EntityId owner, NameId terminal);
	IdentityTypeId InternType(const Program& program, TypeId type,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternFunctionSignature(const Program& program, TypeId type,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternTypeSequence(const Program& program,
		const TypeId* types, std::size_t count,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternBindingTemplateArguments(const Program& program,
		const BindingRecord& binding, std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternEntityTemplateArguments(const Program& program,
		const EntityRecord& entity, std::vector<IdentityTypeId>& cache);
	std::size_t StorageBytes() const;

private:
	IdentityNameId InternName(const std::string& name);
	static bool HasChild(TypeKind kind);
	static void PushDependency(TypeId dependency,
		std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending);
	static void PushTypeDependencies(const Program& program,
		const TypeRecord& source, TypeId type,
		std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending);
	IdentityTypeKey MakeTypeKey(const Program& program,
		const TypeRecord& source, TypeId type,
		const std::vector<IdentityTypeId>& cache);
	IdentityPathId InternPathKey(const IdentityPathKey& key);
	void RehashPaths(std::size_t capacity);
	IdentityTypeId InternTypeKey(const IdentityTypeKey& key);
	void RehashTypes(std::size_t capacity);

	InternedStringTable names_;
	std::vector<IdentityPathKey> path_records_;
	std::vector<IdentityPathId> path_slots_;
	std::vector<IdentityTypeKey> type_records_;
	std::vector<IdentityTypeId> type_slots_;
	std::vector<NameId> path_scratch_;
};

struct SymbolIdentity
{
	Symbol::Kind kind;
	IdentityPathId path;
	IdentityTypeId signature;
	IdentityTypeId template_arguments;
	IdentityTypeId owner_template_arguments;
	std::size_t internal_owner;

	bool operator==(const SymbolIdentity& other) const
	{
		return kind == other.kind && path == other.path &&
			signature == other.signature &&
			template_arguments == other.template_arguments &&
			owner_template_arguments == other.owner_template_arguments &&
			internal_owner == other.internal_owner;
	}
};

struct SymbolIdentityHash
{
	std::size_t operator()(const SymbolIdentity& key) const
	{
		return static_cast<std::size_t>(key.kind) * 16777619U ^
			key.path * 257U ^ key.signature * 17U ^
			key.template_arguments * 65537U ^
			key.owner_template_arguments * 4099U ^ key.internal_owner;
	}
};

class SymbolIdentityTable
{
public:
	SymbolIdentityTable();

	bool Find(const SymbolIdentity& key, SymbolId* symbol) const;
	void Insert(const SymbolIdentity& key, SymbolId symbol);
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);

	std::vector<SymbolIdentity> keys_;
	std::vector<SymbolId> symbols_;
	std::vector<SymbolId> slots_;
};

class StringCounterTable
{
public:
	StringCounterTable();

	std::size_t& operator[](const std::string& spelling);
	void Clear();
	std::size_t StorageBytes() const;

private:
	InternedStringTable names_;
	std::vector<std::size_t> values_;
};

struct TypedProgram
{
	std::vector<Symbol> symbols;
	std::vector<GlobalDeclaration> global_declarations;
	std::vector<FunctionDeclaration> declarations;
	std::vector<Global> globals;
	std::vector<Function> functions;
	std::vector<Operand> call_arguments;
	std::vector<std::uint8_t> call_argument_references;
	std::vector<std::int64_t> switch_case_values;
	std::vector<BlockId> switch_case_targets;
	InternedStringTable literals;
	std::vector<SymbolId> string_literal_symbols;
	EmissionIdentityTable identities;
	SymbolIdentityTable symbol_index;
	StringCounterTable symbol_name_counts;
	std::size_t string_literal_count;

	TypedProgram() : string_literal_count(0) {}
};

std::size_t TypedStorageBytes(const TypedProgram& program);

}
}
