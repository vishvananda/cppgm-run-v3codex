#include "pa12_semantic_tables.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

CompactIndexSequence::CompactIndexSequence()
	: inline_values_(), size_(0)
{
}

std::size_t CompactIndexSequence::Size() const
{
	return size_;
}

std::size_t CompactIndexSequence::operator[](std::size_t index) const
{
	return index < 2 ? inline_values_[index] : overflow_[index - 2];
}

bool CompactIndexSequence::Contains(std::size_t value) const
{
	for (std::size_t i = 0; i < size_; ++i)
		if ((*this)[i] == value) return true;
	return false;
}

void CompactIndexSequence::Push(std::size_t value)
{
	if (size_ < 2) inline_values_[size_] = value;
	else overflow_.push_back(value);
	++size_;
}

std::vector<std::size_t> CompactIndexSequence::Copy() const
{
	std::vector<std::size_t> result;
	result.reserve(size_);
	for (std::size_t i = 0; i < size_; ++i) result.push_back((*this)[i]);
	return result;
}

std::size_t CompactIndexSequence::StorageBytes() const
{
	return overflow_.capacity() * sizeof(std::size_t);
}

IndexedSequenceTable::Entry::Entry(std::uint64_t key_value)
	: key(key_value)
{
}

IndexedSequenceTable::IndexedSequenceTable()
	: slots_(32, 0)
{
}

std::size_t IndexedSequenceTable::Hash(std::uint64_t key)
{
	return MixHash(static_cast<std::size_t>(key >> 32),
		static_cast<std::uint32_t>(key));
}

void IndexedSequenceTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = Hash(entries_[i].key) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

CompactIndexSequence& IndexedSequenceTable::Ensure(std::uint64_t key)
{
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return entry.values;
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many indexed semantic sequences");
	entries_.push_back(Entry(key));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
	return entries_.back().values;
}

const CompactIndexSequence* IndexedSequenceTable::Find(
	std::uint64_t key) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		const Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return &entry.values;
		slot = (slot + 1) & mask;
	}
	return 0;
}

std::size_t IndexedSequenceTable::StorageBytes() const
{
	std::size_t bytes = entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t);
	for (std::size_t i = 0; i < entries_.size(); ++i)
		bytes += entries_[i].values.StorageBytes();
	return bytes;
}

CallConversionTable::Entry::Entry(std::uint64_t key_value,
	const CallConversionFact& fact_value)
	: key(key_value), fact(fact_value)
{
}

CallConversionTable::CallConversionTable()
	: slots_(32, 0)
{
}

std::size_t CallConversionTable::Hash(std::uint64_t key)
{
	return MixHash(static_cast<std::size_t>(key >> 32),
		static_cast<std::uint32_t>(key));
}

void CallConversionTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = Hash(entries_[i].key) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

const CallConversionFact* CallConversionTable::Find(std::uint64_t key) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		const Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return &entry.fact;
		slot = (slot + 1) & mask;
	}
	return 0;
}

void CallConversionTable::Insert(std::uint64_t key,
	const CallConversionFact& fact)
{
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key)
		{
			entry.fact = fact;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many call conversion facts");
	entries_.push_back(Entry(key, fact));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
}

FunctionSignatureTable::Entry::Entry(const FunctionSignatureKey& key_value,
	BindingId binding_value)
	: key(key_value), binding(binding_value)
{
}

FunctionSignatureTable::FunctionSignatureTable()
	: slots_(32, 0)
{
}

BindingId FunctionSignatureTable::Find(
	const FunctionSignatureKey& key) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = FunctionSignatureHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		const Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return entry.binding;
		slot = (slot + 1) & mask;
	}
	return kNoBinding;
}

void FunctionSignatureTable::Insert(const FunctionSignatureKey& key,
	BindingId binding)
{
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = FunctionSignatureHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key)
		{
			entry.binding = binding;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many function signatures");
	entries_.push_back(Entry(key, binding));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
}

void FunctionSignatureTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = FunctionSignatureHash()(entries_[i].key) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

std::size_t FunctionSignatureTable::StorageBytes() const
{
	return entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t);
}

TemplateSpecializationTable::Entry::Entry(
	const TemplateSpecializationKey& key_value, BindingId binding_value)
	: key(key_value), binding(binding_value)
{
}

TemplateSpecializationTable::TemplateSpecializationTable()
	: slots_(32, 0)
{
}

BindingId TemplateSpecializationTable::Find(
	const TemplateSpecializationKey& key) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = TemplateSpecializationHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		const Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return entry.binding;
		slot = (slot + 1) & mask;
	}
	return kNoBinding;
}

void TemplateSpecializationTable::Insert(
	const TemplateSpecializationKey& key, BindingId binding)
{
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = TemplateSpecializationHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key)
		{
			entry.binding = binding;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many template specializations");
	entries_.push_back(Entry(key, binding));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
}

void TemplateSpecializationTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = TemplateSpecializationHash()(entries_[i].key) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

std::size_t TemplateSpecializationTable::StorageBytes() const
{
	std::size_t bytes = entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t);
	for (std::size_t i = 0; i < entries_.size(); ++i)
		bytes += entries_[i].key.arguments.capacity() * sizeof(TypeId);
	return bytes;
}

}
}
