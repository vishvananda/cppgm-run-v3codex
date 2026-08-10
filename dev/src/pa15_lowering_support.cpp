#include "pa15_lowering_support.h"
#include "pa12_semantic_model.h"
#include "post_tokenizer.h"

#include <algorithm>

namespace cppgm
{
namespace pa15_lowering_support
{

bool IsIncompletePointeeNullPointerCast(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& source, pa11::TypeId target)
{
	target = program.types.RemoveTopCv(target);
	const pa11::TypeRecord& target_record = program.types.Get(target);
	if (target_record.kind != pa11::TYPE_POINTER) return false;
	const pa11::TypeId pointee = program.types.RemoveTopCv(target_record.child);
	const pa11::TypeRecord& pointee_record = program.types.Get(pointee);
	return pointee_record.kind == pa11::TYPE_NAMED &&
		!program.entities[pointee_record.entity].complete &&
		source.kind == pa12_semantic_detail::DUMP_LITERAL && source.constant &&
		source.constant_value == 0;
}

FlatIdMap::FlatIdMap() : slots_(16, 0) {}

std::size_t FlatIdMap::Hash(std::uint32_t key)
{
	std::uint64_t value = key;
	value ^= value >> 16;
	value *= UINT64_C(0x7feb352d);
	value ^= value >> 15;
	return static_cast<std::size_t>(value);
}

bool FlatIdMap::Find(std::uint32_t key, std::uint32_t* value) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (keys_[entry] == key)
		{
			*value = values_[entry];
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

void FlatIdMap::Insert(std::uint32_t key, std::uint32_t value)
{
	if ((keys_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(key) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (keys_[entry] == key)
		{
			values_[entry] = value;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (keys_.size() >= UINT32_MAX)
		throw std::runtime_error("too many flat identity map entries");
	keys_.push_back(key);
	values_.push_back(value);
	slots_[slot] = static_cast<std::uint32_t>(keys_.size());
	occupied_slots_.push_back(slot);
}

void FlatIdMap::Clear()
{
	keys_.clear();
	values_.clear();
	for (std::size_t i = 0; i < occupied_slots_.size(); ++i)
		slots_[occupied_slots_[i]] = 0;
	occupied_slots_.clear();
}

void FlatIdMap::Rehash(std::size_t capacity)
{
	slots_.assign(capacity, 0);
	occupied_slots_.clear();
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < keys_.size(); ++i)
	{
		std::size_t slot = Hash(keys_[i]) & mask;
		while (slots_[slot] != 0) slot = (slot + 1) & mask;
		slots_[slot] = static_cast<std::uint32_t>(i + 1);
		occupied_slots_.push_back(slot);
	}
}

std::size_t FlatIdMap::StorageBytes() const
{
	return keys_.capacity() * sizeof(std::uint32_t) +
		values_.capacity() * sizeof(std::uint32_t) +
		slots_.capacity() * sizeof(std::uint32_t) +
		occupied_slots_.capacity() * sizeof(std::size_t);
}

std::string StripOperationPrefix(const std::string& operation)
{
	const std::size_t colon = operation.rfind(':');
	return colon == std::string::npos ? operation : operation.substr(colon + 1);
}

std::string SanitizeSymbol(const std::string& name)
{
	std::string result;
	result.reserve(name.size() + 8);
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
		{
			result += "__";
			++i;
		}
		else
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			result += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' ? static_cast<char>(c) : '_';
		}
	}
	if (result.empty()) result = "anonymous";
	return result;
}

std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed)
{
	if (width >= 64) return value;
	const std::uint64_t mask = (std::uint64_t(1) << width) - 1;
	std::uint64_t narrowed = static_cast<std::uint64_t>(value) & mask;
	if (is_signed &&
		(narrowed & (std::uint64_t(1) << (width - 1))) != 0)
		narrowed |= ~mask;
	return static_cast<std::int64_t>(narrowed);
}

std::vector<unsigned char> DecodeStringLiteral(const std::string& spelling)
{
	std::string decoded;
	if (!DecodeNarrowStringLiteral(spelling, &decoded))
		throw std::runtime_error("invalid PA15 string literal spelling");
	std::vector<unsigned char> bytes(decoded.begin(), decoded.end());
	bytes.push_back(0);
	return bytes;
}

CountingStreamBuffer::CountingStreamBuffer(std::streambuf* destination)
	: destination_(destination), bytes_(0)
{
}

std::size_t CountingStreamBuffer::Bytes() const
{
	return bytes_;
}

CountingStreamBuffer::int_type CountingStreamBuffer::overflow(int_type character)
{
	if (traits_type::eq_int_type(character, traits_type::eof()))
		return traits_type::not_eof(character);
	const int_type written = destination_->sputc(
		traits_type::to_char_type(character));
	if (!traits_type::eq_int_type(written, traits_type::eof())) ++bytes_;
	return written;
}

std::streamsize CountingStreamBuffer::xsputn(const char* data,
	std::streamsize size)
{
	const std::streamsize written = destination_->sputn(data, size);
	if (written > 0) bytes_ += static_cast<std::size_t>(written);
	return written;
}

int CountingStreamBuffer::sync()
{
	return destination_->pubsync();
}

}
}
