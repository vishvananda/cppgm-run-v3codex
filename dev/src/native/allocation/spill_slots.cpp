#include "native/allocation/spill_slots.h"

#include <algorithm>
#include <limits>

namespace lowir_native
{
namespace spill_slots
{
namespace
{

std::size_t size_class(std::size_t value)
{
	switch (value)
	{
	case 1: return 0;
	case 2: return 1;
	case 4: return 2;
	case 8: return 3;
	case 16: return 4;
	default: return std::numeric_limits<std::size_t>::max();
	}
}

}

bool Pool::LaterAvailability::operator()(
	const Entry& left, const Entry& right) const
{
	if (left.available_after != right.available_after)
		return left.available_after > right.available_after;
	return left.offset > right.offset;
}

std::size_t Pool::bucket(std::size_t size, std::size_t alignment)
{
	const std::size_t size_index = size_class(size);
	const std::size_t alignment_index = size_class(alignment);
	if (size_index == std::numeric_limits<std::size_t>::max() ||
		alignment_index == std::numeric_limits<std::size_t>::max())
		return kBucketCount;
	return size_index * kClasses + alignment_index;
}

bool Pool::acquire(std::size_t size, std::size_t alignment,
	std::size_t position, std::size_t available_after, long long* offset)
{
	const std::size_t index = bucket(size, alignment);
	if (index == kBucketCount || buckets_[index].empty() ||
		buckets_[index].front().available_after >= position)
		return false;
	std::pop_heap(buckets_[index].begin(), buckets_[index].end(),
		LaterAvailability());
	Entry reused = buckets_[index].back();
	buckets_[index].pop_back();
	*offset = reused.offset;
	reused.available_after = available_after;
	buckets_[index].push_back(reused);
	std::push_heap(buckets_[index].begin(), buckets_[index].end(),
		LaterAvailability());
	return true;
}

void Pool::remember(std::size_t size, std::size_t alignment,
	std::size_t available_after, long long offset)
{
	const std::size_t index = bucket(size, alignment);
	if (index == kBucketCount) return;
	buckets_[index].push_back(Entry(available_after, offset));
	std::push_heap(buckets_[index].begin(), buckets_[index].end(),
		LaterAvailability());
}

}
}
