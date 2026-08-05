#include "pa8_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ostream>
#include <stdexcept>

namespace cppgm
{
namespace pa8
{

namespace
{

std::size_t HashSpelling(const std::string& spelling)
{
	std::size_t value = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1469598103934665603ULL) :
		static_cast<std::size_t>(2166136261U);
	const std::size_t prime = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1099511628211ULL) :
		static_cast<std::size_t>(16777619U);
	for (std::size_t i = 0; i < spelling.size(); ++i)
	{
		value ^= static_cast<unsigned char>(spelling[i]);
		value *= prime;
	}
	return value;
}

std::uint64_t Align(std::uint64_t offset, std::size_t alignment)
{
	if (alignment == 0) throw std::logic_error("zero alignment");
	const std::uint64_t remainder = offset % alignment;
	return remainder == 0 ? offset : offset + alignment - remainder;
}

bool IsCharacterType(FundamentalType type)
{
	return type == FT_CHAR || type == FT_SIGNED_CHAR ||
		type == FT_UNSIGNED_CHAR || type == FT_CHAR16_T ||
		type == FT_CHAR32_T || type == FT_WCHAR_T;
}

bool PointerTargetConvertible(const TypeTable& types, TypeId source,
	TypeId destination)
{
	if (types.QualificationConvertible(source, destination)) return true;
	unsigned char source_cv = CV_NONE;
	unsigned char destination_cv = CV_NONE;
	const TypeRecord* source_record = &types.Get(source);
	const TypeRecord* destination_record = &types.Get(destination);
	if (source_record->kind == TYPE_QUALIFIED)
	{
		source_cv = source_record->cv;
		source_record = &types.Get(source_record->child);
	}
	if (destination_record->kind == TYPE_QUALIFIED)
	{
		destination_cv = destination_record->cv;
		destination_record = &types.Get(destination_record->child);
	}
	return destination_record->kind == TYPE_FUNDAMENTAL &&
		destination_record->fundamental == FT_VOID &&
		source_record->kind != TYPE_FUNCTION &&
		(source_cv & ~destination_cv) == 0;
}

void StoreUnsigned(std::array<unsigned char, 16>* bytes,
	std::uint64_t value, std::size_t size)
{
	bytes->fill(0);
	for (std::size_t i = 0; i < size; ++i)
		(*bytes)[i] = static_cast<unsigned char>(value >> (i * 8));
}

}

std::size_t MixHash(std::size_t seed, std::uint64_t value)
{
	seed ^= static_cast<std::size_t>(value) +
		static_cast<std::size_t>(0x9e3779b9U) + (seed << 6) + (seed >> 2);
	return seed;
}

IdentifierTable::IdentifierTable() : slots_(16, 0)
{
	spellings_.push_back(std::string());
}

