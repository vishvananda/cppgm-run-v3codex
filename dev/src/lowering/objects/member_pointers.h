#ifndef CPPGM_LOWERING_OBJECTS_MEMBER_POINTERS_H
#define CPPGM_LOWERING_OBJECTS_MEMBER_POINTERS_H

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"
#include "lowering/objects/member_function_pointers.h"

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
class MemberPointerLowering :
	public MemberFunctionPointerLowering<Derived>
{
protected:
	Operand MemberPointerObject(const DumpNode& application,
		const NodeChildren& children)
	{
		if (children.size() != 2 ||
			!this->IsMemberPointerApplication(application))
			ThrowLoweringInternal("invalid member pointer application");
		Derived& derived = static_cast<Derived&>(*this);
		Operand object = application.OperationIs(OP_ARROWSTAR) ?
			derived.LowerValue(children[0], LowPtr()) :
			derived.AddressOfStorage(derived.LowerStorage(children[0]));
		if (!application.has_base_projection_offset ||
			application.base_projection_offset == 0)
			return object;
		const Operand projected = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = projected.id;
		index.type = LowI8();
		index.first = object;
		index.second = Operand(static_cast<std::int64_t>(
			application.base_projection_offset), LowI64());
		derived.Emit(index);
		return projected;
	}

	Operand LowerMemberPointerConversion(const DumpNode& conversion,
		const NodeChildren& children)
	{
		if (children.size() != 1 || !conversion.member_pointer_conversion ||
			!conversion.has_base_projection_offset)
			ThrowLoweringInternal("invalid member pointer conversion");
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& target = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(conversion.type));
		if (target.kind != TYPE_MEMBER_POINTER)
			ThrowLoweringInternal("member pointer conversion has no target");
		const bool function_member =
			derived.program_.types.IsFunction(target.child);
		const LowType value_type = function_member ? LowI128() : LowI64();
		const Operand value = derived.LowerValue(children[0], value_type);
		const Operand low_word = function_member ?
			derived.Convert(value, LowU64(), false) : value;
		const Operand nonnull = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = nonnull.id;
		compare.op = LOW_OP_NE;
		compare.type = low_word.type;
		compare.first = low_word;
		compare.second = Operand(0, low_word.type);
		derived.Emit(compare);
		const Operand widened = derived.Convert(nonnull, LowI64(), false);
		const Operand adjustment = derived.Temp(LowI64());
		Instruction multiply(Instruction::BINARY);
		multiply.dest = adjustment.id;
		multiply.op = LOW_OP_MUL;
		multiply.type = LowI64();
		multiply.first = widened;
		multiply.second = Operand(static_cast<std::int64_t>(
			conversion.base_projection_offset), LowI64());
		derived.Emit(multiply);
		Operand added = adjustment;
		if (function_member)
		{
			const Operand adjustment128 = derived.Convert(
				adjustment, LowI128(), false);
			const Operand shifted = derived.Temp(LowI128());
			Instruction shift(Instruction::BINARY);
			shift.dest = shifted.id;
			shift.op = LOW_OP_SHL;
			shift.type = LowI128();
			shift.first = adjustment128;
			shift.second = Operand(64, LowI128());
			derived.Emit(shift);
			added = shifted;
		}
		const Operand result = derived.Temp(value_type);
		Instruction add(Instruction::BINARY);
		add.dest = result.id;
		add.op = LOW_OP_ADD;
		add.type = value_type;
		add.first = value;
		add.second = added;
		derived.Emit(add);
		return result;
	}

	Operand LowerMemberPointerStorage(const DumpNode& application,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand object = MemberPointerObject(application, children);
		const Operand encoded = derived.LowerValue(children[1], LowI64());
		const Operand displacement = derived.Temp(LowI64());
		Instruction subtract(Instruction::BINARY);
		subtract.dest = displacement.id;
		subtract.op = LOW_OP_SUB;
		subtract.type = LowI64();
		subtract.first = encoded;
		subtract.second = Operand(1, LowI64());
		derived.Emit(subtract);
		const Operand result = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = result.id;
		index.type = LowI8();
		index.first = object;
		index.second = displacement;
		index.projection = INDEX_PROJECTION_FIELD;
		derived.Emit(index);
		return result;
	}

};

}
}

#endif
