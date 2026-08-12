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
		index.projection = INDEX_PROJECTION_FIELD;
		derived.Emit(index);
		return projected;
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

	Operand LowerMemberPointerCallee(const DumpNode& application,
		const NodeChildren& children)
	{
		if (children.size() != 2 || !IsMemberPointerApplication(application))
			throw std::runtime_error("invalid member function pointer application");
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& designator = derived.arena_.nodes[children[1]];
		if (designator.binding != kNoBinding &&
			designator.binding < derived.program_.bindings.size() &&
			derived.program_.bindings[designator.binding].kind == BIND_FUNCTION)
			return derived.AddressOfStorage(
				derived.LowerStorage(children[1]));
		const Operand encoded = derived.LowerValue(children[1], LowI128());
		return derived.Convert(derived.Convert(encoded, LowU64(), false),
			LowPtr(), false);
	}
};

}
}

#endif
