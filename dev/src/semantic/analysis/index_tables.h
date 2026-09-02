#pragma once

#include "semantic/model/graph.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace semantic
{


struct FunctionSignatureKey
{
	ScopeId owner;
	NameId name;
	TypeId signature;

	FunctionSignatureKey()
		: owner(kNoScope), name(0), signature(kNoType) {}
	FunctionSignatureKey(ScopeId owner_value, NameId name_value,
		TypeId signature_value)
		: owner(owner_value), name(name_value), signature(signature_value) {}
	bool operator==(const FunctionSignatureKey& other) const
	{
		return owner == other.owner && name == other.name &&
			signature == other.signature;
	}
};

struct FunctionSignatureHash
{
	std::size_t operator()(const FunctionSignatureKey& key) const
	{
		return MixHash(MixHash(key.owner, key.name), key.signature);
	}
};

struct UsingFunctionIdentityKey
{
	ScopeId owner;
	NameId name;
	BindingId canonical;

	UsingFunctionIdentityKey(ScopeId owner_value, NameId name_value,
		BindingId canonical_value)
		: owner(owner_value), name(name_value), canonical(canonical_value) {}
	bool operator==(const UsingFunctionIdentityKey& other) const
	{
		return owner == other.owner && name == other.name &&
			canonical == other.canonical;
	}
};

class UsingFunctionIdentityTable
{
public:
	UsingFunctionIdentityTable();
	bool Insert(const UsingFunctionIdentityKey& key);
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);
	std::vector<UsingFunctionIdentityKey> entries_;
	std::vector<std::uint32_t> slots_;
};

struct EnumOperatorCandidateKey
{
	ScopeId owner;
	NameId name;
	TypeId enum_type;
	std::uint8_t operand;

	EnumOperatorCandidateKey()
		: owner(kNoScope), name(0), enum_type(kNoType), operand(0) {}
	EnumOperatorCandidateKey(ScopeId owner_value, NameId name_value,
		TypeId enum_type_value, std::uint8_t operand_value)
		: owner(owner_value), name(name_value), enum_type(enum_type_value),
		  operand(operand_value) {}
	bool operator==(const EnumOperatorCandidateKey& other) const
	{
		return owner == other.owner && name == other.name &&
			enum_type == other.enum_type && operand == other.operand;
	}
};

struct EnumOperatorCandidateHash
{
	std::size_t operator()(const EnumOperatorCandidateKey& key) const
	{
		return MixHash(MixHash(MixHash(key.owner, key.name), key.enum_type),
			key.operand);
	}
};

// Request-local canonical binding set. Open addressing keeps the candidate
// path contiguous and preserves first-seen overload order without node-based
// allocation.
class FlatBindingIdSet
{
public:
	FlatBindingIdSet();
	bool Insert(BindingId binding);

private:
	void Rehash(std::size_t capacity);
	std::vector<std::uint32_t> slots_;
	std::size_t size_;
};

class CompactIndexSequence
{
public:
	CompactIndexSequence();
	std::size_t Size() const;
	std::size_t operator[](std::size_t index) const;
	bool Contains(std::size_t value) const;
	void Push(std::size_t value);
	void Clear();
	std::vector<std::size_t> Copy() const;
	std::size_t StorageBytes() const;

private:
	std::size_t inline_values_[2];
	std::vector<std::size_t> overflow_;
	std::size_t size_;
};

// One canonical (scope, name) pack fact with both direct and per-scope access.
// The secondary links avoid either rescanning all packs or duplicating their
// names in a separate reverse table.
class TemplateArgumentPackBindingTable
{
public:
	TemplateArgumentPackBindingTable();
	CompactIndexSequence& Insert(ScopeId scope, NameId name);
	const CompactIndexSequence* Find(std::uint64_t key) const;
	void CopyNames(ScopeId scope, std::vector<NameId>* names) const;
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		std::uint64_t key;
		CompactIndexSequence values;
		std::uint32_t next_in_scope;
		Entry(std::uint64_t key_value, std::uint32_t next_value);
	};
	static std::size_t Hash(std::uint64_t key);
	void Rehash(std::size_t capacity);

	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
	std::vector<std::uint32_t> scope_heads_;
};

class IndexedSequenceTable
{
public:
	IndexedSequenceTable();
	CompactIndexSequence& Ensure(std::uint64_t key);
	const CompactIndexSequence* Find(std::uint64_t key) const;
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		std::uint64_t key;
		CompactIndexSequence values;
		explicit Entry(std::uint64_t key_value);
	};
	static std::size_t Hash(std::uint64_t key);
	void Rehash(std::size_t capacity);

	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

class EnumOperatorCandidateTable
{
public:
	EnumOperatorCandidateTable();
	CompactIndexSequence& Ensure(const EnumOperatorCandidateKey& key);
	const CompactIndexSequence* Find(
		const EnumOperatorCandidateKey& key) const;
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		EnumOperatorCandidateKey key;
		CompactIndexSequence values;
		explicit Entry(const EnumOperatorCandidateKey& key_value);
	};
	void Rehash(std::size_t capacity);

	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

