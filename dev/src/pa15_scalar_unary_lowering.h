#ifndef CPPGM_PA15_SCALAR_UNARY_LOWERING_H
#define CPPGM_PA15_SCALAR_UNARY_LOWERING_H

#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa15_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class ScalarUnaryLowering
{
protected:
	Operand LowerUnary(const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			throw std::runtime_error("invalid semantic unary");
		const std::string operation = StripOperationPrefix(
			derived.program_.names.Get(record.text));
		if (operation == "&")
		{
			const TypeRecord& result_type = derived.program_.types.Get(
				derived.program_.types.RemoveTopCv(record.type));
			if (result_type.kind == TYPE_MEMBER_POINTER)
			{
				if (record.binding == kNoBinding ||
					record.binding >= derived.program_.bindings.size())
					throw std::runtime_error(
						"member pointer constant has no binding");
				const BindingRecord& member =
					derived.program_.bindings[record.binding];
				if (member.non_static_data_member)
				{
					const Operand result = derived.Temp(LowI64());
					Instruction constant(Instruction::CONST);
					constant.dest = result.id;
					constant.type = LowI64();
					constant.first = Operand(static_cast<std::int64_t>(
						member.member_offset + 1), LowI64());
					derived.Emit(constant);
					return result;
				}
				const Operand address = derived.AddressOfStorage(
					derived.LowerStorage(children[0]));
				return derived.Convert(derived.Convert(address, LowU64()),
					LowI128(), false);
			}
			return derived.AddressOfStorage(derived.LowerStorage(children[0]));
		}
		if (operation == "*")
			return derived.LoadStorage(
				derived.LowerValue(children[0], LowPtr()),
				derived.LowerExpressionType(record.type));
		if (operation == "++" || operation == "--")
			return derived.LowerIncrement(record, children[0], false);
		if (operation == "!")
		{
			const Operand value = derived.LowerValue(children[0]);
			const Operand result = derived.Temp(LowU8());
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
		if (operation == "+") return value;
		const Operand result = derived.Temp(type);
		Instruction instruction(Instruction::UNARY);
		instruction.dest = result.id;
		instruction.op = operation == "-" ? LOW_OP_NEG :
			operation == "~" ? LOW_OP_BITNOT : LOW_OP_NONE;
		if (instruction.op == LOW_OP_NONE)
			throw std::runtime_error(
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
