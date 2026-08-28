#ifndef CPPGM_LOWERING_CONTROL_EXPRESSIONS_H
#define CPPGM_LOWERING_CONTROL_EXPRESSIONS_H

#include "semantic/model/graph.h"
#include "lowering/ir/model.h"
#include "lowering/support/sequences.h"

#include <cstdint>
#include <string>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class ControlExpressionLowering
{
protected:
	Operand LowerLogical(std::uint32_t node, const NodeChildren& children,
		bool conjunction)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& left_record = derived.arena_.nodes[children[0]];
		if (derived.FoldNamedLogicalConstant(children[0]))
		{
			const bool left_truth = left_record.constant_value != 0;
			if ((conjunction && !left_truth) || (!conjunction && left_truth))
				return Operand(left_truth ? 1 : 0, LowU8());
			const Operand result = derived.LowerCanonicalCondition(children[1]);
			derived.LowerBranchCleanupActions(node, children[1]);
			return result;
		}
		const Operand slot(derived.EnsureGeneratedSlot(node,
			conjunction ? "land" : "lor", LowI64()), LowI64());
		const char* prefix = conjunction ? "land" : "lor";
		const BlockId rhs_block = derived.AddBlock(
			derived.NewLabel(std::string(prefix) + "_rhs"));
		const BlockId short_block = derived.AddBlock(
			derived.NewLabel(std::string(prefix) + "_short"));
		const BlockId end_block = derived.AddBlock(
			derived.NewLabel(std::string(prefix) + "_end"));
		const Operand left = derived.LowerCondition(children[0]);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitBranch(left, conjunction ? rhs_block : short_block,
			conjunction ? short_block : rhs_block);
		derived.SelectBlock(rhs_block);
		const Operand right = derived.LowerCondition(children[1]);
		const Operand truth = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = truth.id;
		compare.op = LOW_OP_NE;
		compare.type = right.type.kind == LOW_PTR ? right.type : LowI64();
		compare.first = right;
		compare.second = right.type.kind == LOW_PTR ?
			Operand(0, right.type) : Operand(0, LowI64());
		derived.Emit(compare);
		Instruction rhs_store(Instruction::STORE);
		rhs_store.type = LowI64();
		rhs_store.first = truth;
		rhs_store.second = slot;
		derived.Emit(rhs_store);
		derived.LowerBranchCleanupActions(node, children[1]);
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		derived.EmitJump(end_block);
		derived.SelectBlock(short_block);
		Instruction short_store(Instruction::STORE);
		short_store.type = LowI64();
		short_store.first = Operand(conjunction ? 0 : 1, LowI64());
		short_store.second = slot;
		derived.Emit(short_store);
		derived.EmitJump(end_block);
		derived.SelectBlock(end_block);
		const Operand result = derived.Temp(LowI64());
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = LowI64();
		load.first = slot;
		derived.Emit(load);
		return result;
	}
};

}
}

#endif
