#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "pa8_semantic.h"
#include "post_tokenizer.h"

namespace cppgm
{
namespace pa8
{

typedef std::uint32_t NameId;
typedef std::uint32_t TypeId;
typedef std::uint32_t ScopeId;
typedef std::uint32_t PathId;
typedef std::uint32_t BindingId;
typedef std::uint32_t EntityId;
typedef std::uint32_t CandidateId;
typedef std::uint32_t StringId;
typedef std::uint32_t TemporaryId;

const ScopeId kNoScope = std::numeric_limits<ScopeId>::max();
const BindingId kNoBinding = std::numeric_limits<BindingId>::max();
const EntityId kNoEntity = std::numeric_limits<EntityId>::max();
const CandidateId kNoCandidate = std::numeric_limits<CandidateId>::max();
const StringId kNoString = std::numeric_limits<StringId>::max();
const TemporaryId kNoTemporary = std::numeric_limits<TemporaryId>::max();

std::size_t MixHash(std::size_t seed, std::uint64_t value);

class IdentifierTable
{
public:
	IdentifierTable();
	NameId Intern(const std::string& spelling);
	const std::string& Get(NameId id) const;
	std::size_t Size() const;
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);
	std::vector<std::string> spellings_;
	std::vector<NameId> slots_;
};

enum TypeKind
{
	TYPE_INVALID,
	TYPE_FUNDAMENTAL,
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
	std::uint64_t bound;
	std::uint32_t parameter_offset;
	std::uint32_t parameter_count;
	unsigned char cv;
	bool variadic;
	FundamentalType fundamental;

	TypeRecord();
};

class TypeTable
{
public:
	TypeTable();
	TypeId Fundamental(FundamentalType fundamental);
	TypeId Pointer(TypeId child);
	TypeId Reference(TypeKind kind, TypeId child, bool collapse_allowed);
	TypeId Array(TypeId child, std::uint64_t bound);
	TypeId Function(TypeId result, const std::vector<TypeId>& parameters,
		bool variadic);
	TypeId Qualify(TypeId type, unsigned char cv);
	TypeId AddTopConst(TypeId type);
	TypeId AdjustParameter(TypeId type);
	TypeId MergeRedeclaration(TypeId first, TypeId second);
	TypeId CompleteArray(TypeId type, std::uint64_t bound);
	TypeId RemoveTopCv(TypeId type) const;
	TypeId Referred(TypeId type) const;
	const TypeRecord& Get(TypeId type) const;
	bool IsFunction(TypeId type) const;
	bool IsReference(TypeId type) const;
	bool IsArray(TypeId type) const;
	bool IsPointer(TypeId type) const;
	bool IsVoid(TypeId type) const;
	bool IsConst(TypeId type) const;
	bool SameFunctionSignature(TypeId left, TypeId right) const;
	bool QualificationConvertible(TypeId source, TypeId destination) const;
	std::size_t SizeOf(TypeId type) const;
	std::size_t AlignOf(TypeId type) const;
	std::size_t Size() const;
	std::size_t StorageBytes() const;

private:
	TypeId Unary(TypeKind kind, TypeId child);
	TypeId Intern(TypeRecord candidate, const TypeId* parameters,
		std::size_t parameter_count);
	std::size_t Hash(const TypeRecord& record, const TypeId* parameters,
		std::size_t parameter_count) const;
	bool Equal(const TypeRecord& existing, const TypeRecord& candidate,
		const TypeId* parameters, std::size_t parameter_count) const;
	void Rehash(std::size_t capacity);
	std::vector<TypeRecord> types_;
	std::vector<TypeId> parameters_;
	std::vector<TypeId> slots_;
};

struct QualifiedName
{
	bool absolute;
	std::vector<NameId> segments;
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
	ScopeId owner;
	NameId name;
	TypeId type;
	ScopeId name_space;
	EntityId variable;
	CandidateId first_function;
	CandidateId last_function;
	bool namespace_alias;

	Binding(ScopeId owner_value = 0, NameId name_value = 0);
};

struct ScopeRecord
{
	ScopeId parent;
	PathId path;
	NameId name;
	ScopeId unnamed_child;
	std::vector<ScopeId> using_targets;
	bool is_inline;
	bool internal_context;
	std::uint32_t translation_unit;

