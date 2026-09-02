#include "namespace_initialization/program.h"

#include "support/exceptions.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ostream>

namespace cppgm
{
namespace namespace_initialization
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
	if (alignment == 0) ThrowSemanticInternal("zero alignment");
	const std::uint64_t remainder = offset % alignment;
	if (remainder == 0) return offset;
	const std::uint64_t padding = alignment - remainder;
	if (offset > std::numeric_limits<std::uint64_t>::max() - padding)
		ThrowSemanticResourceLimit("program image is too large");
	return offset + padding;
}

void AddImageSize(std::uint64_t* offset, std::size_t size)
{
	if (*offset > std::numeric_limits<std::uint64_t>::max() - size)
		ThrowSemanticResourceLimit("program image is too large");
	*offset += size;
}

void WriteRaw(std::ostream& output, const void* data, std::size_t size)
{
	if (size == 0) return;
	output.write(static_cast<const char*>(data), size);
	if (!output) ThrowSemanticInputOutput("unable to write program image");
}

void WriteZeros(std::ostream& output, std::uint64_t count)
{
	const std::array<unsigned char, 4096> zeros = {{0}};
	while (count != 0)
	{
		const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(
			count, zeros.size()));
		WriteRaw(output, zeros.data(), chunk);
		count -= chunk;
	}
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
	if (types.PointeeQualificationConvertible(source, destination)) return true;
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
		ThrowSemanticResourceLimit("too many identifiers");
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
		ThrowSemanticError("pointer to reference type");
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
			ThrowSemanticError("direct reference to reference type");
		if (referred.kind == TYPE_LVALUE_REFERENCE ||
			kind == TYPE_LVALUE_REFERENCE)
			return Unary(TYPE_LVALUE_REFERENCE, referred.child);
		return Unary(TYPE_RVALUE_REFERENCE, referred.child);
	}
	if (IsVoid(child)) ThrowSemanticError("reference to void type");
	return Unary(kind, child);
}

