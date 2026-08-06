#pragma once

#include "pa11_model.h"

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
	std::vector<TypeId> arguments;

	TemplateSpecializationKey() : pattern(0) {}
	TemplateSpecializationKey(std::size_t pattern_value,
		const std::vector<TypeId>& argument_values)
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
			result = MixHash(result, key.arguments[i]);
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
