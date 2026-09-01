#ifndef CPPGM_LOWERING_OBJECTS_MEMBER_FUNCTION_POINTERS_H
#define CPPGM_LOWERING_OBJECTS_MEMBER_FUNCTION_POINTERS_H

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"

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
class MemberFunctionPointerLowering
{
protected:
	struct MemberPointerCallOperands
	{
		Operand object;
		Operand callee;

		MemberPointerCallOperands(const Operand& object_value,
			const Operand& callee_value)
			: object(object_value), callee(callee_value) {}
	};

	bool IsMemberPointerApplication(const DumpNode& node) const
	{
		if (node.kind != DUMP_BINARY_EXPRESSION) return false;
		return node.OperationIs(OP_DOTSTAR) || node.OperationIs(OP_ARROWSTAR);
	}

	Operand MemberFunctionPointerAdjustment(const Operand& encoded)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand shifted = derived.Temp(LowI128());
		Instruction shift(Instruction::BINARY);
		shift.dest = shifted.id;
		shift.op = LOW_OP_SHR;
		shift.type = LowI128();
		shift.first = encoded;
		shift.second = Operand(64, LowI128());
		derived.Emit(shift);
		return derived.Convert(shifted, LowI64(), false);
	}

	MemberPointerCallOperands LowerMemberPointerCall(
		std::uint32_t application_node,
		const DumpNode& application,
		const NodeChildren& children, const Operand& object_value)
	{
		if (children.size() != 2 || !IsMemberPointerApplication(application))
			ThrowLoweringInternal("invalid member function pointer application");
		Derived& derived = static_cast<Derived&>(*this);
		Operand object = object_value;
		const DumpNode& designator = derived.arena_.nodes[children[1]];
		Operand callee;
		Operand adjustment(0, LowI64());
		const TypeRecord& pointer_type = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(application.operand_type));
		const EntityId pointer_owner = pointer_type.kind == TYPE_MEMBER_POINTER ?
			derived.BaseEntityForType(static_cast<TypeId>(pointer_type.bound)) :
			kNoEntity;
		const bool owner_may_adjust = pointer_owner != kNoEntity &&
			derived.program_.entities[pointer_owner].
				has_nonzero_base_subobject_offset;
		const bool owner_may_dispatch_virtual = pointer_owner != kNoEntity &&
			derived.program_.entities[pointer_owner].polymorphic_class;
		if (designator.binding != kNoBinding &&
			designator.binding < derived.program_.bindings.size() &&
			derived.program_.bindings[designator.binding].kind == BIND_FUNCTION &&
			!derived.program_.bindings[designator.binding].virtual_function)
		{
			std::uint32_t address_node = children[1];
			if (designator.kind == DUMP_UNARY_EXPRESSION)
			{
				const NodeChildren address_children =
					derived.Children(address_node);
				if (address_children.size() != 1)
					ThrowLoweringInternal(
						"direct member pointer has no address operand");
				address_node = address_children[0];
			}
			callee = derived.AddressOfStorage(
				derived.LowerStorage(address_node));
			adjustment = Operand(designator.constant_value, LowI64());
		}
		else
		{
			const Operand encoded = derived.LowerValue(children[1], LowI128());
			if (owner_may_adjust)
				adjustment = MemberFunctionPointerAdjustment(encoded);
			const Operand low_word = derived.Convert(encoded, LowU64(), false);
			if (!owner_may_dispatch_virtual)
				callee = derived.Convert(low_word, LowPtr(), false);
			else
			{
				const Operand virtual_bit = derived.Temp(LowU64());
				Instruction mask(Instruction::BINARY);
				mask.dest = virtual_bit.id;
				mask.op = LOW_OP_AND;
				mask.type = LowU64();
				mask.first = low_word;
				mask.second = Operand(1, LowU64());
				derived.Emit(mask);
				const Operand is_virtual = derived.Temp(LowI64());
				Instruction compare(Instruction::CMP);
				compare.dest = is_virtual.id;
				compare.op = LOW_OP_NE;
				compare.type = LowU64();
				compare.first = virtual_bit;
				compare.second = Operand(0, LowU64());
				derived.Emit(compare);

				const Operand adjusted = adjustment.kind == Operand::INTEGER &&
					adjustment.integer_value == 0 ? object :
					derived.IndexAddress(LowI8(), object, adjustment, false);
				const SlotId callee_slot = derived.EnsureGeneratedSlot(
					application_node, "member_pointer_callee", LowPtr());
				const Operand callee_storage(callee_slot, LowPtr());
				const BlockId virtual_block = derived.AddBlock(
					derived.NewLabel("member_pointer_virtual"));
				const BlockId direct_block = derived.AddBlock(
					derived.NewLabel("member_pointer_direct"));
				const BlockId end_block = derived.AddBlock(
					derived.NewLabel("member_pointer_end"));
				derived.EmitBranch(is_virtual, virtual_block, direct_block);

				derived.SelectBlock(virtual_block);
				const Operand table = derived.LoadStorage(adjusted, LowPtr());
				const Operand slot_offset = derived.Temp(LowU64());
				Instruction subtract(Instruction::BINARY);
				subtract.dest = slot_offset.id;
				subtract.op = LOW_OP_SUB;
				subtract.type = LowU64();
				subtract.first = low_word;
				subtract.second = Operand(1, LowU64());
				derived.Emit(subtract);
				const Operand slot = derived.IndexAddress(
					LowI8(), table, slot_offset, false);
				Instruction virtual_store(Instruction::STORE);
				virtual_store.type = LowPtr();
				virtual_store.first = derived.LoadStorage(slot, LowPtr());
				virtual_store.second = callee_storage;
				derived.Emit(virtual_store);
				derived.EmitJump(end_block);

				derived.SelectBlock(direct_block);
				Instruction direct_store(Instruction::STORE);
				direct_store.type = LowPtr();
				direct_store.first = derived.Convert(low_word, LowPtr(), false);
				direct_store.second = callee_storage;
				derived.Emit(direct_store);
				derived.EmitJump(end_block);

				derived.SelectBlock(end_block);
				callee = derived.LoadStorage(callee_storage, LowPtr());
				object = adjusted;
				adjustment = Operand(0, LowI64());
			}
		}
		if (adjustment.kind == Operand::INTEGER &&
			adjustment.integer_value == 0)
			return MemberPointerCallOperands(object, callee);
		const Operand adjusted = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = adjusted.id;
		index.type = LowI8();
		index.first = object;
		index.second = adjustment;
		derived.Emit(index);
		return MemberPointerCallOperands(adjusted, callee);
	}
};

}
}

#endif
