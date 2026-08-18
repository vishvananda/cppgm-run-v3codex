#pragma once

#include "lowir_native_mir.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native
{
namespace elf_detail
{

struct CodeOffsetAdjustment
{
	struct Removal
	{
		std::size_t end = 0;
		std::size_t count = 0;
		std::size_t total = 0;
	};

	std::size_t translate(std::size_t offset) const;
	std::size_t bytes_removed() const;
	std::vector<Removal> removals;
};

struct Fixup
{
	enum Kind { RELATIVE32, ABSOLUTE64, ADDRESS32, TLS_OFFSET32 };

	Kind kind = RELATIVE32;
	mir_model::MirOperand::AddressBinding address_binding =
		mir_model::MirOperand::ADDRESS_LOCAL;
	std::size_t offset = 0;
	std::string target;
	long long addend = 0;
};

class CodeBuffer
{
public:
	CodeBuffer();
	explicit CodeBuffer(std::size_t base_offset,
		bool relocatable_addresses = false);

	void byte(unsigned value);
	void zeros(std::size_t count);
	void little(std::uint64_t value, unsigned count);
	void patch(std::size_t offset, std::uint64_t value, unsigned count);
	void align(std::size_t alignment);
	void label(const std::string& name);
	void label_at(const std::string& name, std::size_t offset);
	void alias(const std::string& name, const std::string& target);
	std::size_t size() const;
	bool relocatable_addresses() const;
	void bind_symbol_names(const std::vector<std::string>& names);
	const std::string& symbol_name(lowir_model::SymbolId symbol) const;
	void bind_strings(const lowir_model::StringPool& strings);
	const std::string& literal_spelling(lowir_model::StringId literal) const;
	void append(const std::vector<unsigned char>& bytes);
	bool short_relative(unsigned opcode, const std::string& target);
	CodeOffsetAdjustment relax_forward_branches(std::size_t begin);
	void relative32(const std::string& target);
	void absolute64(const std::string& target, long long addend = 0);
	void address32(const std::string& target,
		mir_model::MirOperand::AddressBinding address_binding);
	void tls_offset32(const std::string& target);
	void relative32_at(std::size_t offset, const std::string& target,
		long long elf_addend);
	void absolute64_at(std::size_t offset, const std::string& target,
		long long addend);
	void resolve();
	const std::vector<unsigned char>& bytes() const;
	std::vector<unsigned char> take_bytes();
	const std::unordered_map<std::string, std::size_t>& labels() const;
	const std::vector<Fixup>& fixups() const;
	std::size_t fixup_count() const;
	std::string internal_label(const char* purpose);

private:
	struct ShortRelativeFixup
	{
		std::size_t offset = 0;
		std::string target;
	};

	void resolve_short_relatives(std::size_t begin);
	std::size_t base_offset_;
	bool relocatable_addresses_;
	const std::vector<std::string>* symbol_names_;
	const lowir_model::StringPool* strings_;
	std::vector<unsigned char> bytes_;
	std::unordered_map<std::string, std::size_t> labels_;
	std::vector<std::size_t*> label_offsets_;
	std::vector<Fixup> fixups_;
	std::vector<ShortRelativeFixup> short_relative_fixups_;
	std::size_t next_internal_label_ = 0;
};

}
}
