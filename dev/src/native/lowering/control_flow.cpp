#include "native/lowering/control_flow.h"

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
	  cyclic_(function.blocks.size(), 0),
	  component_(function.blocks.size(), kNoBlock),
	  reachable_(function.blocks.size(), 0),
	  dominated_(function.blocks.size(), 0),
	  reachable_epoch_(0), dominated_epoch_(0), current_block_(0),
	  reachability_ready_(false), dominance_ready_(false)
{
	std::vector<std::size_t> blocks(function.next_block_id, kNoBlock);
	std::vector<std::size_t> block_last_position(function.blocks.size(), 0);
	std::size_t next_position = 0;
	for (std::size_t i = 0; i < function.blocks.size(); ++i)
	{
		blocks[function.blocks[i].id] = i;
		next_position += function.blocks[i].instructions.size();
		block_last_position[i] = next_position ? next_position - 1 : 0;
	}
	std::size_t position = 0;
	for (std::size_t i = 0; i < function.blocks.size(); ++i)
	{
		const lowir_model::LowirBlock& block = function.blocks[i];
		for (std::size_t j = 0; j < block.instructions.size(); ++j, ++position)
		{
			const Instruction& instruction = block.instructions[j];
			if (instruction.kind == Instruction::IK_PHI)
			{
				for (std::size_t k = 1; k < instruction.args.size(); k += 2)
				{
					const std::uint32_t predecessor = instruction.args[k - 1].block;
					if (predecessor >= blocks.size() || blocks[predecessor] == kNoBlock)
						continue;
					const std::size_t predecessor_block = blocks[predecessor];
					RecordUse(instruction.args[k],
						block_last_position[predecessor_block], predecessor_block);
				}
			}
			else
			{
				RecordUse(instruction.first, position, i);
				RecordUse(instruction.second, position, i);
				RecordUse(instruction.third, position, i);
				for (std::size_t k = 0; k < instruction.args.size(); ++k)
					RecordUse(instruction.args[k], position, i);
			}
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
			terminal.kind != Instruction::IK_UNREACHABLE &&
			terminal.kind != Instruction::IK_RESUME &&
			terminal.kind != Instruction::IK_THROW && i + 1 < function.blocks.size())
		{
			Operand fallthrough;
			fallthrough.kind = Operand::OP_LABEL;
			fallthrough.block = function.blocks[i + 1].id;
			AppendSuccessor(i, fallthrough, blocks);
		}
	}
	std::vector<int> component_index(function.blocks.size(), -1);
	std::vector<int> component_low(function.blocks.size(), -1);
	std::vector<unsigned char> component_stacked(function.blocks.size(), 0);
	std::vector<std::size_t> component_stack;
	int next_component_index = 0;
	for (std::size_t block = 0; block < function.blocks.size(); ++block)
		if (component_index[block] < 0)
			VisitComponent(block, component_index, component_low,
				component_stacked, component_stack, next_component_index);
	// Interval-mark layout-backward edges: a spill point inside such a span
	// can execute after later-emitted blocks have already run, so register
	// contents there cannot be assumed by walk order.
	backedge_covered_.assign(function.blocks.size(), 0);
	std::vector<int> delta(function.blocks.size() + 1, 0);
	for (std::size_t from = 0; from < successors_.size(); ++from)
		for (std::size_t edge = 0; edge < successors_[from].size(); ++edge)
		{
			const std::size_t target = successors_[from][edge];
			if (target > from) continue;
			++delta[target];
			--delta[from + 1];
		}
	int depth = 0;
	for (std::size_t i = 0; i < backedge_covered_.size(); ++i)
	{
		depth += delta[i];
		backedge_covered_[i] = depth > 0 ? 1 : 0;
	}
}