NameId IdentifierTable::Intern(const std::string& spelling)
{
	if ((spellings_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = HashSpelling(spelling) & mask;
	while (slots_[slot] != 0)
	{
		const NameId id = slots_[slot];
		if (spellings_[id] == spelling) return id;
		slot = (slot + 1) & mask;
	}
	if (spellings_.size() > std::numeric_limits<NameId>::max())
		throw std::runtime_error("too many identifiers");
	const NameId id = static_cast<NameId>(spellings_.size());
	spellings_.push_back(spelling);
	slots_[slot] = id;
	return id;
}

const std::string& IdentifierTable::Get(NameId id) const
{
	return spellings_[id];
}

std::size_t IdentifierTable::Size() const
{
	return spellings_.size() - 1;
}

std::size_t IdentifierTable::StorageBytes() const
{
	std::size_t bytes = spellings_.capacity() * sizeof(std::string) +
		slots_.capacity() * sizeof(NameId);
	for (std::size_t i = 1; i < spellings_.size(); ++i)
		bytes += spellings_[i].capacity();
	return bytes;
}

void IdentifierTable::Rehash(std::size_t capacity)
{
	std::vector<NameId> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (NameId id = 1; id < spellings_.size(); ++id)
	{
		std::size_t slot = HashSpelling(spellings_[id]) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = id;
	}
	slots_.swap(replacement);
}

TypeRecord::TypeRecord()
	: kind(TYPE_INVALID), child(0), bound(0), parameter_offset(0),
	  parameter_count(0), cv(0), variadic(false), fundamental(FT_INT)
{
}

TypeTable::TypeTable() : slots_(32, 0)
{
	types_.push_back(TypeRecord());
}

TypeId TypeTable::Fundamental(FundamentalType fundamental)
{
	TypeRecord candidate;
	candidate.kind = TYPE_FUNDAMENTAL;
	candidate.fundamental = fundamental;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Unary(TypeKind kind, TypeId child)
{
	TypeRecord candidate;
	candidate.kind = kind;
	candidate.child = child;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Pointer(TypeId child)
{
	if (IsReference(child))
		throw std::runtime_error("pointer to reference type");
	return Unary(TYPE_POINTER, child);
}

TypeId TypeTable::Reference(TypeKind kind, TypeId child,
	bool collapse_allowed)
{
	const TypeRecord& referred = Get(child);
	if (referred.kind == TYPE_LVALUE_REFERENCE ||
		referred.kind == TYPE_RVALUE_REFERENCE)
	{
		if (!collapse_allowed)
			throw std::runtime_error("direct reference to reference type");
		if (referred.kind == TYPE_LVALUE_REFERENCE ||
			kind == TYPE_LVALUE_REFERENCE)
			return Unary(TYPE_LVALUE_REFERENCE, referred.child);
		return Unary(TYPE_RVALUE_REFERENCE, referred.child);
	}
	if (IsVoid(child)) throw std::runtime_error("reference to void type");
	return Unary(kind, child);
}

TypeId TypeTable::Array(TypeId child, std::uint64_t bound)
{
	const TypeRecord& element = Get(child);
	if (element.kind == TYPE_LVALUE_REFERENCE ||
		element.kind == TYPE_RVALUE_REFERENCE || element.kind == TYPE_FUNCTION ||
		IsVoid(child)) throw std::runtime_error("invalid array element type");
	TypeRecord candidate;
	candidate.kind = TYPE_ARRAY;
	candidate.child = child;
	candidate.bound = bound;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Function(TypeId result,
	const std::vector<TypeId>& parameters, bool variadic)
{
	const TypeRecord& returned = Get(result);
	if (returned.kind == TYPE_ARRAY || returned.kind == TYPE_FUNCTION)
		throw std::runtime_error("invalid function return type");
	TypeRecord candidate;
	candidate.kind = TYPE_FUNCTION;
	candidate.child = result;
	candidate.parameter_count =
		static_cast<std::uint32_t>(parameters.size());
	candidate.variadic = variadic;
	return Intern(candidate, parameters.empty() ? 0 : &parameters[0],
		parameters.size());
}

TypeId TypeTable::Qualify(TypeId type, unsigned char cv)
{
	if (cv == CV_NONE) return type;
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_ARRAY)
		return Array(Qualify(record.child, cv), record.bound);
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE) return type;
	if (record.kind == TYPE_FUNCTION)
		throw std::runtime_error("cv-qualified function type");
	if (record.kind == TYPE_QUALIFIED)
		return Qualify(record.child, record.cv | cv);
	TypeRecord candidate;
	candidate.kind = TYPE_QUALIFIED;
	candidate.child = type;
	candidate.cv = cv;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::AddTopConst(TypeId type)
{
	return Qualify(type, CV_CONST);
}

TypeId TypeTable::AdjustParameter(TypeId type)
{
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_ARRAY) return Pointer(record.child);
	if (record.kind == TYPE_FUNCTION) return Pointer(type);
	if (record.kind == TYPE_QUALIFIED) return record.child;
	return type;
}

TypeId TypeTable::MergeRedeclaration(TypeId first, TypeId second)
{
	if (first == second) return first;
	const TypeRecord& left = Get(first);
	const TypeRecord& right = Get(second);
	if (left.kind != TYPE_ARRAY || right.kind != TYPE_ARRAY)
		throw std::runtime_error("incompatible redeclaration");
	const TypeId child = MergeRedeclaration(left.child, right.child);
	if (left.bound != 0 && right.bound != 0 && left.bound != right.bound)
		throw std::runtime_error("incompatible array bounds");
	return Array(child, left.bound == 0 ? right.bound : left.bound);
}

TypeId TypeTable::CompleteArray(TypeId type, std::uint64_t bound)
{
	const TypeRecord& record = Get(type);
	if (record.kind != TYPE_ARRAY) throw std::logic_error("not an array");
	if (record.bound != 0 && record.bound != bound)
		throw std::runtime_error("array initializer bound mismatch");
	return record.bound == 0 ? Array(record.child, bound) : type;
}

TypeId TypeTable::RemoveTopCv(TypeId type) const
{
	const TypeRecord& record = Get(type);
	return record.kind == TYPE_QUALIFIED ? record.child : type;
}

TypeId TypeTable::Referred(TypeId type) const
{
	const TypeRecord& record = Get(type);
	if (record.kind != TYPE_LVALUE_REFERENCE &&
		record.kind != TYPE_RVALUE_REFERENCE)
		throw std::logic_error("not a reference type");
	return record.child;
}

const TypeRecord& TypeTable::Get(TypeId type) const
{
	return types_[type];
}

bool TypeTable::IsFunction(TypeId type) const
{
	return Get(type).kind == TYPE_FUNCTION;
}

bool TypeTable::IsReference(TypeId type) const
{
	const TypeKind kind = Get(type).kind;
	return kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE;
}

bool TypeTable::IsArray(TypeId type) const
{
	return Get(type).kind == TYPE_ARRAY;
}

bool TypeTable::IsPointer(TypeId type) const
{
	return Get(RemoveTopCv(type)).kind == TYPE_POINTER;
}

bool TypeTable::IsVoid(TypeId type) const
{
	type = RemoveTopCv(type);
	const TypeRecord& record = Get(type);
	return record.kind == TYPE_FUNDAMENTAL && record.fundamental == FT_VOID;
}

bool TypeTable::IsConst(TypeId type) const
{
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_QUALIFIED) return (record.cv & CV_CONST) != 0;
	if (record.kind == TYPE_ARRAY) return IsConst(record.child);
	return false;
}

bool TypeTable::SameFunctionSignature(TypeId left, TypeId right) const
{
	const TypeRecord& a = Get(left);
	const TypeRecord& b = Get(right);
	if (a.kind != TYPE_FUNCTION || b.kind != TYPE_FUNCTION ||
		a.parameter_count != b.parameter_count || a.variadic != b.variadic)
		return false;
	for (std::uint32_t i = 0; i < a.parameter_count; ++i)
	{
		if (parameters_[a.parameter_offset + i] !=
			parameters_[b.parameter_offset + i]) return false;
	}
	return true;
}

bool TypeTable::QualificationConvertible(TypeId source,
	TypeId destination) const
{
	unsigned char source_cv = CV_NONE;
	unsigned char destination_cv = CV_NONE;
	const TypeRecord* left = &Get(source);
	const TypeRecord* right = &Get(destination);
	if (left->kind == TYPE_QUALIFIED)
	{
		source_cv = left->cv;
		left = &Get(left->child);
	}
	if (right->kind == TYPE_QUALIFIED)
	{
		destination_cv = right->cv;
		right = &Get(right->child);
	}
	if ((source_cv & ~destination_cv) != 0) return false;
	if (left->kind != right->kind) return false;
	if (left->kind == TYPE_POINTER)
		return QualificationConvertible(left->child, right->child);
	return RemoveTopCv(source) == RemoveTopCv(destination);
}

std::size_t FundamentalSize(FundamentalType type)
{
	switch (type)
	{
	case FT_SIGNED_CHAR: case FT_UNSIGNED_CHAR: case FT_CHAR: case FT_BOOL:
		return 1;
	case FT_SHORT_INT: case FT_UNSIGNED_SHORT_INT: case FT_CHAR16_T:
		return 2;
	case FT_INT: case FT_UNSIGNED_INT: case FT_WCHAR_T: case FT_CHAR32_T:
	case FT_FLOAT:
		return 4;
	case FT_LONG_INT: case FT_LONG_LONG_INT: case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT: case FT_DOUBLE: case FT_NULLPTR_T:
		return 8;
	case FT_LONG_DOUBLE:
		return 16;
	case FT_VOID:
		break;
	}
	throw std::runtime_error("incomplete fundamental type");
}

std::size_t TypeTable::SizeOf(TypeId type) const
{
	const TypeRecord& record = Get(type);
	switch (record.kind)
	{
	case TYPE_QUALIFIED: return SizeOf(record.child);
	case TYPE_FUNDAMENTAL: return FundamentalSize(record.fundamental);
	case TYPE_POINTER: case TYPE_LVALUE_REFERENCE: case TYPE_RVALUE_REFERENCE:
		return 8;
	case TYPE_ARRAY:
		if (record.bound == 0) throw std::runtime_error("incomplete array type");
		return static_cast<std::size_t>(record.bound) * SizeOf(record.child);
	case TYPE_FUNCTION: return 4;
	default: break;
	}
	throw std::runtime_error("invalid complete type");
}

std::size_t TypeTable::AlignOf(TypeId type) const
{
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_ARRAY)
		return AlignOf(record.child);
	if (record.kind == TYPE_FUNCTION) return 4;
	return SizeOf(type);
}

std::size_t TypeTable::Size() const
{
	return types_.size() - 1;
}

std::size_t TypeTable::StorageBytes() const
{
	return types_.capacity() * sizeof(TypeRecord) +
		parameters_.capacity() * sizeof(TypeId) +
		slots_.capacity() * sizeof(TypeId);
}

std::size_t TypeTable::Hash(const TypeRecord& record,
	const TypeId* parameters, std::size_t parameter_count) const
{
	std::size_t hash = MixHash(0, record.kind);
	hash = MixHash(hash, record.child);
	hash = MixHash(hash, record.bound);
	hash = MixHash(hash, record.cv);
	hash = MixHash(hash, record.variadic ? 1 : 0);
	hash = MixHash(hash, record.fundamental);
	for (std::size_t i = 0; i < parameter_count; ++i)
		hash = MixHash(hash, parameters[i]);
	return hash;
}

bool TypeTable::Equal(const TypeRecord& existing,
	const TypeRecord& candidate, const TypeId* parameters,
	std::size_t parameter_count) const
{
	if (existing.kind != candidate.kind || existing.child != candidate.child ||
		existing.bound != candidate.bound || existing.cv != candidate.cv ||
		existing.variadic != candidate.variadic ||
		existing.fundamental != candidate.fundamental ||
		existing.parameter_count != parameter_count) return false;
	for (std::size_t i = 0; i < parameter_count; ++i)
	{
		if (parameters_[existing.parameter_offset + i] != parameters[i])
			return false;
	}
	return true;
}

TypeId TypeTable::Intern(TypeRecord candidate, const TypeId* parameters,
	std::size_t parameter_count)
{
	if ((types_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(candidate, parameters, parameter_count) & mask;
	while (slots_[slot] != 0)
	{
		const TypeId id = slots_[slot];
		if (Equal(types_[id], candidate, parameters, parameter_count)) return id;
		slot = (slot + 1) & mask;
	}
	if (types_.size() > std::numeric_limits<TypeId>::max())
		throw std::runtime_error("too many canonical types");
	candidate.parameter_offset = static_cast<std::uint32_t>(parameters_.size());
	if (parameter_count != 0)
		parameters_.insert(parameters_.end(), parameters,
			parameters + parameter_count);
	const TypeId id = static_cast<TypeId>(types_.size());
	types_.push_back(candidate);
	slots_[slot] = id;
	return id;
}

void TypeTable::Rehash(std::size_t capacity)
{
	std::vector<TypeId> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (TypeId id = 1; id < types_.size(); ++id)
	{
		const TypeRecord& record = types_[id];
		const TypeId* parameters = record.parameter_count == 0 ? 0 :
			&parameters_[record.parameter_offset];
		std::size_t slot = Hash(record, parameters,
			record.parameter_count) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = id;
	}
	slots_.swap(replacement);
}

QualifiedName::QualifiedName() : absolute(false) {}

InitialValue::InitialValue()
	: kind(INITIAL_ZERO), scalar_type(FT_INT), target(0), byte_offset(0),
	  byte_size(0), addend(0)
{
	bytes.fill(0);
}

Expression::Expression()
	: type(0), category(VALUE_PRVALUE), entity(kNoEntity),
	  constant_expression(false), null_pointer_constant(false),
	  string_literal(false), string_id(kNoString)
{
}

CandidateLink::CandidateLink(EntityId value, CandidateId next_value)
	: entity(value), next(next_value)
{
}

Binding::Binding(ScopeId owner_value, NameId name_value)
	: owner(owner_value), name(name_value), type(0), name_space(kNoScope),
	  variable(kNoEntity), first_function(kNoCandidate),
	  last_function(kNoCandidate), namespace_alias(false)
{
}

ScopeRecord::ScopeRecord(ScopeId parent_value, PathId path_value,
	NameId name_value, bool inline_value, bool internal_value,
	std::uint32_t unit)
	: parent(parent_value), path(path_value), name(name_value),
	  unnamed_child(kNoScope), is_inline(inline_value),
	  internal_context(internal_value), translation_unit(unit)
{
}

EntityRecord::EntityRecord(NameId name_value, TypeId type_value,
	ScopeId owner_value, Linkage linkage_value, std::uint32_t ordinal,
	bool function_value)
	: name(name_value), type(type_value), owner(owner_value),
	  linkage(linkage_value), first_ordinal(ordinal),
	  definition_unit(std::numeric_limits<std::uint32_t>::max()),
	  declaration_count(0),
	  image_offset(std::numeric_limits<std::uint64_t>::max()),
	  function(function_value), defined(false), declared_inline(false),
	  has_thread_storage(false), constexpr_declared(false),
	  constant_usable(false)
{
}

DeclarationSpecifiers::DeclarationSpecifiers()
	: type(0), is_typedef(false), is_static(false), is_extern(false),
	  is_thread_local(false), is_constexpr(false), is_inline(false)
{
}

DeclaratorOperation::DeclaratorOperation(TypeKind kind_value)
	: kind(kind_value), cv(CV_NONE), bound(0), variadic(false)
{
}

Declarator::Declarator()
	: resolved_owner(kNoScope), has_name(false),
	  has_function_operation(false)
{
}

ProgramModel::ProgramModel(InitializationStats* stats_value)
	: stats(stats_value), binding_slots_(32, 0), external_slots_(32, 0),
	  path_slots_(16, 0), lookup_generation_(0), current_unit_(0),
	  declaration_ordinal_(0), image_written_(false)
{
	path_parents_.push_back(0);
	path_names_.push_back(0);
}

ScopeId ProgramModel::NewTranslationUnit()
{
	if (scopes_.size() >= kNoScope)
		throw std::runtime_error("too many scopes");
	const ScopeId id = static_cast<ScopeId>(scopes_.size());
	scopes_.push_back(ScopeRecord(kNoScope, 0, 0, false, false,
		current_unit_));
	++current_unit_;
	return id;
}

Binding* ProgramModel::FindDirect(ScopeId owner, NameId name)
{
	const ProgramModel* self = this;
	return const_cast<Binding*>(self->FindDirect(owner, name));
}

const Binding* ProgramModel::FindDirect(ScopeId owner, NameId name) const
{
	const std::size_t mask = binding_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(0, owner), name) & mask;
	while (binding_slots_[slot] != 0)
	{
		const Binding& binding = bindings_[binding_slots_[slot] - 1];
		if (binding.owner == owner && binding.name == name) return &binding;
		slot = (slot + 1) & mask;
	}
	return 0;
}

Binding& ProgramModel::EnsureBinding(ScopeId owner, NameId name)
{
	if ((bindings_.size() + 1) * 10 > binding_slots_.size() * 7)
	{
		std::vector<std::uint32_t> replacement(binding_slots_.size() * 2, 0);
		const std::size_t mask = replacement.size() - 1;
		for (std::uint32_t i = 0; i < bindings_.size(); ++i)
		{
			std::size_t slot = MixHash(MixHash(0, bindings_[i].owner),
				bindings_[i].name) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = i + 1;
		}
		binding_slots_.swap(replacement);
	}
	const std::size_t mask = binding_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(0, owner), name) & mask;
	while (binding_slots_[slot] != 0)
	{
		const std::uint32_t index = binding_slots_[slot] - 1;
		if (bindings_[index].owner == owner && bindings_[index].name == name)
			return bindings_[index];
		slot = (slot + 1) & mask;
	}
	bindings_.push_back(Binding(owner, name));
	binding_slots_[slot] = static_cast<std::uint32_t>(bindings_.size());
	return bindings_.back();
}

PathId ProgramModel::InternPath(PathId parent, NameId name)
{
	if ((path_names_.size() + 1) * 10 > path_slots_.size() * 7)
	{
		std::vector<PathId> replacement(path_slots_.size() * 2, 0);
		const std::size_t mask = replacement.size() - 1;
		for (PathId id = 1; id < path_names_.size(); ++id)
		{
			std::size_t slot = MixHash(MixHash(0, path_parents_[id]),
				path_names_[id]) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = id;
		}
		path_slots_.swap(replacement);
	}
	const std::size_t mask = path_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(0, parent), name) & mask;
	while (path_slots_[slot] != 0)
	{
		const PathId id = path_slots_[slot];
		if (path_parents_[id] == parent && path_names_[id] == name) return id;
		slot = (slot + 1) & mask;
	}
	const PathId id = static_cast<PathId>(path_names_.size());
	path_parents_.push_back(parent);
	path_names_.push_back(name);
	path_slots_[slot] = id;
	return id;
}

