#include "native/object/code_buffer.h"

#include "native/driver/stats.h"
#include "native/errors.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <utility>

namespace lowir_native
{
namespace elf_detail
{
namespace
{

const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kExecutableContentOffset = 120;

std::uint32_t checked_relative32_delta(std::size_t target,
	std::size_t offset, long long addend, const char* error)
{
	if (target > static_cast<std::size_t>(LLONG_MAX) ||
		offset > static_cast<std::size_t>(LLONG_MAX - 4))
		native_errors::ThrowResourceLimit(error);
	std::int64_t delta = static_cast<std::int64_t>(target) -
		static_cast<std::int64_t>(offset + 4);
	if ((addend > 0 && delta > LLONG_MAX - addend) ||
		(addend < 0 && delta < LLONG_MIN - addend))
		native_errors::ThrowResourceLimit(error);
	delta += addend;
	if (delta < INT32_MIN || delta > INT32_MAX)
		native_errors::ThrowResourceLimit(error);
	return static_cast<std::uint32_t>(delta);
}

std::uint64_t apply_absolute_addend(std::uint64_t address, long long addend)
{
	if (addend >= 0)
	{
		const std::uint64_t magnitude = static_cast<std::uint64_t>(addend);
		if (UINT64_MAX - address < magnitude)
			native_errors::ThrowResourceLimit("native address fixup overflows");
		return address + magnitude;
	}
	const std::uint64_t magnitude =
		static_cast<std::uint64_t>(-(addend + 1)) + 1;
	if (address < magnitude)
		native_errors::ThrowResourceLimit("native address fixup underflows");
	return address - magnitude;
}

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
		native_errors::ThrowInternal("native code offset adjustment underflows");
	return offset - removed;
}

std::size_t CodeOffsetAdjustment::bytes_removed() const
{
	return removals.empty() ? 0 : removals.back().total;
}

CodeBuffer::CodeBuffer()
	: base_offset_(kExecutableContentOffset), relocatable_addresses_(false),
	  symbol_names_(0), strings_(0), stats_(0)
{
}

CodeBuffer::CodeBuffer(std::size_t base_offset, bool relocatable_addresses)
	: base_offset_(base_offset),
	  relocatable_addresses_(relocatable_addresses), symbol_names_(0),
	  strings_(0), stats_(0)
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
		native_errors::ThrowInternal("invalid ELF patch");
	for (unsigned i = 0; i < count; ++i)
		bytes_[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}

void CodeBuffer::align(std::size_t alignment)
{
	if (!alignment) native_errors::ThrowInternal("zero data alignment");
	while ((base_offset_ + bytes_.size()) % alignment) byte(0);
}

void CodeBuffer::label(const std::string& name)
{
	if (stats_) ++stats_->code_buffer_named_labels;
	insert_named_label(name, bytes_.size(), false);
}

void CodeBuffer::label(lowir_model::SymbolId symbol)
{
	if (stats_) ++stats_->code_buffer_typed_labels;
	const std::uint32_t index = symbol;
	if (!symbol.valid() || index >= symbol_label_offsets_.size())
		native_errors::ThrowInternal("invalid native symbol identity");
	if (symbol_label_known_[index])
		native_errors::ThrowInternal("duplicate native symbol label");
	symbol_label_offsets_[index] = bytes_.size();
	symbol_label_known_[index] = 1;
	label_offsets_.push_back(&symbol_label_offsets_[index]);
	LabelBinding binding;
	binding.symbol = true;
	binding.symbol_id = symbol;
	binding.offset = &symbol_label_offsets_[index];
	label_bindings_.push_back(binding);
}

void CodeBuffer::label_object(lowir_model::StringId symbol)
{
	if (stats_) ++stats_->code_buffer_object_labels;
	const std::uint32_t index = symbol;
	if (!symbol.valid() || index >= object_label_offsets_.size())
		native_errors::ThrowInternal("invalid native object-symbol identity");
	if (object_label_known_[index])
		native_errors::ThrowInternal("duplicate native object-symbol label");
	object_label_offsets_[index] = bytes_.size();
	object_label_known_[index] = 1;
	label_offsets_.push_back(&object_label_offsets_[index]);
}

void CodeBuffer::label_eh_type_ref(lowir_model::SymbolId symbol)
{
	if (stats_) ++stats_->code_buffer_typed_labels;
	const std::uint32_t identity = symbol;
	if (!symbol.valid() || !symbol_names_ || identity >= symbol_names_->size())
		native_errors::ThrowInternal("invalid native EH type-reference identity");
	for (std::size_t i = 0; i < eh_type_ref_labels_.size(); ++i)
		if (eh_type_ref_labels_[i].symbol == symbol)
			native_errors::ThrowInternal("duplicate native EH type-reference label");
	EhTypeRefLabelBinding label;
	label.symbol = symbol;
	label.offset = bytes_.size();
	eh_type_ref_labels_.push_back(label);
	label_offsets_.push_back(&eh_type_ref_labels_.back().offset);
}

void CodeBuffer::label_eh_personality_ref()
{
	if (stats_) ++stats_->code_buffer_typed_labels;
	if (eh_personality_ref_label_known_)
		native_errors::ThrowInternal("duplicate native EH personality-reference label");
	eh_personality_ref_label_offset_ = bytes_.size();
	eh_personality_ref_label_known_ = true;
	label_offsets_.push_back(&eh_personality_ref_label_offset_);
}

lowir_model::LocalLabelId CodeBuffer::allocate_local_label()
{
	if (local_label_offsets_.size() >= lowir_model::kInvalidCompactId)
		native_errors::ThrowResourceLimit("too many native local labels");
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
		native_errors::ThrowInternal("unbound native local label");
	return local_label_offsets_[index];
}

void CodeBuffer::label(lowir_model::LocalLabelId label)
{
	const std::uint32_t index = label;
	if (!label.valid() || index >= local_label_offsets_.size())
		native_errors::ThrowInternal("invalid native local label");
	if (local_label_offsets_[index] != std::numeric_limits<std::size_t>::max())
		native_errors::ThrowInternal("duplicate native local label");
	local_label_offsets_[index] = bytes_.size();
	bound_local_labels_.push_back(label);
}

void CodeBuffer::label_at(const std::string& name, std::size_t offset)
{
	if (offset > bytes_.size())
		native_errors::ThrowInternal("native label is out of bounds");
	if (stats_) ++stats_->code_buffer_named_labels;
	insert_named_label(name, offset, true);
}

void CodeBuffer::insert_named_label(const std::string& name,
	std::size_t offset, bool duplicate_is_runtime_error)
{
	const std::pair<std::unordered_map<std::string, std::size_t>::iterator,
		bool> inserted = labels_.emplace(name, offset);
	if (!inserted.second)
	{
		if (duplicate_is_runtime_error)
			native_errors::ThrowSource("duplicate native symbol: " + name);
		native_errors::ThrowInternal("duplicate native label: " + name);
	}
	// References to unordered_map elements remain valid across rehashes.  Keep
	// insertion-order pointers so per-function compaction only adjusts labels
	// emitted in that function instead of rescanning every earlier symbol.
	label_offsets_.push_back(&inserted.first->second);
	LabelBinding binding;
	binding.name = &inserted.first->first;
	binding.offset = &inserted.first->second;
	label_bindings_.push_back(binding);
}

void CodeBuffer::alias(const std::string& name, const std::string& target)
{
	const std::unordered_map<std::string, std::size_t>::const_iterator found =
		labels_.find(target);
	if (found == labels_.end())
		native_errors::ThrowSource("native alias has undefined target: " + target);
	label_at(name, found->second);
}

void CodeBuffer::alias(const std::string& name,
	lowir_model::SymbolId target)
{
	const std::uint32_t index = target;
	if (!target.valid() || index >= symbol_label_offsets_.size() ||
		!symbol_label_known_[index])
		native_errors::ThrowSource("native alias has undefined target: " +
			symbol_name(target));
	label_at(name, symbol_label_offsets_[index]);
}

void CodeBuffer::alias_object(lowir_model::StringId name,
	lowir_model::SymbolId target)
{
	const std::uint32_t symbol = target;
	if (!target.valid() || symbol >= symbol_label_offsets_.size() ||
		!symbol_label_known_[symbol])
		native_errors::ThrowSource("native alias has undefined target: " +
			symbol_name(target));
	const std::uint32_t object = name;
	if (!name.valid() || object >= object_label_offsets_.size())
		native_errors::ThrowInternal("invalid native object-symbol identity");
	if (object_label_known_[object])
		native_errors::ThrowSource("duplicate native object symbol: " +
			literal_spelling(name));
	object_label_offsets_[object] = symbol_label_offsets_[symbol];
	object_label_known_[object] = 1;
	label_offsets_.push_back(&object_label_offsets_[object]);
	if (stats_) ++stats_->code_buffer_object_labels;
}

void CodeBuffer::begin_function_blocks(std::size_t count)
{
	if (!local_fixups_.empty() || !short_relative_fixups_.empty() ||
		!named_short_relative_fixups_.empty())
		native_errors::ThrowInternal("native local labels outlive their function");
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
		native_errors::ThrowInternal("invalid native block label");
	return block_labels_[index];
}

std::size_t CodeBuffer::label_offset(
	lowir_model::LocalLabelId label) const
{
	return local_label_offset(label);
}

std::size_t CodeBuffer::size() const
{
	return bytes_.size();
}

bool CodeBuffer::relocatable_addresses() const
{
	return relocatable_addresses_;
}

void CodeBuffer::bind_symbol_names(
	const std::vector<lowir_model::StringId>& names)
{
	if (symbol_names_)
	{
		if (symbol_names_ != &names)
			native_errors::ThrowInternal("native symbol names rebound");
		return;
	}
	if (!label_bindings_.empty())
		native_errors::ThrowInternal("native symbol names bound after labels");
	symbol_names_ = &names;
	symbol_label_offsets_.assign(names.size(), 0);
	symbol_label_known_.assign(names.size(), 0);
}

const std::string& CodeBuffer::symbol_name(lowir_model::SymbolId symbol) const
{
	const std::uint32_t index = symbol;
	if (!symbol_names_ || !strings_ || !symbol.valid() ||
		index >= symbol_names_->size())
		native_errors::ThrowInternal("invalid native symbol identity");
	return strings_->get((*symbol_names_)[index]);
}

void CodeBuffer::bind_strings(const lowir_model::SealedStringPool& strings)
{
	if (strings_)
	{
		if (strings_ != &strings)
			native_errors::ThrowInternal("native string pool rebound");
		return;
	}
	strings_ = &strings;
	object_label_offsets_.assign(strings.size() + 1, 0);
	object_label_known_.assign(strings.size() + 1, 0);
}

void CodeBuffer::bind_stats(Stats* stats)
{
	stats_ = stats;
}

bool CodeBuffer::collects_stats() const
{
	return stats_ != 0;
}

void CodeBuffer::note_literal_text_parse(std::uint64_t nanoseconds)
{
	if (!stats_) return;
	++stats_->native_literal_text_parses;
	stats_->native_literal_parse_nanoseconds += nanoseconds;
}

void CodeBuffer::note_direct_zero_encoding(std::size_t bytes,
	std::size_t stores)
{
	if (!stats_) return;
	++stats_->direct_zero_operations_selected;
	stats_->direct_zero_stores_emitted += stores;
	stats_->direct_zero_bytes += bytes;
}

const lowir_model::SealedStringPool& CodeBuffer::strings() const
{
	if (!strings_) native_errors::ThrowInternal("native string pool is not bound");
	return *strings_;
}

const std::string& CodeBuffer::literal_spelling(
	lowir_model::StringId literal) const
{
	if (!strings_ || !literal.valid())
		native_errors::ThrowInternal("invalid native literal identity");
	if (stats_) ++stats_->native_semantic_string_reads;
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
	if (stats_) ++stats_->code_buffer_named_fixups;
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
		native_errors::ThrowInternal("invalid native local branch target");
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
			native_errors::ThrowInternal("native branch displacement exceeds rel8");
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
			native_errors::ThrowInternal("native rel8 fixup lost its target");
		const std::int64_t delta = static_cast<std::int64_t>(target->second) -
			static_cast<std::int64_t>(fixup.offset + 1);
		if (delta < INT8_MIN || delta > INT8_MAX)
			native_errors::ThrowInternal("native branch displacement exceeds rel8");
		patch(fixup.offset, static_cast<std::uint8_t>(delta), 1);
	}
	named_short_relative_fixups_.resize(first);
}

CodeOffsetAdjustment CodeBuffer::relax_forward_branches(std::size_t begin)
{
	if (begin > bytes_.size())
		native_errors::ThrowInternal("native branch relaxation begins out of bounds");
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
			native_errors::ThrowInternal("overlapping native branch relaxation");
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
	std::size_t symbol_fixup_begin = symbol_fixups_.size();
	while (symbol_fixup_begin &&
		symbol_fixups_[symbol_fixup_begin - 1].offset >= begin)
		--symbol_fixup_begin;
	for (std::size_t i = symbol_fixup_begin; i < symbol_fixups_.size(); ++i)
		symbol_fixups_[i].offset = adjustment.translate(symbol_fixups_[i].offset);
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		if (candidates[i].omit) continue;
		const std::int64_t delta = static_cast<std::int64_t>(
			local_label_offset(candidates[i].target)) -
			static_cast<std::int64_t>(short_starts[i] + 2);
		if (delta < INT8_MIN || delta > INT8_MAX)
			native_errors::ThrowInternal("relaxed native branch exceeds rel8");
		bytes_[short_starts[i] + 1] = static_cast<unsigned char>(delta);
	}
	resolve_short_relatives(begin);
	resolve_local_fixups(begin);
	return adjustment;
}

