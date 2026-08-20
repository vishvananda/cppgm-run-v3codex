#pragma once

#include "pa15_lowering.h"
#include "pa15_lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa21_constant_template_lowering
{

class Pool
{
public:
	Pool(pa15_lowir_detail::TypedProgram& output, LowIRLoweringStats* stats);

	pa15_lowir_detail::SymbolId Intern(pa15_lowir_detail::Global candidate);

private:
	struct Entry
	{
		std::size_t hash;
		std::size_t global;

		Entry(std::size_t hash_value, std::size_t global_value)
			: hash(hash_value), global(global_value) {}
	};

	static std::size_t Hash(const pa15_lowir_detail::Global& global);
	static bool Equal(const pa15_lowir_detail::Global& left,
		const pa15_lowir_detail::Global& right);
	void EnsureCapacity();
	void Rehash(std::size_t capacity);
	std::size_t FindSlot(std::size_t hash,
		const pa15_lowir_detail::Global& candidate, bool* found) const;

	pa15_lowir_detail::TypedProgram& output_;
	LowIRLoweringStats* stats_;
	std::vector<Entry> entries_;
	std::vector<std::uint32_t> slots_;
};

}
}
