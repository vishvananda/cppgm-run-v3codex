#pragma once

#include "lowering/ir/model.h"
#include "lowering/support/errors.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace support
{

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
			ThrowLoweringInternal("cannot pop an empty small sequence");
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
typedef SmallSequence<std::uint32_t, 8> CallArgumentSizes;
typedef SmallSequence<std::uint32_t, 8> SwitchCases;

}  // namespace support
}  // namespace lowering
}  // namespace cppgm
