#ifndef CPPGM_LOWERING_EXTENSIONS_RANGE_FOR_H
#define CPPGM_LOWERING_EXTENSIONS_RANGE_FOR_H

#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class RangeForLowering
{
protected:
	bool LowerScalarCallReferenceInitialization(const DumpNode& record,
		const NodeChildren& children, const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsReferenceType(record.type) || children.size() != 1)
			return false;
		const DumpNode& source = derived.arena_.nodes[children[0]];
		if (source.kind != DUMP_CALL_EXPRESSION ||
			!source.reference_call_materialization ||
			source.category != VALUE_PRVALUE ||
			derived.IsClassObjectType(source.type))
			return false;
		const LowType value_type = derived.LowerStorageType(source.type);
		const Operand temporary(derived.EnsureGeneratedSlot(
			children[0], "tmpref", value_type), value_type);
		Instruction retain(Instruction::STORE);
		retain.type = value_type;
		retain.first = derived.LowerInitializerConvertedValue(
			children[0], value_type);
		retain.second = temporary;
		derived.Emit(retain);
		Instruction bind(Instruction::STORE);
		bind.type = LowPtr();
		bind.first = derived.AddressOfStorage(temporary);
		bind.second = retained_destination.kind == Operand::NONE ?
			derived.StorageFor(record.binding, LowPtr()) : retained_destination;
		derived.Emit(bind);
		return true;
	}
};

}
}

#endif
