#include "lowering/constants/templates.h"
#include "lowering/support/errors.h"

#include <string>
#include <utility>

namespace cppgm
{
namespace lowering
{
namespace constant_pool
{
namespace
{

using namespace lowering::ir;

const std::uint32_t kEmptySlot = kNoLowId;

void Mix(std::size_t* hash, std::uint64_t value)
{
	*hash ^= static_cast<std::size_t>(value) +
		static_cast<std::size_t>(0x9e3779b9U) + (*hash << 6) + (*hash >> 2);
}

void MixType(std::size_t* hash, const LowType& type)
{
	Mix(hash, type.kind);
	Mix(hash, type.width);
	Mix(hash, type.alignment);
	Mix(hash, type.is_signed);
}

bool EqualType(const LowType& left, const LowType& right)
{
	return left.kind == right.kind && left.width == right.width &&
		left.alignment == right.alignment &&
		left.is_signed == right.is_signed;
}

}

Pool::Pool(lowering::ir::Program& output, lowering::Stats* stats)
	: output_(output), stats_(stats) {}

std::size_t Pool::Hash(const Global& global)
{
	std::size_t hash = static_cast<std::size_t>(2166136261U);
	MixType(&hash, global.type);
	Mix(&hash, global.items.size());
	for (std::size_t i = 0; i < global.items.size(); ++i)
	{
		const Global::DataItem& item = global.items[i];
		Mix(&hash, item.kind);
		if (item.kind == Global::DataItem::ZERO_ITEM)
			Mix(&hash, item.zero_bytes);
		else if (item.kind == Global::DataItem::ADDRESS_ITEM)
		{
			MixType(&hash, item.type);
			Mix(&hash, item.symbol);
			Mix(&hash, static_cast<std::uint64_t>(item.offset));
		}
		else
		{
			MixType(&hash, item.type);
			Mix(&hash, item.kind == Global::DataItem::FLOATING_ITEM ?
				item.floating_low :
				static_cast<std::uint64_t>(item.integer_value));
			Mix(&hash, item.integer_high);
		}
	}
	return hash;
}

bool Pool::Equal(const Global& left, const Global& right)
{
	if (!EqualType(left.type, right.type) ||
		left.items.size() != right.items.size()) return false;
	for (std::size_t i = 0; i < left.items.size(); ++i)
	{
		const Global::DataItem& a = left.items[i];
		const Global::DataItem& b = right.items[i];
		if (a.kind != b.kind) return false;
		if (a.kind == Global::DataItem::ZERO_ITEM)
		{
			if (a.zero_bytes != b.zero_bytes) return false;
			continue;
		}
		if (!EqualType(a.type, b.type)) return false;
		if (a.kind == Global::DataItem::ADDRESS_ITEM)
		{
			if (a.symbol != b.symbol || a.offset != b.offset) return false;
		}
		else if ((a.kind == Global::DataItem::FLOATING_ITEM ?
			a.floating_low != b.floating_low :
			a.integer_value != b.integer_value) ||
			a.integer_high != b.integer_high) return false;
	}
	return true;
}

void Pool::EnsureCapacity()
{
	if (slots_.empty())
	{
		Rehash(16);
		return;
	}
	if ((entries_.size() + 1) * 10 >= slots_.size() * 7)
		Rehash(slots_.size() * 2);
}

void Pool::Rehash(std::size_t capacity)
{
	if (capacity < 16) capacity = 16;
	std::vector<std::uint32_t> replacement(capacity, kEmptySlot);
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = entries_[i].hash & (capacity - 1);
		while (replacement[slot] != kEmptySlot)
			slot = (slot + 1) & (capacity - 1);
		replacement[slot] = static_cast<std::uint32_t>(i);
	}
	slots_.swap(replacement);
}

std::size_t Pool::FindSlot(std::size_t hash, const Global& candidate,
	bool* found) const
{
	std::size_t slot = hash & (slots_.size() - 1);
	while (slots_[slot] != kEmptySlot)
	{
		const Entry& entry = entries_[slots_[slot]];
		if (entry.hash == hash &&
			Equal(output_.globals[entry.global], candidate))
		{
			*found = true;
			return slot;
		}
		slot = (slot + 1) & (slots_.size() - 1);
	}
	*found = false;
	return slot;
}

SymbolId Pool::Intern(Global candidate)
{
	if (candidate.initializer_kind != Global::STRUCTURED_VALUE ||
		candidate.type.kind != LOW_OBJECT)
		ThrowLoweringInternal("invalid automatic constant-data template");
	const std::size_t hash = Hash(candidate);
	EnsureCapacity();
	bool found = false;
	const std::size_t slot = FindSlot(hash, candidate, &found);
	if (found)
	{
		const Entry& entry = entries_[slots_[slot]];
		return output_.globals[entry.global].symbol;
	}
	if (entries_.size() >= kEmptySlot)
		ThrowLoweringResourceLimit(
			"too many automatic constant-data templates");
	const std::string name = "__constexpr_template__" +
		std::to_string(entries_.size() + 1);
	const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
	output_.symbols.push_back(Symbol(Symbol::GLOBAL_SYMBOL,
		output_.InternUniqueSymbolName(name), lowir_model::StringId(),
		false, true, false));
	output_.symbols.back().definition_emitted = true;
	output_.symbols.back().referenced = true;
	candidate.symbol = symbol;
	candidate.storage = Global::STORAGE_READONLY;
	const std::size_t global_index = output_.globals.size();
	output_.globals.push_back(std::move(candidate));
	entries_.push_back(Entry(hash, global_index));
	slots_[slot] = static_cast<std::uint32_t>(entries_.size() - 1);
	if (stats_)
	{
		++stats_->globals;
		++stats_->constant_template_globals;
		stats_->constant_template_bytes +=
			static_cast<std::size_t>(output_.globals.back().type.width / 8);
	}
	return symbol;
}

}  // namespace constant_pool
}  // namespace lowering
}  // namespace cppgm
