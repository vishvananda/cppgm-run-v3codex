#pragma once

#include <cstddef>
#include <vector>

namespace lowir_native
{
namespace spill_slots
{

class Pool
{
public:
	bool acquire(std::size_t size, std::size_t alignment,
		std::size_t position, std::size_t available_after,
		long long* offset);
	void remember(std::size_t size, std::size_t alignment,
		std::size_t available_after, long long offset);

private:
	struct Entry
	{
		std::size_t available_after;
		long long offset;

		Entry(std::size_t available_after_value, long long offset_value)
			: available_after(available_after_value), offset(offset_value) {}
	};
	struct LaterAvailability
	{
		bool operator()(const Entry& left, const Entry& right) const;
	};

	static const std::size_t kClasses = 5;
	static const std::size_t kBucketCount = kClasses * kClasses;
	static std::size_t bucket(std::size_t size, std::size_t alignment);
	std::vector<Entry> buckets_[kBucketCount];
};

}
}