void ControlFlowQueries::VisitComponent(std::size_t block,
	std::vector<int>& index, std::vector<int>& low,
	std::vector<unsigned char>& stacked,
	std::vector<std::size_t>& stack, int& next_index)
{
	index[block] = next_index;
	low[block] = next_index++;
	stack.push_back(block);
	stacked[block] = 1;
	for (std::size_t i = 0; i < successors_[block].size(); ++i)
	{
		const std::size_t successor = successors_[block][i];
		if (index[successor] < 0)
		{
			VisitComponent(successor, index, low, stacked, stack, next_index);
			low[block] = std::min(low[block], low[successor]);
		}
		else if (stacked[successor])
			low[block] = std::min(low[block], index[successor]);
	}
	if (low[block] != index[block]) return;
	std::size_t first_member = kNoBlock;
	std::size_t component_size = 0;
	for (;;)
	{
		const std::size_t member = stack.back();
		stack.pop_back();
		stacked[member] = 0;
		component_[member] = block;
		if (component_size == 0)
			first_member = member;
		else
		{
			cyclic_[first_member] = 1;
			cyclic_[member] = 1;
		}
		++component_size;
		if (member == block) break;
	}
	if (component_size == 1 &&
		std::find(successors_[block].begin(), successors_[block].end(), block) !=
			successors_[block].end())
		cyclic_[block] = 1;
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
		// The stamp marks blocks reachable from entry without entering the
		// current block (the current block itself is stamped only as the
		// traversal barrier); a stamped block is therefore NOT dominated.
		++dominated_epoch_;
		std::vector<std::size_t> work;
		dominated_[current_block_] = dominated_epoch_;
		dominated_[0] = dominated_epoch_;
		work.push_back(0);
		while (!work.empty())
		{
			const std::size_t block = work.back();
			work.pop_back();
			for (std::size_t i = 0; i < successors_[block].size(); ++i)
			{
				const std::size_t successor = successors_[block][i];
				if (dominated_[successor] == dominated_epoch_) continue;
				dominated_[successor] = dominated_epoch_;
				work.push_back(successor);
			}
		}
		dominance_ready_ = true;
	}
	return dominated_[target] != dominated_epoch_;
}

bool ControlFlowQueries::CurrentBlockReaches(std::size_t target) const
{
	if (!reachability_ready_)
	{
		++reachable_epoch_;
		std::vector<std::size_t> work = successors_[current_block_];
		while (!work.empty())
		{
			const std::size_t block = work.back();
			work.pop_back();
			if (reachable_[block] == reachable_epoch_) continue;
			reachable_[block] = reachable_epoch_;
			work.insert(work.end(), successors_[block].begin(),
				successors_[block].end());
		}
		reachability_ready_ = true;
	}
	return reachable_[target] == reachable_epoch_;
}

bool ControlFlowQueries::CurrentBlockIsCyclic() const
{
	return cyclic_[current_block_] != 0;
}

bool ControlFlowQueries::BlocksShareCyclicComponent(
	std::size_t left, std::size_t right) const
{
	return left < component_.size() && right < component_.size() &&
		cyclic_[left] && cyclic_[right] && component_[left] == component_[right];
}

bool ControlFlowQueries::SpillIsSafe(
	lowir_model::ValueId value, std::size_t position) const
{
	// A block inside a layout-backward edge span is reached again after
	// later-layout blocks execute, so a spill store here may read a register
	// those blocks have already repurposed.
	if (current_block_ < backedge_covered_.size() &&
		backedge_covered_[current_block_]) return false;
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

bool ControlFlowQueries::FindDominatedUseTail(
	lowir_model::ValueId value, std::size_t after_position,
	std::size_t* begin, std::size_t* end)
{
	const std::vector<ValueUseSite>& uses = use_sites_[value];
	std::size_t first = static_cast<std::size_t>(-1);
	std::size_t last = 0;
	std::size_t first_block = kNoBlock;
	for (std::size_t i = 0; i < uses.size(); ++i)
	{
		if (uses[i].position <= after_position) continue;
		if (uses[i].position < first)
		{
			first = uses[i].position;
			first_block = uses[i].block;
		}
		last = std::max(last, uses[i].position);
	}
	// A one-position tail cannot amortize its boundary reload.
	if (first == static_cast<std::size_t>(-1) || first >= last ||
		first_block == kNoBlock) return false;
	const std::size_t selected_block = current_block_;
	SelectBlock(first_block);
	bool dominated = true;
	for (std::size_t i = 0; i < uses.size(); ++i)
		if (uses[i].position > after_position &&
			(cyclic_[uses[i].block] ||
			 !CurrentBlockDominates(uses[i].block)))
		{
			dominated = false;
			break;
		}
	SelectBlock(selected_block);
	if (!dominated) return false;
	*begin = first;
	*end = last;
	return true;
}

bool ControlFlowQueries::CyclicDefinitionDominatesUses(
	lowir_model::ValueId value, std::size_t definition_position,
	std::size_t definition_block)
{
	if (definition_block >= component_.size() || !cyclic_[definition_block])
		return false;
	const std::size_t selected_block = current_block_;
	SelectBlock(definition_block);
	bool valid = true;
	const std::vector<ValueUseSite>& uses = use_sites_[value];
	for (std::size_t i = 0; i < uses.size(); ++i)
		if (uses[i].position <= definition_position ||
			!BlocksShareCyclicComponent(definition_block, uses[i].block) ||
			!CurrentBlockDominates(uses[i].block))
		{
			valid = false;
			break;
		}
	SelectBlock(selected_block);
	return valid && !uses.empty();
}

}
}
