#ifndef CPPGM_PA16_ARRAY_LIFETIME_LOWERING_H
#define CPPGM_PA16_ARRAY_LIFETIME_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

const std::size_t kConstructorArrayCleanupInlineLimit = 8;

template <class Derived>
class ArrayLifetimeLowering
{
protected:
	Operand BoundObjectAddress(BindingId object_binding, TypeId type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& object = derived.program_.bindings[object_binding];
		if (!object.non_static_data_member)
			return derived.AddressOfStorage(derived.StorageFor(object_binding,
				derived.LowerStorageType(type)));
		if (derived.current_this_binding_ == kNoBinding)
			throw std::logic_error("member object has no this binding");
		Operand address = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		return derived.ProjectAggregateMember(address, object_binding);
	}

	Operand BoundFlatArrayElementAddress(BindingId object_binding,
		TypeId array_type, TypeId element_type, std::size_t index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand base = derived.DecayAddress(
			BoundObjectAddress(object_binding, array_type));
		const std::size_t element_size = derived.program_.SizeOf(element_type);
		Operand displacement(static_cast<std::int64_t>(index), LowI64());
		if (element_size != 1)
		{
			const Operand scaled = derived.Temp(LowI64());
			Instruction multiply(Instruction::BINARY);
			multiply.dest = scaled.id;
			multiply.op = LOW_OP_MUL;
			multiply.type = LowI64();
			multiply.first = displacement;
			multiply.second = Operand(
				static_cast<std::int64_t>(element_size), LowI64());
			derived.Emit(multiply);
			displacement = scaled;
		}
		return derived.IndexAddress(LowI8(), base, displacement, true);
	}

	Operand BoundArrayElementAddress(BindingId object_binding, TypeId type,
		std::size_t index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		type = derived.RemoveTopQualifiers(type);
		const TypeRecord& array = derived.program_.types.Get(type);
		if (array.kind != TYPE_ARRAY || index >= array.bound)
			throw std::logic_error("invalid bound array element action");
		return BoundFlatArrayElementAddress(
			object_binding, type, array.child, index);
	}

