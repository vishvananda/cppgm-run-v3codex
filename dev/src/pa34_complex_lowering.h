#pragma once

#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa34_lowering_detail
{

template <typename Derived>
class ComplexLowering
{
protected:
	pa15_lowir_detail::Operand LowerComplexConstruction(
		std::uint32_t node,
		const pa12_semantic_detail::DumpNode& record,
		const pa15_lowering_support::NodeChildren& children)
	{
		using namespace pa11;
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 2)
			throw std::logic_error("invalid complex construction graph");
		const TypeId type = derived.ExpressionObjectType(record.type);
		const TypeRecord& complex = derived.program_.types.Get(type);
		if (complex.kind != TYPE_COMPLEX)
			throw std::logic_error("complex construction has non-complex type");
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

	pa15_lowir_detail::Operand LowerComplexComponentStorage(
		const pa12_semantic_detail::DumpNode& record,
		const pa15_lowering_support::NodeChildren& children)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			throw std::logic_error("invalid complex component graph");
		const pa12_semantic_detail::DumpNode& source =
			derived.arena_.nodes[children[0]];
		const pa11::TypeId type = derived.ExpressionObjectType(source.type);
		const pa11::TypeRecord& complex = derived.program_.types.Get(type);
		if (complex.kind != pa11::TYPE_COMPLEX)
			throw std::logic_error("complex component source is not complex");
		const Operand storage = derived.LowerStorage(children[0]);
		const Operand base = derived.AddressOfStorage(storage);
		if (derived.program_.names.Get(record.text) == "__real__") return base;
		return derived.IndexAddress(LowI8(), base,
			Operand(static_cast<std::int64_t>(
				derived.program_.SizeOf(complex.child)), LowI64()), false);
	}

	bool TryLowerComplexStorage(std::uint32_t node,
		const pa12_semantic_detail::DumpNode& record,
		const pa15_lowering_support::NodeChildren& children,
		pa15_lowir_detail::Operand* result)
	{
		if (record.kind == pa12_semantic_detail::DUMP_COMPLEX_CONSTRUCTION)
			*result = LowerComplexConstruction(node, record, children);
		else if (record.kind == pa12_semantic_detail::DUMP_COMPLEX_COMPONENT)
			*result = LowerComplexComponentStorage(record, children);
		else return false;
		return true;
	}

	bool TryLowerComplexValue(std::uint32_t node,
		const pa12_semantic_detail::DumpNode& record,
		const pa15_lowering_support::NodeChildren& children,
		pa15_lowir_detail::Operand* result)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (record.kind == pa12_semantic_detail::DUMP_COMPLEX_CONSTRUCTION)
			*result = LowerComplexConstruction(node, record, children);
		else if (record.kind == pa12_semantic_detail::DUMP_COMPLEX_COMPONENT)
			*result = derived.LoadStorage(
				LowerComplexComponentStorage(record, children),
				derived.LowerExpressionType(record.type));
		else return false;
		return true;
	}

	bool LowerIndirectComplexResult(std::uint32_t node,
		const pa15_lowir_detail::Operand& destination)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		const pa12_semantic_detail::DumpNode& record = derived.arena_.nodes[node];
		if (!derived.IsComplexObjectType(record.type)) return false;
		const pa15_lowering_support::NodeChildren children = derived.Children(node);
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
		const pa12_semantic_detail::DumpNode& record,
		const pa15_lowering_support::NodeChildren& children,
		const pa15_lowir_detail::Operand& retained_destination)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsComplexObjectType(record.type)) return false;
		if (children.empty()) return true;
		if (children.size() != 1)
			throw std::logic_error("invalid complex variable initializer");
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
