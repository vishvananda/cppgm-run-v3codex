#pragma once

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

template <typename Derived>
class ComplexLowering
{
protected:
	lowering::ir::Operand LowerComplexConstruction(
		std::uint32_t node,
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children)
	{
		using namespace semantic;
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 2)
			ThrowLoweringInternal("invalid complex construction graph");
		const TypeId type = derived.ExpressionObjectType(record.type);
		const TypeRecord& complex = derived.program_.types.Get(type);
		if (complex.kind != TYPE_COMPLEX)
			ThrowLoweringInternal("complex construction has non-complex type");
		const LowType object_type = derived.LowerStorageType(type);
		const LowType element_type = derived.LowerType(complex.child);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "complex", object_type), object_type);
		const Operand base = derived.AddressOfStorage(slot);
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			Instruction store(Instruction::STORE);
			store.type = element_type;
			store.first = derived.LowerValue(children[i], element_type);
			store.second = i == 0 ? base : derived.IndexAddress(
				LowI8(), base, Operand(static_cast<std::int64_t>(
					derived.program_.SizeOf(complex.child)), LowI64()), false);
			derived.Emit(store);
		}
		return slot;
	}

	lowering::ir::Operand LowerComplexComponentStorage(
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			ThrowLoweringInternal("invalid complex component graph");
		const semantic::DumpNode& source =
			derived.arena_.nodes[children[0]];
		const semantic::TypeId type = derived.ExpressionObjectType(source.type);
		const semantic::TypeRecord& complex = derived.program_.types.Get(type);
		if (complex.kind != semantic::TYPE_COMPLEX)
			ThrowLoweringInternal("complex component source is not complex");
		const Operand storage = derived.LowerStorage(children[0]);
		const Operand base = derived.AddressOfStorage(storage);
		if (derived.program_.names.Get(record.text) == "__real__") return base;
		return derived.IndexAddress(LowI8(), base,
			Operand(static_cast<std::int64_t>(
				derived.program_.SizeOf(complex.child)), LowI64()), false);
	}

	bool TryLowerComplexStorage(std::uint32_t node,
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children,
		lowering::ir::Operand* result)
	{
		if (record.kind == semantic::DUMP_COMPLEX_CONSTRUCTION)
			*result = LowerComplexConstruction(node, record, children);
		else if (record.kind == semantic::DUMP_COMPLEX_COMPONENT)
			*result = LowerComplexComponentStorage(record, children);
		else return false;
		return true;
	}

	bool TryLowerComplexValue(std::uint32_t node,
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children,
		lowering::ir::Operand* result)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (record.kind == semantic::DUMP_COMPLEX_CONSTRUCTION)
			*result = LowerComplexConstruction(node, record, children);
		else if (record.kind == semantic::DUMP_COMPLEX_COMPONENT)
			*result = derived.LoadStorage(
				LowerComplexComponentStorage(record, children),
				derived.LowerExpressionType(record.type));
		else return false;
		return true;
	}

	bool LowerIndirectComplexResult(std::uint32_t node,
		const lowering::ir::Operand& destination)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		const semantic::DumpNode& record = derived.arena_.nodes[node];
		if (!derived.IsComplexObjectType(record.type)) return false;
		const lowering::support::NodeChildren children = derived.Children(node);
		Operand source;
		if (!TryLowerComplexStorage(node, record, children, &source))
			source = derived.LowerStorage(node);
		Instruction copy(Instruction::COPY_OBJECT);
		copy.type = derived.LowerStorageType(record.type);
		copy.first = derived.AddressOfStorage(source);
		copy.second = destination;
		derived.Emit(copy);
		return true;
	}

	bool TryLowerComplexVariableInitialization(
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children,
		const lowering::ir::Operand& retained_destination)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsComplexObjectType(record.type)) return false;
		if (children.empty()) return true;
		if (children.size() != 1)
			ThrowLoweringInternal("invalid complex variable initializer");
		const LowType type = derived.LowerStorageType(record.type);
		const Operand destination = retained_destination.kind == Operand::NONE ?
			derived.AddressOfStorage(derived.StorageFor(record.binding, type)) :
			retained_destination;
		(void)LowerIndirectComplexResult(children[0], destination);
		return true;
	}
};

}
}
