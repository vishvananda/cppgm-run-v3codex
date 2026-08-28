#ifndef CPPGM_PA21_CONSTANT_LOWERING_H
#define CPPGM_PA21_CONSTANT_LOWERING_H

#include "lowering/support/utilities.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

#include <cstdint>
#include <string>

namespace cppgm
{
namespace pa21_lowering_detail
{

using namespace semantic;
using namespace lowering::ir;
using namespace pa15_lowering_support;

template <class Derived>
class ConstantLowering
{
protected:
	bool CanonicalizeAdditiveImmediates(std::uint32_t left,
		int operation, bool comparison,
		bool preserves_enum_conversion) const
	{
		if (preserves_enum_conversion || comparison ||
			(operation != OP_PLUS && operation != OP_MINUS)) return false;
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[left];
		return operation != OP_MINUS ||
			record.kind != DUMP_BINARY_EXPRESSION ||
			!record.OperationIs(OP_STAR);
	}

	bool FoldNamedLogicalConstant(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		return record.constant && (record.kind == DUMP_LITERAL ||
			record.kind == DUMP_ID_EXPRESSION ||
			record.kind == DUMP_MEMBER_EXPRESSION);
	}

	Operand LowerCanonicalCondition(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand value = derived.LowerCondition(node);
		const Operand truth = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = truth.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type.kind == LOW_PTR ? value.type : LowI64();
		compare.first = value;
		compare.second = value.type.kind == LOW_PTR ?
			Operand(0, value.type) : Operand(0, LowI64());
		derived.Emit(compare);
		return truth;
	}
};

}
}

#endif
