#include "lowir_native_code_buffer.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
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
	std::size_t first = 0;
	std::size_t last = removals.size();
	while (first < last)
	{
		const std::size_t middle = first + (last - first) / 2;
		if (removals[middle].end <= offset) first = middle + 1;
		else last = middle;
	}
	const std::size_t removed = first ? removals[first - 1].total : 0;
	if (removed > offset)
		throw std::logic_error("native code offset adjustment underflows");
	return offset - removed;
}

std::size_t CodeOffsetAdjustment::bytes_removed() const
{
	return removals.empty() ? 0 : removals.back().total;
}

CodeBuffer::CodeBuffer()
	: base_offset_(kExecutableContentOffset), relocatable_addresses_(false),
	  symbol_names_(0), strings_(0)
{
}

CodeBuffer::CodeBuffer(std::size_t base_offset, bool relocatable_addresses)
	: base_offset_(base_offset),
	  relocatable_addresses_(relocatable_addresses), symbol_names_(0),
	  strings_(0)
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
	const std::pair<std::unordered_map<std::string, std::size_t>::iterator,
		bool> inserted = labels_.emplace(name, bytes_.size());
	if (!inserted.second)
		throw std::logic_error("duplicate native label: " + name);
	label_offsets_.push_back(&inserted.first->second);
}

lowir_model::LocalLabelId CodeBuffer::allocate_local_label()
{
	if (local_label_offsets_.size() >= lowir_model::kInvalidCompactId)
		throw std::runtime_error("too many native local labels");
	const lowir_model::LocalLabelId result(
		static_cast<std::uint32_t>(local_label_offsets_.size()));
	local_label_offsets_.push_back(std::numeric_limits<std::size_t>::max());
	return result;
}

std::size_t CodeBuffer::local_label_offset(
	lowir_model::LocalLabelId label) const
{
	const std::uint32_t index = label;
	if (!label.valid() || index >= local_label_offsets_.size() ||
		local_label_offsets_[index] == std::numeric_limits<std::size_t>::max())
		throw std::logic_error("unbound native local label");
	return local_label_offsets_[index];
}

void CodeBuffer::label(lowir_model::LocalLabelId label)
{
	const std::uint32_t index = label;
	if (!label.valid() || index >= local_label_offsets_.size())
		throw std::logic_error("invalid native local label");
	if (local_label_offsets_[index] != std::numeric_limits<std::size_t>::max())
		throw std::logic_error("duplicate native local label");
	local_label_offsets_[index] = bytes_.size();
	bound_local_labels_.push_back(label);
}

void CodeBuffer::label_at(const std::string& name, std::size_t offset)
{
	if (offset > bytes_.size())
		throw std::logic_error("native label is out of bounds");
	const std::pair<std::unordered_map<std::string, std::size_t>::iterator,
		bool> inserted = labels_.emplace(name, offset);
	if (!inserted.second)
		throw std::runtime_error("duplicate native symbol: " + name);
	// References to unordered_map elements remain valid across rehashes.  Keep
	// insertion-order pointers so per-function compaction only adjusts labels
	// emitted in that function instead of rescanning every earlier symbol.
	label_offsets_.push_back(&inserted.first->second);
}

void CodeBuffer::alias(const std::string& name, const std::string& target)
{
	const std::unordered_map<std::string, std::size_t>::const_iterator found =
		labels_.find(target);
	if (found == labels_.end())
		throw std::runtime_error("native alias has undefined target: " + target);
	label_at(name, found->second);
}

void CodeBuffer::begin_function_blocks(std::size_t count)
{
	if (!local_fixups_.empty() || !short_relative_fixups_.empty() ||
		!named_short_relative_fixups_.empty())
		throw std::logic_error("native local labels outlive their function");
	local_label_offsets_.clear();
	bound_local_labels_.clear();
	block_labels_.clear();
	block_labels_.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
		block_labels_.push_back(allocate_local_label());
}

lowir_model::LocalLabelId CodeBuffer::block_label(
	lowir_model::BlockId block) const
{
	const std::uint32_t index = block;
	if (!block.valid() || index >= block_labels_.size())
		throw std::logic_error("invalid native block label");
	return block_labels_[index];
}

std::size_t CodeBuffer::size() const
{
	return bytes_.size();
}

bool CodeBuffer::relocatable_addresses() const
{
	return relocatable_addresses_;
}

void CodeBuffer::bind_symbol_names(const std::vector<std::string>& names)
{
	symbol_names_ = &names;
}

const std::string& CodeBuffer::symbol_name(lowir_model::SymbolId symbol) const
{
	const std::uint32_t index = symbol;
	if (!symbol_names_ || !symbol.valid() || index >= symbol_names_->size())
		throw std::logic_error("invalid native symbol identity");
	return (*symbol_names_)[index];
}

void CodeBuffer::bind_strings(const lowir_model::StringPool& strings)
{
	strings_ = &strings;
}

