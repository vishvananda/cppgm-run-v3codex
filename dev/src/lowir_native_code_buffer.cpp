#include "lowir_native_code_buffer.h"

#include <climits>
#include <stdexcept>

namespace lowir_native
{
namespace elf_detail
{
namespace
{

const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kExecutableContentOffset = 120;

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
