#pragma once

#include "lowering/ir/model.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{
struct Stats;
struct DumpNode;
}
namespace pa15_lowering_support
{


std::string SanitizeSymbol(const std::string& name);
std::vector<unsigned char> DecodeStringLiteral(const std::string& spelling);
std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed);
bool IsNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target);
bool IsIntNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target);
bool NeedsAggregateStorageAddress(bool namespace_object, bool has_leaf,
	const semantic::BindingRecord& binding);
semantic::EntityId LambdaClosureEntity(
	const semantic::Program& program, semantic::TypeId type);
bool IsLambdaCaptureMember(
	const semantic::Program& program, semantic::BindingId binding);
std::string MissingStorageBindingDetail(
	const semantic::Program& program, semantic::BindingId binding);
std::string NormalizeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type);
bool DecodeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type, std::uint64_t* low,
	std::uint64_t* high);

class PresentationNameMap
{
	public:
	PresentationNameMap(const semantic::Program& program,
		semantic::Stats* stats);
	std::string Apply(const semantic::BindingRecord& binding) const;

private:
	const std::string& ClassTemplatePresentation(
		std::uint32_t presentation) const;
	const semantic::Program& program_;
	semantic::Stats* stats_;
	std::vector<std::uint32_t> replacement_presentations_;
	std::vector<semantic::EntityId> presentation_entities_;
	mutable std::vector<std::uint32_t> rendered_indices_;
	mutable std::vector<std::string> rendered_presentations_;
	mutable std::vector<semantic::NameId> path_;
};

class FlatIdMap
{
public:
	FlatIdMap();
	bool Find(std::uint32_t key, std::uint32_t* value) const;
	void Insert(std::uint32_t key, std::uint32_t value);
	void Clear();
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);
	static std::size_t Hash(std::uint32_t key);

	std::vector<std::uint32_t> keys_;
	std::vector<std::uint32_t> values_;
	std::vector<std::uint32_t> slots_;
	std::vector<std::size_t> occupied_slots_;
};

class FlatIdPairMap
{
public:
	FlatIdPairMap();
	bool Find(std::uint32_t first, std::uint32_t second,
		std::uint32_t* value) const;
	void Insert(std::uint32_t first, std::uint32_t second,
		std::uint32_t value);
	void Clear();
	std::size_t StorageBytes() const;

private:
	void Rehash(std::size_t capacity);
	static std::size_t Hash(std::uint32_t first, std::uint32_t second);

	std::vector<std::uint32_t> first_keys_;
	std::vector<std::uint32_t> second_keys_;
	std::vector<std::uint32_t> values_;
	std::vector<std::uint32_t> slots_;
	std::vector<std::size_t> occupied_slots_;
};

template <typename Value, std::size_t InlineCount>
class SmallSequence
{
public:
	SmallSequence() : count_(0) {}

	void Push(const Value& value)
	{
		if (count_ < InlineCount) inline_[count_] = value;
		else overflow_.push_back(value);
		++count_;
	}
	void Pop()
	{
		if (count_ == 0)
			throw std::logic_error("cannot pop an empty small sequence");
		if (count_ > InlineCount) overflow_.pop_back();
		--count_;
	}

	std::size_t size() const { return count_; }
	bool empty() const { return count_ == 0; }
	const Value& operator[](std::size_t index) const
	{
		return index < InlineCount ? inline_[index] : overflow_[index - InlineCount];
	}
	Value& operator[](std::size_t index)
	{
		return index < InlineCount ? inline_[index] :
			overflow_[index - InlineCount];
	}

private:
	Value inline_[InlineCount];
	std::vector<Value> overflow_;
	std::size_t count_;
};

typedef SmallSequence<std::uint32_t, 8> NodeChildren;
typedef SmallSequence<lowering::ir::Operand, 8> CallArguments;
typedef SmallSequence<std::uint8_t, 8> CallArgumentFlags;
typedef SmallSequence<std::uint32_t, 8> SwitchCases;

class CountingStreamBuffer : public std::streambuf
{
public:
	explicit CountingStreamBuffer(std::streambuf* destination);
	std::size_t Bytes() const;

protected:
	int_type overflow(int_type character);
	std::streamsize xsputn(const char* data, std::streamsize size);
	int sync();

private:
	std::streambuf* destination_;
	std::size_t bytes_;
};

}
}