ScopeId ProgramModel::OpenNamespace(ScopeId parent, NameId name,
	bool is_inline)
{
	if (name == 0)
	{
		if (scopes_[parent].unnamed_child != kNoScope)
			return scopes_[parent].unnamed_child;
		const ScopeId id = static_cast<ScopeId>(scopes_.size());
		scopes_.push_back(ScopeRecord(parent, scopes_[parent].path, 0,
			false, true, scopes_[parent].translation_unit));
		scopes_[parent].unnamed_child = id;
		AddUsingDirective(parent, id);
		return id;
	}
	Binding* existing = FindDirect(parent, name);
	if (existing)
	{
		if (existing->name_space == kNoScope || existing->namespace_alias)
			throw std::runtime_error("namespace name conflict");
		ScopeRecord& scope = scopes_[existing->name_space];
		if (is_inline && !scope.is_inline)
			throw std::runtime_error("namespace inline mismatch");
		return existing->name_space;
	}
	const ScopeId id = static_cast<ScopeId>(scopes_.size());
	const PathId path = InternPath(scopes_[parent].path, name);
	scopes_.push_back(ScopeRecord(parent, path, name, is_inline,
		scopes_[parent].internal_context, scopes_[parent].translation_unit));
	Binding& binding = EnsureBinding(parent, name);
	binding.name_space = id;
	if (is_inline) AddUsingDirective(parent, id);
	return id;
}

