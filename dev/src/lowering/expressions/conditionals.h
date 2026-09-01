#pragma once

#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"

#include <cstdint>

namespace cppgm
{
namespace lowering
{

template <class Derived>
class ConditionalLowering
{
protected:
	void FinishConditionalBranch(std::uint32_t node, std::uint32_t child,
		lowering::ir::BlockId end_block)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.CurrentBlock().terminated) return;
		derived.LowerBranchCleanupActions(node, child);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitJump(end_block);
	}

	lowering::ir::Operand LowerDiscardedConditional(
		std::uint32_t node,
		const lowering::support::NodeChildren& children)
	{
		using namespace lowering::ir;
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
		std::uint32_t child, const lowering::ir::Operand& slot,
		lowering::ir::BlockId end_block)
	{
		using namespace semantic;
		using namespace lowering::ir;
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

	lowering::ir::Operand LowerConditionalAddress(
		std::uint32_t node,
		const lowering::support::NodeChildren& children)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 3)
			ThrowLoweringInternal("invalid semantic address conditional");
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

	lowering::ir::Operand LowerConditional(std::uint32_t node,
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children)
	{
		using namespace semantic;
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 3)
			ThrowLoweringInternal("invalid semantic conditional");
		const LowType type = derived.LowerExpressionType(record.type);
		if (type.kind == LOW_VOID)
			return LowerDiscardedConditional(node, children);
		const Operand slot(
			derived.EnsureGeneratedSlot(node, "cond", type), type);
		const BlockId then_block =
			derived.AddBlock(derived.NewLabel("cond_then"));
		const BlockId else_block =
			derived.AddBlock(derived.NewLabel("cond_else"));
		const BlockId end_block =
			derived.AddBlock(derived.NewLabel("cond_end"));
		const Operand condition = derived.LowerCondition(children[0]);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitBranch(condition, then_block, else_block);
		derived.SelectBlock(then_block);
		if (derived.arena_.nodes[children[1]].kind == DUMP_THROW_EXPRESSION)
			(void)derived.LowerValue(children[1]);
		else
		{
			Instruction yes_store(Instruction::STORE);
			yes_store.type = type;
			yes_store.first =
				derived.LowerConvertedValue(children[1], type, false);
			yes_store.second = slot;
			derived.Emit(yes_store);
		}
		if (!derived.CurrentBlock().terminated)
		{
			derived.LowerBranchCleanupActions(node, children[1]);
			if (derived.full_expression_cleanup_active_)
				derived.PauseFullExpressionCleanupSegment();
			derived.EmitJump(end_block);
		}
		derived.SelectBlock(else_block);
		if (derived.arena_.nodes[children[2]].kind == DUMP_THROW_EXPRESSION)
			(void)derived.LowerValue(children[2]);
		else
		{
			Instruction no_store(Instruction::STORE);
			no_store.type = type;
			no_store.first =
				derived.LowerConvertedValue(children[2], type, false);
			no_store.second = slot;
			derived.Emit(no_store);
		}
		if (!derived.CurrentBlock().terminated)
		{
			derived.LowerBranchCleanupActions(node, children[2]);
			if (derived.full_expression_cleanup_active_)
				derived.PauseFullExpressionCleanupSegment();
			derived.EmitJump(end_block);
		}
		derived.SelectBlock(end_block);
		const Operand result = derived.Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = slot;
		derived.Emit(load);
		return result;
	}
};

}
}
