#include "lowir_native_control_flow.h"

#include <algorithm>

namespace lowir_native
{
namespace analysis
{

using lowir_model::Instruction;
using lowir_model::Operand;

const std::size_t kNoBlock = static_cast<std::size_t>(-1);

ControlFlowQueries::ControlFlowQueries(
	const lowir_model::LowirFunction& function)
	: successors_(function.blocks.size()), use_sites_(function.value_names.size()),
	  reachable_(function.blocks.size()),
	  dominated_(function.blocks.size()), current_block_(0),
	  reachability_ready_(false), dominance_ready_(false)
{
	std::vector<std::size_t> blocks(function.next_block_id, kNoBlock);
	for (std::size_t i = 0; i < function.blocks.size(); ++i)
		blocks[function.blocks[i].id] = i;
	std::size_t position = 0;
	for (std::size_t i = 0; i < function.blocks.size(); ++i)
	{
		const lowir_model::LowirBlock& block = function.blocks[i];
		for (std::size_t j = 0; j < block.instructions.size(); ++j, ++position)
		{
			const Instruction& instruction = block.instructions[j];
			RecordUse(instruction.first, position, i);
			RecordUse(instruction.second, position, i);
			RecordUse(instruction.third, position, i);
			for (std::size_t k = 0; k < instruction.args.size(); ++k)
				RecordUse(instruction.args[k], position, i);
			if (instruction.kind == Instruction::IK_EH_TRY ||
				instruction.kind == Instruction::IK_EH_CLEANUP)
				AppendSuccessor(i, instruction.first, blocks);
		}
		if (block.instructions.empty()) continue;
		const Instruction& terminal = block.instructions.back();
		if (terminal.kind == Instruction::IK_JUMP)
			AppendSuccessor(i, terminal.first, blocks);
		else if (terminal.kind == Instruction::IK_BRANCH)
		{
			AppendSuccessor(i, terminal.second, blocks);
			AppendSuccessor(i, terminal.third, blocks);
		}
		else if (terminal.kind == Instruction::IK_SWITCH)
		{
			AppendSuccessor(i, terminal.second, blocks);
			for (std::size_t j = 1; j < terminal.args.size(); j += 2)
				AppendSuccessor(i, terminal.args[j], blocks);
		}
		else if (terminal.kind != Instruction::IK_RETURN &&
			terminal.kind != Instruction::IK_RESUME &&
			terminal.kind != Instruction::IK_THROW && i + 1 < function.blocks.size())
		{
			Operand fallthrough;
			fallthrough.kind = Operand::OP_LABEL;
			fallthrough.block = function.blocks[i + 1].id;
			AppendSuccessor(i, fallthrough, blocks);
		}
	}
}

void ControlFlowQueries::AppendSuccessor(std::size_t from,
	const Operand& target,
	const std::vector<std::size_t>& blocks)
{
	if (target.kind != Operand::OP_LABEL) return;
	const std::uint32_t id = target.block;
	if (id >= blocks.size() || blocks[id] == kNoBlock) return;
	std::vector<std::size_t>& successors = successors_[from];
	if (std::find(successors.begin(), successors.end(), blocks[id]) ==
		successors.end()) successors.push_back(blocks[id]);
}

void ControlFlowQueries::RecordUse(
	const Operand& operand, std::size_t position, std::size_t block)
{
	if (operand.kind != Operand::OP_TEMP) return;
	ValueUseSite site;
	site.position = position;
	site.block = block;
	use_sites_[operand.value].push_back(site);
}

void ControlFlowQueries::SelectBlock(std::size_t block)
{
	current_block_ = block;
	reachability_ready_ = false;
	dominance_ready_ = false;
}

bool ControlFlowQueries::CurrentBlockDominates(std::size_t target) const
{
	if (current_block_ == 0 || current_block_ == target) return true;
	if (!dominance_ready_)
	{
		std::fill(dominated_.begin(), dominated_.end(), 1);
		std::vector<unsigned char> seen(successors_.size(), 0);
		std::vector<std::size_t> work;
		seen[current_block_] = 1;
		seen[0] = 1;
		work.push_back(0);
		while (!work.empty())
		{
			const std::size_t block = work.back();
			work.pop_back();
			dominated_[block] = 0;
			for (std::size_t i = 0; i < successors_[block].size(); ++i)
			{
				const std::size_t successor = successors_[block][i];
				if (seen[successor]) continue;
				seen[successor] = 1;
				work.push_back(successor);
			}
		}
		dominated_[current_block_] = 1;
		dominance_ready_ = true;
	}
	return dominated_[target] != 0;
}

bool ControlFlowQueries::CurrentBlockReaches(std::size_t target) const
{
	if (!reachability_ready_)
	{
		std::fill(reachable_.begin(), reachable_.end(), 0);
		std::vector<std::size_t> work = successors_[current_block_];
		while (!work.empty())
		{
			const std::size_t block = work.back();
			work.pop_back();
			if (reachable_[block]) continue;
			reachable_[block] = 1;
			work.insert(work.end(), successors_[block].begin(),
				successors_[block].end());
		}
		reachability_ready_ = true;
	}
	return reachable_[target] != 0;
}

bool ControlFlowQueries::CurrentBlockIsCyclic() const
{
	return CurrentBlockReaches(current_block_);
}

bool ControlFlowQueries::SpillIsSafe(
	lowir_model::ValueId value, std::size_t position) const
{
	if (CurrentBlockIsCyclic()) return false;
	const std::vector<ValueUseSite>& uses = use_sites_[value];
	for (std::size_t i = 0; i < uses.size(); ++i)
	{
		const ValueUseSite& use = uses[i];
		if (use.position <= position && CurrentBlockReaches(use.block)) return false;
		if (use.position > position && !CurrentBlockDominates(use.block)) return false;
	}
	return true;
}

}
}
