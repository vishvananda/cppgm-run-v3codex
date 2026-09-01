#ifndef CPPGM_LOWERING_EXPRESSIONS_SCALAR_UNARY_H
#define CPPGM_LOWERING_EXPRESSIONS_SCALAR_UNARY_H

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"

#include <cstdint>
#include <string>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class ScalarUnaryLowering
{
protected:
	Operand LowerUnary(const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			ThrowLoweringInternal("invalid semantic unary");
		const int operation = static_cast<int>(record.operation_kind) - 1;
		if (operation == OP_AMP)
		{
			const TypeRecord& result_type = derived.program_.types.Get(
				derived.program_.types.RemoveTopCv(record.type));
			if (result_type.kind == TYPE_MEMBER_POINTER)
			{
				if (record.binding == kNoBinding ||
					record.binding >= derived.program_.bindings.size())
					ThrowLoweringInternal(
						"member pointer constant has no binding");
				const BindingRecord& member =
					derived.program_.bindings[record.binding];
				if (member.non_static_data_member)
				{
					const Operand result = derived.Temp(LowI64());
					Instruction constant(Instruction::CONST);
					constant.dest = result.id;
					constant.type = LowI64();
					constant.first = Operand(record.constant_value, LowI64());
					derived.Emit(constant);
					return result;
				}
				Operand low_word;
				if (member.virtual_function)
				{
					if (record.virtual_slot == kNoDumpEdge)
						ThrowLoweringInternal(
							"virtual member pointer has no slot fact");
					low_word = Operand(static_cast<std::int64_t>(
						static_cast<std::uint64_t>(record.virtual_slot) * 8 + 1),
						LowU64());
				}
				else
				{
					const Operand address = derived.AddressOfStorage(
						derived.LowerStorage(children[0]));
					low_word = derived.Convert(address, LowU64());
				}
				Operand encoded = derived.Convert(low_word, LowI128(), false);
				if (record.constant_value != 0)
				{
					const Operand shifted = derived.Temp(LowI128());
					Instruction shift(Instruction::BINARY);
					shift.dest = shifted.id;
					shift.op = LOW_OP_SHL;
					shift.type = LowI128();
					shift.first = Operand(record.constant_value, LowI128());
					shift.second = Operand(64, LowI128());
					derived.Emit(shift);
					const Operand combined = derived.Temp(LowI128());
					Instruction add(Instruction::BINARY);
					add.dest = combined.id;
					add.op = LOW_OP_ADD;
					add.type = LowI128();
					add.first = encoded;
					add.second = shifted;
					derived.Emit(add);
					encoded = combined;
				}
				return encoded;
			}
			return derived.AddressOfStorage(derived.LowerStorage(children[0]));
		}
		if (operation == OP_STAR)
			return derived.LoadStorage(
				derived.LowerValue(children[0], LowPtr()),
				derived.LowerExpressionType(record.type),
				derived.TypeIsVolatile(record.type));
		if (operation == OP_INC || operation == OP_DEC)
			return derived.LowerIncrement(record, children[0], false);
		if (operation == OP_LNOT)
		{
			const Operand value = derived.LowerValue(children[0]);
			const Operand result = derived.Temp(LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = result.id;
			compare.op = LOW_OP_EQ;
			compare.type = value.type;
			compare.first = value;
			compare.second = IsFloating(value.type) ?
				derived.FloatingOperand("0.0", value.type) : Operand(0, value.type);
			derived.Emit(compare);
			return result;
		}
		const LowType type = derived.LowerExpressionType(record.type);
		const Operand value = derived.LowerValue(children[0], type);
		if (operation == OP_PLUS) return value;
		const Operand result = derived.Temp(type);
		Instruction instruction(Instruction::UNARY);
		instruction.dest = result.id;
		instruction.op = operation == OP_MINUS ? LOW_OP_NEG :
			operation == OP_COMPL ? LOW_OP_BITNOT : LOW_OP_NONE;
		if (instruction.op == LOW_OP_NONE)
			ThrowLoweringSource(
				"increment/address unary lowering is outside the active checkpoint");
		instruction.type = type;
		instruction.first = value;
		derived.Emit(instruction);
		return result;
	}
};

}
}

#endif
