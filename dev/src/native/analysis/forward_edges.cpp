#include "native/analysis/forward_edges.h"

#include "native/driver/stats.h"

#include <cstdint>

namespace lowir_native
{
namespace analysis
{

using lowir_model::Instruction;
using lowir_model::Operand;

void mark_exact_forward_edge_values(FunctionFacts& facts,
	const lowir_model::LowirFunction& function,
	const std::vector<std::size_t>& definition_blocks,
	const std::vector<std::pair<lowir_model::ValueId, std::size_t> >& block_uses,
	Stats* stats)
{
	const std::size_t block_count = function.blocks.size();
	const std::size_t no_block = FunctionFacts::missing_position();
	const std::size_t ambiguous = no_block - 1;
	std::vector<std::size_t> block_index(function.next_block_id, no_block);
	std::vector<std::size_t> sole_successor(block_count, no_block);
	std::vector<std::size_t> sole_predecessor(block_count, no_block);
	for (std::size_t block = 0; block < block_count; ++block)
		block_index[function.blocks[block].id] = block;

	const auto note_edge = [&](std::size_t from, const Operand& target) {
		if (target.kind != Operand::OP_LABEL ||
			target.block >= block_index.size()) return;
		const std::size_t to = block_index[target.block];
		if (to == no_block) return;
		if (sole_successor[from] == no_block) sole_successor[from] = to;
		else if (sole_successor[from] != to) sole_successor[from] = ambiguous;
		if (sole_predecessor[to] == no_block) sole_predecessor[to] = from;
		else if (sole_predecessor[to] != from) sole_predecessor[to] = ambiguous;
	};

	for (std::size_t block = 0; block < block_count; ++block)
	{
		const std::vector<Instruction>& instructions =
			function.blocks[block].instructions;
		for (std::size_t i = 0; i < instructions.size(); ++i)
			if (instructions[i].kind == Instruction::IK_EH_TRY ||
				instructions[i].kind == Instruction::IK_EH_CLEANUP)
				note_edge(block, instructions[i].first);
		if (instructions.empty())
		{
			if (block + 1 < block_count)
			{
				Operand fallthrough;
				fallthrough.kind = Operand::OP_LABEL;
				fallthrough.block = function.blocks[block + 1].id;
				note_edge(block, fallthrough);
			}
			continue;
		}
		const Instruction& terminal = instructions.back();
		if (terminal.kind == Instruction::IK_JUMP)
			note_edge(block, terminal.first);
		else if (terminal.kind == Instruction::IK_BRANCH)
		{
			note_edge(block, terminal.second);
			note_edge(block, terminal.third);
		}
		else if (terminal.kind == Instruction::IK_SWITCH)
		{
			note_edge(block, terminal.second);
			for (std::size_t i = 1; i < terminal.args.size(); i += 2)
				note_edge(block, terminal.args[i]);
		}
		else if (terminal.kind != Instruction::IK_RETURN &&
			terminal.kind != Instruction::IK_UNREACHABLE &&
			terminal.kind != Instruction::IK_RESUME &&
			terminal.kind != Instruction::IK_THROW && block + 1 < block_count)
		{
			Operand fallthrough;
			fallthrough.kind = Operand::OP_LABEL;
			fallthrough.block = function.blocks[block + 1].id;
			note_edge(block, fallthrough);
		}
	}

	std::vector<std::size_t> sole_cross_block_use(
		definition_blocks.size(), no_block);
	for (std::size_t i = 0; i < block_uses.size(); ++i)
	{
		const lowir_model::ValueId value = block_uses[i].first;
		const std::size_t use_block = block_uses[i].second;
		const std::size_t definition = definition_blocks[value];
		if (definition == no_block || definition == use_block) continue;
		std::size_t& target = sole_cross_block_use[value];
		if (target == no_block) target = use_block;
		else if (target != use_block) target = ambiguous;
	}
	for (std::size_t raw_value = 0;
		raw_value < definition_blocks.size(); ++raw_value)
	{
		const lowir_model::ValueId value(
			static_cast<std::uint32_t>(raw_value));
		const std::size_t definition = definition_blocks[value];
		const std::size_t target = sole_cross_block_use[value];
		if (definition == no_block || target == no_block ||
			target == ambiguous || target != definition + 1 ||
			sole_successor[definition] != target ||
			sole_predecessor[target] != definition ||
			facts.has(value, FunctionFacts::VF_PARAMETER) ||
			facts.has(value, FunctionFacts::VF_LOOP_INVARIANT)) continue;
		facts.mark(value, FunctionFacts::VF_EXACT_FORWARD_EDGE);
		if (stats) ++stats->exact_forward_edge_values;
	}
}

}
}
