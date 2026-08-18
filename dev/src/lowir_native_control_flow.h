#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <string>
#include <unordered_map>
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
	bool SpillIsSafe(const std::string& name, std::size_t position) const;

private:
	struct ValueUseSite
	{
		std::size_t position;
		std::size_t block;
	};

	void AppendSuccessor(std::size_t from, const lowir_model::Operand& target,
		const std::vector<std::size_t>& blocks);
	void RecordUse(const lowir_model::Operand& operand,
		std::size_t position, std::size_t block);
	bool CurrentBlockDominates(std::size_t target) const;
	bool CurrentBlockReaches(std::size_t target) const;

	std::vector<std::vector<std::size_t> > successors_;
	std::unordered_map<std::string, std::vector<ValueUseSite> > use_sites_;
	mutable std::vector<unsigned char> reachable_;
	mutable std::vector<unsigned char> dominated_;
	std::size_t current_block_;
	mutable bool reachability_ready_;
	mutable bool dominance_ready_;
};

}
}