class CallConversionTable
{
public:
	CallConversionTable();
	const CallConversionFact* Find(std::uint64_t key) const;
	void Insert(std::uint64_t key, const CallConversionFact& fact);

private:
	struct Entry
	{
		std::uint64_t key;
		CallConversionFact fact;
		Entry(std::uint64_t key_value, const CallConversionFact& fact_value);
	};
	static std::size_t Hash(std::uint64_t key);
	void Rehash(std::size_t capacity);

	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

class FunctionSignatureTable
{
public:
	FunctionSignatureTable();
	BindingId Find(const FunctionSignatureKey& key) const;
	void Insert(const FunctionSignatureKey& key, BindingId binding);
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		FunctionSignatureKey key;
		BindingId binding;
		Entry(const FunctionSignatureKey& key_value, BindingId binding_value);
	};
	void Rehash(std::size_t capacity);
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

typedef std::uint32_t TemplateArgumentPartitionId;

const TemplateArgumentPartitionId kEmptyTemplateArgumentPartition = 0;

class TemplateArgumentPartitionTable
{
public:
	TemplateArgumentPartitionTable();
	TemplateArgumentPartitionId Intern(
		const std::vector<std::uint32_t>& offsets);
	std::size_t Requests() const;
	std::size_t CacheHits() const;
	std::size_t IndexProbes() const;
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		std::uint32_t first, count;
		std::size_t hash;
		Entry(std::uint32_t first_value, std::uint32_t count_value,
			std::size_t hash_value)
			: first(first_value), count(count_value), hash(hash_value) {}
	};
	void Rehash(std::size_t capacity);
	std::vector<std::uint32_t> offsets_;
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
	ObservationCounter requests_, cache_hits_, index_probes_;
};

// Canonical identity for alias-expanded dependent function results. The
// sequence contains typed compact atoms (node kinds, interned names, canonical
// declarations, and structural delimiters), never rendered source text.
enum FunctionTemplateResultIdentityAtomKind
{
	FUNCTION_TEMPLATE_RESULT_NODE_BEGIN = 1,
	FUNCTION_TEMPLATE_RESULT_NODE_PAYLOAD,
	FUNCTION_TEMPLATE_RESULT_NODE_END,
	FUNCTION_TEMPLATE_RESULT_PARAMETER,
	FUNCTION_TEMPLATE_RESULT_SUBSTITUTION_BEGIN,
	FUNCTION_TEMPLATE_RESULT_SUBSTITUTION_END,
	FUNCTION_TEMPLATE_RESULT_TYPE,
	FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN,
	FUNCTION_TEMPLATE_RESULT_QUALIFIED_END,
	FUNCTION_TEMPLATE_RESULT_COMPONENT,
	FUNCTION_TEMPLATE_RESULT_DECLARATION,
	FUNCTION_TEMPLATE_RESULT_ENTITY,
	FUNCTION_TEMPLATE_RESULT_ARGUMENTS_BEGIN,
	FUNCTION_TEMPLATE_RESULT_ARGUMENT_BEGIN,
	FUNCTION_TEMPLATE_RESULT_ARGUMENT_END,
	FUNCTION_TEMPLATE_RESULT_ARGUMENTS_END,
	FUNCTION_TEMPLATE_RESULT_PACK_EXPANSION
};

class FunctionTemplateResultIdentityTable
{
public:
	FunctionTemplateResultIdentityTable();
	FunctionTemplateResultIdentityId Intern(
		const std::vector<std::uint64_t>& atoms);
	void CopyAtoms(FunctionTemplateResultIdentityId identity,
		std::vector<std::uint64_t>* atoms) const;
	std::size_t Requests() const;
	std::size_t CacheHits() const;
	std::size_t IndexProbes() const;
	std::size_t AtomVisits() const;
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		std::uint32_t first, count;
		std::size_t hash;
		Entry(std::uint32_t first_value, std::uint32_t count_value,
			std::size_t hash_value)
			: first(first_value), count(count_value), hash(hash_value) {}
	};
	void Rehash(std::size_t capacity);
	std::vector<std::uint64_t> atoms_;
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
	ObservationCounter requests_, cache_hits_, index_probes_, atom_visits_;
};

struct TemplateSpecializationKey
{
	std::size_t pattern;
	TemplateArgumentListId arguments;
	TemplateArgumentPartitionId partition;

	TemplateSpecializationKey()
		: pattern(0), arguments(kNoTemplateArgumentList),
		  partition(kEmptyTemplateArgumentPartition) {}
	TemplateSpecializationKey(std::size_t pattern_value,
		TemplateArgumentListId argument_values)
		: pattern(pattern_value), arguments(argument_values),
		  partition(kEmptyTemplateArgumentPartition) {}
	TemplateSpecializationKey(std::size_t pattern_value,
		TemplateArgumentListId argument_values,
		TemplateArgumentPartitionId offset_values)
		: pattern(pattern_value), arguments(argument_values),
		  partition(offset_values) {}
	bool operator==(const TemplateSpecializationKey& other) const
	{
		return pattern == other.pattern && arguments == other.arguments &&
			partition == other.partition;
	}
};

struct TemplateSpecializationHash
{
	std::size_t operator()(const TemplateSpecializationKey& key) const
	{
		return MixHash(MixHash(key.pattern, key.arguments),
			key.partition);
	}
};

enum TemplateRequestState
{
	TEMPLATE_REQUEST_NOT_STARTED,
	TEMPLATE_REQUEST_IN_PROGRESS,
	TEMPLATE_REQUEST_SUCCEEDED,
	TEMPLATE_REQUEST_FAILED
};

class TemplateSpecializationTable
{
public:
	TemplateSpecializationTable();
	BindingId Find(const TemplateSpecializationKey& key) const;
	void Insert(const TemplateSpecializationKey& key, BindingId binding);
	TemplateRequestState FindRequest(const TemplateSpecializationKey& key,
		BindingId* binding) const;
	void SetRequest(const TemplateSpecializationKey& key,
		TemplateRequestState state, BindingId binding = kNoBinding);
	void ResetInProgressRequest(const TemplateSpecializationKey& key);
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		TemplateSpecializationKey key;
		BindingId binding;
		TemplateRequestState state;
		Entry(const TemplateSpecializationKey& key_value,
			BindingId binding_value, TemplateRequestState state_value);
	};
	void Rehash(std::size_t capacity);
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

}
}