	ScopeRecord(ScopeId parent_value, PathId path_value, NameId name_value,
		bool inline_value, bool internal_value, std::uint32_t unit);
};

struct EntityRecord
{
	NameId name;
	TypeId type;
	ScopeId owner;
	Linkage linkage;
	std::uint32_t first_ordinal;
	std::uint32_t definition_unit;
	std::uint32_t declaration_count;
	InitialValue initial;
	std::uint64_t image_offset;
	bool function;
	bool defined;
	bool declared_inline;
	bool has_thread_storage;
	bool constexpr_declared;
	bool constant_usable;

	EntityRecord(NameId name_value, TypeId type_value, ScopeId owner_value,
		Linkage linkage_value, std::uint32_t ordinal, bool function_value);
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

class ProgramModel
{
public:
	explicit ProgramModel(InitializationStats* stats);
	ScopeId NewTranslationUnit();
	ScopeId OpenNamespace(ScopeId parent, NameId name, bool is_inline);
	void AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target);
	void AddUsingDirective(ScopeId owner, ScopeId target);
	void AddUsingDeclaration(ScopeId owner, NameId name,
		const Binding& target);
	void AddTypeAlias(ScopeId owner, NameId name, TypeId type);
	ScopeId ResolveDeclaratorOwner(ScopeId current,
		const QualifiedName& name) const;
	bool ResolveNamespaceName(ScopeId current, const QualifiedName& name,
		ScopeId* result) const;
	bool ResolveTypeName(ScopeId current, const QualifiedName& name,
		TypeId* result) const;
	const Binding* ResolveUsingTarget(ScopeId current,
		const QualifiedName& name) const;
	EntityId ResolveExpressionEntity(ScopeId current,
		const QualifiedName& name) const;
	EntityId Declare(ScopeId current, const Declarator& declarator,
		TypeId type, const DeclarationSpecifiers& specifiers,
		bool definition, bool function_definition, std::uint32_t unit);
	void Define(EntityId entity, TypeId completed_type,
		const InitialValue& initial, bool constant_usable,
		std::uint32_t unit);
	Expression ExpressionForEntity(EntityId entity) const;
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

	IdentifierTable identifiers;
	TypeTable types;
	InitializationStats* stats;
	std::vector<unsigned char> retained_bytes;

private:
	Binding* FindDirect(ScopeId owner, NameId name);
	const Binding* FindDirect(ScopeId owner, NameId name) const;
	Binding& EnsureBinding(ScopeId owner, NameId name);
	const Binding* SearchScopeGraph(ScopeId start, NameId name) const;
	const Binding* LookupUnqualified(ScopeId current, NameId name) const;
	ScopeId ResolvePrefix(ScopeId current, const QualifiedName& name) const;
	PathId InternPath(PathId parent, NameId name);
	EntityId FindFunction(const Binding& binding, TypeId type) const;
	void AddFunctionCandidate(Binding& binding, EntityId entity);
	EntityId FindExternal(PathId path, NameId name, TypeId type,
		bool function) const;
	void AddExternal(PathId path, NameId name, EntityId entity);
	bool ScopeEncloses(ScopeId enclosing, ScopeId nested) const;
	InitialValue ResolveReferenceAddress(EntityId entity) const;
	InitialValue ResolveObjectValue(EntityId entity, bool* constant) const;
	std::uint64_t ResolveAddress(const InitialValue& value) const;

	std::vector<ScopeRecord> scopes_;
	std::vector<Binding> bindings_;
	std::vector<std::uint32_t> binding_slots_;
	std::vector<CandidateLink> candidates_;
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
	mutable std::uint32_t lookup_generation_;
	std::uint32_t current_unit_;
	std::uint32_t declaration_ordinal_;
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

	explicit Token(std::uint16_t kind_value);
};

struct TokenBuffer
{
	std::vector<Token> tokens;
	std::vector<unsigned char> bytes;
};

void ParseTranslationUnit(const TokenBuffer& input, ProgramModel& model,
	ScopeId root, std::uint32_t unit);

bool IsIntegralFundamental(FundamentalType type);
bool IsFloatingFundamental(FundamentalType type);
bool IsUnsignedFundamental(FundamentalType type);
std::size_t FundamentalSize(FundamentalType type);
long double ReadArithmetic(const InitialValue& value);
InitialValue ConvertArithmetic(const InitialValue& source,
	FundamentalType destination);

}
}