TypeId TypeTable::Array(TypeId child, std::uint64_t bound)
{
	const TypeRecord& element = Get(child);
	if (element.kind == TYPE_LVALUE_REFERENCE ||
		element.kind == TYPE_RVALUE_REFERENCE || element.kind == TYPE_FUNCTION ||
		IsVoid(child)) ThrowSemanticError("invalid array element type");
	TypeRecord candidate;
	candidate.kind = TYPE_ARRAY;
	candidate.child = child;
	candidate.bound = bound;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Function(TypeId result,
	const std::vector<TypeId>& parameters, bool variadic)
{
	if (parameters.size() > std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many function parameters");
	const TypeRecord& returned = Get(result);
	if (returned.kind == TYPE_ARRAY || returned.kind == TYPE_FUNCTION)
		ThrowSemanticError("invalid function return type");
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
		ThrowSemanticError("cv-qualified function type");
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
		ThrowSemanticError("incompatible redeclaration");
	const TypeId child = MergeRedeclaration(left.child, right.child);
	if (left.bound != 0 && right.bound != 0 && left.bound != right.bound)
		ThrowSemanticError("incompatible array bounds");
	return Array(child, left.bound == 0 ? right.bound : left.bound);
}

TypeId TypeTable::CompleteArray(TypeId type, std::uint64_t bound)
{
	const TypeRecord& record = Get(type);
	if (record.kind != TYPE_ARRAY) ThrowSemanticInternal("not an array");
	if (record.bound != 0 && record.bound != bound)
		ThrowSemanticError("array initializer bound mismatch");
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
		ThrowSemanticInternal("not a reference type");
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
	while (true)
	{
		const TypeRecord& record = Get(type);
		if (record.kind == TYPE_QUALIFIED)
			return (record.cv & CV_CONST) != 0;
		if (record.kind != TYPE_ARRAY) return false;
		type = record.child;
	}
}

bool TypeTable::IsVolatile(TypeId type) const
{
	while (true)
	{
		const TypeRecord& record = Get(type);
		if (record.kind == TYPE_QUALIFIED)
			return (record.cv & CV_VOLATILE) != 0;
		if (record.kind != TYPE_ARRAY) return false;
		type = record.child;
	}
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
	return QualificationConvertibleAtDepth(source, destination, 0);
}

bool TypeTable::PointeeQualificationConvertible(TypeId source,
	TypeId destination) const
{
	return QualificationConvertibleAtDepth(source, destination, 1);
}

bool TypeTable::ReferenceRelated(TypeId source, TypeId destination) const
{
	while (true)
	{
		source = RemoveTopCv(source);
		destination = RemoveTopCv(destination);
		const TypeRecord& left = Get(source);
		const TypeRecord& right = Get(destination);
		if (left.kind == TYPE_ARRAY || right.kind == TYPE_ARRAY)
		{
			if (left.kind != TYPE_ARRAY || right.kind != TYPE_ARRAY ||
				left.bound != right.bound) return false;
			source = left.child;
			destination = right.child;
			continue;
		}
		if (left.kind == TYPE_POINTER || right.kind == TYPE_POINTER)
		{
			if (left.kind != TYPE_POINTER || right.kind != TYPE_POINTER)
				return false;
			source = left.child;
			destination = right.child;
			continue;
		}
		return source == destination;
	}
}

bool TypeTable::ReferenceCompatible(TypeId source, TypeId destination) const
{
	return ReferenceRelated(source, destination) &&
		QualificationConvertible(source, destination);
}

bool TypeTable::QualificationConvertibleAtDepth(TypeId source,
	TypeId destination, std::size_t pointer_depth) const
{
	bool intermediate_destination_const = true;
	while (true)
	{
		unsigned char source_cv = CV_NONE;
		unsigned char destination_cv = CV_NONE;
		const TypeRecord* left = &Get(source);
		const TypeRecord* right = &Get(destination);
		if (left->kind == TYPE_QUALIFIED)
		{
			source_cv = left->cv;
			source = left->child;
			left = &Get(source);
		}
		if (right->kind == TYPE_QUALIFIED)
		{
			destination_cv = right->cv;
			destination = right->child;
			right = &Get(destination);
		}
		if ((source_cv & ~destination_cv) != 0) return false;
		if (source_cv != destination_cv && pointer_depth > 1 &&
			!intermediate_destination_const) return false;
		if (pointer_depth >= 1)
			intermediate_destination_const =
				intermediate_destination_const &&
				(destination_cv & CV_CONST) != 0;
		if (left->kind != right->kind) return false;
		if (left->kind == TYPE_POINTER)
		{
			source = left->child;
			destination = right->child;
			++pointer_depth;
			continue;
		}
		if (left->kind == TYPE_ARRAY && left->bound == right->bound)
		{
			source = left->child;
			destination = right->child;
			continue;
		}
		return source == destination;
	}
}

std::size_t FundamentalSize(FundamentalType type)
{
	switch (type)
	{
	case FT_SIGNED_CHAR: case FT_UNSIGNED_CHAR: case FT_CHAR: case FT_BOOL:
		return 1;
	case FT_SHORT_INT: case FT_UNSIGNED_SHORT_INT: case FT_CHAR16_T:
	case FT_FLOAT16:
		return 2;
	case FT_INT: case FT_UNSIGNED_INT: case FT_WCHAR_T: case FT_CHAR32_T:
	case FT_FLOAT: case FT_FLOAT32:
		return 4;
	case FT_LONG_INT: case FT_LONG_LONG_INT: case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT: case FT_DOUBLE: case FT_NULLPTR_T:
	case FT_FLOAT32X: case FT_FLOAT64:
		return 8;
	case FT_LONG_DOUBLE: case FT_FLOAT64X: case FT_FLOAT128:
		return 16;
	case FT_VOID:
		break;
	}
	ThrowSemanticError("incomplete fundamental type");
}

std::size_t TypeTable::SizeOf(TypeId type) const
{
	std::size_t multiplier = 1;
	while (true)
	{
		const TypeRecord& record = Get(type);
		if (record.kind == TYPE_QUALIFIED)
		{
			type = record.child;
			continue;
		}
		if (record.kind == TYPE_ARRAY)
		{
			if (record.bound == 0)
				ThrowSemanticError("incomplete array type");
			if (record.bound > std::numeric_limits<std::size_t>::max() ||
				multiplier > std::numeric_limits<std::size_t>::max() /
					static_cast<std::size_t>(record.bound))
				ThrowSemanticResourceLimit("object type is too large");
			multiplier *= static_cast<std::size_t>(record.bound);
			type = record.child;
			continue;
		}
		std::size_t size = 0;
		switch (record.kind)
		{
		case TYPE_FUNDAMENTAL: size = FundamentalSize(record.fundamental); break;
		case TYPE_POINTER: case TYPE_LVALUE_REFERENCE:
		case TYPE_RVALUE_REFERENCE: size = 8; break;
		case TYPE_FUNCTION: size = 4; break;
		default: ThrowSemanticInternal("invalid complete type");
		}
		if (multiplier > std::numeric_limits<std::size_t>::max() / size)
			ThrowSemanticResourceLimit("object type is too large");
		return multiplier * size;
	}
}

std::size_t TypeTable::AlignOf(TypeId type) const
{
	const TypeRecord* record = &Get(type);
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_ARRAY)
	{
		type = record->child;
		record = &Get(type);
	}
	if (record->kind == TYPE_FUNCTION) return 4;
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
	if (parameters_.size() > std::numeric_limits<std::uint32_t>::max() -
		parameter_count)
		ThrowSemanticResourceLimit("canonical type parameter storage is too large");
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
		ThrowSemanticResourceLimit("too many canonical types");
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

NameSequence::NameSequence() : inline_names_(), size_(0) {}

void NameSequence::push_back(NameId name)
{
	if (size_ < inline_names_.size()) inline_names_[size_] = name;
	else overflow_names_.push_back(name);
	++size_;
}

bool NameSequence::empty() const
{
	return size_ == 0;
}

std::size_t NameSequence::size() const
{
	return size_;
}

NameId NameSequence::operator[](std::size_t index) const
{
	return index < inline_names_.size() ? inline_names_[index] :
		overflow_names_[index - inline_names_.size()];
}

NameId NameSequence::back() const
{
	return (*this)[size_ - 1];
}

std::size_t NameSequence::StorageBytes() const
{
	return overflow_names_.capacity() * sizeof(NameId);
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
	  string_literal(false), string_id(kNoString),
	  first_function(kNoCandidate), last_function(kNoCandidate),
	  translation_unit(std::numeric_limits<std::uint32_t>::max())
{
}

CandidateLink::CandidateLink(EntityId value, CandidateId next_value)
	: entity(value), next(next_value)
{
}

Binding::Binding(BindingId identity_value, ScopeId owner_value,
	NameId name_value)
	: identity(identity_value), owner(owner_value), name(name_value), type(0),
	  name_space(kNoScope),
	  variable(kNoEntity), first_function(kNoCandidate),
	  last_function(kNoCandidate), type_origin(kNoBinding),
	  namespace_origin(kNoBinding), namespace_alias(false)
{
}

ScopeRecord::ScopeRecord(ScopeId parent_value, PathId path_value,
	NameId name_value, bool inline_value, bool internal_value,
	std::uint32_t unit)
	: parent(parent_value), path(path_value), name(name_value),
	  unnamed_child(kNoScope), first_using_edge(kNoUsingEdge),
	  last_using_edge(kNoUsingEdge),
	  first_using_predecessor(kNoUsingEdge),
	  last_using_predecessor(kNoUsingEdge), is_inline(inline_value),
	  internal_context(internal_value), translation_unit(unit)
{
}

UsingEdgeRecord::UsingEdgeRecord(ScopeId owner_value, ScopeId target_value)
	: owner(owner_value), target(target_value),
	  next_from_owner(kNoUsingEdge), next_to_target(kNoUsingEdge)
{
}

LookupResult::LookupResult()
	: type(0), name_space(kNoScope), variable(kNoEntity),
	  first_function(kNoCandidate), last_function(kNoCandidate),
	  type_origin(kNoBinding), namespace_origin(kNoBinding),
	  declarations_found(false), ambiguous(false)
{
}

bool LookupResult::Found(LookupKind kind) const
{
	if (ambiguous) return true;
	switch (kind)
	{
	case LOOKUP_TYPE: return type != 0;
	case LOOKUP_NAMESPACE: return name_space != kNoScope;
	case LOOKUP_EXPRESSION:
		return variable != kNoEntity || first_function != kNoCandidate;
	case LOOKUP_USING_TARGET:
		return type != 0 || variable != kNoEntity ||
			first_function != kNoCandidate;
	}
	return false;
}

LookupCacheEntry::LookupCacheEntry(ScopeId start_value, NameId name_value,
	LookupKind kind_value, std::uint32_t generation_value,
	const LookupResult& result_value)
	: start(start_value), name(name_value), kind(kind_value),
	  generation(generation_value), result(result_value)
{
}

EntityRecord::EntityRecord(NameId name_value, TypeId type_value,
	ScopeId owner_value, Linkage linkage_value, bool function_value)
	: name(name_value), type(type_value), owner(owner_value),
	  linkage(linkage_value),
	  definition_unit(std::numeric_limits<std::uint32_t>::max()),
	  declaration_count(0),
	  image_offset(std::numeric_limits<std::uint64_t>::max()),
	  function(function_value), defined(false),
	  definitions_inline(false),
	  has_thread_storage(false), constexpr_declared(false),
	  constant_initialized(false), constant_usable(false)
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

Model::Model(Stats* stats_value)
	: stats(stats_value), binding_slots_(32, 0), using_edge_slots_(16, 0),
	  lookup_cache_slots_(32, 0), external_slots_(32, 0),
	  path_slots_(16, 0), lookup_generation_(0), candidate_generation_(0),
	  current_unit_(0), image_written_(false)
{
	path_parents_.push_back(0);
	path_names_.push_back(0);
}

ScopeId Model::NewTranslationUnit()
{
	if (scopes_.size() >= kNoScope)
		ThrowSemanticResourceLimit("too many scopes");
	const ScopeId id = static_cast<ScopeId>(scopes_.size());
	scopes_.push_back(ScopeRecord(kNoScope, 0, 0, false, false,
		current_unit_));
	scope_lookup_generations_.push_back(1);
	++current_unit_;
	return id;
}

Binding* Model::FindDirect(ScopeId owner, NameId name)
{
	const Model* self = this;
	return const_cast<Binding*>(self->FindDirect(owner, name));
}

const Binding* Model::FindDirect(ScopeId owner, NameId name) const
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

Binding& Model::EnsureBinding(ScopeId owner, NameId name)
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
	if (bindings_.size() >= kNoBinding)
		ThrowSemanticResourceLimit("too many bindings");
	const BindingId id = static_cast<BindingId>(bindings_.size());
	bindings_.push_back(Binding(id, owner, name));
	binding_slots_[slot] = static_cast<std::uint32_t>(bindings_.size());
	return bindings_.back();
}

PathId Model::InternPath(PathId parent, NameId name)
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
	if (path_names_.size() > std::numeric_limits<PathId>::max())
		ThrowSemanticResourceLimit("too many namespace paths");
	const PathId id = static_cast<PathId>(path_names_.size());
	path_parents_.push_back(parent);
	path_names_.push_back(name);
	path_slots_[slot] = id;
	return id;
}

ScopeId Model::OpenNamespace(ScopeId parent, NameId name,
	bool is_inline)
{
	if (name == 0)
	{
		if (scopes_[parent].unnamed_child != kNoScope)
			return scopes_[parent].unnamed_child;
		if (scopes_.size() >= kNoScope)
			ThrowSemanticResourceLimit("too many scopes");
		const ScopeId id = static_cast<ScopeId>(scopes_.size());
		scopes_.push_back(ScopeRecord(parent, scopes_[parent].path, 0,
			false, true, scopes_[parent].translation_unit));
		scope_lookup_generations_.push_back(1);
		scopes_[parent].unnamed_child = id;
		AddUsingDirective(parent, id);
		return id;
	}
	Binding* existing = FindDirect(parent, name);
	if (existing)
	{
		if (existing->name_space == kNoScope || existing->namespace_alias)
			ThrowSemanticError("namespace name conflict");
		ScopeRecord& scope = scopes_[existing->name_space];
		if (is_inline && !scope.is_inline)
			ThrowSemanticError("namespace inline mismatch");
		return existing->name_space;
	}
	if (scopes_.size() >= kNoScope)
		ThrowSemanticResourceLimit("too many scopes");
	const ScopeId id = static_cast<ScopeId>(scopes_.size());
	const PathId path = InternPath(scopes_[parent].path, name);
	scopes_.push_back(ScopeRecord(parent, path, name, is_inline,
		scopes_[parent].internal_context, scopes_[parent].translation_unit));
	scope_lookup_generations_.push_back(1);
	InvalidateLookupName(parent, name);
	Binding& binding = EnsureBinding(parent, name);
	binding.name_space = id;
	binding.namespace_origin = binding.identity;
	if (is_inline) AddUsingDirective(parent, id);
	return id;
}

void Model::AddNamespaceAlias(ScopeId owner, NameId name,
	ScopeId target)
{
	Binding* existing = FindDirect(owner, name);
	if (existing) ThrowSemanticError("namespace alias name conflict");
	InvalidateLookupName(owner, name);
	Binding& binding = EnsureBinding(owner, name);
	binding.name_space = target;
	binding.namespace_origin = binding.identity;
	binding.namespace_alias = true;
}

void Model::AddUsingDirective(ScopeId owner, ScopeId target)
{
	AddUsingEdge(owner, target);
}

void Model::AddFunctionCandidate(Binding& binding, EntityId entity)
{
	for (CandidateId id = binding.first_function; id != kNoCandidate;
		id = candidates_[id].next)
	{
		if (candidates_[id].entity == entity) return;
	}
	if (candidates_.size() >= kNoCandidate)
		ThrowSemanticResourceLimit("too many function candidates");
	const CandidateId id = static_cast<CandidateId>(candidates_.size());
	candidates_.push_back(CandidateLink(entity, kNoCandidate));
	if (binding.last_function == kNoCandidate) binding.first_function = id;
	else candidates_[binding.last_function].next = id;
	binding.last_function = id;
}

void Model::AddUsingDeclaration(ScopeId owner, NameId name,
	const LookupResult& target)
{
	if (target.ambiguous || !target.Found(LOOKUP_USING_TARGET))
		ThrowSemanticError("ambiguous using-declaration target");
	InvalidateLookupName(owner, name);
	Binding& binding = EnsureBinding(owner, name);
	if (target.type != 0)
	{
		if (binding.type != 0 && binding.type_origin != target.type_origin)
			ThrowSemanticError("using type conflict");
		binding.type = target.type;
		binding.type_origin = target.type_origin;
	}
	if (target.variable != kNoEntity)
	{
		if (binding.variable != kNoEntity &&
			binding.variable != target.variable)
			ThrowSemanticError("using variable conflict");
		binding.variable = target.variable;
	}
	for (CandidateId id = target.first_function; id != kNoCandidate;
		id = candidates_[id].next)
		AddFunctionCandidate(binding, candidates_[id].entity);
}

void Model::AddTypeAlias(ScopeId owner, NameId name, TypeId type)
{
	InvalidateLookupName(owner, name);
	Binding& binding = EnsureBinding(owner, name);
	if (binding.name_space != kNoScope || binding.variable != kNoEntity ||
		binding.first_function != kNoCandidate ||
		(binding.type != 0 && binding.type != type))
		ThrowSemanticError("type alias name conflict");
	binding.type = type;
	if (binding.type_origin == kNoBinding)
		binding.type_origin = binding.identity;
}

void Model::RehashUsingEdges(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (UsingEdgeId id = 0; id < using_edges_.size(); ++id)
	{
		const UsingEdgeRecord& edge = using_edges_[id];
		std::size_t slot = MixHash(MixHash(0, edge.owner), edge.target) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = id + 1;
	}
	using_edge_slots_.swap(replacement);
}

void Model::AddUsingEdge(ScopeId owner, ScopeId target)
{
	if ((using_edges_.size() + 1) * 10 > using_edge_slots_.size() * 7)
		RehashUsingEdges(using_edge_slots_.size() * 2);
	const std::size_t mask = using_edge_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(0, owner), target) & mask;
	while (using_edge_slots_[slot] != 0)
	{
		const UsingEdgeRecord& edge =
			using_edges_[using_edge_slots_[slot] - 1];
		if (edge.owner == owner && edge.target == target) return;
		slot = (slot + 1) & mask;
	}
	if (using_edges_.size() >= kNoUsingEdge)
		ThrowSemanticResourceLimit("too many using-directive edges");
	const UsingEdgeId id = static_cast<UsingEdgeId>(using_edges_.size());
	using_edges_.push_back(UsingEdgeRecord(owner, target));
	using_edge_slots_[slot] = id + 1;
	ScopeRecord& source = scopes_[owner];
	if (source.last_using_edge == kNoUsingEdge)
		source.first_using_edge = id;
	else using_edges_[source.last_using_edge].next_from_owner = id;
	source.last_using_edge = id;
	ScopeRecord& destination = scopes_[target];
	if (destination.last_using_predecessor == kNoUsingEdge)
		destination.first_using_predecessor = id;
	else using_edges_[destination.last_using_predecessor].next_to_target = id;
	destination.last_using_predecessor = id;
	InvalidateLookupGraph(owner);
	if (stats) ++stats->using_edges;
}

