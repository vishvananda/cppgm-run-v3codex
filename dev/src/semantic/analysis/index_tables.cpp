#include "semantic/analysis/index_tables.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

namespace
{

template<typename Entry>
void RebuildCachedHashSlots(const std::vector<Entry>& entries,
	std::size_t capacity, std::vector<std::uint32_t>* slots)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		std::size_t slot = entries[i].hash & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots->swap(replacement);
}

}

UsingFunctionIdentityTable::UsingFunctionIdentityTable()
	: slots_(32, 0)
{
}

bool UsingFunctionIdentityTable::Insert(const UsingFunctionIdentityKey& key)
{
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = MixHash(MixHash(key.owner, key.name), key.canonical) & mask;
	while (slots_[slot] != 0)
	{
		if (entries_[slots_[slot] - 1] == key) return false;
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many using-function identities");
	entries_.push_back(key);
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
	return true;
}

void UsingFunctionIdentityTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		const UsingFunctionIdentityKey& key = entries_[i];
		std::size_t slot =
			MixHash(MixHash(key.owner, key.name), key.canonical) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

std::size_t UsingFunctionIdentityTable::StorageBytes() const
{
	return entries_.capacity() * sizeof(UsingFunctionIdentityKey) +
		slots_.capacity() * sizeof(std::uint32_t);
}

FlatBindingIdSet::FlatBindingIdSet()
	: slots_(8, 0), size_(0)
{
}

bool FlatBindingIdSet::Insert(BindingId binding)
{
	if (binding == kNoBinding)
		ThrowInternalCompilerError("invalid canonical binding identity");
	if ((size_ + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = MixHash(0, binding) & mask;
	while (slots_[slot] != 0)
	{
		if (slots_[slot] - 1 == binding) return false;
		slot = (slot + 1) & mask;
	}
	slots_[slot] = binding + 1;
	++size_;
	return true;
}

void FlatBindingIdSet::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < slots_.size(); ++i)
	{
		if (slots_[i] == 0) continue;
		const BindingId binding = slots_[i] - 1;
		std::size_t slot = MixHash(0, binding) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = binding + 1;
	}
	slots_.swap(replacement);
}

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

void CompactIndexSequence::Clear()
{
	overflow_.clear();
	size_ = 0;
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

TemplateArgumentPackBindingTable::Entry::Entry(
	std::uint64_t key_value, std::uint32_t next_value)
	: key(key_value), next_in_scope(next_value)
{
}

TemplateArgumentPackBindingTable::TemplateArgumentPackBindingTable()
	: slots_(32, 0)
{
}

std::size_t TemplateArgumentPackBindingTable::Hash(std::uint64_t key)
{
	return MixHash(static_cast<std::size_t>(key >> 32),
		static_cast<std::uint32_t>(key));
}

void TemplateArgumentPackBindingTable::Rehash(std::size_t capacity)
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

CompactIndexSequence& TemplateArgumentPackBindingTable::Insert(
	ScopeId scope, NameId name)
{
	if (scope == kNoScope || name == 0)
		ThrowInternalCompilerError("template argument pack key is invalid");
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::uint64_t key =
		(static_cast<std::uint64_t>(scope) << 32) | name;
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		if (entries_[slots_[slot] - 1].key == key)
			ThrowInternalCompilerError(
				"template argument pack rebound in one scope");
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many template argument pack bindings");
	if (scope_heads_.size() <= scope)
		scope_heads_.resize(static_cast<std::size_t>(scope) + 1, 0);
	entries_.push_back(Entry(key, scope_heads_[scope]));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
	scope_heads_[scope] = static_cast<std::uint32_t>(entries_.size());
	return entries_.back().values;
}

const CompactIndexSequence* TemplateArgumentPackBindingTable::Find(
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

void TemplateArgumentPackBindingTable::CopyNames(
	ScopeId scope, std::vector<NameId>* names) const
{
	names->clear();
	if (scope >= scope_heads_.size()) return;
	for (std::uint32_t link = scope_heads_[scope]; link != 0;)
	{
		if (link > entries_.size())
			ThrowInternalCompilerError(
				"template argument pack scope index is invalid");
		const Entry& entry = entries_[link - 1];
		if (static_cast<ScopeId>(entry.key >> 32) != scope)
			ThrowInternalCompilerError(
				"template argument pack scope index owner is invalid");
		names->push_back(static_cast<NameId>(entry.key));
		link = entry.next_in_scope;
	}
}

std::size_t TemplateArgumentPackBindingTable::StorageBytes() const
{
	std::size_t bytes = entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t) +
		scope_heads_.capacity() * sizeof(std::uint32_t);
	for (std::size_t i = 0; i < entries_.size(); ++i)
		bytes += entries_[i].values.StorageBytes();
	return bytes;
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
		ThrowSemanticResourceLimit("too many indexed semantic sequences");
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

EnumOperatorCandidateTable::Entry::Entry(
	const EnumOperatorCandidateKey& key_value)
	: key(key_value)
{
}

EnumOperatorCandidateTable::EnumOperatorCandidateTable()
	: slots_(32, 0)
{
}

void EnumOperatorCandidateTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = EnumOperatorCandidateHash()(entries_[i].key) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

CompactIndexSequence& EnumOperatorCandidateTable::Ensure(
	const EnumOperatorCandidateKey& key)
{
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = EnumOperatorCandidateHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return entry.values;
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many indexed enum operator sequences");
	entries_.push_back(Entry(key));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
	return entries_.back().values;
}

const CompactIndexSequence* EnumOperatorCandidateTable::Find(
	const EnumOperatorCandidateKey& key) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = EnumOperatorCandidateHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		const Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key) return &entry.values;
		slot = (slot + 1) & mask;
	}
	return 0;
}