void ProgramModel::AddNamespaceAlias(ScopeId owner, NameId name,
	ScopeId target)
{
	Binding* existing = FindDirect(owner, name);
	if (existing) throw std::runtime_error("namespace alias name conflict");
	Binding& binding = EnsureBinding(owner, name);
	binding.name_space = target;
	binding.namespace_alias = true;
}

void ProgramModel::AddUsingDirective(ScopeId owner, ScopeId target)
{
	std::vector<ScopeId>& targets = scopes_[owner].using_targets;
	if (std::find(targets.begin(), targets.end(), target) == targets.end())
		targets.push_back(target);
}

void ProgramModel::AddFunctionCandidate(Binding& binding, EntityId entity)
{
	for (CandidateId id = binding.first_function; id != kNoCandidate;
		id = candidates_[id].next)
	{
		if (candidates_[id].entity == entity) return;
	}
	const CandidateId id = static_cast<CandidateId>(candidates_.size());
	candidates_.push_back(CandidateLink(entity, kNoCandidate));
	if (binding.last_function == kNoCandidate) binding.first_function = id;
	else candidates_[binding.last_function].next = id;
	binding.last_function = id;
}

void ProgramModel::AddUsingDeclaration(ScopeId owner, NameId name,
	const Binding& target)
{
	if (target.name_space != kNoScope && target.type == 0 &&
		target.variable == kNoEntity && target.first_function == kNoCandidate)
		throw std::runtime_error("using-declaration names a namespace");
	Binding& binding = EnsureBinding(owner, name);
	if (target.type != 0)
	{
		if (binding.type != 0 && binding.type != target.type)
			throw std::runtime_error("using type conflict");
		binding.type = target.type;
	}
	if (target.variable != kNoEntity)
	{
		if (binding.variable != kNoEntity &&
			binding.variable != target.variable)
			throw std::runtime_error("using variable conflict");
		binding.variable = target.variable;
	}
	for (CandidateId id = target.first_function; id != kNoCandidate;
		id = candidates_[id].next)
		AddFunctionCandidate(binding, candidates_[id].entity);
}

void ProgramModel::AddTypeAlias(ScopeId owner, NameId name, TypeId type)
{
	Binding& binding = EnsureBinding(owner, name);
	if (binding.name_space != kNoScope || binding.variable != kNoEntity ||
		binding.first_function != kNoCandidate ||
		(binding.type != 0 && binding.type != type))
		throw std::runtime_error("type alias name conflict");
	binding.type = type;
}

const Binding* ProgramModel::SearchScopeGraph(ScopeId start,
	NameId name) const
{
	if (stats)
	{
		++stats->lookup_queries;
		++stats->lookup_scope_visits;
	}
	const Binding* direct = FindDirect(start, name);
	if (direct) return direct;
	if (lookup_marks_.size() < scopes_.size())
		lookup_marks_.resize(scopes_.size(), 0);
	++lookup_generation_;
	if (lookup_generation_ == 0)
	{
		std::fill(lookup_marks_.begin(), lookup_marks_.end(), 0);
		++lookup_generation_;
	}
	lookup_worklist_.clear();
	lookup_worklist_.push_back(start);
	lookup_marks_[start] = lookup_generation_;
	for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
	{
		const ScopeId scope = lookup_worklist_[head];
		const std::vector<ScopeId>& targets = scopes_[scope].using_targets;
		for (std::size_t i = 0; i < targets.size(); ++i)
		{
			if (stats) ++stats->lookup_edge_visits;
			const ScopeId target = targets[i];
			if (lookup_marks_[target] == lookup_generation_) continue;
			lookup_marks_[target] = lookup_generation_;
			if (stats) ++stats->lookup_scope_visits;
			direct = FindDirect(target, name);
			if (direct) return direct;
			lookup_worklist_.push_back(target);
		}
	}
	return 0;
}

const Binding* ProgramModel::LookupUnqualified(ScopeId current,
	NameId name) const
{
	for (ScopeId scope = current; scope != kNoScope;
		scope = scopes_[scope].parent)
	{
		const Binding* binding = SearchScopeGraph(scope, name);
		if (binding) return binding;
	}
	return 0;
}