void Model::BeginScopeWalk(ScopeId start)
{
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
}

void Model::AddWalkTarget(ScopeId target)
{
	if (lookup_marks_[target] == lookup_generation_) return;
	lookup_marks_[target] = lookup_generation_;
	lookup_worklist_.push_back(target);
}

void Model::AdvanceLookupGeneration(ScopeId scope)
{
	++scope_lookup_generations_[scope];
	if (scope_lookup_generations_[scope] != 0) return;
	for (std::size_t i = 0; i < lookup_cache_entries_.size(); ++i)
		lookup_cache_entries_[i].generation = 0;
	std::fill(scope_lookup_generations_.begin(),
		scope_lookup_generations_.end(), 1);
}

void Model::InvalidateLookupGraph(ScopeId changed)
{
	if (lookup_cache_entries_.empty()) return;
	BeginScopeWalk(changed);
	for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
	{
		const ScopeId scope = lookup_worklist_[head];
		AdvanceLookupGeneration(scope);
		if (stats) ++stats->lookup_cache_invalidations;
		for (UsingEdgeId id = scopes_[scope].first_using_predecessor;
			id != kNoUsingEdge; id = using_edges_[id].next_to_target)
			AddWalkTarget(using_edges_[id].owner);
	}
}

std::uint32_t Model::FindLookupCache(ScopeId start, NameId name,
	LookupKind kind) const
{
	const std::size_t mask = lookup_cache_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(MixHash(0, start), name), kind) & mask;
	while (lookup_cache_slots_[slot] != 0)
	{
		const std::uint32_t index = lookup_cache_slots_[slot] - 1;
		const LookupCacheEntry& entry = lookup_cache_entries_[index];
		if (entry.start == start && entry.name == name && entry.kind == kind)
			return index;
		slot = (slot + 1) & mask;
	}
	return std::numeric_limits<std::uint32_t>::max();
}