std::size_t EnumOperatorCandidateTable::StorageBytes() const
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
		ThrowSemanticResourceLimit("too many call conversion facts");
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
		ThrowSemanticResourceLimit("too many function signatures");
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

TemplateArgumentPartitionTable::TemplateArgumentPartitionTable()
	: slots_(32, 0), requests_(0), cache_hits_(0), index_probes_(0)
{
	// Empty partitions are common to class, alias, and variable templates.
	// Reserve identity zero so those hot keys need no interner probe.
	entries_.push_back(Entry(0, 0, MixHash(0, 0)));
	slots_[entries_[0].hash & (slots_.size() - 1)] = 1;
}

void TemplateArgumentPartitionTable::Rehash(std::size_t capacity)
{
	RebuildCachedHashSlots(entries_, capacity, &slots_);
}

TemplateArgumentPartitionId TemplateArgumentPartitionTable::Intern(
	const std::vector<std::uint32_t>& offsets)
{
	if (offsets.empty()) return kEmptyTemplateArgumentPartition;
	++requests_;
	std::size_t hash = MixHash(0, offsets.size());
	for (std::size_t i = 0; i < offsets.size(); ++i)
		hash = MixHash(hash, offsets[i]);
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = hash & mask;
	while (slots_[slot] != 0)
	{
		++index_probes_;
		const TemplateArgumentPartitionId id = slots_[slot] - 1;
		const Entry& entry = entries_[id];
		bool equal = entry.hash == hash && entry.count == offsets.size();
		for (std::size_t i = 0; equal && i < offsets.size(); ++i)
			equal = offsets_[entry.first + i] == offsets[i];
		if (equal)
		{
			++cache_hits_;
			return id;
		}
		slot = (slot + 1) & mask;
	}
	++index_probes_;
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max() ||
		offsets_.size() >
			std::numeric_limits<std::uint32_t>::max() - offsets.size())
		ThrowSemanticResourceLimit("too many template argument partitions");
	const TemplateArgumentPartitionId id =
		static_cast<TemplateArgumentPartitionId>(entries_.size());
	const std::uint32_t first = static_cast<std::uint32_t>(offsets_.size());
	offsets_.insert(offsets_.end(), offsets.begin(), offsets.end());
	entries_.push_back(Entry(first,
		static_cast<std::uint32_t>(offsets.size()), hash));
	slots_[slot] = id + 1;
	return id;
}

std::size_t TemplateArgumentPartitionTable::Requests() const
{
	return requests_;
}

std::size_t TemplateArgumentPartitionTable::CacheHits() const
{
	return cache_hits_;
}

std::size_t TemplateArgumentPartitionTable::IndexProbes() const
{
	return index_probes_;
}

std::size_t TemplateArgumentPartitionTable::StorageBytes() const
{
	return offsets_.capacity() * sizeof(std::uint32_t) +
		entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t);
}

FunctionTemplateResultIdentityTable::
	FunctionTemplateResultIdentityTable()
	: slots_(32, 0), requests_(0), cache_hits_(0), index_probes_(0),
	  atom_visits_(0)
{
}

void FunctionTemplateResultIdentityTable::Rehash(std::size_t capacity)
{
	RebuildCachedHashSlots(entries_, capacity, &slots_);
}

FunctionTemplateResultIdentityId FunctionTemplateResultIdentityTable::Intern(
	const std::vector<std::uint64_t>& atoms)
{
	if (atoms.empty()) return kNoFunctionTemplateResultIdentity;
	++requests_;
	atom_visits_ += atoms.size();
	std::size_t hash = MixHash(0, atoms.size());
	for (std::size_t i = 0; i < atoms.size(); ++i)
		hash = MixHash(hash, atoms[i]);
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = hash & mask;
	while (slots_[slot] != 0)
	{
		++index_probes_;
		const FunctionTemplateResultIdentityId id = slots_[slot] - 1;
		const Entry& entry = entries_[id];
		bool equal = entry.hash == hash && entry.count == atoms.size();
		for (std::size_t i = 0; equal && i < atoms.size(); ++i)
		{
			++atom_visits_;
			equal = atoms_[entry.first + i] == atoms[i];
		}
		if (equal)
		{
			++cache_hits_;
			return id;
		}
		slot = (slot + 1) & mask;
	}
	++index_probes_;
	if (entries_.size() >= kNoFunctionTemplateResultIdentity ||
		atoms.size() > std::numeric_limits<std::uint32_t>::max() ||
		atoms_.size() >
			std::numeric_limits<std::uint32_t>::max() - atoms.size())
		ThrowSemanticResourceLimit(
			"too many canonical function-template result identities");
	const FunctionTemplateResultIdentityId id =
		static_cast<FunctionTemplateResultIdentityId>(entries_.size());
	const std::uint32_t first = static_cast<std::uint32_t>(atoms_.size());
	atoms_.insert(atoms_.end(), atoms.begin(), atoms.end());
	entries_.push_back(Entry(first,
		static_cast<std::uint32_t>(atoms.size()), hash));
	slots_[slot] = id + 1;
	return id;
}

