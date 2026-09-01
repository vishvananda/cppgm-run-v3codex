#pragma once

#include "lowering/presentation/local_names.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"

#include <limits>
#include <string>

namespace cppgm
{
namespace lowering
{

using namespace lowering::ir;

template <class Derived>
class ControlFlowLowering
{
protected:
	void ResetControlFlowReachability()
	{
		static_cast<Derived&>(*this).block_incoming_.clear();
	}

	BlockId AddBlock(const std::string& label)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return AddBlock(lowering::presentation::ExactBlockPresentation(
			derived.output_, label));
	}

	BlockId AddBlock(const BlockPresentationName& presentation)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.function_->blocks.size() >= kNoLowId)
			ThrowLoweringResourceLimit("too many PA15 LowIR blocks");
		const BlockId block = static_cast<BlockId>(
			derived.function_->blocks.size());
		derived.function_->blocks.push_back(
			lowering::presentation::MakePresentedBlock(
				derived.output_, derived.function_, presentation));
		derived.block_incoming_.push_back(0);
		return block;
	}

	void SelectBlock(BlockId block)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.current_block_ = block;
		if (!derived.function_->blocks[block].selected)
		{
			derived.function_->blocks[block].selected = true;
			derived.function_->block_order.push_back(block);
		}
	}

	void RecordBlockIncoming(BlockId block)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (block >= derived.block_incoming_.size())
			ThrowLoweringInternal("CFG edge has no target block");
		if (derived.block_incoming_[block] ==
			std::numeric_limits<std::uint32_t>::max())
			ThrowLoweringResourceLimit("too many incoming CFG edges");
		++derived.block_incoming_[block];
	}

	bool HasBlockIncoming(BlockId block) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return block < derived.block_incoming_.size() &&
			derived.block_incoming_[block] != 0;
	}

	void EmitJump(BlockId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction jump(Instruction::JUMP);
		jump.target = target;
		derived.Emit(jump);
		RecordBlockIncoming(target);
	}

	void EmitContinuationJump(BlockId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const bool reachable = HasBlockIncoming(derived.current_block_);
		Instruction jump(Instruction::JUMP);
		jump.target = target;
		derived.Emit(jump);
		if (reachable) RecordBlockIncoming(target);
	}

	void EmitBranch(const Operand& condition, BlockId yes, BlockId no)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction branch(Instruction::BRANCH);
		branch.first = condition;
		branch.target = yes;
		branch.alternate = no;
		derived.Emit(branch);
		if (condition.kind != Operand::INTEGER || condition.integer_value != 0)
			RecordBlockIncoming(yes);
		if (condition.kind != Operand::INTEGER || condition.integer_value == 0)
			RecordBlockIncoming(no);
	}
};

}
}
