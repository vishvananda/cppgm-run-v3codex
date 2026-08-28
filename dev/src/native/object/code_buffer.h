#pragma once

#include "native/mir/construction.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace lowir_native
{
struct Stats;
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

struct LocalFixup
{
	enum Kind { RELATIVE32, ABSOLUTE64, ADDRESS32 };

	Kind kind = RELATIVE32;
	std::size_t offset = 0;
	lowir_model::LocalLabelId target;
};

struct SymbolFixup
{
	Fixup::Kind kind = Fixup::RELATIVE32;
	mir_model::MirOperand::AddressBinding address_binding =
		mir_model::MirOperand::ADDRESS_LOCAL;
	std::size_t offset = 0;
	lowir_model::SymbolId target;
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
	void label(lowir_model::SymbolId symbol);
	void label_object(lowir_model::StringId symbol);
	void label_eh_type_ref(lowir_model::SymbolId symbol);
	void label_eh_personality_ref();
	void label(lowir_model::LocalLabelId label);
	void label_at(const std::string& name, std::size_t offset);
	void alias(const std::string& name, const std::string& target);
	void alias(const std::string& name, lowir_model::SymbolId target);
	void alias_object(lowir_model::StringId name,
		lowir_model::SymbolId target);
	void begin_function_blocks(std::size_t count);
	lowir_model::LocalLabelId block_label(lowir_model::BlockId block) const;
	std::size_t label_offset(lowir_model::LocalLabelId label) const;
	std::size_t size() const;
	bool relocatable_addresses() const;
	void bind_symbol_names(const std::vector<lowir_model::StringId>& names);
	const std::string& symbol_name(lowir_model::SymbolId symbol) const;
	void bind_strings(const lowir_model::SealedStringPool& strings);
	void bind_stats(Stats* stats);
	bool collects_stats() const;
	void note_literal_text_parse(std::uint64_t nanoseconds = 0);
	void note_direct_zero_encoding(std::size_t bytes, std::size_t stores);
	const lowir_model::SealedStringPool& strings() const;
	const std::string& literal_spelling(lowir_model::StringId literal) const;
	void append(const std::vector<unsigned char>& bytes);
	bool short_relative(unsigned opcode, const std::string& target);
	bool short_relative(unsigned opcode, lowir_model::LocalLabelId target);
	CodeOffsetAdjustment relax_forward_branches(std::size_t begin);
	void relative32(const std::string& target);
	void relative32(lowir_model::LocalLabelId target);
	void relative32(lowir_model::SymbolId target);
	void absolute64(const std::string& target, long long addend = 0);
	void absolute64(lowir_model::LocalLabelId target);
	void absolute64(lowir_model::SymbolId target, long long addend = 0);
	void address32(const std::string& target,
		mir_model::MirOperand::AddressBinding address_binding);
	void address32(lowir_model::LocalLabelId target);
	void address32(lowir_model::SymbolId target,
		mir_model::MirOperand::AddressBinding address_binding);
	void tls_offset32(const std::string& target);
	void tls_offset32(lowir_model::SymbolId target);
	void relative32_at(std::size_t offset, const std::string& target,
		long long elf_addend);
	void absolute64_at(std::size_t offset, const std::string& target,
		long long addend);
	void resolve();
	const std::vector<unsigned char>& bytes() const;
	std::vector<unsigned char> take_bytes();
	std::unordered_map<std::string, std::size_t> materialized_labels() const;
	const std::unordered_map<std::string, std::size_t>& named_labels() const;
	std::size_t symbol_label_capacity() const;
	bool has_symbol_label(lowir_model::SymbolId symbol) const;
	std::size_t symbol_label_offset(lowir_model::SymbolId symbol) const;
	std::size_t object_label_capacity() const;
	bool has_object_label(lowir_model::StringId symbol) const;
	std::size_t object_label_offset(lowir_model::StringId symbol) const;
	std::size_t eh_type_ref_label_count() const;
	lowir_model::SymbolId eh_type_ref_label_symbol(std::size_t index) const;
	std::size_t eh_type_ref_label_offset(std::size_t index) const;
	bool has_eh_personality_ref_label() const;
	std::size_t eh_personality_ref_label_offset() const;
	const std::vector<Fixup>& fixups() const;
	const std::vector<SymbolFixup>& symbol_fixups() const;
	std::size_t fixup_count() const;
	lowir_model::LocalLabelId internal_label(const char* purpose);

private:
	struct ShortRelativeFixup
	{
		std::size_t offset = 0;
		lowir_model::LocalLabelId target;
	};
	struct NamedShortRelativeFixup
	{
		std::size_t offset = 0;
		std::string target;
	};
	struct LabelBinding
	{
		bool symbol = false;
		lowir_model::SymbolId symbol_id;
		const std::string* name = 0;
		std::size_t* offset = 0;
	};
	struct EhTypeRefLabelBinding
	{
		lowir_model::SymbolId symbol;
		std::size_t offset = 0;
	};

	void resolve_short_relatives(std::size_t begin);
	void resolve_local_fixups(std::size_t begin);
	lowir_model::LocalLabelId allocate_local_label();
	std::size_t local_label_offset(lowir_model::LocalLabelId label) const;
	void insert_named_label(const std::string& name, std::size_t offset,
		bool duplicate_is_runtime_error);
	std::size_t base_offset_;
	bool relocatable_addresses_;
	const std::vector<lowir_model::StringId>* symbol_names_;
	const lowir_model::SealedStringPool* strings_;
	Stats* stats_;
	std::vector<unsigned char> bytes_;
	std::unordered_map<std::string, std::size_t> labels_;
	std::vector<std::size_t> symbol_label_offsets_;
	std::vector<unsigned char> symbol_label_known_;
	std::vector<std::size_t> object_label_offsets_;
	std::vector<unsigned char> object_label_known_;
	std::deque<EhTypeRefLabelBinding> eh_type_ref_labels_;
	std::size_t eh_personality_ref_label_offset_ = 0;
	bool eh_personality_ref_label_known_ = false;
	std::vector<LabelBinding> label_bindings_;
	std::vector<std::size_t*> label_offsets_;
	std::vector<Fixup> fixups_;
	std::vector<SymbolFixup> symbol_fixups_;
	std::vector<LocalFixup> local_fixups_;
	std::vector<std::size_t> local_label_offsets_;
	std::vector<lowir_model::LocalLabelId> bound_local_labels_;
	std::vector<lowir_model::LocalLabelId> block_labels_;
	std::vector<ShortRelativeFixup> short_relative_fixups_;
	std::vector<NamedShortRelativeFixup> named_short_relative_fixups_;
};

}
}
