#ifndef CPPGM_PA26_INITIALIZER_LIST_LOWERING_H
#define CPPGM_PA26_INITIALIZER_LIST_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa26_lowering_detail
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class InitializerListLowering
{
protected:
	Operand LowerInitializerListValue(std::uint32_t node,
		const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (record.kind == DUMP_INITIALIZER_LIST)
			return derived.LowerClassArgumentStaging(node, record.type);
		if (children.size() != 1)
			throw std::logic_error("initializer-list projection has no object");
		const Operand base = derived.AddressOfStorage(
			derived.LowerStorage(children[0]));
		const Operand field = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = field.id;
		index.type = LowI8();
		index.first = base;
		index.second = Operand(
			record.kind == DUMP_INITIALIZER_LIST_BEGIN ? 0 : 8, LowI64());
		index.projection = INDEX_PROJECTION_FIELD;
		derived.Emit(index);
		return derived.LoadStorage(field,
			record.kind == DUMP_INITIALIZER_LIST_BEGIN ? LowPtr() : LowI64());
	}

	void LowerInitializerListObject(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		if (record.kind != DUMP_INITIALIZER_LIST || children.size() > 1)
			throw std::logic_error("invalid initializer-list object recipe");
		const Operand backing = children.empty() ?
			Operand::NullPointer(LowPtr()) : derived.LowerStorage(children[0]);
		Instruction store_begin(Instruction::STORE);
		store_begin.type = LowPtr();
		store_begin.first = backing;
		store_begin.second = destination;
		derived.Emit(store_begin);
		const Operand size_field = derived.IndexAddress(LowI8(), destination,
			Operand(8, LowI64()), false);
		Instruction store_size(Instruction::STORE);
		store_size.type = LowI64();
		store_size.first = Operand(
			static_cast<std::int64_t>(record.array_count), LowI64());
		store_size.second = size_field;
		derived.Emit(store_size);
	}

	bool LowerInitializerListVariable(const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsClassObjectType(record.type) || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_INITIALIZER_LIST)
			return false;
		LowerInitializerListObject(children[0], derived.AddressOfStorage(
			derived.StorageFor(record.binding,
				derived.LowerStorageType(record.type))));
		return true;
	}

	bool LowerInitializerListRuntimeValue(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[node].kind != DUMP_INITIALIZER_LIST)
			return false;
		LowerInitializerListObject(node, destination);
		return true;
	}

	void LowerRuntimeZeroValue(TypeId type, const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& record = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		if (record.kind == TYPE_ARRAY || derived.IsClassObjectType(type))
			throw std::runtime_error(
				"omitted runtime aggregate element is outside the checkpoint");
		Instruction store(Instruction::STORE);
		store.type = derived.LowerExpressionType(type);
		store.first = store.type.kind == LOW_PTR ?
			Operand::NullPointer(store.type) : IsFloating(store.type) ?
			derived.FloatingOperand("0.0", store.type) : Operand(0, store.type);
		store.second = destination;
		derived.Emit(store);
	}
};

}
}

#endif