ScopeId ProgramModel::ResolvePrefix(ScopeId current,
	const QualifiedName& name) const
{
	if (name.segments.size() < 2)
	{
		if (name.absolute)
		{
			while (scopes_[current].parent != kNoScope)
				current = scopes_[current].parent;
			return current;
		}
		return kNoScope;
	}
	ScopeId owner = current;
	std::size_t index = 0;
	if (name.absolute)
	{
		while (scopes_[owner].parent != kNoScope) owner = scopes_[owner].parent;
	}
	else
	{
		const Binding* first = LookupUnqualified(current, name.segments[0]);
		if (!first || first->name_space == kNoScope) return kNoScope;
		owner = first->name_space;
		index = 1;
	}
	for (; index + 1 < name.segments.size(); ++index)
	{
		const Binding* next = SearchScopeGraph(owner, name.segments[index]);
		if (!next || next->name_space == kNoScope) return kNoScope;
		owner = next->name_space;
	}
	return owner;
}

bool ProgramModel::ResolveNamespaceName(ScopeId current,
	const QualifiedName& name, ScopeId* result) const
{
	if (name.segments.empty()) return false;
	if (!name.absolute && name.segments.size() == 1)
	{
		const Binding* binding = LookupUnqualified(current, name.segments[0]);
		if (!binding || binding->name_space == kNoScope) return false;
		*result = binding->name_space;
		return true;
	}
	ScopeId owner = ResolvePrefix(current, name);
	if (owner == kNoScope) return false;
	const Binding* binding = SearchScopeGraph(owner, name.segments.back());
	if (!binding || binding->name_space == kNoScope) return false;
	*result = binding->name_space;
	return true;
}

bool ProgramModel::ResolveTypeName(ScopeId current,
	const QualifiedName& name, TypeId* result) const
{
	if (name.segments.empty()) return false;
	const Binding* binding = 0;
	if (!name.absolute && name.segments.size() == 1)
		binding = LookupUnqualified(current, name.segments[0]);
	else
	{
		const ScopeId owner = ResolvePrefix(current, name);
		if (owner != kNoScope)
			binding = SearchScopeGraph(owner, name.segments.back());
	}
	if (!binding || binding->type == 0) return false;
	*result = binding->type;
	return true;
}

const Binding* ProgramModel::ResolveUsingTarget(ScopeId current,
	const QualifiedName& name) const
{
	if (name.segments.size() < 2 && !name.absolute) return 0;
	const ScopeId owner = ResolvePrefix(current, name);
	return owner == kNoScope ? 0 : SearchScopeGraph(owner,
		name.segments.back());
}

EntityId ProgramModel::ResolveExpressionEntity(ScopeId current,
	const QualifiedName& name) const
{
	if (name.segments.empty()) return kNoEntity;
	const Binding* binding = 0;
	if (!name.absolute && name.segments.size() == 1)
		binding = LookupUnqualified(current, name.segments[0]);
	else
	{
		const ScopeId owner = ResolvePrefix(current, name);
		if (owner != kNoScope)
			binding = SearchScopeGraph(owner, name.segments.back());
	}
	if (!binding) return kNoEntity;
	if (binding->variable != kNoEntity) return binding->variable;
	if (binding->first_function != kNoCandidate &&
		candidates_[binding->first_function].next == kNoCandidate)
		return candidates_[binding->first_function].entity;
	return kNoEntity;
}

bool ProgramModel::ScopeEncloses(ScopeId enclosing, ScopeId nested) const
{
	for (ScopeId scope = nested; scope != kNoScope;
		scope = scopes_[scope].parent)
		if (scope == enclosing) return true;
	return false;
}

ScopeId ProgramModel::ResolveDeclaratorOwner(ScopeId current,
	const QualifiedName& name) const
{
	if (!name.absolute && name.segments.size() == 1) return current;
	const ScopeId owner = ResolvePrefix(current, name);
	if (owner == kNoScope || !ScopeEncloses(current, owner))
		throw std::runtime_error("qualified declarator is not in an enclosing namespace");
	return owner;
}

EntityId ProgramModel::FindFunction(const Binding& binding,
	TypeId type) const
{
	for (CandidateId id = binding.first_function; id != kNoCandidate;
		id = candidates_[id].next)
	{
		const EntityId entity = candidates_[id].entity;
		if (types.SameFunctionSignature(entities_[entity].type, type))
			return entity;
	}
	return kNoEntity;
}

EntityId ProgramModel::FindExternal(PathId path, NameId name, TypeId type,
	bool function) const
{
	const std::size_t mask = external_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(0, path), name) & mask;
	while (external_slots_[slot] != 0)
	{
		if (stats) ++stats->linkage_candidates;
		const EntityId entity = external_entities_[external_slots_[slot] - 1];
		const EntityRecord& candidate = entities_[entity];
		if (scopes_[candidate.owner].path == path && candidate.name == name &&
			candidate.function == function)
		{
			if (!function || types.SameFunctionSignature(candidate.type, type))
				return entity;
		}
		slot = (slot + 1) & mask;
	}
	return kNoEntity;
}

void ProgramModel::AddExternal(PathId path, NameId name, EntityId entity)
{
	if ((external_entities_.size() + 1) * 10 > external_slots_.size() * 7)
	{
		std::vector<std::uint32_t> replacement(external_slots_.size() * 2, 0);
		const std::size_t mask = replacement.size() - 1;
		for (std::uint32_t i = 0; i < external_entities_.size(); ++i)
		{
			const EntityRecord& item = entities_[external_entities_[i]];
			std::size_t slot = MixHash(MixHash(0,
				scopes_[item.owner].path), item.name) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = i + 1;
		}
		external_slots_.swap(replacement);
	}
	const std::size_t mask = external_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(0, path), name) & mask;
	while (external_slots_[slot] != 0) slot = (slot + 1) & mask;
	external_entities_.push_back(entity);
	external_slots_[slot] = static_cast<std::uint32_t>(external_entities_.size());
}

