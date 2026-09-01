#pragma once

#include "lowering/ir/types.h"

namespace cppgm
{
namespace lowering
{
namespace ir
{

using namespace semantic;

const char* LowOperationText(LowOperation operation);

typedef std::uint32_t IdentityNameId;
typedef std::uint32_t IdentityPathId;
typedef std::uint32_t IdentityTypeId;

enum IdentityPathComponentKind : std::uint8_t
{
	IDENTITY_PATH_NAME,
	IDENTITY_PATH_LAMBDA
};

struct IdentityPathKey
{
	IdentityPathId parent;
	std::uint32_t value;
	IdentityPathComponentKind kind;

	bool operator==(const IdentityPathKey& other) const
	{
		return parent == other.parent && value == other.value &&
			kind == other.kind;
	}
};

struct IdentityPathHash
{
	std::size_t operator()(const IdentityPathKey& key) const
	{
		return (static_cast<std::size_t>(key.parent) * 16777619U ^
			key.value) * 257U ^ key.kind;
	}
};

struct IdentityTypeKey
{
	TypeKind kind;
	FundamentalKind fundamental;
	IdentityTypeId child;
	IdentityPathId named;
	IdentityPathId local_context;
	IdentityTypeId local_context_signature;
	std::uint32_t local_ordinal;
	std::uint64_t bound;
	std::uint8_t cv;
	std::uint8_t ref_qualifier;
	bool variadic;
	bool zero_length_array;
	std::vector<IdentityTypeId> parameters;

	IdentityTypeKey()
		: kind(TYPE_FUNDAMENTAL), fundamental(FUND_VOID), child(kNoLowId),
		  named(kNoLowId), local_context(kNoLowId),
		  local_context_signature(kNoLowId), local_ordinal(0), bound(0), cv(0),
		  ref_qualifier(FUNCTION_REF_NONE), variadic(false),
		  zero_length_array(false) {}

