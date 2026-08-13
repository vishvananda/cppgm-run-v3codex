#pragma once

#include "pa12_semantic_model.h"
#include "pa15_lowir_model.h"
#include "pa15_lowering_support.h"

#include <stdexcept>

namespace cppgm
{
namespace pa34_lowering_detail
{

template <class Derived>
class GnuAsmLowering
{
protected:
	bool TryLowerGnuAsmStatement(const pa12_semantic_detail::DumpNode& record,
		const pa15_lowering_support::NodeChildren& children)
	{
		using namespace pa12_semantic_detail;
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (record.kind != DUMP_GNU_ASM_STATEMENT) return false;
		if (record.array_count != children.size() || record.storage_size != 0)
			throw std::logic_error("invalid typed GNU asm operands");
		if (record.gnu_asm_operation == GNU_ASM_NOP ||
			record.gnu_asm_operation == GNU_ASM_PAUSE) return true;
		if (record.gnu_asm_operation == GNU_ASM_COMPILER_FENCE)
		{
			Instruction fence(Instruction::ATOMIC_SIGNAL_FENCE);
			fence.atomic_order = 5;
			derived.Emit(fence);
			return true;
		}
		if (children.size() != 1)
			throw std::logic_error("GNU asm operation has invalid arity");
		const std::uint32_t operand = children[0];
		const LowType type = derived.LowerExpressionType(
			derived.arena_.nodes[operand].type);
		const Operand storage = derived.LowerStorage(operand);
		if (!IsInteger(type) || type.width > 64)
			throw std::logic_error("invalid lowered GNU asm operand type");
		if (record.gnu_asm_operation == GNU_ASM_LOCK_INCREMENT)
		{
			const Operand result = derived.Temp(type);
			Instruction update(Instruction::ATOMIC_ADD_FETCH);
			update.dest = result.id;
			update.type = type;
			update.first = derived.AddressOfStorage(storage);
			update.second = Operand(1, type);
			update.atomic_order = 5;
			derived.Emit(update);
			return true;
		}
		const Operand value = derived.LoadStorage(storage, type);
		const Operand result = derived.Temp(type);
		Instruction unary(Instruction::UNARY);
		unary.dest = result.id;
		unary.op = record.gnu_asm_operation == GNU_ASM_BSWAP ?
			LOW_OP_BSWAP : LOW_OP_BITNOT;
		unary.type = type;
		unary.first = value;
		derived.Emit(unary);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = result;
		store.second = storage;
		derived.Emit(store);
		if (record.gnu_asm_operation == GNU_ASM_LOCK_NOT)
		{
			Instruction fence(Instruction::ATOMIC_THREAD_FENCE);
			fence.atomic_order = 5;
			derived.Emit(fence);
		}
		return true;
	}
};

}
}