EntityId ProgramModel::Declare(ScopeId current,
	const Declarator& declarator, TypeId type,
	const DeclarationSpecifiers& specifiers, bool definition,
	bool function_definition, std::uint32_t unit)
{
	if (!declarator.has_name || declarator.name.segments.empty())
		throw std::runtime_error("declarator has no name");
	const ScopeId owner = declarator.resolved_owner;
	if (!ScopeEncloses(current, owner))
		throw std::runtime_error("declaration owner is outside its namespace");
	const NameId name = declarator.name.segments.back();
	const bool function = types.IsFunction(type);
	if (specifiers.is_inline && !function)
		throw std::runtime_error("inline specifier on variable");
	if (function_definition && (!function || !declarator.has_function_operation))
		throw std::runtime_error("invalid function definition declarator");
	Binding* existing_binding = FindDirect(owner, name);
	const bool qualified = declarator.name.absolute ||
		declarator.name.segments.size() > 1;
	if (qualified && !existing_binding)
		throw std::runtime_error("qualified declaration was not previously declared");
	Binding& binding = existing_binding ? *existing_binding :
		EnsureBinding(owner, name);
	if (binding.type != 0 || binding.name_space != kNoScope ||
		(function && binding.variable != kNoEntity) ||
		(!function && binding.first_function != kNoCandidate))
		throw std::runtime_error("declaration name conflict");
	EntityId entity = function ? FindFunction(binding, type) : binding.variable;
	Linkage linkage = LINKAGE_EXTERNAL;
	if (scopes_[owner].internal_context || specifiers.is_static ||
		(!function && types.IsConst(type) && !specifiers.is_extern))
		linkage = LINKAGE_INTERNAL;
	if (entity == kNoEntity && qualified)
		throw std::runtime_error("qualified declaration does not match an entity");
	if (entity == kNoEntity && linkage == LINKAGE_EXTERNAL)
		entity = FindExternal(scopes_[owner].path, name, type, function);
	if (entity == kNoEntity)
	{
		entity = static_cast<EntityId>(entities_.size());
		entities_.push_back(EntityRecord(name, type, owner, linkage,
			declaration_ordinal_++, function));
		if (linkage == LINKAGE_EXTERNAL)
			AddExternal(scopes_[owner].path, name, entity);
	}
	EntityRecord& record = entities_[entity];
	if (record.function != function)
		throw std::runtime_error("entity kind mismatch");
	if (function)
	{
		if (!types.SameFunctionSignature(record.type, type) ||
			record.type != type)
			throw std::runtime_error("incompatible function redeclaration");
		AddFunctionCandidate(binding, entity);
	}
	else
	{
		record.type = types.MergeRedeclaration(record.type, type);
		binding.variable = entity;
	}
	if (record.declaration_count != 0 &&
		record.has_thread_storage != specifiers.is_thread_local)
		throw std::runtime_error("thread_local redeclaration mismatch");
	record.has_thread_storage = specifiers.is_thread_local;
	++record.declaration_count;
	record.declared_inline = record.declared_inline || specifiers.is_inline;
	record.constexpr_declared = record.constexpr_declared ||
		specifiers.is_constexpr;
	if (definition)
	{
		if (record.defined &&
			!(function && record.declared_inline && record.definition_unit != unit))
			throw std::runtime_error("multiple definitions");
		if (!record.defined)
		{
			record.defined = true;
			record.definition_unit = unit;
		}
	}
	if (stats) ++stats->declarations;
	return entity;
}

void ProgramModel::Define(EntityId entity, TypeId completed_type,
	const InitialValue& initial, bool constant_usable, std::uint32_t)
{
	EntityRecord& record = entities_[entity];
	record.type = types.MergeRedeclaration(record.type, completed_type);
	record.initial = initial;
	record.constant_usable = constant_usable;
}

Expression ProgramModel::ExpressionForEntity(EntityId entity) const
{
	const EntityRecord& record = entities_[entity];
	Expression expression;
	expression.entity = entity;
	expression.category = VALUE_LVALUE;
	expression.type = types.IsReference(record.type) ?
		types.Referred(record.type) : record.type;
	expression.constant_expression = record.constant_usable ||
		types.IsArray(record.type) || record.function ||
		types.IsReference(record.type);
	return expression;
}

InitialValue ProgramModel::ResolveReferenceAddress(EntityId entity) const
{
	const EntityRecord& record = entities_[entity];
	if (!types.IsReference(record.type))
		throw std::logic_error("entity is not a reference");
	return record.initial;
}

InitialValue ProgramModel::LvalueAddress(const Expression& expression) const
{
	if (expression.category != VALUE_LVALUE || expression.entity == kNoEntity)
		throw std::runtime_error("expression has no object address");
	const EntityRecord& entity = entities_[expression.entity];
	if (types.IsReference(entity.type)) return ResolveReferenceAddress(
		expression.entity);
	InitialValue result;
	result.kind = INITIAL_ADDRESS_ENTITY;
	result.target = expression.entity;
	return result;
}

InitialValue ProgramModel::ResolveObjectValue(EntityId entity,
	bool* constant) const
{
	const EntityRecord& record = entities_[entity];
	if (types.IsReference(record.type))
	{
		const InitialValue address = ResolveReferenceAddress(entity);
		if (address.kind == INITIAL_ADDRESS_ENTITY)
			return ResolveObjectValue(address.target, constant);
		*constant = false;
		InitialValue unknown;
		unknown.kind = INITIAL_UNKNOWN;
		return unknown;
	}
	*constant = record.constant_usable;
	if (!*constant)
	{
		InitialValue unknown;
		unknown.kind = INITIAL_UNKNOWN;
		return unknown;
	}
	return record.initial;
}

InitialValue ProgramModel::LvalueToRvalue(const Expression& expression,
	bool* constant) const
{
	if (expression.category != VALUE_LVALUE) return expression.value;
	if (expression.entity == kNoEntity)
	{
		*constant = expression.constant_expression;
		return expression.value;
	}
	return ResolveObjectValue(expression.entity, constant);
}

StringId ProgramModel::AddString(FundamentalType type,
	const unsigned char* bytes, std::size_t size)
{
	StringRecord record;
	record.element_type = type;
	record.byte_offset = static_cast<std::uint32_t>(retained_bytes.size());
	record.byte_size = static_cast<std::uint32_t>(size);
	record.image_offset = 0;
	retained_bytes.insert(retained_bytes.end(), bytes, bytes + size);
	const StringId id = static_cast<StringId>(strings_.size());
	strings_.push_back(record);
	if (stats) ++stats->strings;
	return id;
}

TemporaryId ProgramModel::AddTemporary(TypeId type,
	const InitialValue& initial)
{
	TemporaryRecord record;
	record.type = type;
	record.initial = initial;
	record.image_offset = 0;
	const TemporaryId id = static_cast<TemporaryId>(temporaries_.size());
	temporaries_.push_back(record);
	if (stats) ++stats->temporaries;
	return id;
}

bool IsIntegralFundamental(FundamentalType type)
{
	return type != FT_FLOAT && type != FT_DOUBLE && type != FT_LONG_DOUBLE &&
		type != FT_VOID && type != FT_NULLPTR_T;
}

bool IsFloatingFundamental(FundamentalType type)
{
	return type == FT_FLOAT || type == FT_DOUBLE || type == FT_LONG_DOUBLE;
}

bool IsUnsignedFundamental(FundamentalType type)
{
	return type == FT_UNSIGNED_CHAR || type == FT_UNSIGNED_SHORT_INT ||
		type == FT_UNSIGNED_INT || type == FT_UNSIGNED_LONG_INT ||
		type == FT_UNSIGNED_LONG_LONG_INT || type == FT_CHAR16_T ||
		type == FT_CHAR32_T || type == FT_BOOL;
}