	bool operator==(const IdentityTypeKey& other) const
	{
		return kind == other.kind && fundamental == other.fundamental &&
			child == other.child && named == other.named &&
			local_context == other.local_context &&
			local_context_signature == other.local_context_signature &&
			local_ordinal == other.local_ordinal &&
			bound == other.bound &&
			cv == other.cv && ref_qualifier == other.ref_qualifier &&
			variadic == other.variadic &&
			zero_length_array == other.zero_length_array &&
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
		hash = hash * 16777619U ^ key.local_context;
		hash = hash * 16777619U ^ key.local_context_signature;
		hash = hash * 16777619U ^ key.local_ordinal;
		hash = hash * 16777619U ^ static_cast<std::size_t>(key.bound);
		hash = hash * 16777619U ^ key.cv;
		hash = hash * 16777619U ^ key.ref_qualifier;
		hash = hash * 16777619U ^ key.variadic;
		hash = hash * 16777619U ^ key.zero_length_array;
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
	void UseDirectNames(bool enabled);

	IdentityPathId InternPath(const semantic::Program& program, ScopeId owner,
		NameId terminal);
	IdentityPathId InternEntityPath(const semantic::Program& program, EntityId entity);
	IdentityPathId InternClassMemberPath(const semantic::Program& program,
		EntityId owner, NameId terminal);
	IdentityTypeId InternType(const semantic::Program& program, TypeId type,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternFunctionSignature(const semantic::Program& program, TypeId type,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternTypeSequence(const semantic::Program& program,
		const TypeId* types, std::size_t count,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternBindingTemplateArguments(const semantic::Program& program,
		const BindingRecord& binding, std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternLambdaContextIdentity(const semantic::Program& program,
		EntityId entity, std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternEntityTemplateArguments(const semantic::Program& program,
		const EntityRecord& entity, std::vector<IdentityTypeId>& cache);
	std::size_t StorageBytes() const;
	std::size_t PathCount() const;
	std::size_t TypeCount() const;

private:
	IdentityNameId InternName(const semantic::Program& program, NameId name);
	IdentityPathId InternScopePath(const semantic::Program& program, ScopeId owner);
	static bool HasChild(TypeKind kind);
	static void PushDependency(TypeId dependency,
		std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending);
	static void PushTypeDependencies(const semantic::Program& program,
		const TypeRecord& source, TypeId type,
		std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending);
	IdentityTypeKey MakeTypeKey(const semantic::Program& program,
		const TypeRecord& source, TypeId type,
		std::vector<IdentityTypeId>& cache);
	IdentityTypeId InternStoredTemplateArgument(const semantic::Program& program,
		std::size_t argument, const std::vector<IdentityTypeId>& cache);
	IdentityPathId InternPathKey(const IdentityPathKey& key);
	void RehashPaths(std::size_t capacity);
	IdentityTypeId InternTypeKey(const IdentityTypeKey& key);
	void RehashTypes(std::size_t capacity);

	InternedStringTable names_;
	std::vector<IdentityPathKey> path_records_;
	std::vector<IdentityPathId> path_slots_;
	std::vector<IdentityTypeKey> type_records_;
	std::vector<IdentityTypeId> type_slots_;
	std::vector<ScopeId> scope_scratch_;
	bool direct_names_;
};

struct SymbolIdentity
{
	Symbol::Kind kind;
	IdentityPathId path;
	IdentityTypeId signature;
	IdentityTypeId template_arguments;
	IdentityTypeId owner_template_arguments;
	// Complete and lifecycle base entries share a source path and signature,
	// so the lifecycle role participates in symbol equality directly instead
	// of being encoded into a synthetic terminal spelling.
	std::uint32_t lifecycle_role;
	std::size_t internal_owner;

	bool operator==(const SymbolIdentity& other) const
	{
		return kind == other.kind && path == other.path &&
			signature == other.signature &&
			template_arguments == other.template_arguments &&
			owner_template_arguments == other.owner_template_arguments &&
			lifecycle_role == other.lifecycle_role &&
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
			key.owner_template_arguments * 4099U ^
			key.lifecycle_role * 40503U ^ key.internal_owner;
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

struct ObjectAlias
{
	lowir_model::StringId object_name;
	SymbolId target;

	ObjectAlias(lowir_model::StringId object_name_value, SymbolId target_value)
		: object_name(object_name_value), target(target_value) {}
};

struct Program
{
	std::vector<Symbol> symbols;
	std::vector<GlobalDeclaration> global_declarations;
	std::vector<FunctionDeclaration> declarations;
	std::vector<Global> globals;
	std::vector<Function> functions;
	std::vector<ObjectAlias> object_aliases;
	std::vector<Operand> call_arguments;
	std::vector<std::uint8_t> call_argument_references;
	std::vector<std::uint32_t> call_argument_object_bytes;
	std::vector<SymbolId> exception_filter_types;
	std::vector<std::int64_t> switch_case_values;
	std::vector<BlockId> switch_case_targets;
	lowir_model::StringPool strings;
	std::vector<SymbolId> string_literal_symbols;
	EmissionIdentityTable identities;
	SymbolIdentityTable symbol_index;
	std::vector<std::uint32_t> symbol_name_counts;
	std::size_t string_literal_count;
	SymbolId terminate_runtime_symbol, terminate_helper_symbol;
	SymbolId call_unexpected_symbol;
	bool host_object_emission;
	bool retain_local_names;

	Program()
		: string_literal_count(0), terminate_runtime_symbol(kNoLowId),
		  terminate_helper_symbol(kNoLowId), call_unexpected_symbol(kNoLowId),
		  host_object_emission(false), retain_local_names(true) {}

	lowir_model::StringId InternUniqueSymbolName(
		const std::string& proposed);
};

inline lowir_model::StringId InternLocalName(
	Program& program, const std::string& name)
{
	return program.retain_local_names ? program.strings.intern(name) :
		lowir_model::StringId();
}

std::size_t ProgramStorageBytes(const Program& program);

}
}
}