void Model::InvalidateLookupName(ScopeId changed, NameId name)
{
	if (lookup_cache_entries_.empty()) return;
	BeginScopeWalk(changed);
	for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
	{
		const ScopeId scope = lookup_worklist_[head];
		for (int value = LOOKUP_TYPE; value <= LOOKUP_USING_TARGET; ++value)
		{
			const std::uint32_t index = FindLookupCache(scope, name,
				static_cast<LookupKind>(value));
			if (index != std::numeric_limits<std::uint32_t>::max() &&
				lookup_cache_entries_[index].generation ==
					scope_lookup_generations_[scope])
			{
				lookup_cache_entries_[index].generation = 0;
				if (stats) ++stats->lookup_cache_invalidations;
			}
		}
		for (UsingEdgeId id = scopes_[scope].first_using_predecessor;
			id != kNoUsingEdge; id = using_edges_[id].next_to_target)
			AddWalkTarget(using_edges_[id].owner);
	}
}

void Model::RehashLookupCache(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::uint32_t i = 0; i < lookup_cache_entries_.size(); ++i)
	{
		const LookupCacheEntry& entry = lookup_cache_entries_[i];
		std::size_t slot = MixHash(MixHash(MixHash(0, entry.start), entry.name),
			entry.kind) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = i + 1;
	}
	lookup_cache_slots_.swap(replacement);
}

void Model::StoreLookupCache(ScopeId start, NameId name,
	LookupKind kind, const LookupResult& result)
{
	std::uint32_t index = FindLookupCache(start, name, kind);
	if (index != std::numeric_limits<std::uint32_t>::max())
	{
		LookupCacheEntry& entry = lookup_cache_entries_[index];
		entry.generation = scope_lookup_generations_[start];
		entry.result = result;
		return;
	}
	if ((lookup_cache_entries_.size() + 1) * 10 >
		lookup_cache_slots_.size() * 7)
		RehashLookupCache(lookup_cache_slots_.size() * 2);
	const std::size_t mask = lookup_cache_slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(MixHash(0, start), name), kind) & mask;
	while (lookup_cache_slots_[slot] != 0) slot = (slot + 1) & mask;
	lookup_cache_entries_.push_back(LookupCacheEntry(start, name, kind,
		scope_lookup_generations_[start], result));
	lookup_cache_slots_[slot] =
		static_cast<std::uint32_t>(lookup_cache_entries_.size());
}

LookupResult Model::DirectLookup(ScopeId owner, NameId name,
	LookupKind) const
{
	LookupResult result;
	const Binding* binding = FindDirect(owner, name);
	if (!binding) return result;
	result.declarations_found = true;
	result.type = binding->type;
	result.name_space = binding->name_space;
	result.variable = binding->variable;
	result.first_function = binding->first_function;
	result.last_function = binding->last_function;
	result.type_origin = binding->type_origin;
	result.namespace_origin = binding->namespace_origin;
	return result;
}

LookupResult Model::MergeLookupResults(LookupKind,
	const std::vector<LookupResult>& found)
{
	LookupResult result;
	if (found.empty()) return result;
	if (found.size() == 1) return found[0];
	result.declarations_found = true;
	++candidate_generation_;
	if (candidate_generation_ == 0)
	{
		std::fill(candidate_marks_.begin(), candidate_marks_.end(), 0);
		++candidate_generation_;
	}
	if (candidate_marks_.size() < entities_.size())
		candidate_marks_.resize(entities_.size(), 0);
	std::size_t categories = 0;
	for (std::size_t i = 0; i < found.size(); ++i)
	{
		const LookupResult& source = found[i];
		if (source.ambiguous) result.ambiguous = true;
		if (source.type != 0)
		{
			if (result.type == 0)
			{
				result.type = source.type;
				result.type_origin = source.type_origin;
				++categories;
			}
			else if (result.type_origin != source.type_origin)
				result.ambiguous = true;
		}
		if (source.name_space != kNoScope)
		{
			if (result.name_space == kNoScope)
			{
				result.name_space = source.name_space;
				result.namespace_origin = source.namespace_origin;
				++categories;
			}
			else if (result.namespace_origin != source.namespace_origin)
				result.ambiguous = true;
		}
		if (source.variable != kNoEntity)
		{
			if (result.variable == kNoEntity)
			{
				result.variable = source.variable;
				++categories;
			}
			else if (result.variable != source.variable)
				result.ambiguous = true;
		}
		for (CandidateId id = source.first_function; id != kNoCandidate;
			id = candidates_[id].next)
		{
			const EntityId entity = candidates_[id].entity;
			if (candidate_marks_[entity] == candidate_generation_) continue;
			candidate_marks_[entity] = candidate_generation_;
			if (candidates_.size() >= kNoCandidate)
				ThrowSemanticResourceLimit("too many lookup candidates");
			const CandidateId appended =
				static_cast<CandidateId>(candidates_.size());
			candidates_.push_back(CandidateLink(entity, kNoCandidate));
			if (result.last_function == kNoCandidate)
			{
				result.first_function = appended;
				++categories;
			}
			else candidates_[result.last_function].next = appended;
			result.last_function = appended;
		}
	}
	if (categories > 1) result.ambiguous = true;
	return result;
}

