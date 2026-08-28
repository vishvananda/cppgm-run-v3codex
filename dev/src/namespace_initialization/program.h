#pragma once

#include "namespace_initialization/internal.h"

namespace cppgm
{
namespace namespace_initialization
{

class Model
{
public:
	explicit Model(Stats* stats);
	ScopeId NewTranslationUnit();
	ScopeId OpenNamespace(ScopeId parent, NameId name, bool is_inline);
	void AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target);
	void AddUsingDirective(ScopeId owner, ScopeId target);
	void AddUsingDeclaration(ScopeId owner, NameId name,
		const LookupResult& target);
	void AddTypeAlias(ScopeId owner, NameId name, TypeId type);
	ScopeId ResolveDeclaratorOwner(ScopeId current,
		const QualifiedName& name);
	bool ResolveNamespaceName(ScopeId current, const QualifiedName& name,
		ScopeId* result);
	bool ResolveTypeName(ScopeId current, const QualifiedName& name,
		TypeId* result);
	bool ResolveUsingTarget(ScopeId current, const QualifiedName& name,
		LookupResult* result);
	EntityId ResolveExpressionEntity(ScopeId current,
		const QualifiedName& name, LookupResult* result);
	EntityId Declare(ScopeId current, const Declarator& declarator,
		TypeId type, const DeclarationSpecifiers& specifiers,
		bool definition, bool function_definition, std::uint32_t unit);
	void Define(EntityId entity, TypeId completed_type,
		const InitialValue& initial, bool constant_initialized,
		bool constant_usable, std::uint32_t unit);
	Expression ExpressionForEntity(EntityId entity,
		std::uint32_t translation_unit) const;
	InitialValue LvalueAddress(const Expression& expression) const;
	InitialValue LvalueToRvalue(const Expression& expression,
		bool* constant) const;
	StringId AddString(FundamentalType type, const unsigned char* bytes,
		std::size_t size);
	TemporaryId AddTemporary(TypeId type, const InitialValue& initial);
	InitialValue ConvertInitializer(TypeId* destination,
		const Expression& source, bool* constant);
	bool ContextualBool(const Expression& expression, bool* constant) const;
	void WriteImage(std::ostream& output);
	void FinishStats();
	std::uint32_t CurrentUnit() const;
	std::size_t StorageBytes() const;

	IdentifierTable identifiers;
	TypeTable types;
	Stats* stats;
	std::vector<unsigned char> retained_bytes;

private:
	Binding* FindDirect(ScopeId owner, NameId name);
	const Binding* FindDirect(ScopeId owner, NameId name) const;
	Binding& EnsureBinding(ScopeId owner, NameId name);
	LookupResult DirectLookup(ScopeId owner, NameId name,
		LookupKind kind) const;
	LookupResult SearchScopeGraph(ScopeId start, NameId name,
		LookupKind kind);
	LookupResult LookupUnqualified(ScopeId current, NameId name,
		LookupKind kind);
	ScopeId ResolvePrefix(ScopeId current, const QualifiedName& name);
	PathId InternPath(PathId parent, NameId name);
	EntityId FindFunction(const Binding& binding, TypeId type) const;
	void AddFunctionCandidate(Binding& binding, EntityId entity);
	void AddUsingEdge(ScopeId owner, ScopeId target);
	void BeginScopeWalk(ScopeId start);
	void AddWalkTarget(ScopeId target);
	void InvalidateLookupGraph(ScopeId changed);
	void InvalidateLookupName(ScopeId changed, NameId name);
	void AdvanceLookupGeneration(ScopeId scope);
	LookupResult MergeLookupResults(LookupKind kind,
		const std::vector<LookupResult>& results);
	std::uint32_t FindLookupCache(ScopeId start, NameId name,
		LookupKind kind) const;
	void StoreLookupCache(ScopeId start, NameId name, LookupKind kind,
		const LookupResult& result);
	void RehashLookupCache(std::size_t capacity);
	void RehashUsingEdges(std::size_t capacity);
	EntityId FindExternal(PathId path, NameId name, TypeId type,
		bool function) const;
	void AddExternal(PathId path, NameId name, EntityId entity);
	bool ScopeEncloses(ScopeId enclosing, ScopeId nested) const;
	InitialValue ResolveReferenceAddress(EntityId entity) const;
	InitialValue ResolveObjectValue(EntityId entity,
		std::uint32_t translation_unit, bool* constant) const;
	std::uint64_t ResolveAddress(const InitialValue& value) const;

	std::vector<ScopeRecord> scopes_;
	std::vector<Binding> bindings_;
	std::vector<std::uint32_t> binding_slots_;
	std::vector<CandidateLink> candidates_;
	std::vector<UsingEdgeRecord> using_edges_;
	std::vector<std::uint32_t> using_edge_slots_;
	std::vector<LookupCacheEntry> lookup_cache_entries_;
	std::vector<std::uint32_t> lookup_cache_slots_;
	std::vector<std::uint32_t> scope_lookup_generations_;
	std::vector<EntityRecord> entities_;
	std::vector<std::uint32_t> external_slots_;
	std::vector<EntityId> external_entities_;
	std::vector<PathId> path_parents_;
	std::vector<NameId> path_names_;
	std::vector<PathId> path_slots_;
	std::vector<StringRecord> strings_;
	std::vector<TemporaryRecord> temporaries_;
	mutable std::vector<std::uint32_t> lookup_marks_;
	mutable std::vector<ScopeId> lookup_worklist_;
	std::vector<LookupResult> lookup_results_;
	std::vector<std::uint32_t> candidate_marks_;
	mutable std::uint32_t lookup_generation_;
	std::uint32_t candidate_generation_;
	std::uint32_t current_unit_;
	bool image_written_;
};

struct Token
{
	std::uint16_t kind;
	NameId name;
	FundamentalType literal_type;
	std::uint32_t byte_offset;
	std::uint32_t byte_size;
	std::uint32_t elements;
	bool literal_array;
	bool integer_literal;

	explicit Token(std::uint16_t kind_value);
};

struct TokenBuffer
{
	std::vector<Token> tokens;
	std::vector<unsigned char> bytes;
};

void ParseTranslationUnit(const TokenBuffer& input, Model& model,
	ScopeId root, std::uint32_t unit);

}
}
