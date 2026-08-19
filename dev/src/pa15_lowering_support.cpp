#include "pa15_lowering_support.h"
#include "lowir_model.h"
#include "pa12_semantic_model.h"
#include "post_tokenizer.h"

#include <algorithm>

namespace cppgm
{
namespace pa15_lowering_support
{

std::string NormalizeFloatingLiteral(const std::string& spelling,
	const pa15_lowir_detail::LowType& type)
{
	std::string numeric = spelling;
	if (numeric.empty() ||
		!((numeric[0] >= '0' && numeric[0] <= '9') || numeric[0] == '.'))
		return numeric;
	static const char* const suffixes[] = {
		"F128", "f128", "F32x", "f32x", "F64x", "f64x",
		"F16", "f16", "F32", "f32", "F64", "f64", "Q", "q"
	};
	for (std::size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		const std::size_t count = std::char_traits<char>::length(suffixes[i]);
		if (numeric.size() < count ||
			numeric.compare(numeric.size() - count, count, suffixes[i]) != 0)
			continue;
		numeric.erase(numeric.size() - count);
		if (type.kind == pa15_lowir_detail::LOW_F32) numeric += "f";
		else if (type.kind == pa15_lowir_detail::LOW_F80) numeric += "L";
		break;
	}
	return numeric;
}

bool DecodeFloatingLiteral(const std::string& spelling,
	const pa15_lowir_detail::LowType& type, std::uint64_t* low,
	std::uint64_t* high)
{
	lowir_model::LowType decoded_type;
	switch (type.kind)
	{
	case pa15_lowir_detail::LOW_F32:
		decoded_type = lowir_model::builtin_lowir_type(lowir_model::LTK_F32);
		break;
	case pa15_lowir_detail::LOW_F64:
		decoded_type = lowir_model::builtin_lowir_type(lowir_model::LTK_F64);
		break;
	case pa15_lowir_detail::LOW_F80:
		decoded_type = lowir_model::builtin_lowir_type(lowir_model::LTK_F80);
		break;
	default:
		return false;
	}
	return lowir_model::parse_lowir_floating_literal_bits(
		spelling, decoded_type, low, high);
}

PresentationNameMap::PresentationNameMap(const pa11::Program& program)
{
	for (std::size_t i = 0; i < program.entities.size(); ++i)
	{
		const pa11::EntityRecord& entity = program.entities[i];
		if (entity.presentation_name == 0 || entity.name == 0 ||
			entity.presentation_name == entity.name) continue;
		const std::string internal = program.names.Get(entity.name);
		const std::size_t separator = internal.rfind("::");
		names_[separator == std::string::npos ? internal :
			internal.substr(separator + 2)] =
			program.names.Get(entity.presentation_name);
	}
}

std::string PresentationNameMap::Apply(const std::string& qualified) const
{
	if (names_.empty()) return qualified;
	std::string result;
	std::size_t begin = 0;
	while (begin <= qualified.size())
	{
		const std::size_t end = qualified.find("::", begin);
		const std::string component = qualified.substr(begin,
			end == std::string::npos ? std::string::npos : end - begin);
		const std::unordered_map<std::string, std::string>::const_iterator found =
			names_.find(component);
		result += found == names_.end() ? component : found->second;
		if (end == std::string::npos) break;
		result += "::";
		begin = end + 2;
	}
	return result;
}

bool NeedsAggregateStorageAddress(bool namespace_object, bool has_leaf,
	const pa11::BindingRecord& binding)
{
	return (!namespace_object && has_leaf) ||
		(namespace_object && binding.variable_template_specialization);
}

pa11::EntityId LambdaClosureEntity(
	const pa11::Program& program, pa11::TypeId type)
{
	type = program.types.RemoveTopCv(type);
	const pa11::TypeRecord& record = program.types.Get(type);
	if (record.kind != pa11::TYPE_NAMED ||
		record.entity >= program.entities.size() ||
		!program.entities[record.entity].lambda_closure)
		return pa11::kNoEntity;
	return record.entity;
}

bool IsLambdaCaptureMember(
	const pa11::Program& program, pa11::BindingId binding)
{
	const pa11::BindingRecord& member = program.bindings[binding];
	return program.BindingLayout(member).member_offset == 0 &&
		member.member_owner != pa11::kNoEntity &&
		member.member_owner < program.entities.size() &&
		program.entities[member.member_owner].lambda_closure;
}

std::string MissingStorageBindingDetail(
	const pa11::Program& program, pa11::BindingId binding)
{
	std::string detail = std::to_string(binding);
	if (binding >= program.bindings.size()) return detail;
	const pa11::BindingRecord& missing = program.bindings[binding];
	detail += " name=" + program.names.Get(missing.name);
	if (missing.qualified_name != 0)
		detail += " qualified=" + program.names.Get(missing.qualified_name);
	detail += " kind=" +
		std::to_string(static_cast<unsigned>(missing.kind));
	return detail;
}

bool IsNullPointerLiteralCast(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& source, pa11::TypeId target)
{
	target = program.types.RemoveTopCv(target);
	const pa11::TypeRecord& target_record = program.types.Get(target);
	if (target_record.kind != pa11::TYPE_POINTER) return false;
	return source.integer_literal_zero;
}

bool IsIntNullPointerLiteralCast(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& source, pa11::TypeId target)
{
	const pa11::TypeRecord source_type = program.types.Get(
		program.types.RemoveTopCv(source.type));
	return source_type.kind == pa11::TYPE_FUNDAMENTAL &&
		source_type.fundamental == pa11::FUND_INT &&
		IsNullPointerLiteralCast(program, source, target);
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

FlatIdPairMap::FlatIdPairMap() : slots_(16, 0) {}

std::size_t FlatIdPairMap::Hash(std::uint32_t first, std::uint32_t second)
{
	std::uint64_t value = (static_cast<std::uint64_t>(first) << 32) | second;
	value ^= value >> 30;
	value *= UINT64_C(0xbf58476d1ce4e5b9);
	value ^= value >> 27;
	value *= UINT64_C(0x94d049bb133111eb);
	value ^= value >> 31;
	return static_cast<std::size_t>(value);
}

bool FlatIdPairMap::Find(std::uint32_t first, std::uint32_t second,
	std::uint32_t* value) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(first, second) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (first_keys_[entry] == first && second_keys_[entry] == second)
		{
			*value = values_[entry];
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

void FlatIdPairMap::Insert(std::uint32_t first, std::uint32_t second,
	std::uint32_t value)
{
	if ((first_keys_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(first, second) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t entry = slots_[slot] - 1;
		if (first_keys_[entry] == first && second_keys_[entry] == second)
		{
			values_[entry] = value;
			return;
		}
		slot = (slot + 1) & mask;
	}
	if (first_keys_.size() >= UINT32_MAX)
		throw std::runtime_error("too many flat identity-pair map entries");
	first_keys_.push_back(first);
	second_keys_.push_back(second);
	values_.push_back(value);
	slots_[slot] = static_cast<std::uint32_t>(first_keys_.size());
	occupied_slots_.push_back(slot);
}

void FlatIdPairMap::Clear()
{
	first_keys_.clear();
	second_keys_.clear();
	values_.clear();
	for (std::size_t i = 0; i < occupied_slots_.size(); ++i)
		slots_[occupied_slots_[i]] = 0;
	occupied_slots_.clear();
}

void FlatIdPairMap::Rehash(std::size_t capacity)
{
	slots_.assign(capacity, 0);
	occupied_slots_.clear();
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < first_keys_.size(); ++i)
	{
		std::size_t slot = Hash(first_keys_[i], second_keys_[i]) & mask;
		while (slots_[slot] != 0) slot = (slot + 1) & mask;
		slots_[slot] = static_cast<std::uint32_t>(i + 1);
		occupied_slots_.push_back(slot);
	}
}

std::size_t FlatIdPairMap::StorageBytes() const
{
	return first_keys_.capacity() * sizeof(std::uint32_t) +
		second_keys_.capacity() * sizeof(std::uint32_t) +
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
