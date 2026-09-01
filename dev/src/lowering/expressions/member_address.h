#ifndef CPPGM_LOWERING_EXPRESSIONS_MEMBER_ADDRESS_H
#define CPPGM_LOWERING_EXPRESSIONS_MEMBER_ADDRESS_H

#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

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
class MemberAddressLowering
{
protected:
	Operand MemberAddress(const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1 || record.binding == kNoBinding ||
			record.binding >= derived.program_.bindings.size())
			ThrowLoweringInternal("invalid resolved member expression");
		const BindingRecord& member = derived.program_.bindings[record.binding];
		if (!member.non_static_data_member)
		{
			if (member.kind != BIND_VARIABLE)
				ThrowLoweringInternal(
					"member expression is not a data-member lvalue");
			const Operand storage = derived.StorageFor(record.binding,
				derived.LowerStorageType(member.type), record.text);
			return derived.IsReferenceType(member.type) ?
				derived.LoadStorage(storage, LowPtr()) : storage;
		}
		Operand base, virtual_base;
		if (derived.CurrentVirtualBasePathAddressForExpression(
			children[0], member.member_owner, &virtual_base))
			base = derived.ProjectBaseSubobjectOffset(virtual_base, 0);
		else
		{
			const TypeId object_type = derived.ExpressionObjectType(
				derived.arena_.nodes[children[0]].type);
			const bool pointer_object =
				derived.program_.types.Get(object_type).kind == TYPE_POINTER;
			base = pointer_object ? derived.LowerValue(children[0], LowPtr()) :
				derived.AddressOfStorage(derived.LowerStorage(children[0]));
			if (!derived.RuntimeVirtualBaseAddressForExpression(
				children[0], base, member.member_owner, &virtual_base))
			{
				base = derived.ProjectBaseSubobjects(base,
					record.base_projection_count,
					derived.arena_.nodes[children[0]].type,
					record.base_projection_offset,
					record.has_base_projection_offset);
				const DumpNode& source = derived.arena_.nodes[children[0]];
				if (source.kind == DUMP_CALL_EXPRESSION &&
					derived.IsReferenceType(source.type) &&
					record.base_projection_count != 0)
					base = derived.ProjectBaseSubobjectOffset(base, 0);
			}
			else base = virtual_base;
		}
		const Operand result = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = result.id;
		index.type = LowI8();
		index.first = base;
		index.second = Operand(
			static_cast<std::int64_t>(
				derived.program_.BindingLayout(member).member_offset), LowI64());
		index.projection = INDEX_PROJECTION_FIELD;
		derived.Emit(index);
		return derived.IsReferenceType(member.type) ?
			derived.LoadStorage(result, LowPtr()) : result;
	}
};

}
}

#endif
