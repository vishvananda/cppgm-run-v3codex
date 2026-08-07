#ifndef CPPGM_PA16_CALL_ARGUMENT_LOWERING_H
#define CPPGM_PA16_CALL_ARGUMENT_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class CallArgumentLowering
{
protected:
	bool IsClassValueType(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord& record = derived.program_.types.Get(type);
		if (record.kind != TYPE_NAMED) return false;
		const NamedFlavor flavor =
			derived.program_.entities[record.entity].flavor;
		return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
			flavor == NAMED_UNION;
	}

	Operand LowerClassArgumentStaging(std::uint32_t node, TypeId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = derived.LowerType(target);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "argobj", type), type);
		const Operand destination = derived.AddressOfStorage(slot);
		if (derived.arena_.nodes[node].kind == DUMP_CLASS_VALUE_TRANSFER)
			derived.LowerClassValueTransfer(node, destination);
		else (void)derived.AddressOfStorage(derived.LowerStorage(node));
		return slot;
	}

	bool IsProjectedClassReference(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& argument = derived.arena_.nodes[node];
		return argument.kind == DUMP_CAST_EXPRESSION &&
			argument.base_projection_count != 0 &&
			derived.IsClassObjectType(argument.type);
	}

	Operand LowerProjectedClassPointer(std::uint32_t child,
		std::uint32_t projection_count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& source = derived.arena_.nodes[child];
		const Operand address = derived.IsClassObjectType(source.type) ?
			derived.AddressOfStorage(derived.LowerStorage(child)) :
			derived.LowerValue(child, LowPtr());
		return derived.ProjectBaseSubobjects(address, projection_count);
	}

	Operand LowerReferenceCallArgument(std::uint32_t node, TypeId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& argument = derived.arena_.nodes[node];
		if (argument.category == VALUE_LVALUE ||
			argument.category == VALUE_XVALUE)
			return derived.AddressOfStorage(derived.LowerStorage(node));
		if (IsProjectedClassReference(node))
			return derived.LowerValue(node, LowPtr());
		const LowType type = derived.LowerExpressionType(target);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "refarg", type), type);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = derived.LowerConvertedValue(node, type);
		store.second = slot;
		derived.Emit(store);
		return derived.AddressOfStorage(slot);
	}
};

}
}

#endif