const std::string& CodeBuffer::literal_spelling(
	lowir_model::StringId literal) const
{
	if (!strings_ || !literal.valid())
		throw std::logic_error("invalid native literal identity");
	return strings_->get(literal);
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
	NamedShortRelativeFixup fixup;
	fixup.offset = bytes_.size();
	fixup.target = target;
	named_short_relative_fixups_.push_back(fixup);
	byte(static_cast<std::uint8_t>(delta));
	return true;
}

bool CodeBuffer::short_relative(unsigned opcode,
	lowir_model::LocalLabelId target)
{
	const std::uint32_t target_index = target;
	if (!target.valid() || target_index >= local_label_offsets_.size())
		throw std::logic_error("invalid native local branch target");
	const std::size_t target_offset = local_label_offsets_[target_index];
	if (target_offset == std::numeric_limits<std::size_t>::max()) return false;
	if (target_offset > static_cast<std::size_t>(LLONG_MAX) ||
		bytes_.size() > static_cast<std::size_t>(LLONG_MAX - 2)) return false;
	const std::int64_t delta = static_cast<std::int64_t>(target_offset) -
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
	std::size_t first = short_relative_fixups_.size();
	while (first && short_relative_fixups_[first - 1].offset >= begin) --first;
	for (std::size_t i = first; i < short_relative_fixups_.size(); ++i)
	{
		const ShortRelativeFixup& fixup = short_relative_fixups_[i];
		const std::int64_t delta = static_cast<std::int64_t>(
			local_label_offset(fixup.target)) -
			static_cast<std::int64_t>(fixup.offset + 1);
		if (delta < INT8_MIN || delta > INT8_MAX)
			throw std::logic_error("native branch displacement exceeds rel8");
		patch(fixup.offset, static_cast<std::uint8_t>(delta), 1);
	}
	short_relative_fixups_.resize(first);
	first = named_short_relative_fixups_.size();
	while (first && named_short_relative_fixups_[first - 1].offset >= begin)
		--first;
	for (std::size_t i = first; i < named_short_relative_fixups_.size(); ++i)
	{
		const NamedShortRelativeFixup& fixup = named_short_relative_fixups_[i];
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
	named_short_relative_fixups_.resize(first);
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
		bool omit = false;
		lowir_model::LocalLabelId target;
	};
	std::size_t fixup_begin = local_fixups_.size();
	while (fixup_begin && local_fixups_[fixup_begin - 1].offset >= begin)
		--fixup_begin;
	std::vector<Candidate> candidates;
	for (std::size_t i = fixup_begin; i < local_fixups_.size(); ++i)
	{
		const LocalFixup& fixup = local_fixups_[i];
		if (fixup.kind != LocalFixup::RELATIVE32 || fixup.offset < begin ||
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
		const std::size_t target = local_label_offset(candidate.target);
		if (target <= candidate.start || target > bytes_.size()) continue;
		candidate.omit = candidate.opcode == 0xeb &&
			target == candidate.start + candidate.size;
		const std::size_t delta = target - (candidate.start + 2);
		if (!candidate.omit && delta > static_cast<std::size_t>(INT8_MAX))
			continue;
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
		resolve_local_fixups(begin);
		return adjustment;
	}
	const std::size_t old_size = bytes_.size();
	std::size_t read_cursor = begin;
	std::size_t write_cursor = begin;
	std::vector<std::size_t> short_starts;
	short_starts.reserve(candidates.size());
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		if (candidates[i].start < read_cursor ||
			candidates[i].start + candidates[i].size > old_size)
			throw std::logic_error("overlapping native branch relaxation");
		const std::size_t retained = candidates[i].start - read_cursor;
		if (retained)
			std::memmove(bytes_.data() + write_cursor,
				bytes_.data() + read_cursor, retained);
		write_cursor += retained;
		short_starts.push_back(write_cursor);
		if (!candidates[i].omit)
		{
			bytes_[write_cursor++] =
				static_cast<unsigned char>(candidates[i].opcode);
			bytes_[write_cursor++] = 0;
		}
		CodeOffsetAdjustment::Removal removal;
		removal.end = candidates[i].start + candidates[i].size;
		removal.count = candidates[i].size -
			(candidates[i].omit ? 0 : 2);
		removal.total = removal.count +
			(adjustment.removals.empty() ? 0 :
			 adjustment.removals.back().total);
		adjustment.removals.push_back(removal);
		read_cursor = candidates[i].start + candidates[i].size;
	}
	const std::size_t tail = old_size - read_cursor;
	if (tail)
		std::memmove(bytes_.data() + write_cursor,
			bytes_.data() + read_cursor, tail);
	write_cursor += tail;
	bytes_.resize(write_cursor);
	std::size_t label_begin = label_offsets_.size();
	while (label_begin && *label_offsets_[label_begin - 1] >= begin)
		--label_begin;
	for (std::size_t i = label_begin; i < label_offsets_.size(); ++i)
		*label_offsets_[i] = adjustment.translate(*label_offsets_[i]);
	std::size_t local_label_begin = bound_local_labels_.size();
	while (local_label_begin && local_label_offset(
		bound_local_labels_[local_label_begin - 1]) >= begin)
		--local_label_begin;
	for (std::size_t i = local_label_begin; i < bound_local_labels_.size(); ++i)
	{
		const std::uint32_t label = bound_local_labels_[i];
		local_label_offsets_[label] = adjustment.translate(
			local_label_offsets_[label]);
	}
	std::size_t short_begin = short_relative_fixups_.size();
	while (short_begin &&
		short_relative_fixups_[short_begin - 1].offset >= begin)
		--short_begin;
	for (std::size_t i = short_begin; i < short_relative_fixups_.size(); ++i)
		short_relative_fixups_[i].offset =
			adjustment.translate(short_relative_fixups_[i].offset);
	std::size_t named_short_begin = named_short_relative_fixups_.size();
	while (named_short_begin &&
		named_short_relative_fixups_[named_short_begin - 1].offset >= begin)
		--named_short_begin;
	for (std::size_t i = named_short_begin;
		i < named_short_relative_fixups_.size(); ++i)
		named_short_relative_fixups_[i].offset =
			adjustment.translate(named_short_relative_fixups_[i].offset);
	std::vector<unsigned char> removed_fixups(
		local_fixups_.size() - fixup_begin, 0);
	for (std::size_t i = 0; i < candidates.size(); ++i)
		removed_fixups[candidates[i].fixup - fixup_begin] = 1;
	std::size_t fixup_write = fixup_begin;
	for (std::size_t i = fixup_begin; i < local_fixups_.size(); ++i)
	{
		if (removed_fixups[i - fixup_begin]) continue;
		LocalFixup fixup = local_fixups_[i];
		fixup.offset = adjustment.translate(fixup.offset);
		local_fixups_[fixup_write++] = fixup;
	}
	local_fixups_.resize(fixup_write);
	std::size_t external_fixup_begin = fixups_.size();
	while (external_fixup_begin && fixups_[external_fixup_begin - 1].offset >= begin)
		--external_fixup_begin;
	for (std::size_t i = external_fixup_begin; i < fixups_.size(); ++i)
		fixups_[i].offset = adjustment.translate(fixups_[i].offset);
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		if (candidates[i].omit) continue;
		const std::int64_t delta = static_cast<std::int64_t>(
			local_label_offset(candidates[i].target)) -
			static_cast<std::int64_t>(short_starts[i] + 2);
		if (delta < INT8_MIN || delta > INT8_MAX)
			throw std::logic_error("relaxed native branch exceeds rel8");
		bytes_[short_starts[i] + 1] = static_cast<unsigned char>(delta);
	}
	resolve_short_relatives(begin);
	resolve_local_fixups(begin);
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

