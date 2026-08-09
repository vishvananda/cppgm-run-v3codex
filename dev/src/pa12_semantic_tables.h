#pragma once

#include "pa12_semantic_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa11;

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

class CompactIndexSequence
{
public:
	CompactIndexSequence();
	std::size_t Size() const;
	std::size_t operator[](std::size_t index) const;
	bool Contains(std::size_t value) const;
	void Push(std::size_t value);
	std::vector<std::size_t> Copy() const;
	std::size_t StorageBytes() const;

private:
	std::size_t inline_values_[2];
	std::vector<std::size_t> overflow_;
	std::size_t size_;
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

struct TemplateSpecializationKey
{
	std::size_t pattern;
	std::vector<TemplateArgument> arguments;

	TemplateSpecializationKey() : pattern(0) {}
	TemplateSpecializationKey(std::size_t pattern_value,
		const std::vector<TemplateArgument>& argument_values)
		: pattern(pattern_value), arguments(argument_values) {}
	bool operator==(const TemplateSpecializationKey& other) const
	{
		return pattern == other.pattern && arguments == other.arguments;
	}
};

struct TemplateSpecializationHash
{
	std::size_t operator()(const TemplateSpecializationKey& key) const
	{
		std::size_t result = key.pattern;
		for (std::size_t i = 0; i < key.arguments.size(); ++i)
		{
			result = MixHash(result, key.arguments[i].kind);
			result = MixHash(result, key.arguments[i].type);
			result = MixHash(result,
				static_cast<std::uint64_t>(key.arguments[i].value));
		}
		return result;
	}
};

class TemplateSpecializationTable
{
public:
	TemplateSpecializationTable();
	BindingId Find(const TemplateSpecializationKey& key) const;
	void Insert(const TemplateSpecializationKey& key, BindingId binding);
	std::size_t StorageBytes() const;

private:
	struct Entry
	{
		TemplateSpecializationKey key;
		BindingId binding;
		Entry(const TemplateSpecializationKey& key_value, BindingId binding_value);
	};
	void Rehash(std::size_t capacity);
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

}
}
