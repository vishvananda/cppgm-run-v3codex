#pragma once

#include "lowering/api.h"
#include "lowering/ir/model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace constant_pool
{

class Pool
{
public:
	Pool(lowering::ir::Program& output, lowering::Stats* stats);

	lowering::ir::SymbolId Intern(lowering::ir::Global candidate);

private:
	struct Entry
	{
		std::size_t hash;
		std::size_t global;

		Entry(std::size_t hash_value, std::size_t global_value)
			: hash(hash_value), global(global_value) {}
	};

	static std::size_t Hash(const lowering::ir::Global& global);
	static bool Equal(const lowering::ir::Global& left,
		const lowering::ir::Global& right);
	void EnsureCapacity();
	void Rehash(std::size_t capacity);
	std::size_t FindSlot(std::size_t hash,
		const lowering::ir::Global& candidate, bool* found) const;

	lowering::ir::Program& output_;
	lowering::Stats* stats_;
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

}  // namespace constant_pool
}  // namespace lowering
}  // namespace cppgm