void CodeBuffer::relative32(const std::string& target)
{
	if (stats_) ++stats_->code_buffer_named_fixups;
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

void CodeBuffer::relative32(lowir_model::SymbolId target)
{
	if (stats_) ++stats_->code_buffer_typed_fixups;
	SymbolFixup fixup;
	fixup.kind = Fixup::RELATIVE32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	symbol_fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::absolute64(const std::string& target, long long addend)
{
	if (stats_) ++stats_->code_buffer_named_fixups;
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

void CodeBuffer::absolute64(lowir_model::SymbolId target, long long addend)
{
	if (stats_) ++stats_->code_buffer_typed_fixups;
	SymbolFixup fixup;
	fixup.kind = Fixup::ABSOLUTE64;
	fixup.offset = bytes_.size();
	fixup.target = target;
	fixup.addend = addend;
	symbol_fixups_.push_back(fixup);
	zeros(8);
}

void CodeBuffer::address32(
	const std::string& target,
	mir_model::MirOperand::AddressBinding address_binding)
{
	if (stats_) ++stats_->code_buffer_named_fixups;
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

void CodeBuffer::address32(
	lowir_model::SymbolId target,
	mir_model::MirOperand::AddressBinding address_binding)
{
	if (stats_) ++stats_->code_buffer_typed_fixups;
	SymbolFixup fixup;
	fixup.kind = Fixup::ADDRESS32;
	fixup.address_binding = address_binding;
	fixup.offset = bytes_.size();
	fixup.target = target;
	symbol_fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::tls_offset32(const std::string& target)
{
	if (stats_) ++stats_->code_buffer_named_fixups;
	Fixup fixup;
	fixup.kind = Fixup::TLS_OFFSET32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::tls_offset32(lowir_model::SymbolId target)
{
	if (stats_) ++stats_->code_buffer_typed_fixups;
	SymbolFixup fixup;
	fixup.kind = Fixup::TLS_OFFSET32;
	fixup.offset = bytes_.size();
	fixup.target = target;
	symbol_fixups_.push_back(fixup);
	zeros(4);
}

void CodeBuffer::relative32_at(std::size_t offset,
	const std::string& target, long long elf_addend)
{
	if (stats_) ++stats_->code_buffer_named_fixups;
	if (offset > bytes_.size() || 4 > bytes_.size() - offset)
		native_errors::ThrowInternal("native relative relocation is out of bounds");
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
	if (stats_) ++stats_->code_buffer_named_fixups;
	if (offset > bytes_.size() || 8 > bytes_.size() - offset)
		native_errors::ThrowInternal("native absolute relocation is out of bounds");
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
		patch(fixup.offset, checked_relative32_delta(target, fixup.offset, 0,
			"native local fixup exceeds rel32"), 4);
	}
	local_fixups_.resize(first);
}

void CodeBuffer::resolve()
{
	resolve_short_relatives(0);
	resolve_local_fixups(0);
	const std::unordered_map<std::string, std::size_t> labels =
		materialized_labels();
	for (std::size_t i = 0; i < fixups_.size(); ++i)
	{
		const Fixup& fixup = fixups_[i];
		const std::unordered_map<std::string, std::size_t>::const_iterator target =
			labels.find(fixup.target);
		if (target == labels.end())
			native_errors::ThrowSource("undefined native symbol: " + fixup.target);
		if (fixup.kind == Fixup::RELATIVE32 ||
			fixup.kind == Fixup::ADDRESS32 ||
			fixup.kind == Fixup::TLS_OFFSET32)
		{
			patch(fixup.offset, checked_relative32_delta(target->second,
				fixup.offset, fixup.addend,
				"native branch displacement exceeds rel32"), 4);
			continue;
		}

		const std::uint64_t address = apply_absolute_addend(
			kLoadAddress + kExecutableContentOffset + target->second,
			fixup.addend);
		patch(fixup.offset, address, 8);
	}
	for (std::size_t i = 0; i < symbol_fixups_.size(); ++i)
	{
		const SymbolFixup& fixup = symbol_fixups_[i];
		const std::uint32_t symbol = fixup.target;
		if (!fixup.target.valid() || symbol >= symbol_label_offsets_.size())
			native_errors::ThrowInternal("invalid native symbol fixup identity");
		std::size_t target = 0;
		if (symbol_label_known_[symbol])
			target = symbol_label_offsets_[symbol];
		else
		{
			const std::string& name = symbol_name(fixup.target);
			const std::unordered_map<std::string, std::size_t>::const_iterator found =
				labels.find(name);
			if (found == labels.end())
				native_errors::ThrowSource("undefined native symbol: " + name);
			target = found->second;
		}
		if (fixup.kind == Fixup::RELATIVE32 ||
			fixup.kind == Fixup::ADDRESS32 ||
			fixup.kind == Fixup::TLS_OFFSET32)
		{
			patch(fixup.offset, checked_relative32_delta(target, fixup.offset,
				fixup.addend, "native branch displacement exceeds rel32"), 4);
			continue;
		}
		const std::uint64_t address = apply_absolute_addend(
			kLoadAddress + kExecutableContentOffset + target, fixup.addend);
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

std::unordered_map<std::string, std::size_t>
CodeBuffer::materialized_labels() const
{
	std::unordered_map<std::string, std::size_t> result;
	for (std::size_t i = 0; i < label_bindings_.size(); ++i)
	{
		const LabelBinding& binding = label_bindings_[i];
		const std::string& name = binding.symbol ?
			symbol_name(binding.symbol_id) : *binding.name;
		if (!result.emplace(name, *binding.offset).second)
			native_errors::ThrowSource("duplicate native symbol: " + name);
	}
	for (std::size_t i = 1; i < object_label_known_.size(); ++i)
	{
		if (!object_label_known_[i]) continue;
		const std::string& raw = literal_spelling(
			lowir_model::StringId(static_cast<std::uint32_t>(i)));
		const std::string name = !raw.empty() && raw[0] == '@' ?
			raw.substr(1) : raw;
		if (!result.emplace(name, object_label_offsets_[i]).second)
			native_errors::ThrowSource("duplicate native symbol: " + name);
	}
	return result;
}

const std::unordered_map<std::string, std::size_t>&
CodeBuffer::named_labels() const
{
	return labels_;
}

std::size_t CodeBuffer::symbol_label_capacity() const
{
	return symbol_label_offsets_.size();
}

bool CodeBuffer::has_symbol_label(lowir_model::SymbolId symbol) const
{
	const std::uint32_t index = symbol;
	return symbol.valid() && index < symbol_label_known_.size() &&
		symbol_label_known_[index];
}

std::size_t CodeBuffer::symbol_label_offset(
	lowir_model::SymbolId symbol) const
{
	if (!has_symbol_label(symbol))
		native_errors::ThrowInternal("undefined native symbol identity");
	return symbol_label_offsets_[static_cast<std::uint32_t>(symbol)];
}

std::size_t CodeBuffer::object_label_capacity() const
{
	return object_label_offsets_.size();
}

bool CodeBuffer::has_object_label(lowir_model::StringId symbol) const
{
	const std::uint32_t index = symbol;
	return symbol.valid() && index < object_label_known_.size() &&
		object_label_known_[index];
}

std::size_t CodeBuffer::object_label_offset(
	lowir_model::StringId symbol) const
{
	if (!has_object_label(symbol))
		native_errors::ThrowInternal("undefined native object-symbol identity");
	return object_label_offsets_[static_cast<std::uint32_t>(symbol)];
}

std::size_t CodeBuffer::eh_type_ref_label_count() const
{
	return eh_type_ref_labels_.size();
}

lowir_model::SymbolId CodeBuffer::eh_type_ref_label_symbol(
	std::size_t index) const
{
	if (index >= eh_type_ref_labels_.size())
		native_errors::ThrowInternal("invalid native EH type-reference label index");
	return eh_type_ref_labels_[index].symbol;
}

std::size_t CodeBuffer::eh_type_ref_label_offset(std::size_t index) const
{
	if (index >= eh_type_ref_labels_.size())
		native_errors::ThrowInternal("invalid native EH type-reference label index");
	return eh_type_ref_labels_[index].offset;
}

bool CodeBuffer::has_eh_personality_ref_label() const
{
	return eh_personality_ref_label_known_;
}

std::size_t CodeBuffer::eh_personality_ref_label_offset() const
{
	if (!eh_personality_ref_label_known_)
		native_errors::ThrowInternal(
			"undefined native EH personality-reference identity");
	return eh_personality_ref_label_offset_;
}

const std::vector<Fixup>& CodeBuffer::fixups() const
{
	return fixups_;
}

const std::vector<SymbolFixup>& CodeBuffer::symbol_fixups() const
{
	return symbol_fixups_;
}

std::size_t CodeBuffer::fixup_count() const
{
	return fixups_.size() + symbol_fixups_.size();
}

lowir_model::LocalLabelId CodeBuffer::internal_label(const char*)
{
	return allocate_local_label();
}

}
}