LookupResult Model::SearchScopeGraph(ScopeId start, NameId name,
	LookupKind kind)
{
	if (stats) ++stats->lookup_queries;
	if (stats) ++stats->lookup_scope_visits;
	LookupResult direct = DirectLookup(start, name, kind);
	if (direct.declarations_found ||
		scopes_[start].first_using_edge == kNoUsingEdge) return direct;
	const std::uint32_t cache = FindLookupCache(start, name, kind);
	if (cache != std::numeric_limits<std::uint32_t>::max() &&
		lookup_cache_entries_[cache].generation ==
			scope_lookup_generations_[start])
	{
		if (stats) ++stats->lookup_cache_hits;
		return lookup_cache_entries_[cache].result;
	}
	if (stats) ++stats->lookup_cache_misses;
	BeginScopeWalk(start);
	lookup_results_.clear();
	for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
	{
		const ScopeId scope = lookup_worklist_[head];
		if (scope != start)
		{
			if (stats) ++stats->lookup_scope_visits;
			const LookupResult nested = DirectLookup(scope, name, kind);
			if (nested.declarations_found)
			{
				lookup_results_.push_back(nested);
				continue;
			}
			const std::uint32_t nested_cache =
				FindLookupCache(scope, name, kind);
			if (nested_cache != std::numeric_limits<std::uint32_t>::max() &&
				lookup_cache_entries_[nested_cache].generation ==
					scope_lookup_generations_[scope])
			{
				if (stats) ++stats->lookup_cache_hits;
				const LookupResult& cached =
					lookup_cache_entries_[nested_cache].result;
				if (cached.declarations_found)
					lookup_results_.push_back(cached);
				continue;
			}
		}
		for (UsingEdgeId id = scopes_[scope].first_using_edge;
			id != kNoUsingEdge; id = using_edges_[id].next_from_owner)
		{
			if (stats) ++stats->lookup_edge_visits;
			AddWalkTarget(using_edges_[id].target);
		}
	}
	LookupResult result = MergeLookupResults(kind, lookup_results_);
	StoreLookupCache(start, name, kind, result);
	return result;
}

LookupResult Model::LookupUnqualified(ScopeId current, NameId name,
	LookupKind kind)
{
	for (ScopeId scope = current; scope != kNoScope;
		scope = scopes_[scope].parent)
	{
		const LookupResult result = SearchScopeGraph(scope, name, kind);
		if (result.declarations_found) return result;
	}
	return LookupResult();
}

ScopeId Model::ResolvePrefix(ScopeId current,
	const QualifiedName& name)
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
		const LookupResult first = LookupUnqualified(current,
			name.segments[0], LOOKUP_NAMESPACE);
		if (first.ambiguous || first.name_space == kNoScope) return kNoScope;
		owner = first.name_space;
		index = 1;
	}
	for (; index + 1 < name.segments.size(); ++index)
	{
		const LookupResult next = SearchScopeGraph(owner,
			name.segments[index], LOOKUP_NAMESPACE);
		if (next.ambiguous || next.name_space == kNoScope) return kNoScope;
		owner = next.name_space;
	}
	return owner;
}

bool Model::ResolveNamespaceName(ScopeId current,
	const QualifiedName& name, ScopeId* result)
{
	if (name.segments.empty()) return false;
	if (!name.absolute && name.segments.size() == 1)
	{
		const LookupResult found = LookupUnqualified(current, name.segments[0],
			LOOKUP_NAMESPACE);
		if (found.ambiguous || found.name_space == kNoScope) return false;
		*result = found.name_space;
		return true;
	}
	ScopeId owner = ResolvePrefix(current, name);
	if (owner == kNoScope) return false;
	const LookupResult found = SearchScopeGraph(owner, name.segments.back(),
		LOOKUP_NAMESPACE);
	if (found.ambiguous || found.name_space == kNoScope) return false;
	*result = found.name_space;
	return true;
}

bool Model::ResolveTypeName(ScopeId current,
	const QualifiedName& name, TypeId* result)
{
	if (name.segments.empty()) return false;
	LookupResult found;
	if (!name.absolute && name.segments.size() == 1)
		found = LookupUnqualified(current, name.segments[0], LOOKUP_TYPE);
	else
	{
		const ScopeId owner = ResolvePrefix(current, name);
		if (owner != kNoScope)
			found = SearchScopeGraph(owner, name.segments.back(), LOOKUP_TYPE);
	}
	if (found.ambiguous || found.type == 0) return false;
	*result = found.type;
	return true;
}

bool Model::ResolveUsingTarget(ScopeId current,
	const QualifiedName& name, LookupResult* result)
{
	if (name.segments.size() < 2 && !name.absolute) return false;
	const ScopeId owner = ResolvePrefix(current, name);
	if (owner == kNoScope) return false;
	*result = SearchScopeGraph(owner, name.segments.back(),
		LOOKUP_USING_TARGET);
	return !result->ambiguous && result->name_space == kNoScope &&
		result->Found(LOOKUP_USING_TARGET);
}

EntityId Model::ResolveExpressionEntity(ScopeId current,
	const QualifiedName& name, LookupResult* result)
{
	if (name.segments.empty()) return kNoEntity;
	if (!name.absolute && name.segments.size() == 1)
		*result = LookupUnqualified(current, name.segments[0],
			LOOKUP_EXPRESSION);
	else
	{
		const ScopeId owner = ResolvePrefix(current, name);
		if (owner != kNoScope)
			*result = SearchScopeGraph(owner, name.segments.back(),
				LOOKUP_EXPRESSION);
	}
	if (result->ambiguous) return kNoEntity;
	if (result->variable != kNoEntity) return result->variable;
	if (result->first_function != kNoCandidate &&
		candidates_[result->first_function].next == kNoCandidate)
		return candidates_[result->first_function].entity;
	return kNoEntity;
}

bool Model::ScopeEncloses(ScopeId enclosing, ScopeId nested) const
{
	for (ScopeId scope = nested; scope != kNoScope;
		scope = scopes_[scope].parent)
		if (scope == enclosing) return true;
	return false;
}

ScopeId Model::ResolveDeclaratorOwner(ScopeId current,
	const QualifiedName& name)
{
	if (!name.absolute && name.segments.size() == 1) return current;
	const ScopeId owner = ResolvePrefix(current, name);
	if (owner == kNoScope || !ScopeEncloses(current, owner))
		ThrowSemanticError("qualified declarator is not in an enclosing namespace");
	return owner;
}

