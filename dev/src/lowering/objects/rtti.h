#ifndef CPPGM_LOWERING_OBJECTS_RTTI_H
#define CPPGM_LOWERING_OBJECTS_RTTI_H

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"

#include <cstdint>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class RttiLowering
{
protected:
	TypeId CanonicalRttiType(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const TypeRecord* record = &derived.program_.types.Get(type);
		if (record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE)
		{
			type = record->child;
			record = &derived.program_.types.Get(type);
		}
		while (record->kind == TYPE_QUALIFIED)
		{
			type = record->child;
			record = &derived.program_.types.Get(type);
		}
		return type;
	}

	Operand RttiAddress(TypeId requested)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->rtti_symbol_lookups;
		const TypeId type = CanonicalRttiType(requested);
		if (type >= derived.polymorphism_.type_rtti_symbols.size() ||
			derived.polymorphism_.type_rtti_symbols[type] == kNoLowId)
			ThrowLoweringInternal("semantic RTTI fact has no emitted symbol");
		const SymbolId symbol = derived.polymorphism_.type_rtti_symbols[type];
		derived.output_.symbols[symbol].referenced = true;
		const Operand result = derived.Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = result.id;
		address.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		derived.Emit(address);
		return result;
	}

	Operand EmitRttiRuntimeCall(SymbolId symbol, const LowType& result_type,
		const CallArguments& arguments)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (symbol == kNoLowId)
			ThrowLoweringInternal("RTTI runtime call has no symbol");
		Instruction call = derived.DirectCallInstruction(symbol, result_type);
		CallArgumentFlags references;
		for (std::size_t i = 0; i < arguments.size(); ++i)
			references.Push(Instruction::CALL_PASS_VALUE);
		derived.AttachCallArguments(&call, arguments, references);
		if (result_type.kind == LOW_VOID)
		{
			derived.Emit(call);
			return Operand(0, LowVoid());
		}
		const Operand result = derived.Temp(result_type);
		call.dest = result.id;
		derived.Emit(call);
		return result;
	}

	Operand PointerIsNull(const Operand& pointer)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = result.id;
		compare.op = LOW_OP_EQ;
		compare.type = LowPtr();
		compare.first = pointer;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		return result;
	}

	void EmitNoreturnFallback()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.current_result_.kind == LOW_VOID)
		{
			derived.Emit(Instruction(Instruction::RETURN_VOID));
			return;
		}
		Instruction instruction(Instruction::RETURN_VALUE);
		instruction.type = derived.current_result_;
		if (derived.current_result_.kind == LOW_OBJECT)
		{
			const Operand slot(derived.CreateGeneratedSlot(
				"retobj", derived.current_result_), derived.current_result_);
			Instruction zero(Instruction::ZERO_OBJECT);
			zero.type = derived.current_result_;
			zero.first = slot;
			derived.Emit(zero);
			instruction.first = slot;
		}
		else instruction.first = IsFloating(derived.current_result_) ?
			derived.FloatingOperand("0.0", derived.current_result_) :
			Operand(0, derived.current_result_);
		derived.Emit(instruction);
	}

	Operand LowerTypeid(const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!record.dynamic_type_query)
			return RttiAddress(record.operand_type);
		if (children.size() != 1)
			ThrowLoweringInternal("dynamic typeid has no object expression");
		const Operand object =
			derived.AddressOfStorage(derived.LowerStorage(children[0]));
		const BlockId fail = derived.AddBlock(derived.NewLabel("typeid_fail"));
		const BlockId scan = derived.AddBlock(derived.NewLabel("typeid_scan"));
		derived.EmitBranch(PointerIsNull(object), fail, scan);
		derived.SelectBlock(fail);
		CallArguments no_arguments;
		(void)EmitRttiRuntimeCall(derived.polymorphism_.bad_typeid_symbol,
			LowVoid(), no_arguments);
		EmitNoreturnFallback();
		derived.SelectBlock(scan);
		const Operand vtable = derived.LoadStorage(object, LowPtr());
		const Operand entry = derived.IndexAddress(
			LowI8(), vtable, Operand(-8, LowI64()), false);
		return derived.LoadStorage(entry, LowPtr());
	}

	TypeId DynamicCastTargetType(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		type = CanonicalRttiType(type);
		const TypeRecord& record = derived.program_.types.Get(type);
		return CanonicalRttiType(
			record.kind == TYPE_POINTER ? record.child : type);
	}

	bool DynamicCastTargetsVoid(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const TypeId target = DynamicCastTargetType(type);
		const TypeRecord& record = derived.program_.types.Get(target);
		return record.kind == TYPE_FUNDAMENTAL &&
			record.fundamental == FUND_VOID;
	}

	Operand LowerDynamicCast(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			ThrowLoweringInternal("dynamic_cast has no source expression");
		const Operand source = record.dynamic_cast_reference ?
			derived.AddressOfStorage(derived.LowerStorage(children[0])) :
			derived.LowerValue(children[0], LowPtr());
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "dyn_cast", LowPtr()), LowPtr());
		Instruction initialize(Instruction::STORE);
		initialize.type = LowPtr();
		initialize.first = Operand(0, LowPtr());
		initialize.second = slot;
		derived.Emit(initialize);

		const BlockId scan = derived.AddBlock(
			derived.NewLabel("dyn_cast_scan"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("dyn_cast_end"));
		derived.EmitBranch(PointerIsNull(source), end, scan);
		derived.SelectBlock(scan);
		const bool target_void = DynamicCastTargetsVoid(record.type);
		if (target_void)
		{
			const Operand vtable = derived.LoadStorage(source, LowPtr());
			const Operand offset_entry = derived.IndexAddress(
				LowI8(), vtable, Operand(-16, LowI64()), false);
			const Operand offset = derived.LoadStorage(offset_entry, LowI64());
			const Operand complete = derived.IndexAddress(
				LowI8(), source, offset, false);
			Instruction save_complete(Instruction::STORE);
			save_complete.type = LowPtr();
			save_complete.first = complete;
			save_complete.second = slot;
			derived.Emit(save_complete);
			derived.EmitJump(end);
			const BlockId fallback = derived.AddBlock(
				derived.NewLabel("block"));
			derived.SelectBlock(fallback);
		}
		CallArguments arguments;
		arguments.Push(source);
		arguments.Push(RttiAddress(record.operand_type));
		arguments.Push(RttiAddress(DynamicCastTargetType(record.type)));
		arguments.Push(Operand(record.dynamic_cast_hint, LowI64()));
		const Operand casted = EmitRttiRuntimeCall(
			derived.polymorphism_.dynamic_cast_symbol, LowPtr(), arguments);
		Instruction save(Instruction::STORE);
		save.type = LowPtr();
		save.first = casted;
		save.second = slot;
		derived.Emit(save);
		if (record.dynamic_cast_reference)
		{
			const BlockId fail = derived.AddBlock(
				derived.NewLabel("dyn_cast_fail"));
			const BlockId found = derived.AddBlock(
				derived.NewLabel("dyn_cast_found"));
			derived.EmitBranch(PointerIsNull(casted), fail, found);
			derived.SelectBlock(fail);
			CallArguments no_arguments;
			(void)EmitRttiRuntimeCall(derived.polymorphism_.bad_cast_symbol,
				LowVoid(), no_arguments);
			EmitNoreturnFallback();
			derived.SelectBlock(found);
			derived.EmitJump(end);
			const BlockId continuation = derived.AddBlock(
				derived.NewLabel("block"));
			derived.SelectBlock(continuation);
			derived.EmitJump(end);
		}
		else derived.EmitJump(end);
		derived.SelectBlock(end);
		return derived.LoadStorage(slot, LowPtr());
	}
};

}
}

#endif
