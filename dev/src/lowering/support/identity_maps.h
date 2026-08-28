#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace support
{

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

}  // namespace support
}  // namespace lowering
}  // namespace cppgm