void FunctionTemplateResultIdentityTable::CopyAtoms(
	FunctionTemplateResultIdentityId identity,
	std::vector<std::uint64_t>* atoms) const
{
	if (!atoms || identity >= entries_.size())
		ThrowInternalCompilerError("function template result identity is invalid");
	const Entry& entry = entries_[identity];
	atoms->assign(atoms_.begin() + entry.first,
		atoms_.begin() + entry.first + entry.count);
}

std::size_t FunctionTemplateResultIdentityTable::Requests() const
{
	return requests_;
}

std::size_t FunctionTemplateResultIdentityTable::CacheHits() const
{
	return cache_hits_;
}

std::size_t FunctionTemplateResultIdentityTable::IndexProbes() const
{
	return index_probes_;
}

std::size_t FunctionTemplateResultIdentityTable::AtomVisits() const
{
	return atom_visits_;
}

std::size_t FunctionTemplateResultIdentityTable::StorageBytes() const
{
	return atoms_.capacity() * sizeof(std::uint64_t) +
		entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t);
}

TemplateSpecializationTable::Entry::Entry(
	const TemplateSpecializationKey& key_value, BindingId binding_value,
	TemplateRequestState state_value)
	: key(key_value), binding(binding_value), state(state_value)
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
		if (entry.key == key)
			return entry.state == TEMPLATE_REQUEST_SUCCEEDED ?
				entry.binding : kNoBinding;
		slot = (slot + 1) & mask;
	}
	return kNoBinding;
}

void TemplateSpecializationTable::Insert(
	const TemplateSpecializationKey& key, BindingId binding)
{
	SetRequest(key, TEMPLATE_REQUEST_SUCCEEDED, binding);
}

TemplateRequestState TemplateSpecializationTable::FindRequest(
	const TemplateSpecializationKey& key, BindingId* binding) const
{
	if (binding) *binding = kNoBinding;
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = TemplateSpecializationHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		const Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key)
		{
			if (binding) *binding = entry.binding;
			return entry.state;
		}
		slot = (slot + 1) & mask;
	}
	return TEMPLATE_REQUEST_NOT_STARTED;
}

void TemplateSpecializationTable::SetRequest(
	const TemplateSpecializationKey& key, TemplateRequestState state,
	BindingId binding)
{
	if (state == TEMPLATE_REQUEST_NOT_STARTED)
		ThrowInternalCompilerError("cannot store an unstarted template request");
	if (state == TEMPLATE_REQUEST_SUCCEEDED && binding == kNoBinding)
		ThrowInternalCompilerError("successful template request has no binding");
	if (state != TEMPLATE_REQUEST_SUCCEEDED) binding = kNoBinding;
	if ((entries_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = TemplateSpecializationHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key)
		{
			if (entry.state == TEMPLATE_REQUEST_SUCCEEDED)
			{
				if (state != TEMPLATE_REQUEST_SUCCEEDED ||
					entry.binding != binding)
					ThrowInternalCompilerError(
						"completed template request changed result");
				return;
			}
			if (entry.state == TEMPLATE_REQUEST_FAILED)
			{
				if (state != TEMPLATE_REQUEST_FAILED)
					ThrowInternalCompilerError(
						"failed template request changed result");
				return;
			}
			if (entry.state == TEMPLATE_REQUEST_IN_PROGRESS &&
				state == TEMPLATE_REQUEST_IN_PROGRESS) return;
			entry.binding = binding;
			entry.state = state;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (entries_.size() >= std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many template specializations");
	entries_.push_back(Entry(key, binding, state));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size());
}

void TemplateSpecializationTable::ResetInProgressRequest(
	const TemplateSpecializationKey& key)
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = TemplateSpecializationHash()(key) & mask;
	while (slots_[slot] != 0)
	{
		Entry& entry = entries_[slots_[slot] - 1];
		if (entry.key == key)
		{
			if (entry.state != TEMPLATE_REQUEST_IN_PROGRESS)
				ThrowInternalCompilerError(
					"cannot reset a completed template request");
			entry.binding = kNoBinding;
			entry.state = TEMPLATE_REQUEST_NOT_STARTED;
			return;
		}
		slot = (slot + 1) & mask;
	}
	ThrowInternalCompilerError("cannot reset an unknown template request");
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
	return entries_.capacity() * sizeof(Entry) +
		slots_.capacity() * sizeof(std::uint32_t);
}

}
}