EntityId Model::FindFunction(const Binding& binding,
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

EntityId Model::FindExternal(PathId path, NameId name, TypeId type,
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

void Model::AddExternal(PathId path, NameId name, EntityId entity)
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

EntityId Model::Declare(ScopeId current,
	const Declarator& declarator, TypeId type,
	const DeclarationSpecifiers& specifiers, bool definition,
	bool function_definition, std::uint32_t unit)
{
	if (!declarator.has_name || declarator.name.segments.empty())
		ThrowSemanticError("declarator has no name");
	const ScopeId owner = declarator.resolved_owner;
	if (!ScopeEncloses(current, owner))
		ThrowSemanticError("declaration owner is outside its namespace");
	const NameId name = declarator.name.segments.back();
	const bool function = types.IsFunction(type);
	if (specifiers.is_inline && !function)
		ThrowSemanticError("inline specifier on variable");
	if (specifiers.is_thread_local && function)
		ThrowSemanticError("thread_local specifier on function");
	if (specifiers.is_constexpr && function &&
		types.IsVoid(types.Get(type).child))
		ThrowSemanticError("constexpr function returns void");
	if (function_definition && (!function || !declarator.has_function_operation))
		ThrowSemanticError("invalid function definition declarator");
	Binding* existing_binding = FindDirect(owner, name);
	const bool qualified = declarator.name.absolute ||
		declarator.name.segments.size() > 1;
	if (qualified && !existing_binding)
		ThrowSemanticError("qualified declaration was not previously declared");
	InvalidateLookupName(owner, name);
	Binding& binding = existing_binding ? *existing_binding :
		EnsureBinding(owner, name);
	if (binding.type != 0 || binding.name_space != kNoScope ||
		(function && binding.variable != kNoEntity) ||
		(!function && binding.first_function != kNoCandidate))
		ThrowSemanticError("declaration name conflict");
	EntityId entity = function ? FindFunction(binding, type) : binding.variable;
	Linkage linkage = LINKAGE_EXTERNAL;
	if (scopes_[owner].internal_context || specifiers.is_static ||
		(!function && types.IsConst(type) && !specifiers.is_extern))
		linkage = LINKAGE_INTERNAL;
	if (entity == kNoEntity && qualified)
		ThrowSemanticError("qualified declaration does not match an entity");
	if (entity == kNoEntity && linkage == LINKAGE_EXTERNAL)
		entity = FindExternal(scopes_[owner].path, name, type, function);
	if (entity == kNoEntity)
	{
		if (entities_.size() >= kNoEntity)
			ThrowSemanticResourceLimit("too many entities");
		entity = static_cast<EntityId>(entities_.size());
		entities_.push_back(EntityRecord(name, type, owner, linkage, function));
		if (linkage == LINKAGE_EXTERNAL)
			AddExternal(scopes_[owner].path, name, entity);
	}
	EntityRecord& record = entities_[entity];
	if (record.function != function)
		ThrowSemanticError("entity kind mismatch");
	if (function)
	{
		if (!types.SameFunctionSignature(record.type, type) ||
			record.type != type)
			ThrowSemanticError("incompatible function redeclaration");
		AddFunctionCandidate(binding, entity);
	}
	else
	{
		record.type = types.MergeRedeclaration(record.type, type);
		binding.variable = entity;
	}
	if (record.declaration_count != 0 && specifiers.is_static &&
		record.linkage == LINKAGE_EXTERNAL)
		ThrowSemanticError("static declaration follows external declaration");
	if (function && record.declaration_count != 0 &&
		record.constexpr_declared != specifiers.is_constexpr)
		ThrowSemanticError("constexpr function redeclaration mismatch");
	if (record.declaration_count != 0 &&
		record.has_thread_storage != specifiers.is_thread_local)
		ThrowSemanticError("thread_local redeclaration mismatch");
	record.has_thread_storage = specifiers.is_thread_local;
	++record.declaration_count;
	const bool declaration_inline = specifiers.is_inline ||
		specifiers.is_constexpr;
	record.constexpr_declared = record.constexpr_declared ||
		specifiers.is_constexpr;
	if (definition)
	{
		if (record.defined && !(function && record.definitions_inline &&
			declaration_inline && record.definition_unit != unit))
			ThrowSemanticError("multiple definitions");
		if (!record.defined)
		{
			record.defined = true;
			record.definition_unit = unit;
			record.definitions_inline = declaration_inline;
		}
	}
	if (stats) ++stats->declarations;
	return entity;
}

void Model::Define(EntityId entity, TypeId completed_type,
	const InitialValue& initial, bool constant_initialized,
	bool constant_usable, std::uint32_t)
{
	EntityRecord& record = entities_[entity];
	record.type = types.MergeRedeclaration(record.type, completed_type);
	record.initial = initial;
	record.constant_initialized = constant_initialized;
	record.constant_usable = constant_usable;
}

Expression Model::ExpressionForEntity(EntityId entity,
	std::uint32_t translation_unit) const
{
	const EntityRecord& record = entities_[entity];
	Expression expression;
	expression.entity = entity;
	expression.translation_unit = translation_unit;
	expression.category = VALUE_LVALUE;
	expression.type = types.IsReference(record.type) ?
		types.Referred(record.type) : record.type;
	expression.constant_expression =
		(record.constant_usable && record.definition_unit == translation_unit) ||
		types.IsArray(record.type) || record.function;
	return expression;
}

InitialValue Model::ResolveReferenceAddress(EntityId entity) const
{
	const EntityRecord& record = entities_[entity];
	if (!types.IsReference(record.type))
		ThrowSemanticInternal("entity is not a reference");
	return record.initial;
}

InitialValue Model::LvalueAddress(const Expression& expression) const
{
	if (expression.category != VALUE_LVALUE)
		ThrowSemanticError("expression has no object address");
	if (expression.entity == kNoEntity)
	{
		if (expression.value.kind == INITIAL_ADDRESS_STRING)
			return expression.value;
		ThrowSemanticError("expression has no object address");
	}
	const EntityRecord& entity = entities_[expression.entity];
	if (types.IsReference(entity.type)) return ResolveReferenceAddress(
		expression.entity);
	InitialValue result;
	result.kind = INITIAL_ADDRESS_ENTITY;
	result.target = expression.entity;
	return result;
}

InitialValue Model::ResolveObjectValue(EntityId entity,
	std::uint32_t translation_unit, bool* constant) const
{
	const EntityRecord& record = entities_[entity];
	if (types.IsReference(record.type))
	{
		const InitialValue address = ResolveReferenceAddress(entity);
		if (address.kind == INITIAL_ADDRESS_ENTITY)
			return ResolveObjectValue(address.target, translation_unit, constant);
		*constant = false;
		InitialValue unknown;
		unknown.kind = INITIAL_UNKNOWN;
		return unknown;
	}
	*constant = record.constant_usable &&
		record.definition_unit == translation_unit;
	if (!*constant)
	{
		InitialValue unknown;
		unknown.kind = INITIAL_UNKNOWN;
		return unknown;
	}
	return record.initial;
}

InitialValue Model::LvalueToRvalue(const Expression& expression,
	bool* constant) const
{
	if (expression.category != VALUE_LVALUE) return expression.value;
	if (expression.entity == kNoEntity)
	{
		*constant = expression.constant_expression;
		return expression.value;
	}
	if (!expression.constant_expression)
	{
		*constant = false;
		InitialValue unknown;
		unknown.kind = INITIAL_UNKNOWN;
		return unknown;
	}
	return ResolveObjectValue(expression.entity, expression.translation_unit,
		constant);
}

StringId Model::AddString(FundamentalType type,
	const unsigned char* bytes, std::size_t size)
{
	if (size > std::numeric_limits<std::uint32_t>::max() ||
		retained_bytes.size() > std::numeric_limits<std::uint32_t>::max() - size)
		ThrowSemanticResourceLimit("retained string storage is too large");
	if (strings_.size() >= kNoString)
		ThrowSemanticResourceLimit("too many string literals");
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

TemporaryId Model::AddTemporary(TypeId type,
	const InitialValue& initial)
{
	if (temporaries_.size() >= kNoTemporary)
		ThrowSemanticResourceLimit("too many lifetime-extended temporaries");
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

InitialValue Model::ConvertInitializer(TypeId* destination,
	const Expression& source, bool* constant)
{
	if (source.first_function != kNoCandidate)
	{
		TypeId target = *destination;
		const TypeRecord* target_record = &types.Get(target);
		if (target_record->kind == TYPE_LVALUE_REFERENCE ||
			target_record->kind == TYPE_RVALUE_REFERENCE)
		{
			target = target_record->child;
			target_record = &types.Get(types.RemoveTopCv(target));
		}
		else
		{
			target = types.RemoveTopCv(target);
			target_record = &types.Get(target);
			if (target_record->kind != TYPE_POINTER)
				ThrowSemanticError("overloaded function has no target type");
			target = target_record->child;
			target_record = &types.Get(target);
		}
		if (target_record->kind != TYPE_FUNCTION)
			ThrowSemanticError("overloaded function target is not a function");
		EntityId selected = kNoEntity;
		for (CandidateId id = source.first_function; id != kNoCandidate;
			id = candidates_[id].next)
		{
			const EntityId candidate = candidates_[id].entity;
			if (entities_[candidate].type != target) continue;
			if (selected != kNoEntity)
				ThrowSemanticError("ambiguous overloaded function initializer");
			selected = candidate;
		}
		if (selected == kNoEntity)
			ThrowSemanticError("no overloaded function matches target type");
		return ConvertInitializer(destination, ExpressionForEntity(selected,
			source.translation_unit), constant);
	}
	const TypeRecord& destination_record = types.Get(*destination);
	if (destination_record.kind == TYPE_ARRAY)
	{
		if (!source.string_literal)
			ThrowSemanticError("array requires a string initializer");
		TypeId element = types.RemoveTopCv(destination_record.child);
		const TypeRecord& element_record = types.Get(element);
		const StringRecord& string = strings_[source.string_id];
		if (element_record.kind != TYPE_FUNDAMENTAL ||
			!IsCharacterType(element_record.fundamental) ||
			!(element_record.fundamental == string.element_type ||
			  (string.element_type == FT_CHAR &&
			   (element_record.fundamental == FT_SIGNED_CHAR ||
			    element_record.fundamental == FT_UNSIGNED_CHAR))))
			ThrowSemanticError("incompatible string array initializer");
		const std::size_t element_size = FundamentalSize(string.element_type);
		const std::uint64_t elements = string.byte_size / element_size;
		if (destination_record.bound != 0 && destination_record.bound < elements)
			ThrowSemanticError("string is too long for array");
		if (destination_record.bound == 0)
			*destination = types.CompleteArray(*destination, elements);
		const std::size_t bytes = types.SizeOf(*destination);
		if (bytes > std::numeric_limits<std::uint32_t>::max() ||
			retained_bytes.size() >
				std::numeric_limits<std::uint32_t>::max() - bytes)
			ThrowSemanticResourceLimit("retained initializer storage is too large");
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
		const bool related = types.ReferenceRelated(source.type, referred);
		if (source.category == VALUE_LVALUE &&
			types.ReferenceCompatible(source.type, referred))
		{
			if (destination_record.kind == TYPE_RVALUE_REFERENCE)
				ThrowSemanticError("rvalue reference cannot bind an lvalue");
			const InitialValue address = LvalueAddress(source);
			*constant = address.kind == INITIAL_ADDRESS_ENTITY ||
				address.kind == INITIAL_ADDRESS_STRING ||
				address.kind == INITIAL_ADDRESS_TEMPORARY;
			return address;
		}
		if ((destination_record.kind == TYPE_LVALUE_REFERENCE &&
			(!types.IsConst(referred) || types.IsVolatile(referred))) ||
			(destination_record.kind == TYPE_RVALUE_REFERENCE &&
			 source.category == VALUE_LVALUE && related))
			ThrowSemanticError("non-const reference cannot bind a prvalue");
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
				ThrowSemanticError("string literal drops qualifiers");
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
				ThrowSemanticError("array-to-pointer qualification mismatch");
			*constant = source.constant_expression;
			return LvalueAddress(source);
		}
		if (source.category == VALUE_LVALUE &&
			source_record.kind == TYPE_FUNCTION)
		{
			if (plain_destination.child != source_plain)
				ThrowSemanticError("function pointer type mismatch");
			*constant = true;
			return LvalueAddress(source);
		}
		bool value_constant = source.constant_expression;
		InitialValue value = source.category == VALUE_LVALUE ?
			LvalueToRvalue(source, &value_constant) : source.value;
		if (source_record.kind != TYPE_POINTER ||
			!PointerTargetConvertible(types, source_record.child,
				plain_destination.child))
			ThrowSemanticError("invalid pointer initializer");
		*constant = value_constant;
		return value;
	}
	if (plain_destination.kind != TYPE_FUNDAMENTAL ||
		plain_destination.fundamental == FT_VOID ||
		plain_destination.fundamental == FT_NULLPTR_T)
		ThrowSemanticError("invalid scalar initializer destination");
	const TypeId source_plain = types.RemoveTopCv(source.type);
	const TypeRecord& source_record = types.Get(source_plain);
	if (plain_destination.fundamental == FT_BOOL &&
		(source_record.kind == TYPE_ARRAY ||
		 source_record.kind == TYPE_FUNCTION))
	{
		InitialValue result;
		result.kind = INITIAL_SCALAR;
		result.scalar_type = FT_BOOL;
		result.bytes[0] = 1;
		*constant = true;
		return result;
	}
	bool value_constant = source.constant_expression;
	InitialValue value = source.category == VALUE_LVALUE ?
		LvalueToRvalue(source, &value_constant) : source.value;
	if (plain_destination.fundamental == FT_BOOL &&
		(source_record.kind == TYPE_POINTER || source.null_pointer_constant))
	{
		if (!value_constant || value.kind == INITIAL_UNKNOWN)
		{
			InitialValue unknown;
			unknown.kind = INITIAL_UNKNOWN;
			*constant = false;
			return unknown;
		}
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
		ThrowSemanticError("invalid arithmetic initializer");
	if (value.kind != INITIAL_SCALAR)
	{
		value.kind = INITIAL_UNKNOWN;
		*constant = false;
		return value;
	}
	*constant = value_constant;
	return ConvertArithmetic(value, plain_destination.fundamental);
}

bool Model::ContextualBool(const Expression& expression,
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
		ThrowSemanticError("expression is not contextually convertible to bool");
	return ReadArithmetic(value) != 0;
}

std::uint64_t Model::ResolveAddress(const InitialValue& value) const
{
	std::uint64_t address = 0;
	if (value.kind == INITIAL_ADDRESS_ENTITY)
	{
		const EntityRecord& entity = entities_[value.target];
		if (entity.image_offset == std::numeric_limits<std::uint64_t>::max())
			ThrowSemanticError("address refers to an undefined entity");
		address = entity.image_offset;
	}
	else if (value.kind == INITIAL_ADDRESS_STRING)
		address = strings_[value.target].image_offset;
	else if (value.kind == INITIAL_ADDRESS_TEMPORARY)
		address = temporaries_[value.target].image_offset;
	else if (value.kind != INITIAL_ZERO)
		ThrowSemanticInternal("value is not an address");
	return static_cast<std::uint64_t>(
		static_cast<std::int64_t>(address) + value.addend);
}

void Model::WriteImage(std::ostream& output)
{
	if (image_written_) ThrowSemanticInternal("program image already written");
	image_written_ = true;
	std::uint64_t offset = 4;
	for (EntityId id = 0; id < entities_.size(); ++id)
	{
		EntityRecord& entity = entities_[id];
		if (!entity.function && !entity.defined) continue;
		offset = Align(offset, types.AlignOf(entity.type));
		entity.image_offset = offset;
		AddImageSize(&offset, types.SizeOf(entity.type));
	}
	for (TemporaryId id = 0; id < temporaries_.size(); ++id)
	{
		TemporaryRecord& temporary = temporaries_[id];
		offset = Align(offset, types.AlignOf(temporary.type));
		temporary.image_offset = offset;
		AddImageSize(&offset, types.SizeOf(temporary.type));
	}
	for (StringId id = 0; id < strings_.size(); ++id)
	{
		StringRecord& string = strings_[id];
		offset = Align(offset, FundamentalSize(string.element_type));
		string.image_offset = offset;
		AddImageSize(&offset, string.byte_size);
	}
	if (offset > std::numeric_limits<std::size_t>::max())
		ThrowSemanticResourceLimit("program image is too large");
	const unsigned char magic[4] = {'P', 'A', '8', 0};
	WriteRaw(output, magic, sizeof(magic));
	std::uint64_t written = sizeof(magic);
	const std::array<unsigned char, 4> function_stub = {{'f', 'u', 'n', 0}};
	const auto write_initial = [this, &output](TypeId type,
		const InitialValue& value)
	{
		const std::size_t size = types.SizeOf(type);
		if (value.kind == INITIAL_SCALAR)
		{
			WriteRaw(output, value.bytes.data(), size);
			return;
		}
		if (value.kind == INITIAL_ARRAY_BYTES)
		{
			if (value.byte_size > size ||
				value.byte_size > retained_bytes.size() ||
				value.byte_offset > retained_bytes.size() - value.byte_size)
				ThrowSemanticInternal("invalid retained initializer range");
			WriteRaw(output, &retained_bytes[value.byte_offset], value.byte_size);
			WriteZeros(output, size - value.byte_size);
			return;
		}
		if (value.kind == INITIAL_ADDRESS_ENTITY ||
			value.kind == INITIAL_ADDRESS_STRING ||
			value.kind == INITIAL_ADDRESS_TEMPORARY)
		{
			if (size != sizeof(std::uint64_t))
				ThrowSemanticInternal("address initializer has non-pointer size");
			const std::uint64_t address = ResolveAddress(value);
			WriteRaw(output, &address, sizeof(address));
			return;
		}
		WriteZeros(output, size);
	};
	for (EntityId id = 0; id < entities_.size(); ++id)
	{
		const EntityRecord& entity = entities_[id];
		if (entity.image_offset == std::numeric_limits<std::uint64_t>::max())
			continue;
		if (entity.image_offset < written)
			ThrowSemanticInternal("entity image order is not monotonic");
		WriteZeros(output, entity.image_offset - written);
		if (entity.function)
			WriteRaw(output, function_stub.data(), function_stub.size());
		else if (entity.constant_initialized)
			write_initial(entity.type, entity.initial);
		else WriteZeros(output, types.SizeOf(entity.type));
		written = entity.image_offset + types.SizeOf(entity.type);
	}
	for (TemporaryId id = 0; id < temporaries_.size(); ++id)
	{
		const TemporaryRecord& temporary = temporaries_[id];
		if (temporary.image_offset < written)
			ThrowSemanticInternal("temporary image order is not monotonic");
		WriteZeros(output, temporary.image_offset - written);
		write_initial(temporary.type, temporary.initial);
		written = temporary.image_offset + types.SizeOf(temporary.type);
	}
	for (StringId id = 0; id < strings_.size(); ++id)
	{
		const StringRecord& string = strings_[id];
		if (string.image_offset < written ||
			string.byte_size > retained_bytes.size() ||
			string.byte_offset > retained_bytes.size() - string.byte_size)
			ThrowSemanticInternal("invalid string image range");
		WriteZeros(output, string.image_offset - written);
		WriteRaw(output, &retained_bytes[string.byte_offset], string.byte_size);
		written = string.image_offset + string.byte_size;
	}
	if (written > offset) ThrowSemanticInternal("program image overflow");
	WriteZeros(output, offset - written);
	if (stats) stats->image_bytes = static_cast<std::size_t>(offset);
}

void Model::FinishStats()
{
	if (!stats) return;
	stats->identifiers = identifiers.Size();
	stats->identifier_bytes = identifiers.StorageBytes();
	stats->canonical_types = types.Size();
	stats->canonical_type_bytes = types.StorageBytes();
	stats->scopes = scopes_.size();
	stats->lookup_cache_entries = lookup_cache_entries_.size();
	stats->semantic_storage_bytes = StorageBytes();
	stats->peak_stage_storage_bytes = std::max(
		stats->peak_stage_storage_bytes, stats->semantic_storage_bytes);
}

std::uint32_t Model::CurrentUnit() const
{
	return current_unit_;
}

std::size_t Model::StorageBytes() const
{
	return identifiers.StorageBytes() + types.StorageBytes() +
		retained_bytes.capacity() +
		scopes_.capacity() * sizeof(ScopeRecord) +
		bindings_.capacity() * sizeof(Binding) +
		binding_slots_.capacity() * sizeof(std::uint32_t) +
		candidates_.capacity() * sizeof(CandidateLink) +
		using_edges_.capacity() * sizeof(UsingEdgeRecord) +
		using_edge_slots_.capacity() * sizeof(std::uint32_t) +
		lookup_cache_entries_.capacity() * sizeof(LookupCacheEntry) +
		lookup_cache_slots_.capacity() * sizeof(std::uint32_t) +
		scope_lookup_generations_.capacity() * sizeof(std::uint32_t) +
		entities_.capacity() * sizeof(EntityRecord) +
		external_slots_.capacity() * sizeof(std::uint32_t) +
		external_entities_.capacity() * sizeof(EntityId) +
		path_parents_.capacity() * sizeof(PathId) +
		path_names_.capacity() * sizeof(NameId) +
		path_slots_.capacity() * sizeof(PathId) +
		strings_.capacity() * sizeof(StringRecord) +
		temporaries_.capacity() * sizeof(TemporaryRecord) +
		lookup_marks_.capacity() * sizeof(std::uint32_t) +
		lookup_worklist_.capacity() * sizeof(ScopeId) +
		lookup_results_.capacity() * sizeof(LookupResult) +
		candidate_marks_.capacity() * sizeof(std::uint32_t);
}

}
}
