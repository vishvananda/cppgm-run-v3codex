#ifndef CPPGM_PA25_RANGE_FOR_LOWERING_H
#define CPPGM_PA25_RANGE_FOR_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

namespace cppgm
{
namespace pa25_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

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
