#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lowir_native
{
namespace analysis
{

class ControlFlowQueries
{
public:
	explicit ControlFlowQueries(const lowir_model::LowirFunction& function);
	void SelectBlock(std::size_t block);
	bool CurrentBlockIsCyclic() const;
	bool BlocksShareCyclicComponent(std::size_t left,
		std::size_t right) const;
	bool SpillIsSafe(lowir_model::ValueId value, std::size_t position) const;
	bool FindDominatedUseTail(lowir_model::ValueId value,
		std::size_t after_position, std::size_t* begin, std::size_t* end);
	bool CyclicDefinitionDominatesUses(lowir_model::ValueId value,
		std::size_t definition_position, std::size_t definition_block);

private:
	struct ValueUseSite
	{
		std::size_t position;
		std::size_t block;
	};

	void AppendSuccessor(std::size_t from, const lowir_model::Operand& target,
		const std::vector<std::size_t>& blocks);
	void VisitComponent(std::size_t block,
		std::vector<int>& index, std::vector<int>& low,
		std::vector<unsigned char>& stacked,
		std::vector<std::size_t>& stack, int& next_index);
	void RecordUse(const lowir_model::Operand& operand,
		std::size_t position, std::size_t block);
	bool CurrentBlockDominates(std::size_t target) const;
	bool CurrentBlockReaches(std::size_t target) const;

	std::vector<std::vector<std::size_t> > successors_;
	std::vector<std::vector<ValueUseSite> > use_sites_;
	// Exact membership in a nontrivial strongly-connected component or a
	// singleton component with a self-edge.
	std::vector<unsigned char> cyclic_;
	std::vector<std::size_t> component_;
	// Blocks lying inside any layout-backward edge span [target, source]:
	// such a block can be re-entered by blocks the walk has not emitted yet.
	std::vector<unsigned char> backedge_covered_;
	// Stamp-validated marks: an entry counts only when it equals the
	// matching epoch, so invalidation at every block change is one
	// counter bump instead of a fill over all blocks.
	mutable std::vector<std::uint32_t> reachable_;
	mutable std::vector<std::uint32_t> dominated_;
	mutable std::uint32_t reachable_epoch_;
	mutable std::uint32_t dominated_epoch_;
	std::size_t current_block_;
	mutable bool reachability_ready_;
	mutable bool dominance_ready_;
};

}
}