long double ReadArithmetic(const InitialValue& value)
{
	const unsigned char* data = value.bytes.data();
	if (IsFloatingFundamental(value.scalar_type))
	{
		if (value.scalar_type == FT_FLOAT)
		{
			float item;
			std::memcpy(&item, data, sizeof(item));
			return item;
		}
		if (value.scalar_type == FT_DOUBLE)
		{
			double item;
			std::memcpy(&item, data, sizeof(item));
			return item;
		}
		long double item;
		std::memcpy(&item, data, sizeof(item));
		return item;
	}
	std::uint64_t raw = 0;
	const std::size_t size = FundamentalSize(value.scalar_type);
	for (std::size_t i = 0; i < size; ++i)
		raw |= static_cast<std::uint64_t>(data[i]) << (i * 8);
	if (IsUnsignedFundamental(value.scalar_type)) return raw;
	if (size < 8 && (raw & (UINT64_C(1) << (size * 8 - 1))) != 0)
		raw |= ~((UINT64_C(1) << (size * 8)) - 1);
	return static_cast<long double>(static_cast<std::int64_t>(raw));
}

InitialValue ConvertArithmetic(const InitialValue& source,
	FundamentalType destination)
{
	InitialValue result;
	result.kind = INITIAL_SCALAR;
	result.scalar_type = destination;
	const long double value = ReadArithmetic(source);
	if (destination == FT_FLOAT)
	{
		const float converted = static_cast<float>(value);
		std::memcpy(result.bytes.data(), &converted, sizeof(converted));
	}
	else if (destination == FT_DOUBLE)
	{
		const double converted = static_cast<double>(value);
		std::memcpy(result.bytes.data(), &converted, sizeof(converted));
	}
	else if (destination == FT_LONG_DOUBLE)
	{
		std::memcpy(result.bytes.data(), &value, sizeof(value));
	}
	else
	{
		const std::uint64_t converted = destination == FT_BOOL ?
			(value != 0 ? 1 : 0) : static_cast<std::uint64_t>(value);
		StoreUnsigned(&result.bytes, converted, FundamentalSize(destination));
	}
	return result;
}

InitialValue ProgramModel::ConvertInitializer(TypeId* destination,
	const Expression& source, bool* constant)
{
	const TypeRecord& destination_record = types.Get(*destination);
	if (destination_record.kind == TYPE_ARRAY)
	{
		if (!source.string_literal)
			throw std::runtime_error("array requires a string initializer");
		TypeId element = types.RemoveTopCv(destination_record.child);
		const TypeRecord& element_record = types.Get(element);
		const StringRecord& string = strings_[source.string_id];
		if (element_record.kind != TYPE_FUNDAMENTAL ||
			!IsCharacterType(element_record.fundamental) ||
			!(element_record.fundamental == string.element_type ||
			  (string.element_type == FT_CHAR &&
			   (element_record.fundamental == FT_SIGNED_CHAR ||
			    element_record.fundamental == FT_UNSIGNED_CHAR))))
			throw std::runtime_error("incompatible string array initializer");
		const std::size_t element_size = FundamentalSize(string.element_type);
		const std::uint64_t elements = string.byte_size / element_size;
		if (destination_record.bound != 0 && destination_record.bound < elements)
			throw std::runtime_error("string is too long for array");
		if (destination_record.bound == 0)
			*destination = types.CompleteArray(*destination, elements);
		const std::size_t bytes = types.SizeOf(*destination);
		InitialValue result;
		result.kind = INITIAL_ARRAY_BYTES;
		result.byte_offset = static_cast<std::uint32_t>(retained_bytes.size());
		result.byte_size = static_cast<std::uint32_t>(bytes);
		retained_bytes.resize(retained_bytes.size() + bytes, 0);
		std::memcpy(&retained_bytes[result.byte_offset],
			&retained_bytes[string.byte_offset],
			std::min<std::size_t>(bytes, string.byte_size));
		*constant = true;
		return result;
	}
	if (destination_record.kind == TYPE_LVALUE_REFERENCE ||
		destination_record.kind == TYPE_RVALUE_REFERENCE)
	{
		const TypeId referred = destination_record.child;
		if (source.category == VALUE_LVALUE)
		{
			if (destination_record.kind == TYPE_RVALUE_REFERENCE)
				throw std::runtime_error("rvalue reference cannot bind an lvalue");
			if (!types.QualificationConvertible(source.type, referred))
				throw std::runtime_error("reference drops qualifiers");
			*constant = source.constant_expression;
			return LvalueAddress(source);
		}
		if (destination_record.kind == TYPE_LVALUE_REFERENCE &&
			!types.IsConst(referred))
			throw std::runtime_error("non-const reference cannot bind a prvalue");
		TypeId temporary_type = referred;
		bool temporary_constant = false;
		const InitialValue temporary_value = ConvertInitializer(&temporary_type,
			source, &temporary_constant);
		const TemporaryId temporary = AddTemporary(temporary_type,
			temporary_value);
		InitialValue result;
		result.kind = INITIAL_ADDRESS_TEMPORARY;
		result.target = temporary;
		*constant = temporary_constant;
		return result;
	}
	const TypeId unqualified_destination = types.RemoveTopCv(*destination);
	const TypeRecord& plain_destination = types.Get(unqualified_destination);
	if (plain_destination.kind == TYPE_POINTER)
	{
		if (source.null_pointer_constant)
		{
			*constant = source.constant_expression;
			return InitialValue();
		}
		if (source.string_literal)
		{
			const TypeRecord& array = types.Get(source.type);
			if (!PointerTargetConvertible(types, array.child,
				plain_destination.child))
				throw std::runtime_error("string literal drops qualifiers");
			InitialValue result;
			result.kind = INITIAL_ADDRESS_STRING;
			result.target = source.string_id;
			*constant = true;
			return result;
		}
		const TypeId source_plain = types.RemoveTopCv(source.type);
		const TypeRecord& source_record = types.Get(source_plain);
		if (source.category == VALUE_LVALUE && source_record.kind == TYPE_ARRAY)
		{
			if (!PointerTargetConvertible(types, source_record.child,
				plain_destination.child))
				throw std::runtime_error("array-to-pointer qualification mismatch");
			*constant = source.constant_expression;
			return LvalueAddress(source);
		}
		if (source.category == VALUE_LVALUE &&
			source_record.kind == TYPE_FUNCTION)
		{
			if (plain_destination.child != source_plain)
				throw std::runtime_error("function pointer type mismatch");
			*constant = true;
			return LvalueAddress(source);
		}
		bool value_constant = source.constant_expression;
		InitialValue value = source.category == VALUE_LVALUE ?
			LvalueToRvalue(source, &value_constant) : source.value;
		if (source_record.kind != TYPE_POINTER ||
			!PointerTargetConvertible(types, source_record.child,
				plain_destination.child))
			throw std::runtime_error("invalid pointer initializer");
		*constant = value_constant;
		return value;
	}
	if (plain_destination.kind != TYPE_FUNDAMENTAL ||
		plain_destination.fundamental == FT_VOID ||
		plain_destination.fundamental == FT_NULLPTR_T)
		throw std::runtime_error("invalid scalar initializer destination");
	bool value_constant = source.constant_expression;
	InitialValue value = source.category == VALUE_LVALUE ?
		LvalueToRvalue(source, &value_constant) : source.value;
	const TypeId source_plain = types.RemoveTopCv(source.type);
	const TypeRecord& source_record = types.Get(source_plain);
	if (plain_destination.fundamental == FT_BOOL &&
		(source_record.kind == TYPE_POINTER || source.null_pointer_constant))
	{
		InitialValue result;
		result.kind = INITIAL_SCALAR;
		result.scalar_type = FT_BOOL;
		result.bytes[0] = value.kind == INITIAL_ZERO ? 0 : 1;
		*constant = value_constant;
		return result;
	}
	if (source_record.kind != TYPE_FUNDAMENTAL ||
		(!IsIntegralFundamental(source_record.fundamental) &&
		 !IsFloatingFundamental(source_record.fundamental)))
		throw std::runtime_error("invalid arithmetic initializer");
	if (value.kind != INITIAL_SCALAR)
	{
		value.kind = INITIAL_UNKNOWN;
		*constant = false;
		return value;
	}
	*constant = value_constant;
	return ConvertArithmetic(value, plain_destination.fundamental);
}

