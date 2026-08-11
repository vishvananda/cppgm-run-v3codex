#pragma once

#include "pa15_lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
struct DumpNode;
}
namespace pa15_lowering_support
{

std::string StripOperationPrefix(const std::string& operation);
std::string SanitizeSymbol(const std::string& name);
std::vector<unsigned char> DecodeStringLiteral(const std::string& spelling);
std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed);
bool IsNullPointerLiteralCast(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& source, pa11::TypeId target);
bool IsIntNullPointerLiteralCast(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& source, pa11::TypeId target);
bool NeedsAggregateStorageAddress(bool namespace_object, bool has_leaf,
	const pa11::BindingRecord& binding);

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

private:
	Value inline_[InlineCount];
	std::vector<Value> overflow_;
	std::size_t count_;
};

typedef SmallSequence<std::uint32_t, 8> NodeChildren;
typedef SmallSequence<pa15_lowir_detail::Operand, 8> CallArguments;
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
