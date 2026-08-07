#ifndef CPPGM_PA16_INITIALIZATION_LOWERING_H
#define CPPGM_PA16_INITIALIZATION_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class InitializationLowering
{
protected:
	bool IsTrivialConstructorAction(TypeId type,
		const NodeChildren& children) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (!derived.IsClassObjectType(type) || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
		const TypeRecord& record = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		return derived.program_.entities[record.entity].trivial_default_constructor;
	}

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
		(void)derived.AddressOfStorage(slot);
		(void)derived.AddressOfStorage(derived.LowerStorage(node));
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

	void EmitZeroInitialization(TypeId type, const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::size_t size = derived.program_.SizeOf(type);
		if (size > 64)
		{
			const LowType zero_type = derived.LowerStorageType(type);
			Instruction store(Instruction::STORE);
			store.type = zero_type;
			store.first = Operand(0, zero_type);
			store.second = destination;
			derived.Emit(store);
			return;
		}
		std::size_t offset = 0;
		while (offset < size)
		{
			const std::size_t remaining = size - offset;
			const LowType zero_type = remaining >= 8 ? LowI64() :
				remaining >= 4 ? LowI32() : remaining >= 2 ? LowI16() : LowI8();
			Operand address = destination;
			if (offset != 0)
				address = derived.IndexAddress(LowI8(), destination,
					Operand(static_cast<std::int64_t>(offset), LowI64()), false);
			Instruction store(Instruction::STORE);
			store.type = zero_type;
			store.first = Operand(0, zero_type);
			store.second = address;
			derived.Emit(store);
			offset += zero_type.width / 8;
		}
	}

	template <class AggregatePath>
	Operand LowerTemporaryObjectStorage(std::uint32_t node,
		const NodeChildren& children, AggregatePath* path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			throw std::runtime_error("invalid temporary object action");
		const bool initialize = derived.generated_slots_[node] == kNoLowId;
		const LowType type = derived.LowerStorageType(
			derived.arena_.nodes[node].type);
		const Operand slot(derived.EnsureGeneratedSlot(node,
			derived.arena_.nodes[node].argument_materialization ?
				"arg" : "tmpobj", type), type);
		const Operand destination = derived.AddressOfStorage(slot);
		if (initialize)
		{
			if (derived.arena_.nodes[children[0]].kind == DUMP_CONSTRUCTOR_ACTION)
				derived.LowerConstructorAction(children[0], destination);
			else if (derived.arena_.nodes[children[0]].kind == DUMP_BRACED_INIT_LIST)
				derived.LowerAggregateActions(children[0], slot, path, Operand());
			else throw std::runtime_error(
				"unsupported temporary object initializer");
		}
		return destination;
	}

	Operand LowerNewExpression(const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty() || children.size() > 2)
			throw std::runtime_error("invalid placement new action");
		const Operand result = derived.LowerValue(children[0], LowPtr());
		if (children.size() == 2)
		{
			const DumpKind kind = derived.arena_.nodes[children[1]].kind;
			if (kind == DUMP_CONSTRUCTOR_ACTION)
				derived.LowerConstructorAction(children[1], result);
			else if (kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
				derived.LowerAggregateConstructionAction(children[1], result);
			else derived.LowerRuntimeObjectValue(
				record.operand_type, children[1], result);
		}
		return result;
	}

	void LowerLocalClassArrayInitializer(const DumpNode& record,
		const NodeChildren& values)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(record.type));
		const Operand base = derived.AddressOfStorage(derived.StorageFor(
			record.binding, derived.LowerStorageType(record.type)));
		const std::size_t element_size = derived.program_.SizeOf(array.child);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			Operand destination = base;
			if (i != 0)
				destination = derived.IndexAddress(LowI8(), base,
					Operand(static_cast<std::int64_t>(i * element_size),
						LowI64()), false);
			const DumpKind kind = derived.arena_.nodes[values[i]].kind;
			if (kind == DUMP_CONSTRUCTOR_ACTION)
				derived.LowerConstructorAction(values[i], destination);
			else if (kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
				derived.LowerAggregateConstructionAction(values[i], destination);
			else derived.LowerRuntimeObjectValue(
				array.child, values[i], destination);
		}
	}

	bool LowerVariableConstructor(const DumpNode& variable,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsClassObjectType(variable.type) || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
		const LowType type = derived.LowerStorageType(variable.type);
		const Operand destination = derived.AddressOfStorage(
			derived.StorageFor(variable.binding, type));
		if (derived.arena_.nodes[children[0]].value_initialization)
			EmitZeroInitialization(variable.type, destination);
		if (!IsTrivialConstructorAction(variable.type, children))
			derived.LowerConstructorAction(children[0], destination);
		return true;
	}

	bool LowerRuntimeConstructorValue(TypeId type, std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[node].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
		if (derived.arena_.nodes[node].value_initialization)
			EmitZeroInitialization(type, destination);
		NodeChildren action;
		action.Push(node);
		if (!IsTrivialConstructorAction(type, action))
			derived.LowerConstructorAction(node, destination);
		return true;
	}

	bool LowerClassValueInitialization(const DumpNode& variable,
		std::uint32_t initializer, const Operand& storage)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.arena_.nodes[initializer].value_initialization) return false;
		EmitZeroInitialization(variable.type, derived.AddressOfStorage(storage));
		const TypeRecord& object = derived.program_.types.Get(
			derived.ExpressionObjectType(variable.type));
		return derived.program_.entities[object.entity].trivial_default_constructor;
	}

	template <class AggregatePath>
	void LowerAggregateArrayLeaf(const DumpNode& action,
		const NodeChildren& values, const Operand& root,
		const AggregatePath& path, const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() != 1 ||
			derived.arena_.nodes[values[0]].kind != DUMP_BRACED_INIT_LIST)
			throw std::runtime_error("array member requires a braced initializer");
		if (retained_destination.kind != Operand::NONE)
		{
			derived.LowerArrayValues(action.type, values[0], retained_destination);
			return;
		}
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(action.type));
		const NodeChildren elements = derived.Children(values[0]);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			elements.size() > array.bound)
			throw std::runtime_error("invalid aggregate array initializer");
		const LowType element = derived.LowerExpressionType(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand base = derived.DecayAddress(
				derived.ProjectAggregatePath(root, path));
			const Operand destination = derived.IndexAddress(element, base,
				Operand(static_cast<std::int64_t>(i), LowI64()), true);
			Instruction store(Instruction::STORE);
			store.type = element;
			if (i < elements.size())
				store.first = derived.LowerConvertedValue(elements[i], element);
			else if (element.kind == LOW_PTR)
				store.first = Operand::NullPointer(element);
			else if (IsFloating(element))
				store.first = derived.FloatingOperand("0.0", element);
			else store.first = Operand(0, element);
			store.second = destination;
			derived.Emit(store);
		}
	}

	template <class AggregatePath>
	bool LowerAggregateConstructorLeaf(const DumpNode& action,
		const NodeChildren& values, const Operand& root,
		const AggregatePath& path, const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() != 1 ||
			derived.arena_.nodes[values[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
		const Operand destination = retained_destination.kind == Operand::NONE ?
			derived.ProjectAggregatePath(root, path) : retained_destination;
		if (derived.arena_.nodes[values[0]].elide_empty_constructor) return true;
		if (derived.arena_.nodes[values[0]].value_initialization)
			EmitZeroInitialization(action.type, destination);
		NodeChildren constructor;
		constructor.Push(values[0]);
		if (!IsTrivialConstructorAction(action.type, constructor))
			derived.LowerConstructorAction(values[0], destination);
		return true;
	}
};

}
}

#endif
