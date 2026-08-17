#include "lowir_native_code_buffer.h"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <utility>

namespace lowir_native
{
namespace elf_detail
{
namespace
{

const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kExecutableContentOffset = 120;

}

std::size_t CodeOffsetAdjustment::translate(std::size_t offset) const
{
	std::size_t removed = 0;
	for (std::size_t i = 0; i < removals.size(); ++i)
	{
		if (removals[i].end > offset) break;
		removed += removals[i].count;
	}
	if (removed > offset)
		throw std::logic_error("native code offset adjustment underflows");
	return offset - removed;
}

std::size_t CodeOffsetAdjustment::bytes_removed() const
{
	std::size_t result = 0;
	for (std::size_t i = 0; i < removals.size(); ++i)
		result += removals[i].count;
	return result;
}

CodeBuffer::CodeBuffer()
	: base_offset_(kExecutableContentOffset), relocatable_addresses_(false)
{
}

CodeBuffer::CodeBuffer(std::size_t base_offset, bool relocatable_addresses)
	: base_offset_(base_offset),
	  relocatable_addresses_(relocatable_addresses)
{
}

void CodeBuffer::byte(unsigned value)
{
	bytes_.push_back(static_cast<unsigned char>(value));
}

void CodeBuffer::zeros(std::size_t count)
{
	bytes_.insert(bytes_.end(), count, 0);
}

void CodeBuffer::little(std::uint64_t value, unsigned count)
{
	for (unsigned i = 0; i < count; ++i)
		byte(static_cast<unsigned>(value >> (i * 8)));
}

void CodeBuffer::patch(std::size_t offset, std::uint64_t value,
	unsigned count)
{
	if (offset + count > bytes_.size())
		throw std::logic_error("invalid ELF patch");
	for (unsigned i = 0; i < count; ++i)
		bytes_[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}

void CodeBuffer::align(std::size_t alignment)
{
	if (!alignment) throw std::logic_error("zero data alignment");
	while ((base_offset_ + bytes_.size()) % alignment) byte(0);
}

void CodeBuffer::label(const std::string& name)
{
	if (!labels_.emplace(name, bytes_.size()).second)
		throw std::logic_error("duplicate native label: " + name);
}

void CodeBuffer::label_at(const std::string& name, std::size_t offset)
{
	if (offset > bytes_.size())
		throw std::logic_error("native label is out of bounds");
	if (!labels_.emplace(name, offset).second)
		throw std::runtime_error("duplicate native symbol: " + name);
}

void CodeBuffer::alias(const std::string& name, const std::string& target)
{
	const std::unordered_map<std::string, std::size_t>::const_iterator found =
		labels_.find(target);
	if (found == labels_.end())
		throw std::runtime_error("native alias has undefined target: " + target);
	label_at(name, found->second);
}

std::size_t CodeBuffer::size() const
{
	return bytes_.size();
}

bool CodeBuffer::relocatable_addresses() const
{
	return relocatable_addresses_;
}

void CodeBuffer::append(const std::vector<unsigned char>& bytes)
{
	bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

bool CodeBuffer::short_relative(unsigned opcode, const std::string& target)
{
	const std::unordered_map<std::string, std::size_t>::const_iterator found =
		labels_.find(target);
	if (found == labels_.end()) return false;
	if (found->second > static_cast<std::size_t>(LLONG_MAX) ||
		bytes_.size() > static_cast<std::size_t>(LLONG_MAX - 2)) return false;
	const std::int64_t delta = static_cast<std::int64_t>(found->second) -
		static_cast<std::int64_t>(bytes_.size() + 2);
	if (delta < INT8_MIN || delta > INT8_MAX) return false;
	byte(opcode);
	ShortRelativeFixup fixup;
	fixup.offset = bytes_.size();
	fixup.target = target;
	short_relative_fixups_.push_back(fixup);
	byte(static_cast<std::uint8_t>(delta));
	return true;
}

void CodeBuffer::resolve_short_relatives(std::size_t begin)
{
	std::vector<ShortRelativeFixup> retained;
	retained.reserve(short_relative_fixups_.size());
	for (std::size_t i = 0; i < short_relative_fixups_.size(); ++i)
	{
		const ShortRelativeFixup& fixup = short_relative_fixups_[i];
		if (fixup.offset < begin)
		{
			retained.push_back(fixup);
			continue;
		}
		const std::unordered_map<std::string, std::size_t>::const_iterator
			target = labels_.find(fixup.target);
		if (target == labels_.end())
			throw std::logic_error("native rel8 fixup lost its target");
		const std::int64_t delta = static_cast<std::int64_t>(target->second) -
			static_cast<std::int64_t>(fixup.offset + 1);
		if (delta < INT8_MIN || delta > INT8_MAX)
			throw std::logic_error("native branch displacement exceeds rel8");
		patch(fixup.offset, static_cast<std::uint8_t>(delta), 1);
	}
	short_relative_fixups_.swap(retained);
}

CodeOffsetAdjustment CodeBuffer::relax_forward_branches(std::size_t begin)
{
	if (begin > bytes_.size())
		throw std::logic_error("native branch relaxation begins out of bounds");
	struct Candidate
	{
		std::size_t fixup = 0;
		std::size_t start = 0;
		std::size_t size = 0;
		unsigned opcode = 0;
		std::string target;
	};
	std::vector<Candidate> candidates;
	for (std::size_t i = 0; i < fixups_.size(); ++i)
	{
		const Fixup& fixup = fixups_[i];
		if (fixup.kind != Fixup::RELATIVE32 || fixup.offset < begin ||
			fixup.offset + 4 > bytes_.size()) continue;
		Candidate candidate;
		candidate.fixup = i;
		candidate.target = fixup.target;
		if (fixup.offset >= begin + 2 &&
			bytes_[fixup.offset - 2] == 0x0f &&
			bytes_[fixup.offset - 1] >= 0x80 &&
			bytes_[fixup.offset - 1] <= 0x8f)
		{
			candidate.start = fixup.offset - 2;
			candidate.size = 6;
			candidate.opcode = 0x70 + bytes_[fixup.offset - 1] - 0x80;
		}
		else if (fixup.offset >= begin + 1 &&
			bytes_[fixup.offset - 1] == 0xe9)
		{
			candidate.start = fixup.offset - 1;
			candidate.size = 5;
			candidate.opcode = 0xeb;
		}
		else continue;
		const std::unordered_map<std::string, std::size_t>::const_iterator
			target = labels_.find(candidate.target);
		if (target == labels_.end() || target->second <= candidate.start ||
			target->second > bytes_.size()) continue;
		const std::size_t delta = target->second - (candidate.start + 2);
		if (delta > static_cast<std::size_t>(INT8_MAX)) continue;
		candidates.push_back(candidate);
	}
	std::sort(candidates.begin(), candidates.end(),
		[](const Candidate& left, const Candidate& right)
		{
			return left.start < right.start;
		});
	CodeOffsetAdjustment adjustment;
	if (candidates.empty())
	{
		resolve_short_relatives(begin);
		return adjustment;
	}
	std::vector<unsigned char> suffix;
	suffix.reserve(bytes_.size() - begin);
	std::size_t cursor = begin;
	std::vector<std::size_t> short_starts;
	short_starts.reserve(candidates.size());
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		if (candidates[i].start < cursor ||
			candidates[i].start + candidates[i].size > bytes_.size())
			throw std::logic_error("overlapping native branch relaxation");
		suffix.insert(suffix.end(), bytes_.begin() + cursor,
			bytes_.begin() + candidates[i].start);
		short_starts.push_back(begin + suffix.size());
		suffix.push_back(static_cast<unsigned char>(candidates[i].opcode));
		suffix.push_back(0);
		CodeOffsetAdjustment::Removal removal;
		removal.end = candidates[i].start + candidates[i].size;
		removal.count = candidates[i].size - 2;
		adjustment.removals.push_back(removal);
		cursor = candidates[i].start + candidates[i].size;
	}
	suffix.insert(suffix.end(), bytes_.begin() + cursor, bytes_.end());
	std::vector<unsigned char> relaxed(bytes_.begin(), bytes_.begin() + begin);
	relaxed.insert(relaxed.end(), suffix.begin(), suffix.end());
	bytes_.swap(relaxed);
	for (std::unordered_map<std::string, std::size_t>::iterator label =
			labels_.begin(); label != labels_.end(); ++label)
		if (label->second >= begin)
			label->second = adjustment.translate(label->second);
	for (std::size_t i = 0; i < short_relative_fixups_.size(); ++i)
		if (short_relative_fixups_[i].offset >= begin)
			short_relative_fixups_[i].offset =
				adjustment.translate(short_relative_fixups_[i].offset);
	std::vector<unsigned char> removed_fixups(fixups_.size(), 0);
	for (std::size_t i = 0; i < candidates.size(); ++i)
		removed_fixups[candidates[i].fixup] = 1;
	std::vector<Fixup> retained;
	retained.reserve(fixups_.size() - candidates.size());
	for (std::size_t i = 0; i < fixups_.size(); ++i)
	{
		if (removed_fixups[i]) continue;
		Fixup fixup = fixups_[i];
		if (fixup.offset >= begin)
			fixup.offset = adjustment.translate(fixup.offset);
		retained.push_back(fixup);
	}
	fixups_.swap(retained);
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const std::unordered_map<std::string, std::size_t>::const_iterator
			target = labels_.find(candidates[i].target);
		if (target == labels_.end())
			throw std::logic_error("relaxed native branch lost its target");
		const std::int64_t delta = static_cast<std::int64_t>(target->second) -
			static_cast<std::int64_t>(short_starts[i] + 2);
		if (delta < INT8_MIN || delta > INT8_MAX)
			throw std::logic_error("relaxed native branch exceeds rel8");
		bytes_[short_starts[i] + 1] = static_cast<unsigned char>(delta);
	}
	resolve_short_relatives(begin);
	return adjustment;
}

void CodeBuffer::relative32(const std::string& target)
{
	Fixup fixup;
	fixup.kind = Fixup::RELATIVE32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::absolute64(const std::string& target, long long addend)
{
	Fixup fixup;
	fixup.kind = Fixup::ABSOLUTE64;
	fixup.offset = bytes_.size();
	fixup.target = target;
	fixup.addend = addend;
	fixups_.push_back(fixup);
	zeros(8);
}

void CodeBuffer::address32(
	const std::string& target,
	mir_model::MirOperand::AddressBinding address_binding)
{
	Fixup fixup;
	fixup.kind = Fixup::ADDRESS32;
	fixup.address_binding = address_binding;
	fixup.offset = bytes_.size();
	fixup.target = target;
	fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::tls_offset32(const std::string& target)
{
	Fixup fixup;
	fixup.kind = Fixup::TLS_OFFSET32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::relative32_at(std::size_t offset,
	const std::string& target, long long elf_addend)
{
	if (offset > bytes_.size() || 4 > bytes_.size() - offset)
		throw std::logic_error("native relative relocation is out of bounds");
	Fixup fixup;
	fixup.kind = Fixup::RELATIVE32;
	fixup.offset = offset;
	fixup.target = target;
	fixup.addend = elf_addend + 4;
	fixups_.push_back(fixup);
}

void CodeBuffer::absolute64_at(std::size_t offset,
	const std::string& target, long long addend)
{
	if (offset > bytes_.size() || 8 > bytes_.size() - offset)
		throw std::logic_error("native absolute relocation is out of bounds");
	Fixup fixup;
	fixup.kind = Fixup::ABSOLUTE64;
	fixup.offset = offset;
	fixup.target = target;
	fixup.addend = addend;
	fixups_.push_back(fixup);
}

void CodeBuffer::resolve()
{
	resolve_short_relatives(0);
	for (std::size_t i = 0; i < fixups_.size(); ++i)
	{
		const Fixup& fixup = fixups_[i];
		const std::unordered_map<std::string, std::size_t>::const_iterator target =
			labels_.find(fixup.target);
		if (target == labels_.end())
			throw std::runtime_error("undefined native symbol: " + fixup.target);
		if (fixup.kind == Fixup::RELATIVE32 ||
			fixup.kind == Fixup::ADDRESS32 ||
			fixup.kind == Fixup::TLS_OFFSET32)
		{
			const std::int64_t delta =
				static_cast<std::int64_t>(target->second) -
				static_cast<std::int64_t>(fixup.offset + 4) + fixup.addend;
			if (delta < INT32_MIN || delta > INT32_MAX)
				throw std::runtime_error(
					"native branch displacement exceeds rel32");
			patch(fixup.offset, static_cast<std::uint32_t>(delta), 4);
			continue;
		}

		std::uint64_t address = kLoadAddress + kExecutableContentOffset +
			target->second;
		if (fixup.addend >= 0)
		{
			const std::uint64_t addend =
				static_cast<std::uint64_t>(fixup.addend);
			if (UINT64_MAX - address < addend)
				throw std::runtime_error("native address fixup overflows");
			address += addend;
		}
		else
		{
			const std::uint64_t magnitude =
				static_cast<std::uint64_t>(-(fixup.addend + 1)) + 1;
			if (address < magnitude)
				throw std::runtime_error("native address fixup underflows");
			address -= magnitude;
		}
		patch(fixup.offset, address, 8);
	}
}

const std::vector<unsigned char>& CodeBuffer::bytes() const
{
	return bytes_;
}

std::vector<unsigned char> CodeBuffer::take_bytes()
{
	resolve_short_relatives(0);
	return std::move(bytes_);
}

const std::unordered_map<std::string, std::size_t>& CodeBuffer::labels() const
{
	return labels_;
}

const std::vector<Fixup>& CodeBuffer::fixups() const
{
	return fixups_;
}

std::size_t CodeBuffer::fixup_count() const
{
	return fixups_.size();
}

std::string CodeBuffer::internal_label(const char* purpose)
{
	return std::string(".__cppgm_x87_") + purpose + "_" +
		std::to_string(next_internal_label_++);
}

}
}