void CodeBuffer::relative32(lowir_model::LocalLabelId target)
{
	LocalFixup fixup;
	fixup.kind = LocalFixup::RELATIVE32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	local_fixups_.push_back(fixup);
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

void CodeBuffer::absolute64(lowir_model::LocalLabelId target)
{
	LocalFixup fixup;
	fixup.kind = LocalFixup::ABSOLUTE64;
	fixup.offset = bytes_.size();
	fixup.target = target;
	local_fixups_.push_back(fixup);
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

void CodeBuffer::address32(lowir_model::LocalLabelId target)
{
	LocalFixup fixup;
	fixup.kind = LocalFixup::ADDRESS32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	local_fixups_.push_back(fixup);
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

void CodeBuffer::resolve_local_fixups(std::size_t begin)
{
	std::size_t first = local_fixups_.size();
	while (first && local_fixups_[first - 1].offset >= begin) --first;
	for (std::size_t i = first; i < local_fixups_.size(); ++i)
	{
		const LocalFixup& fixup = local_fixups_[i];
		const std::size_t target = local_label_offset(fixup.target);
		if (fixup.kind == LocalFixup::ABSOLUTE64)
		{
			const std::uint64_t address = kLoadAddress +
				kExecutableContentOffset + target;
			patch(fixup.offset, address, 8);
			continue;
		}
		if (target > static_cast<std::size_t>(LLONG_MAX) ||
			fixup.offset > static_cast<std::size_t>(LLONG_MAX - 4))
			throw std::runtime_error("native local fixup exceeds rel32");
		const std::int64_t delta = static_cast<std::int64_t>(target) -
			static_cast<std::int64_t>(fixup.offset + 4);
		if (delta < INT32_MIN || delta > INT32_MAX)
			throw std::runtime_error("native local fixup exceeds rel32");
		patch(fixup.offset, static_cast<std::uint32_t>(delta), 4);
	}
	local_fixups_.resize(first);
}

void CodeBuffer::resolve()
{
	resolve_short_relatives(0);
	resolve_local_fixups(0);
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
	resolve_local_fixups(0);
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

lowir_model::LocalLabelId CodeBuffer::internal_label(const char*)
{
	return allocate_local_label();
}

}
}
