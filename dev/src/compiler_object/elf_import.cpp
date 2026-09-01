#include "compiler_object/elf_import.h"
#include "compiler_object/errors.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace compiler_object
{
namespace
{

class ElfObjectReader
{
public:
	explicit ElfObjectReader(const std::string& path)
	{
		std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
		if (!input)
			ThrowCompilerObjectInputOutputError("unable to open ELF object: " + path);
		bytes_.assign(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
		if (input.bad())
			ThrowCompilerObjectInputOutputError("unable to read ELF object: " + path);
	}

	std::uint16_t U16(std::size_t at) const { return Read(at, 2); }
	std::uint32_t U32(std::size_t at) const { return Read(at, 4); }
	std::uint64_t U64(std::size_t at) const { return Read(at, 8); }
	std::int64_t I64(std::size_t at) const
	{
		return static_cast<std::int64_t>(U64(at));
	}
	void Range(std::size_t at, std::size_t size) const
	{
		if (at > bytes_.size() || size > bytes_.size() - at)
			ThrowCompilerObjectInputError("truncated ELF relocatable object");
	}
	std::vector<unsigned char> Bytes(std::size_t at, std::size_t size) const
	{
		Range(at, size);
		return std::vector<unsigned char>(bytes_.begin() + at,
			bytes_.begin() + at + size);
	}
	std::string String(std::size_t table, std::size_t table_size,
		std::size_t offset) const
	{
		if (offset >= table_size) ThrowCompilerObjectInputError("invalid ELF string offset");
		Range(table, table_size);
		std::size_t end = offset;
		while (end < table_size && bytes_[table + end] != 0) ++end;
		if (end == table_size) ThrowCompilerObjectInputError("unterminated ELF string");
		return std::string(bytes_.begin() + table + offset,
			bytes_.begin() + table + end);
	}

private:
	std::uint64_t Read(std::size_t at, unsigned width) const
	{
		Range(at, width);
		std::uint64_t value = 0;
		for (unsigned i = 0; i < width; ++i)
			value |= static_cast<std::uint64_t>(bytes_[at + i]) << (i * 8);
		return value;
	}
	std::vector<unsigned char> bytes_;
};

struct SectionHeader
{
	std::uint32_t name;
	std::uint32_t type;
	std::uint64_t flags;
	std::uint64_t offset;
	std::uint64_t size;
	std::uint32_t link;
	std::uint32_t info;
	std::uint64_t alignment;
	std::uint64_t entry_size;
};

std::size_t Size(std::uint64_t value)
{
	if (value > std::numeric_limits<std::size_t>::max())
		ThrowCompilerObjectResourceLimit("ELF object exceeds host limits");
	return static_cast<std::size_t>(value);
}

bool ImportSection(const std::string& name)
{
	return name.compare(0, 5, ".text") == 0 ||
		name.compare(0, 5, ".data") == 0 ||
		name.compare(0, 7, ".rodata") == 0 ||
		name.compare(0, 4, ".bss") == 0;
}

std::string LocalSymbol(std::size_t object, std::size_t symbol)
{
	return ".__cppgm_elf_" + std::to_string(object) + "_sym_" +
		std::to_string(symbol);
}

}

lowir_native::RelocatableObject ImportElfRelocatable(
	const std::string& path, std::size_t object_ordinal)
{
	const ElfObjectReader in(path);
	if (in.U32(0) != UINT32_C(0x464c457f) || in.U16(16) != 1 ||
		in.U16(18) != 62 || in.U32(20) != 1)
		ThrowCompilerObjectInputError("unsupported ELF relocatable object: " + path);
	if (in.U16(58) != 64)
		ThrowCompilerObjectInputError("invalid ELF section header size");
	const std::size_t section_count = in.U16(60);
	const std::size_t section_names = in.U16(62);
	const std::size_t section_at = Size(in.U64(40));
	if (section_names >= section_count)
		ThrowCompilerObjectInputError("invalid ELF section-name table");
	std::vector<SectionHeader> headers(section_count);
	for (std::size_t i = 0; i < section_count; ++i)
	{
		const std::size_t at = section_at + i * 64;
		SectionHeader& h = headers[i];
		h.name = in.U32(at);
		h.type = in.U32(at + 4);
		h.flags = in.U64(at + 8);
		h.offset = in.U64(at + 24);
		h.size = in.U64(at + 32);
		h.link = in.U32(at + 40);
		h.info = in.U32(at + 44);
		h.alignment = in.U64(at + 48);
		h.entry_size = in.U64(at + 56);
	}
	const SectionHeader& shstr = headers[section_names];
	std::vector<std::string> names(section_count);
	std::unordered_map<std::size_t, std::size_t> output_section;
	lowir_native::RelocatableObject result;
	for (std::size_t i = 0; i < section_count; ++i)
	{
		names[i] = in.String(Size(shstr.offset), Size(shstr.size), headers[i].name);
		if (!ImportSection(names[i])) continue;
		if (headers[i].type != 1 && headers[i].type != 8)
			ThrowCompilerObjectInputError("unsupported allocatable ELF section: " + names[i]);
		lowir_native::RelocatableSection section;
		section.alignment = headers[i].alignment ? Size(headers[i].alignment) : 1;
		if (headers[i].type == 8) section.bytes.resize(Size(headers[i].size), 0);
		else section.bytes = in.Bytes(Size(headers[i].offset), Size(headers[i].size));
		output_section[i] = result.sections.size();
		result.sections.push_back(section);
	}

	std::size_t symbol_section = section_count;
	for (std::size_t i = 0; i < section_count; ++i)
		if (headers[i].type == 2) { symbol_section = i; break; }
	if (symbol_section == section_count)
		ThrowCompilerObjectInputError("ELF relocatable object has no symbol table");
	const SectionHeader& symtab = headers[symbol_section];
	if (symtab.entry_size != 24 || symtab.link >= section_count)
		ThrowCompilerObjectInputError("invalid ELF symbol table");
	const SectionHeader& strtab = headers[symtab.link];
	const std::size_t symbol_count = Size(symtab.size / symtab.entry_size);
	std::vector<std::string> symbols(symbol_count);
	std::vector<std::uint16_t> symbol_sections(symbol_count, 0);
	for (std::size_t i = 0; i < symbol_count; ++i)
	{
		const std::size_t at = Size(symtab.offset) + i * 24;
		const std::uint32_t name_offset = in.U32(at);
		const std::uint8_t info = static_cast<std::uint8_t>(in.U16(at + 4));
		const std::uint16_t defined = in.U16(at + 6);
		const std::size_t value = Size(in.U64(at + 8));
		const unsigned binding = info >> 4;
		const std::string name = in.String(
			Size(strtab.offset), Size(strtab.size), name_offset);
		symbol_sections[i] = defined;
		if (defined != 0 && output_section.count(defined))
		{
			symbols[i] = binding == 0 || name.empty() ?
				LocalSymbol(object_ordinal, i) : name;
			lowir_native::RelocatableLabel label;
			label.name = symbols[i];
			label.offset = value;
			if (label.offset > result.sections[output_section[defined]].bytes.size())
				ThrowCompilerObjectInputError("ELF symbol lies outside imported section");
			result.sections[output_section[defined]].labels.push_back(label);
		}
		else if (defined == 0) symbols[i] = name;
	}

	for (std::size_t i = 0; i < section_count; ++i)
	{
		const SectionHeader& rela = headers[i];
		if (rela.type != 4 || !output_section.count(rela.info)) continue;
		if (rela.entry_size != 24 || rela.link != symbol_section)
			ThrowCompilerObjectInputError("invalid ELF relocation section");
		lowir_native::RelocatableSection& target =
			result.sections[output_section[rela.info]];
		const std::size_t count = Size(rela.size / rela.entry_size);
		for (std::size_t j = 0; j < count; ++j)
		{
			const std::size_t at = Size(rela.offset) + j * 24;
			const std::uint64_t info = in.U64(at + 8);
			const std::size_t symbol = Size(info >> 32);
			const std::uint32_t type = static_cast<std::uint32_t>(info);
			if (symbol >= symbols.size() || symbols[symbol].empty())
				ThrowCompilerObjectInputError("ELF relocation has unavailable symbol");
			lowir_native::RelocatableRelocation relocation;
			relocation.offset = Size(in.U64(at));
			relocation.target = symbols[symbol];
			relocation.addend = in.I64(at + 16);
			if (type == 41 || type == 42)
			{
				if (relocation.offset < 2 ||
					relocation.offset > target.bytes.size() ||
					target.bytes[relocation.offset - 2] != 0x8b ||
					(target.bytes[relocation.offset - 1] & 0xc7) != 0x05)
					ThrowCompilerObjectInputError(
						"unsupported x86_64 GOTPCRELX instruction");
				// Relax a local GOT load to a direct RIP-relative address.
				target.bytes[relocation.offset - 2] = 0x8d;
				relocation.kind =
					lowir_native::RelocatableRelocation::RELATIVE32;
			}
			else if (type == 2 || type == 4)
				relocation.kind = lowir_native::RelocatableRelocation::RELATIVE32;
			else if (type == 1)
				relocation.kind = lowir_native::RelocatableRelocation::ABSOLUTE64;
			else ThrowCompilerObjectInputError("unsupported x86_64 ELF relocation type: " +
				std::to_string(type));
			const std::size_t width = relocation.kind ==
				lowir_native::RelocatableRelocation::RELATIVE32 ? 4 : 8;
			if (relocation.offset > target.bytes.size() ||
				width > target.bytes.size() - relocation.offset)
				ThrowCompilerObjectInputError("ELF relocation lies outside imported section");
			target.relocations.push_back(relocation);
		}
	}
	return result;
}

}
}
