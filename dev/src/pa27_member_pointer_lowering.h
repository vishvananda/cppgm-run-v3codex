#ifndef CPPGM_PA27_MEMBER_POINTER_LOWERING_H
#define CPPGM_PA27_MEMBER_POINTER_LOWERING_H

#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa27_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class MemberPointerLowering
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
		const Derived& derived = static_cast<const Derived&>(*this);
		const std::string operation = StripOperationPrefix(
			derived.program_.names.Get(node.text));
		return operation == ".*" || operation == "->*";
	}

	Operand MemberPointerObject(const DumpNode& application,
		const NodeChildren& children)
	{
		if (children.size() != 2 || !IsMemberPointerApplication(application))
			throw std::runtime_error("invalid member pointer application");
		Derived& derived = static_cast<Derived&>(*this);
		const std::string operation = StripOperationPrefix(
			derived.program_.names.Get(application.text));
		Operand object = operation == "->*" ?
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
		index.projection = INDEX_PROJECTION_BASE_SUBOBJECT;
		derived.Emit(index);
		return projected;
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

	Operand LowerMemberPointerConversion(const DumpNode& conversion,
		const NodeChildren& children)
	{
		if (children.size() != 1 || !conversion.member_pointer_conversion ||
			!conversion.has_base_projection_offset)
			throw std::runtime_error("invalid member pointer conversion");
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& target = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(conversion.type));
		if (target.kind != TYPE_MEMBER_POINTER)
			throw std::runtime_error("member pointer conversion has no target");
		const bool function_member =
			derived.program_.types.IsFunction(target.child);
		const LowType value_type = function_member ? LowI128() : LowI64();
		const Operand value = derived.LowerValue(children[0], value_type);
		const Operand low_word = function_member ?
			derived.Convert(value, LowU64(), false) : value;
		const Operand nonnull = derived.Temp(LowU8());
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

	MemberPointerCallOperands LowerMemberPointerCall(
		std::uint32_t application_node,
		const DumpNode& application,
		const NodeChildren& children, const Operand& object_value)
	{
		if (children.size() != 2 || !IsMemberPointerApplication(application))
			throw std::runtime_error("invalid member function pointer application");
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
			callee = derived.AddressOfStorage(
				derived.LowerStorage(children[1]));
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
				const Operand is_virtual = derived.Temp(LowU8());
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
		index.projection = INDEX_PROJECTION_BASE_SUBOBJECT;
		derived.Emit(index);
		return MemberPointerCallOperands(adjusted, callee);
	}
};

}
}

#endif