	void LowerArrayValues(TypeId type, std::uint32_t list_node,
		const Operand& array_address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		const NodeChildren values = derived.Children(list_node);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			values.size() > array.bound)
			throw std::runtime_error("invalid bounded array initializer");
		const LowType element = derived.LowerExpressionType(array.child);
		const Operand base = derived.DecayAddress(array_address);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand destination = derived.IndexAddress(element, base,
				Operand(static_cast<std::int64_t>(i), LowI64()), true);
			if (derived.IsArrayType(array.child))
			{
				if (i >= values.size() ||
					derived.arena_.nodes[values[i]].kind != DUMP_BRACED_INIT_LIST)
					throw std::runtime_error(
						"nested array requires a braced initializer");
				LowerArrayValues(array.child, values[i], destination);
				continue;
			}
			Instruction store(Instruction::STORE);
			store.type = element;
			if (i < values.size())
				store.first = derived.Convert(
					derived.LowerValue(values[i]), element, false);
			else if (element.kind == LOW_PTR)
				store.first = Operand::NullPointer(element);
			else if (IsFloating(element))
				store.first = derived.FloatingOperand("0.0", element);
			else store.first = Operand(0, element);
			store.second = destination;
			derived.Emit(store);
		}
	}

	void DescribeConstructorArray(std::uint32_t node, std::size_t* count,
		TypeId* element_type, std::uint32_t* element_action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		*count = 1;
		while (derived.arena_.nodes[node].kind == DUMP_CONSTRUCTOR_ARRAY_ACTION)
		{
			const DumpNode& action = derived.arena_.nodes[node];
			if (action.operand_type == kNoType)
				throw std::logic_error("invalid constructor array action");
			const TypeRecord& array = derived.program_.types.Get(
				derived.RemoveTopQualifiers(action.operand_type));
			const NodeChildren children = derived.Children(node);
			if (array.kind != TYPE_ARRAY || array.bound == 0 ||
				children.size() != 1 ||
				array.bound > std::numeric_limits<std::size_t>::max() / *count)
				throw std::runtime_error("invalid bounded constructor array");
			*count *= static_cast<std::size_t>(array.bound);
			*element_type = array.child;
			node = children[0];
		}
		if (derived.arena_.nodes[node].kind != DUMP_CONSTRUCTOR_ACTION)
			throw std::logic_error("constructor array has no element action");
		*element_action = node;
	}

	void LowerCompactConstructorArray(std::uint32_t element_action,
		BindingId object_binding, TypeId array_type, TypeId element_type,
		std::size_t count, BindingId destructor)
	{
		Derived& derived = static_cast<Derived&>(*this);
		SmallSequence<BlockId, 8> cleanup_blocks;
		for (std::size_t i = 1; i < count; ++i)
			cleanup_blocks.Push(derived.AddBlock(
				derived.NewLabel("call_unwind_cleanup")));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("call_unwind_end"));
		for (std::size_t i = 0; i < count; ++i)
		{
			if (i != 0)
				derived.EmitEhTarget(Instruction::EH_TRY, cleanup_blocks[i - 1]);
			const Operand element = BoundFlatArrayElementAddress(
				object_binding, array_type, element_type, i);
			derived.LowerConstructorAction(element_action, element);
			if (i != 0) derived.Emit(Instruction(Instruction::EH_END));
		}
		derived.EmitJump(end);
		for (std::size_t i = 0; i < cleanup_blocks.size(); ++i)
		{
			derived.SelectBlock(cleanup_blocks[i]);
			derived.EmitDestructorCall(destructor,
				BoundFlatArrayElementAddress(object_binding, array_type,
					element_type, i));
			if (i != 0) derived.EmitJump(cleanup_blocks[i - 1]);
			else derived.Emit(Instruction(Instruction::RESUME));
		}
		derived.SelectBlock(end);
	}

	void LowerBoundConstructorArray(std::uint32_t node,
		BindingId object_binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		if (action.kind != DUMP_CONSTRUCTOR_ARRAY_ACTION ||
			action.operand_type == kNoType)
			throw std::logic_error("invalid bound constructor array action");
		std::size_t count = 0;
		TypeId element_type = kNoType;
		std::uint32_t element_node = kNoDumpEdge;
		DescribeConstructorArray(node, &count, &element_type, &element_node);
		const DumpNode& element_action = derived.arena_.nodes[element_node];
		const bool cleanup_needed = action.binding != kNoBinding &&
			(element_action.binding == kNoBinding ||
			 !derived.program_.bindings[element_action.binding].nonthrowing);
		if (cleanup_needed && count > kConstructorArrayCleanupInlineLimit)
		{
			LowerCompactConstructorArray(element_node, object_binding,
				action.operand_type, element_type, count, action.binding);
			return;
		}
		for (std::size_t i = 0; i < count; ++i)
		{
			BlockId dispatch = kNoLowId;
			BlockId end = kNoLowId;
			if (cleanup_needed && i != 0)
			{
				dispatch = derived.AddBlock(
					derived.NewLabel("call_unwind_dispatch"));
				end = derived.AddBlock(derived.NewLabel("call_unwind_end"));
				derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
			}
			const Operand element = BoundFlatArrayElementAddress(
				object_binding, action.operand_type, element_type, i);
			derived.LowerConstructorAction(element_node, element);
			if (dispatch != kNoLowId)
			{
				derived.Emit(Instruction(Instruction::EH_END));
				derived.EmitJump(end);
				derived.SelectBlock(dispatch);
				for (std::size_t built = i; built != 0; --built)
					derived.EmitDestructorCall(action.binding,
						BoundFlatArrayElementAddress(object_binding,
							action.operand_type, element_type, built - 1));
				derived.Emit(Instruction(Instruction::RESUME));
				derived.SelectBlock(end);
			}
		}
	}
};

}
}

#endif
