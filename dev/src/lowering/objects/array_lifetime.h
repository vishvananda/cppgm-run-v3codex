#ifndef CPPGM_LOWERING_LIFETIME_ARRAYS_H
#define CPPGM_LOWERING_LIFETIME_ARRAYS_H

#include "lowering/ir/model.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "semantic/model/graph.h"

#include <cstdint>
#include <limits>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

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
			ThrowLoweringInternal("member object has no this binding");
		Operand address = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		return derived.ProjectAggregateMember(address, object_binding);
	}

	Operand BoundFlatArrayElementAddress(BindingId object_binding,
		TypeId array_type, TypeId element_type, const Operand& index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand base = derived.DecayAddress(
			BoundObjectAddress(object_binding, array_type));
		const std::size_t element_size = derived.program_.SizeOf(element_type);
		Operand displacement = index;
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

	Operand BoundFlatArrayElementAddress(BindingId object_binding,
		TypeId array_type, TypeId element_type, std::size_t index)
	{
		return BoundFlatArrayElementAddress(object_binding, array_type,
			element_type, Operand(static_cast<std::int64_t>(index), LowI64()));
	}

	Operand BoundArrayElementAddress(BindingId object_binding, TypeId type,
		std::size_t index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		type = derived.RemoveTopQualifiers(type);
		const TypeRecord& array = derived.program_.types.Get(type);
		if (array.kind != TYPE_ARRAY || index >= array.bound)
			ThrowLoweringInternal("invalid bound array element action");
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
			ThrowLoweringSource("invalid bounded array initializer");
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
					ThrowLoweringSource(
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
				ThrowLoweringInternal("invalid constructor array action");
			const TypeRecord& array = derived.program_.types.Get(
				derived.RemoveTopQualifiers(action.operand_type));
			const NodeChildren children = derived.Children(node);
			if (array.kind != TYPE_ARRAY || array.bound == 0 ||
				children.size() != 1)
				ThrowLoweringInternal("invalid bounded constructor array");
			if (array.bound > std::numeric_limits<std::size_t>::max() / *count)
				ThrowLoweringResourceLimit("constructor array extent overflow");
			*count *= static_cast<std::size_t>(array.bound);
			*element_type = array.child;
			node = children[0];
		}
		if (derived.arena_.nodes[node].kind != DUMP_CONSTRUCTOR_ACTION)
			ThrowLoweringInternal("constructor array has no element action");
		*element_action = node;
	}

	void LowerLoopConstructorArray(std::uint32_t element_action,
		BindingId object_binding, TypeId array_type, TypeId element_type,
		std::size_t count, BindingId destructor, bool trivial)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (count > static_cast<std::size_t>(
			std::numeric_limits<std::int64_t>::max()))
			ThrowLoweringResourceLimit("constructor array extent exceeds LowIR");
		const SlotId progress_id = static_cast<SlotId>(
			derived.function_->slots.size());
		Slot progress_slot;
		progress_slot.name = InternLocalName(derived.output_,
			derived.GeneratedSlotName("constructor_array_index"));
		progress_slot.type = LowI64();
		derived.function_->slots.push_back(progress_slot);
		const Operand progress(progress_id, LowI64());
		const BlockId condition = derived.AddBlock(
			derived.NewLabel("constructor_array_cond"));
		const BlockId body = derived.AddBlock(
			derived.NewLabel("constructor_array_body"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("constructor_array_end"));
		const bool cleanup_needed = destructor != kNoBinding;
		const BlockId cleanup = cleanup_needed ? derived.AddBlock(
			derived.NewLabel("constructor_array_cleanup")) : BlockId(kNoLowId);
		const BlockId cleanup_body = cleanup_needed ? derived.AddBlock(
			derived.NewLabel("constructor_array_cleanup_body")) : BlockId(kNoLowId);
		const BlockId resume = cleanup_needed ? derived.AddBlock(
			derived.NewLabel("constructor_array_resume")) : BlockId(kNoLowId);
		Instruction initialize(Instruction::STORE);
		initialize.type = LowI64();
		initialize.first = Operand(0, LowI64());
		initialize.second = progress;
		derived.Emit(initialize);
		derived.EmitJump(condition);
		derived.SelectBlock(condition);
		const Operand index = derived.LoadStorage(progress, LowI64());
		const Operand more = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = more.id;
		compare.op = LOW_OP_ULT;
		compare.type = LowI64();
		compare.first = index;
		compare.second = Operand(static_cast<std::int64_t>(count), LowI64());
		derived.Emit(compare);
		derived.EmitBranch(more, body, end);
		derived.SelectBlock(body);
		if (cleanup_needed)
			derived.EmitEhTarget(Instruction::EH_TRY, cleanup);
		const Operand element = BoundFlatArrayElementAddress(
			object_binding, array_type, element_type, index);
		if (derived.arena_.nodes[element_action].value_initialization)
			derived.EmitZeroInitialization(element_type, element);
		if (!trivial)
			derived.LowerConstructorAction(element_action, element);
		if (cleanup_needed) derived.Emit(Instruction(Instruction::EH_END));
		const Operand next = derived.Temp(LowI64());
		Instruction increment(Instruction::BINARY);
		increment.dest = next.id;
		increment.op = LOW_OP_ADD;
		increment.type = LowI64();
		increment.first = index;
		increment.second = Operand(1, LowI64());
		derived.Emit(increment);
		Instruction save(Instruction::STORE);
		save.type = LowI64();
		save.first = next;
		save.second = progress;
		derived.Emit(save);
		derived.EmitJump(condition);
		if (cleanup_needed)
		{
			derived.SelectBlock(cleanup);
			const Operand remaining = derived.LoadStorage(progress, LowI64());
			const Operand any = derived.Temp(LowI64());
			Instruction nonzero(Instruction::CMP);
			nonzero.dest = any.id;
			nonzero.op = LOW_OP_NE;
			nonzero.type = LowI64();
			nonzero.first = remaining;
			nonzero.second = Operand(0, LowI64());
			derived.Emit(nonzero);
			derived.EmitBranch(any, cleanup_body, resume);
			derived.SelectBlock(cleanup_body);
			const Operand previous = derived.Temp(LowI64());
			Instruction decrement(Instruction::BINARY);
			decrement.dest = previous.id;
			decrement.op = LOW_OP_SUB;
			decrement.type = LowI64();
			decrement.first = remaining;
			decrement.second = Operand(1, LowI64());
			derived.Emit(decrement);
			Instruction save_previous(Instruction::STORE);
			save_previous.type = LowI64();
			save_previous.first = previous;
			save_previous.second = progress;
			derived.Emit(save_previous);
			derived.EmitDestructorCall(destructor,
				BoundFlatArrayElementAddress(object_binding, array_type,
					element_type, previous));
			derived.EmitJump(cleanup);
			derived.SelectBlock(resume);
			derived.EmitExceptionResume();
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
			ThrowLoweringInternal("invalid bound constructor array action");
		std::size_t count = 0;
		TypeId element_type = kNoType;
		std::uint32_t element_node = kNoDumpEdge;
		DescribeConstructorArray(node, &count, &element_type, &element_node);
		const DumpNode& element_action = derived.arena_.nodes[element_node];
		NodeChildren constructor;
		constructor.Push(element_node);
		const bool trivial =
			derived.IsTrivialConstructorAction(element_type, constructor);
		if (trivial && !element_action.value_initialization) return;
		const bool cleanup_needed = !trivial && action.binding != kNoBinding &&
			(element_action.binding == kNoBinding ||
			 !derived.program_.bindings[element_action.binding].nonthrowing);
		if (count > kConstructorArrayCleanupInlineLimit)
		{
			LowerLoopConstructorArray(element_node, object_binding,
				action.operand_type, element_type, count,
				cleanup_needed ? action.binding : kNoBinding, trivial);
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
			if (element_action.value_initialization)
				derived.EmitZeroInitialization(element_type, element);
			if (!trivial)
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
				derived.EmitExceptionResume();
				derived.SelectBlock(end);
			}
		}
	}

	Operand EmitArrayAllocation(std::uint32_t call_node, bool retain_size,
		Operand* retained_size)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& call_record = derived.arena_.nodes[call_node];
		const NodeChildren children = derived.Children(call_node);
		if (call_record.kind != DUMP_CALL_EXPRESSION ||
			call_record.binding == kNoBinding || children.size() < 2)
			ThrowLoweringInternal("array allocation has no retained call");
		const BindingId binding = call_record.binding;
		if (binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[binding] == kNoLowId)
			ThrowLoweringInternal("array allocation has no function symbol");
		const TypeId function_type = derived.program_.bindings[binding].type;
		const TypeRecord& function = derived.program_.types.Get(function_type);
		const TypeId* parameters = derived.program_.types.Parameters(function_type);
		CallArguments arguments;
		CallArgumentFlags references;
		Operand byte_size;
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const std::size_t parameter = i - 1;
			const LowType expected = parameter < function.parameter_count ?
				derived.LowerType(parameters[parameter]) : LowType();
			const Operand value = derived.LowerConvertedValue(children[i], expected,
				derived.CanonicalizeImmediateConversion(children[i]));
			if (i == 1)
			{
				byte_size = value;
				byte_size.type = LowI64();
			}
			arguments.Push(value);
			references.Push(parameter < function.parameter_count &&
				derived.IsReferenceType(parameters[parameter]) ? 1 : 0);
		}
		Operand size_slot;
		if (retain_size)
		{
			size_slot = Operand(derived.EnsureGeneratedSlot(children[1],
				"array_new_size", LowI64()), LowI64());
			Instruction store(Instruction::STORE);
			store.type = LowI64();
			store.first = byte_size;
			store.second = size_slot;
			derived.Emit(store);
		}
		const Operand result = derived.Temp(LowPtr());
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[binding], LowPtr());
		call.dest = result.id;
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
		*retained_size = retain_size ?
			derived.LoadStorage(size_slot, LowI64()) : byte_size;
		return result;
	}

	Operand EmitArrayCount(const DumpNode& record, const Operand& byte_size)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (record.array_count_constant)
		{
			const Operand count = derived.Temp(LowI64());
			Instruction constant(Instruction::CONST);
			constant.dest = count.id;
			constant.type = LowI64();
			constant.first = Operand(static_cast<std::int64_t>(
				record.array_count), LowI64());
			derived.Emit(constant);
			return count;
		}
		Operand data_size = byte_size;
		if (record.array_cookie)
		{
			data_size = derived.Temp(LowI64());
			Instruction subtract(Instruction::BINARY);
			subtract.dest = data_size.id;
			subtract.op = LOW_OP_SUB;
			subtract.type = LowI64();
			subtract.first = byte_size;
			subtract.second = Operand(8, LowI64());
			derived.Emit(subtract);
		}
		const std::size_t leaf_size =
			derived.program_.SizeOf(record.operand_type);
		if (leaf_size == 1) return data_size;
		const Operand count = derived.Temp(LowI64());
		Instruction divide(Instruction::BINARY);
		divide.dest = count.id;
		divide.op = LOW_OP_UDIV;
		divide.type = LowI64();
		divide.first = data_size;
		divide.second = Operand(static_cast<std::int64_t>(leaf_size), LowI64());
		derived.Emit(divide);
		return count;
	}

	void EmitArrayDeallocation(BindingId binding, const DumpNode& record,
		const Operand& allocation_pointer, const Operand& count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (binding == kNoBinding || binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[binding] == kNoLowId)
			ThrowLoweringInternal("array deallocation has no function symbol");
		const TypeId function_type = derived.program_.bindings[binding].type;
		const TypeRecord& function = derived.program_.types.Get(function_type);
		const TypeId* parameters = derived.program_.types.Parameters(function_type);
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(allocation_pointer);
		references.Push(0);
		if (function.parameter_count == 2)
		{
			const Operand scaled = derived.Temp(LowI64());
			Instruction multiply(Instruction::BINARY);
			multiply.dest = scaled.id;
			multiply.op = LOW_OP_MUL;
			multiply.type = LowI64();
			multiply.first = count;
			multiply.second = Operand(static_cast<std::int64_t>(
				derived.program_.SizeOf(record.operand_type)), LowI64());
			derived.Emit(multiply);
			Operand size = scaled;
			if (record.array_cookie)
			{
				size = derived.Temp(LowI64());
				Instruction add(Instruction::BINARY);
				add.dest = size.id;
				add.op = LOW_OP_ADD;
				add.type = LowI64();
				add.first = scaled;
				add.second = Operand(8, LowI64());
				derived.Emit(add);
			}
			arguments.Push(derived.Convert(size,
				derived.LowerType(parameters[1])));
			references.Push(0);
		}
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[binding], LowVoid());
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
	}

	void EmitArrayZeroInitialization(std::uint32_t node,
		const Operand& pointer, const Operand& byte_count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand offset(derived.EnsureGeneratedSlot(
			node, "zeroinit_offset", LowI64()), LowI64());
		const BlockId condition = derived.AddBlock(
			derived.NewLabel("zeroinit_cond"));
		const BlockId body = derived.AddBlock(derived.NewLabel("zeroinit_body"));
		const BlockId end = derived.AddBlock(derived.NewLabel("zeroinit_end"));
		Instruction initialize(Instruction::STORE);
		initialize.type = LowI64();
		initialize.first = Operand(0, LowI64());
		initialize.second = offset;
		derived.Emit(initialize);
		derived.EmitJump(condition);
		derived.SelectBlock(condition);
		const Operand index = derived.LoadStorage(offset, LowI64());
		const Operand more = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = more.id;
		compare.op = LOW_OP_ULT;
		compare.type = LowI64();
		compare.first = index;
		compare.second = byte_count;
		derived.Emit(compare);
		derived.EmitBranch(more, body, end);
		derived.SelectBlock(body);
		Instruction store(Instruction::STORE);
		store.type = LowI8();
		store.first = Operand(0, LowI8());
		store.second = derived.IndexAddress(LowI8(), pointer, index, false);
		derived.Emit(store);
		const Operand next = derived.Temp(LowI64());
		Instruction increment(Instruction::BINARY);
		increment.dest = next.id;
		increment.op = LOW_OP_ADD;
		increment.type = LowI64();
		increment.first = index;
		increment.second = Operand(1, LowI64());
		derived.Emit(increment);
		Instruction save(Instruction::STORE);
		save.type = LowI64();
		save.first = next;
		save.second = offset;
		derived.Emit(save);
		derived.EmitJump(condition);
		derived.SelectBlock(end);
	}

	void EmitArrayConstructorLoop(std::uint32_t node,
		const DumpNode& record, std::uint32_t action, const Operand& raw_pointer,
		const Operand& user_pointer, const Operand& count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand index_slot(derived.EnsureGeneratedSlot(
			action, "array_new_index", LowI64()), LowI64());
		const BlockId condition = derived.AddBlock(
			derived.NewLabel("array_new_ctor_cond"));
		const BlockId body = derived.AddBlock(
			derived.NewLabel("array_new_ctor_body"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("array_new_ctor_end"));
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("array_new_ctor_cleanup"));
		const BlockId continuation = derived.AddBlock(
			derived.NewLabel("array_new_ctor_cont"));
		Instruction initialize(Instruction::STORE);
		initialize.type = LowI64();
		initialize.first = Operand(0, LowI64());
		initialize.second = index_slot;
		derived.Emit(initialize);
		derived.EmitJump(condition);
		derived.SelectBlock(condition);
		const Operand index = derived.LoadStorage(index_slot, LowI64());
		const Operand more = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = more.id;
		compare.op = LOW_OP_ULT;
		compare.type = LowI64();
		compare.first = index;
		compare.second = count;
		derived.Emit(compare);
		derived.EmitBranch(more, body, end);
		derived.SelectBlock(body);
		const Operand displacement = derived.Temp(LowI64());
		Instruction multiply(Instruction::BINARY);
		multiply.dest = displacement.id;
		multiply.op = LOW_OP_MUL;
		multiply.type = LowI64();
		multiply.first = index;
		multiply.second = Operand(static_cast<std::int64_t>(
			derived.program_.SizeOf(record.operand_type)), LowI64());
		derived.Emit(multiply);
		const Operand element = derived.IndexAddress(
			LowI8(), user_pointer, displacement, false);
		derived.EmitEhTarget(Instruction::EH_TRY, cleanup);
		derived.LowerConstructorAction(action, element);
		derived.Emit(Instruction(Instruction::EH_END));
		const Operand next = derived.Temp(LowI64());
		Instruction increment(Instruction::BINARY);
		increment.dest = next.id;
		increment.op = LOW_OP_ADD;
		increment.type = LowI64();
		increment.first = index;
		increment.second = Operand(1, LowI64());
		derived.Emit(increment);
		Instruction save(Instruction::STORE);
		save.type = LowI64();
		save.first = next;
		save.second = index_slot;
		derived.Emit(save);
		derived.EmitJump(condition);
		derived.SelectBlock(end);
		derived.EmitJump(continuation);
		derived.SelectBlock(cleanup);
		const bool routes_to_try =
			derived.BeginExceptionTryCleanupDispatch();
		const Operand built = derived.LoadStorage(index_slot, LowI64());
		if (record.selected_binding != kNoBinding)
		{
			const Operand cleanup_slot(derived.EnsureGeneratedSlot(
				node, "array_dtor_index", LowI64()), LowI64());
			Instruction retain(Instruction::STORE);
			retain.type = LowI64();
			retain.first = built;
			retain.second = cleanup_slot;
			derived.Emit(retain);
			const BlockId dtor_condition = derived.AddBlock(
				derived.NewLabel("array_dtor_cond"));
			const BlockId dtor_body = derived.AddBlock(
				derived.NewLabel("array_dtor_body"));
			const BlockId dtor_end = derived.AddBlock(
				derived.NewLabel("array_dtor_end"));
			derived.EmitJump(dtor_condition);
			derived.SelectBlock(dtor_condition);
			const Operand remaining = derived.LoadStorage(cleanup_slot, LowI64());
			const Operand any = derived.Temp(LowI64());
			Instruction nonzero(Instruction::CMP);
			nonzero.dest = any.id;
			nonzero.op = LOW_OP_NE;
			nonzero.type = LowI64();
			nonzero.first = remaining;
			nonzero.second = Operand(0, LowI64());
			derived.Emit(nonzero);
			derived.EmitBranch(any, dtor_body, dtor_end);
			derived.SelectBlock(dtor_body);
			const Operand previous = derived.Temp(LowI64());
			Instruction decrement(Instruction::BINARY);
			decrement.dest = previous.id;
			decrement.op = LOW_OP_SUB;
			decrement.type = LowI64();
			decrement.first = remaining;
			decrement.second = Operand(1, LowI64());
			derived.Emit(decrement);
			Instruction save_previous(Instruction::STORE);
			save_previous.type = LowI64();
			save_previous.first = previous;
			save_previous.second = cleanup_slot;
			derived.Emit(save_previous);
			const Operand dtor_offset = derived.Temp(LowI64());
			Instruction scale(Instruction::BINARY);
			scale.dest = dtor_offset.id;
			scale.op = LOW_OP_MUL;
			scale.type = LowI64();
			scale.first = previous;
			scale.second = Operand(static_cast<std::int64_t>(
				derived.program_.SizeOf(record.operand_type)), LowI64());
			derived.Emit(scale);
			derived.EmitDestructorCall(record.selected_binding,
				derived.IndexAddress(LowI8(), user_pointer, dtor_offset, false));
			derived.EmitJump(dtor_condition);
			derived.SelectBlock(dtor_end);
		}
		EmitArrayDeallocation(record.object_binding, record,
			raw_pointer, count);
		derived.FinishExceptionCleanupDispatch(routes_to_try, false);
		derived.SelectBlock(continuation);
	}

	Operand LowerArrayNewExpression(std::uint32_t node,
		const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty() || children.size() > 2)
			ThrowLoweringInternal("invalid array new action");
		const bool retain_size = !record.array_count_constant &&
			(record.array_cookie || record.value_initialization ||
			 children.size() == 2);
		Operand byte_size;
		const Operand raw_pointer = EmitArrayAllocation(
			children[0], retain_size, &byte_size);
		Operand user_pointer = raw_pointer;
		if (record.array_cookie)
			user_pointer = derived.IndexAddress(LowI8(), raw_pointer,
				Operand(8, LowI64()), false);
		if (record.array_cookie)
		{
			Instruction cookie(Instruction::STORE);
			cookie.type = LowI64();
			cookie.first = EmitArrayCount(record, byte_size);
			cookie.second = raw_pointer;
			derived.Emit(cookie);
		}
		if (record.value_initialization)
		{
			Operand data_size = byte_size;
			if (record.array_cookie)
			{
				data_size = derived.Temp(LowI64());
				Instruction subtract(Instruction::BINARY);
				subtract.dest = data_size.id;
				subtract.op = LOW_OP_SUB;
				subtract.type = LowI64();
				subtract.first = byte_size;
				subtract.second = Operand(8, LowI64());
				derived.Emit(subtract);
			}
			EmitArrayZeroInitialization(children[0], user_pointer, data_size);
		}
		if (children.size() == 2)
			EmitArrayConstructorLoop(node, record, children[1], raw_pointer,
				user_pointer, EmitArrayCount(record, byte_size));
		return user_pointer;
	}

	Operand LowerArrayDeleteExpression(std::uint32_t node,
		const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			ThrowLoweringInternal("invalid array delete action");
		const Operand pointer = derived.LowerValue(children[0], LowPtr());
		if (!record.array_cookie)
		{
			EmitArrayDeallocation(record.binding, record, pointer,
				Operand(0, LowI64()));
			return Operand(0, LowVoid());
		}
		const BlockId nonnull = derived.AddBlock(
			derived.NewLabel("array_delete_nonnull"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("array_delete_end"));
		const Operand condition = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = condition.id;
		compare.op = LOW_OP_NE;
		compare.type = LowPtr();
		compare.first = pointer;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		derived.EmitBranch(condition, nonnull, end);
		derived.SelectBlock(nonnull);
		const Operand raw_pointer = derived.IndexAddress(LowI8(), pointer,
			Operand(-8, LowI64()), false);
		const Operand count = derived.LoadStorage(raw_pointer, LowI64());
		if (record.selected_binding != kNoBinding)
		{
			const Operand index_slot(derived.EnsureGeneratedSlot(
				node, "array_delete_index", LowI64()), LowI64());
			Instruction retain(Instruction::STORE);
			retain.type = LowI64();
			retain.first = count;
			retain.second = index_slot;
			derived.Emit(retain);
			const BlockId dtor_condition = derived.AddBlock(
				derived.NewLabel("array_delete_dtor_cond"));
			const BlockId dtor_body = derived.AddBlock(
				derived.NewLabel("array_delete_dtor_body"));
			const BlockId dtor_end = derived.AddBlock(
				derived.NewLabel("array_delete_dtor_end"));
			derived.EmitJump(dtor_condition);
			derived.SelectBlock(dtor_condition);
			const Operand remaining = derived.LoadStorage(index_slot, LowI64());
			const Operand any = derived.Temp(LowI64());
			Instruction nonzero(Instruction::CMP);
			nonzero.dest = any.id;
			nonzero.op = LOW_OP_NE;
			nonzero.type = LowI64();
			nonzero.first = remaining;
			nonzero.second = Operand(0, LowI64());
			derived.Emit(nonzero);
			derived.EmitBranch(any, dtor_body, dtor_end);
			derived.SelectBlock(dtor_body);
			const Operand previous = derived.Temp(LowI64());
			Instruction decrement(Instruction::BINARY);
			decrement.dest = previous.id;
			decrement.op = LOW_OP_SUB;
			decrement.type = LowI64();
			decrement.first = remaining;
			decrement.second = Operand(1, LowI64());
			derived.Emit(decrement);
			Instruction save(Instruction::STORE);
			save.type = LowI64();
			save.first = previous;
			save.second = index_slot;
			derived.Emit(save);
			const Operand offset = derived.Temp(LowI64());
			Instruction scale(Instruction::BINARY);
			scale.dest = offset.id;
			scale.op = LOW_OP_MUL;
			scale.type = LowI64();
			scale.first = previous;
			scale.second = Operand(static_cast<std::int64_t>(
				derived.program_.SizeOf(record.operand_type)), LowI64());
			derived.Emit(scale);
			derived.EmitDestructorCall(record.selected_binding,
				derived.IndexAddress(LowI8(), pointer, offset, false));
			derived.EmitJump(dtor_condition);
			derived.SelectBlock(dtor_end);
		}
		EmitArrayDeallocation(record.binding, record, raw_pointer, count);
		derived.EmitJump(end);
		derived.SelectBlock(end);
		return Operand(0, LowVoid());
	}
};

}
}

#endif
