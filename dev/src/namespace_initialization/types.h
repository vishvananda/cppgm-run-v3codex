#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "preprocess/tokens/post_tokenizer.h"

namespace cppgm
{
namespace namespace_initialization
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
typedef std::uint32_t UsingEdgeId;

const ScopeId kNoScope = std::numeric_limits<ScopeId>::max();
const BindingId kNoBinding = std::numeric_limits<BindingId>::max();
const EntityId kNoEntity = std::numeric_limits<EntityId>::max();
const CandidateId kNoCandidate = std::numeric_limits<CandidateId>::max();
const StringId kNoString = std::numeric_limits<StringId>::max();
const TemporaryId kNoTemporary = std::numeric_limits<TemporaryId>::max();
const UsingEdgeId kNoUsingEdge =
	std::numeric_limits<UsingEdgeId>::max();

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
	bool IsVolatile(TypeId type) const;
	bool SameFunctionSignature(TypeId left, TypeId right) const;
	bool QualificationConvertible(TypeId source, TypeId destination) const;
	bool PointeeQualificationConvertible(TypeId source,
		TypeId destination) const;
	bool ReferenceRelated(TypeId source, TypeId destination) const;
	bool ReferenceCompatible(TypeId source, TypeId destination) const;
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
	bool QualificationConvertibleAtDepth(TypeId source, TypeId destination,
		std::size_t pointer_depth) const;
	void Rehash(std::size_t capacity);
	std::vector<TypeRecord> types_;
	std::vector<TypeId> parameters_;
	std::vector<TypeId> slots_;
};

}
}
