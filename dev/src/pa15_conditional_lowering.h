#pragma once

#include "semantic/model/graph.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa15_lowering_detail
{

template <class Derived>
class ConditionalLowering
{
protected:
	void FinishConditionalBranch(std::uint32_t node, std::uint32_t child,
		pa15_lowir_detail::BlockId end_block)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.CurrentBlock().terminated) return;
		derived.LowerBranchCleanupActions(node, child);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitJump(end_block);
	}

	pa15_lowir_detail::Operand LowerDiscardedConditional(
		std::uint32_t node,
		const pa15_lowering_support::NodeChildren& children)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		const BlockId then_block =
			derived.AddBlock(derived.NewLabel("discard_cond_then"));
		const BlockId else_block =
			derived.AddBlock(derived.NewLabel("discard_cond_else"));
		const BlockId end_block =
			derived.AddBlock(derived.NewLabel("discard_cond_end"));
		const Operand condition = derived.LowerCondition(children[0]);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitBranch(condition, then_block, else_block);
		derived.SelectBlock(then_block);
		(void)derived.LowerValue(children[1]);
		FinishConditionalBranch(node, children[1], end_block);
		derived.SelectBlock(else_block);
		(void)derived.LowerValue(children[2]);
		FinishConditionalBranch(node, children[2], end_block);
		derived.SelectBlock(end_block);
		return Operand(0, LowVoid());
	}

	void LowerConditionalAddressBranch(std::uint32_t node,
		std::uint32_t child, const pa15_lowir_detail::Operand& slot,
		pa15_lowir_detail::BlockId end_block)
	{
		using namespace semantic;
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[child].kind == DUMP_THROW_EXPRESSION)
			(void)derived.LowerValue(child);
		else
		{
			Instruction store(Instruction::STORE);
			store.type = LowPtr();
			store.first = derived.AddressOfStorage(derived.LowerStorage(child));
			store.second = slot;
			derived.Emit(store);
		}
		FinishConditionalBranch(node, child, end_block);
	}

	pa15_lowir_detail::Operand LowerConditionalAddress(
		std::uint32_t node,
		const pa15_lowering_support::NodeChildren& children)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 3)
			throw std::runtime_error("invalid semantic address conditional");
		const Operand slot(
			derived.EnsureGeneratedSlot(node, "condaddr", LowPtr()), LowPtr());
		const BlockId then_block =
			derived.AddBlock(derived.NewLabel("condaddr_then"));
		const BlockId else_block =
			derived.AddBlock(derived.NewLabel("condaddr_else"));
		const BlockId end_block =
			derived.AddBlock(derived.NewLabel("condaddr_end"));
		const Operand condition = derived.LowerCondition(children[0]);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitBranch(condition, then_block, else_block);
		derived.SelectBlock(then_block);
		LowerConditionalAddressBranch(node, children[1], slot, end_block);
		derived.SelectBlock(else_block);
		LowerConditionalAddressBranch(node, children[2], slot, end_block);
		derived.SelectBlock(end_block);
		return derived.LoadStorage(slot, LowPtr());
	}
};

}
}
