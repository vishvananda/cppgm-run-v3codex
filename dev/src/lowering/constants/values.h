#ifndef CPPGM_LOWERING_CONSTANTS_VALUES_H
#define CPPGM_LOWERING_CONSTANTS_VALUES_H

#include "lowering/core/source_types.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "semantic/model/graph.h"

#include <cstdint>
#include <string>
#include <utility>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;

template <class Derived>
class ConstantLowering
{
protected:
	bool CanonicalizeAdditiveImmediates(std::uint32_t left,
		int operation, bool comparison,
		bool preserves_enum_conversion) const
	{
		if (preserves_enum_conversion || comparison ||
			(operation != OP_PLUS && operation != OP_MINUS)) return false;
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[left];
		return operation != OP_MINUS ||
			record.kind != DUMP_BINARY_EXPRESSION ||
			!record.OperationIs(OP_STAR);
	}

	bool FoldNamedLogicalConstant(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		return record.constant && (record.kind == DUMP_LITERAL ||
			record.kind == DUMP_ID_EXPRESSION ||
			record.kind == DUMP_MEMBER_EXPRESSION);
	}

	Operand LowerCanonicalCondition(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand value = derived.LowerCondition(node);
		const Operand truth = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = truth.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type.kind == LOW_PTR ? value.type : LowI64();
		compare.first = value;
		compare.second = value.type.kind == LOW_PTR ?
			Operand(0, value.type) : Operand(0, LowI64());
		derived.Emit(compare);
		return truth;
	}

	Operand FloatingOperand(const std::string& spelling, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::string normalized = NormalizeFloatingLiteral(spelling, type);
		std::uint64_t low = 0, high = 0;
		if (!DecodeFloatingLiteral(normalized, type, &low, &high))
			ThrowLoweringInternal("invalid typed floating literal");
		return Operand::Floating(derived.output_.retain_local_names ?
			derived.output_.strings.intern(normalized) :
			lowir_model::StringId(), type, low, high);
	}

	bool TryLowerConstantArrayTemplate(const DumpNode& record,
		const lowering::support::NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.lowering_namespace_object_ ||
			record.binding == kNoBinding ||
			record.binding >= derived.program_.bindings.size() ||
			children.size() != 1 ||
			!derived.program_.bindings[record.binding].constant)
			return false;
		const TypeRecord& top = derived.program_.types.Get(record.type);
		if ((top.cv & (CV_VOLATILE | CV_ATOMIC)) != 0) return false;
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(record.type));
		if (array.kind != TYPE_ARRAY || array.IsIncompleteArray()) return false;
		const TypeRecord& element_top =
			derived.program_.types.Get(array.child);
		if ((element_top.cv & (CV_VOLATILE | CV_ATOMIC)) != 0) return false;
		const TypeRecord& element = derived.program_.types.Get(
			derived.RemoveTopQualifiers(array.child));
		const bool enum_element = element.kind == TYPE_NAMED &&
			(element.entity < derived.program_.entities.size()) &&
			(derived.program_.entities[element.entity].flavor == NAMED_ENUM ||
			 derived.program_.entities[element.entity].flavor == NAMED_ENUM_CLASS);
		if (element.kind != TYPE_FUNDAMENTAL && !enum_element &&
			element.kind != TYPE_POINTER)
			return false;
		Global candidate;
		candidate.type = derived.LowerVariableStorage(record);
		if (!derived.static_initializers_.LowerConstantObject(
			record.type, children[0], &candidate))
			return false;
		const SymbolId source =
			derived.constant_templates_.Intern(std::move(candidate));
		const LowType storage = derived.LowerVariableStorage(record);
		Instruction copy(Instruction::COPY_OBJECT);
		copy.type = storage;
		copy.first = Operand(Operand::GLOBAL, source, LowPtr());
		copy.second = derived.AddressOfStorage(
			derived.StorageFor(record.binding, storage));
		derived.Emit(copy);
		if (derived.stats_) ++derived.stats_->constant_template_copies;
		return true;
	}
};

}
}

#endif