bool ProgramModel::ContextualBool(const Expression& expression,
	bool* constant) const
{
	bool value_constant = expression.constant_expression;
	const TypeId plain = types.RemoveTopCv(expression.type);
	const TypeRecord& record = types.Get(plain);
	if (record.kind == TYPE_ARRAY || record.kind == TYPE_FUNCTION)
	{
		*constant = value_constant;
		return true;
	}
	InitialValue value = expression.category == VALUE_LVALUE ?
		LvalueToRvalue(expression, &value_constant) : expression.value;
	*constant = value_constant;
	if (!value_constant) return false;
	if (record.kind == TYPE_POINTER || expression.null_pointer_constant)
		return value.kind != INITIAL_ZERO;
	if (record.kind != TYPE_FUNDAMENTAL)
		throw std::runtime_error("expression is not contextually convertible to bool");
	return ReadArithmetic(value) != 0;
}

std::uint64_t ProgramModel::ResolveAddress(const InitialValue& value) const
{
	std::uint64_t address = 0;
	if (value.kind == INITIAL_ADDRESS_ENTITY)
	{
		const EntityRecord& entity = entities_[value.target];
		if (entity.image_offset == std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("address refers to an undefined entity");
		address = entity.image_offset;
	}
	else if (value.kind == INITIAL_ADDRESS_STRING)
		address = strings_[value.target].image_offset;
	else if (value.kind == INITIAL_ADDRESS_TEMPORARY)
		address = temporaries_[value.target].image_offset;
	else if (value.kind != INITIAL_ZERO)
		throw std::logic_error("value is not an address");
	return static_cast<std::uint64_t>(
		static_cast<std::int64_t>(address) + value.addend);
}

void ProgramModel::WriteImage(std::ostream& output)
{
	if (image_written_) throw std::logic_error("program image already written");
	image_written_ = true;
	std::uint64_t offset = 4;
	for (EntityId id = 0; id < entities_.size(); ++id)
	{
		EntityRecord& entity = entities_[id];
		if (!entity.function && !entity.defined) continue;
		offset = Align(offset, types.AlignOf(entity.type));
		entity.image_offset = offset;
		offset += types.SizeOf(entity.type);
	}
	for (TemporaryId id = 0; id < temporaries_.size(); ++id)
	{
		TemporaryRecord& temporary = temporaries_[id];
		offset = Align(offset, types.AlignOf(temporary.type));
		temporary.image_offset = offset;
		offset += types.SizeOf(temporary.type);
	}
	for (StringId id = 0; id < strings_.size(); ++id)
	{
		StringRecord& string = strings_[id];
		offset = Align(offset, FundamentalSize(string.element_type));
		string.image_offset = offset;
		offset += string.byte_size;
	}
	if (offset > std::numeric_limits<std::size_t>::max())
		throw std::runtime_error("program image is too large");
	std::vector<unsigned char> image(static_cast<std::size_t>(offset), 0);
	image[0] = 'P'; image[1] = 'A'; image[2] = '8'; image[3] = 0;
	for (EntityId id = 0; id < entities_.size(); ++id)
	{
		const EntityRecord& entity = entities_[id];
		if (entity.image_offset == std::numeric_limits<std::uint64_t>::max())
			continue;
		unsigned char* destination = &image[entity.image_offset];
		if (entity.function)
		{
			destination[0] = 'f'; destination[1] = 'u';
			destination[2] = 'n'; destination[3] = 0;
			continue;
		}
		const InitialValue& value = entity.initial;
		if (value.kind == INITIAL_SCALAR)
			std::memcpy(destination, value.bytes.data(), types.SizeOf(entity.type));
		else if (value.kind == INITIAL_ARRAY_BYTES)
			std::memcpy(destination, &retained_bytes[value.byte_offset],
				value.byte_size);
		else if (value.kind == INITIAL_ADDRESS_ENTITY ||
			value.kind == INITIAL_ADDRESS_STRING ||
			value.kind == INITIAL_ADDRESS_TEMPORARY)
		{
			const std::uint64_t address = ResolveAddress(value);
			std::memcpy(destination, &address, sizeof(address));
		}
	}
	for (TemporaryId id = 0; id < temporaries_.size(); ++id)
	{
		const TemporaryRecord& temporary = temporaries_[id];
		unsigned char* destination = &image[temporary.image_offset];
		if (temporary.initial.kind == INITIAL_SCALAR)
			std::memcpy(destination, temporary.initial.bytes.data(),
				types.SizeOf(temporary.type));
		else if (temporary.initial.kind == INITIAL_ADDRESS_ENTITY ||
			temporary.initial.kind == INITIAL_ADDRESS_STRING)
		{
			const std::uint64_t address = ResolveAddress(temporary.initial);
			std::memcpy(destination, &address, sizeof(address));
		}
	}
	for (StringId id = 0; id < strings_.size(); ++id)
	{
		const StringRecord& string = strings_[id];
		std::memcpy(&image[string.image_offset],
			&retained_bytes[string.byte_offset], string.byte_size);
	}
	output.write(reinterpret_cast<const char*>(image.data()), image.size());
	if (!output) throw std::runtime_error("unable to write program image");
	if (stats) stats->image_bytes = image.size();
}

void ProgramModel::FinishStats()
{
	if (!stats) return;
	stats->identifiers = identifiers.Size();
	stats->canonical_types = types.Size();
	stats->scopes = scopes_.size();
}

std::uint32_t ProgramModel::CurrentUnit() const
{
	return current_unit_;
}

}
}
