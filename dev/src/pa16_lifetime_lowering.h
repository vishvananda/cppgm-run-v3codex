#ifndef CPPGM_PA16_LIFETIME_LOWERING_H
#define CPPGM_PA16_LIFETIME_LOWERING_H

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

const std::size_t kDestructorCleanupInlineLimit = 8;

template <class Derived>
class LifetimeActionLowering
{
protected:
	void EmitEhTarget(Instruction::Kind kind, BlockId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction instruction(kind);
		instruction.target = target;
		derived.Emit(instruction);
	}

	void EmitDestructorActionRange(const NodeChildren& children,
		std::size_t first)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = first; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				throw std::logic_error("invalid destructor suffix action");
			if (i + 1 == children.size())
			{
				LowerDestructorAction(derived.arena_.nodes[children[i]]);
				continue;
			}
			const BlockId cleanup = derived.AddBlock(
				derived.NewLabel("destructor_suffix_cleanup"));
			const BlockId next = derived.AddBlock(
				derived.NewLabel("destructor_suffix_next"));
			EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
			LowerDestructorAction(derived.arena_.nodes[children[i]]);
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(next);
			derived.SelectBlock(cleanup);
			for (std::size_t j = i + 1; j < children.size(); ++j)
				LowerDestructorAction(derived.arena_.nodes[children[j]]);
			derived.Emit(Instruction(Instruction::EH_END));
			derived.Emit(Instruction(Instruction::RESUME));
			derived.SelectBlock(next);
		}
	}

	void LowerDestructorBody(std::uint32_t body)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(body);
		std::size_t first_action = children.size();
		for (std::size_t i = 0; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind == DUMP_DESTRUCTOR_ACTION)
			{
				first_action = i;
				break;
			}
		if (first_action == children.size())
		{
			derived.LowerStatement(body);
			return;
		}
		if (children.size() - first_action > kDestructorCleanupInlineLimit)
		{
			LowerCompactDestructorBody(body, children, first_action);
			return;
		}
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("destructor_cleanup"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("destructor_end"));
		EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		for (std::size_t i = 0; i < first_action; ++i)
			derived.LowerStatement(children[i]);
		if (derived.CurrentBlock().terminated) return;
		derived.Emit(Instruction(Instruction::EH_END));
		EmitDestructorActionRange(children, first_action);
		derived.EmitJump(end);
		derived.SelectBlock(cleanup);
		for (std::size_t i = first_action; i < children.size(); ++i)
			LowerDestructorAction(derived.arena_.nodes[children[i]]);
		derived.Emit(Instruction(Instruction::EH_END));
		derived.Emit(Instruction(Instruction::RESUME));
		derived.SelectBlock(end);
	}

	void LowerCompactDestructorBody(std::uint32_t body,
		const NodeChildren& children, std::size_t first_action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::size_t action_count = children.size() - first_action;
		const Operand progress(derived.EnsureGeneratedSlot(
			body, "destructor_progress", LowI64()), LowI64());
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("destructor_cleanup"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("destructor_end"));
		Instruction initial_progress(Instruction::STORE);
		initial_progress.type = LowI64();
		initial_progress.first = Operand(0, LowI64());
		initial_progress.second = progress;
		derived.Emit(initial_progress);
		EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		for (std::size_t i = 0; i < first_action; ++i)
			derived.LowerStatement(children[i]);
		if (derived.CurrentBlock().terminated) return;
		derived.Emit(Instruction(Instruction::EH_END));
		for (std::size_t i = 0; i < action_count; ++i)
		{
			if (i + 1 < action_count)
			{
				Instruction next_progress(Instruction::STORE);
				next_progress.type = LowI64();
				next_progress.first = Operand(
					static_cast<std::int64_t>(i + 1), LowI64());
				next_progress.second = progress;
				derived.Emit(next_progress);
				EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
			}
			LowerDestructorAction(
				derived.arena_.nodes[children[first_action + i]]);
			if (i + 1 < action_count)
				derived.Emit(Instruction(Instruction::EH_END));
		}
		derived.EmitJump(end);

		SmallSequence<BlockId, 8> cleanup_blocks;
		for (std::size_t i = 0; i < action_count; ++i)
			cleanup_blocks.Push(derived.AddBlock(
				derived.NewLabel("destructor_suffix_cleanup")));
		derived.SelectBlock(cleanup);
		const Operand selected = derived.LoadStorage(progress, LowI64());
		Instruction dispatch(Instruction::SWITCH);
		dispatch.first = selected;
		dispatch.target = cleanup_blocks[0];
		SmallSequence<std::int64_t, 8> values;
		for (std::size_t i = 0; i < action_count; ++i)
			values.Push(static_cast<std::int64_t>(i));
		derived.AttachSwitchCases(&dispatch, values, cleanup_blocks);
		derived.Emit(dispatch);
		for (std::size_t i = 0; i < action_count; ++i)
		{
			derived.SelectBlock(cleanup_blocks[i]);
			LowerDestructorAction(
				derived.arena_.nodes[children[first_action + i]]);
			if (i + 1 < action_count)
				derived.EmitJump(cleanup_blocks[i + 1]);
			else
			{
				derived.Emit(Instruction(Instruction::EH_END));
				derived.Emit(Instruction(Instruction::RESUME));
			}
		}
		derived.SelectBlock(end);
	}

	Operand ProjectBaseSubobjects(Operand object,
		std::uint32_t projection_count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::uint32_t i = 0; i < projection_count; ++i)
			object = derived.ProjectBaseSubobject(object);
		return object;
	}

	void LowerBaseInitializationAction(const DumpNode& action,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.kind != DUMP_BASE_INITIALIZER_ACTION ||
			derived.current_this_binding_ == kNoBinding || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			throw std::logic_error(
				"base initialization is outside a constructor");
		if (derived.IsTrivialConstructorAction(action.type, children)) return;
		const Operand object = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		const Operand destination = ProjectBaseSubobjects(object,
			action.base_projection_count);
		derived.LowerConstructorAction(children[0], destination);
	}

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

	Operand BoundArrayElementAddress(BindingId object_binding, TypeId type,
		std::size_t index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		type = derived.RemoveTopQualifiers(type);
		const TypeRecord& array = derived.program_.types.Get(type);
		if (array.kind != TYPE_ARRAY || index >= array.bound)
			throw std::logic_error("invalid bound array element action");
		const Operand base = derived.DecayAddress(
			BoundObjectAddress(object_binding, type));
		const std::size_t element_size = derived.program_.SizeOf(array.child);
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

	void LowerConstructorArrayAt(std::uint32_t node,
		const Operand& address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		if (action.kind != DUMP_CONSTRUCTOR_ARRAY_ACTION ||
			action.operand_type == kNoType || children.size() != 1)
			throw std::logic_error("invalid constructor array action");
		const TypeRecord& array = derived.program_.types.Get(
			derived.RemoveTopQualifiers(action.operand_type));
		if (array.kind != TYPE_ARRAY || array.bound == 0)
			throw std::logic_error("invalid constructor array type");
		const Operand base = derived.DecayAddress(address);
		const std::size_t element_size = derived.program_.SizeOf(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand element = derived.IndexAddress(LowI8(), base,
				Operand(static_cast<std::int64_t>(i * element_size), LowI64()),
				true);
			if (derived.arena_.nodes[children[0]].kind ==
				DUMP_CONSTRUCTOR_ARRAY_ACTION)
				LowerConstructorArrayAt(children[0], element);
			else derived.LowerConstructorAction(children[0], element);
		}
	}

	void LowerBoundConstructorArray(std::uint32_t node,
		BindingId object_binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		if (action.kind != DUMP_CONSTRUCTOR_ARRAY_ACTION ||
			action.operand_type == kNoType)
			throw std::logic_error("invalid bound constructor array action");
		const NodeChildren children = derived.Children(node);
		const TypeRecord& array = derived.program_.types.Get(
			derived.RemoveTopQualifiers(action.operand_type));
		if (array.kind != TYPE_ARRAY || array.bound == 0 || children.size() != 1)
			throw std::logic_error("invalid bound constructor array action");
		const DumpNode& element_action = derived.arena_.nodes[children[0]];
		const bool cleanup_needed = action.binding != kNoBinding &&
			element_action.kind == DUMP_CONSTRUCTOR_ACTION &&
			(element_action.binding == kNoBinding ||
			 !derived.program_.bindings[element_action.binding].nonthrowing);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			BlockId dispatch = kNoLowId;
			BlockId end = kNoLowId;
			if (cleanup_needed && i != 0)
			{
				dispatch = derived.AddBlock(
					derived.NewLabel("call_unwind_dispatch"));
				end = derived.AddBlock(derived.NewLabel("call_unwind_end"));
				EmitEhTarget(Instruction::EH_TRY, dispatch);
			}
			const Operand element = BoundArrayElementAddress(
				object_binding, action.operand_type, i);
			if (element_action.kind == DUMP_CONSTRUCTOR_ARRAY_ACTION)
				LowerConstructorArrayAt(children[0], element);
			else derived.LowerConstructorAction(children[0], element);
			if (dispatch != kNoLowId)
			{
				derived.Emit(Instruction(Instruction::EH_END));
				derived.EmitJump(end);
				derived.SelectBlock(dispatch);
				for (std::size_t built = i; built != 0; --built)
					EmitDestructorCall(action.binding,
						BoundArrayElementAddress(object_binding,
							action.operand_type, built - 1));
				derived.Emit(Instruction(Instruction::RESUME));
				derived.SelectBlock(end);
			}
		}
	}

	void EmitDestructorCall(BindingId destructor, const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (destructor == kNoBinding ||
			destructor >= derived.function_symbols_.size() ||
			derived.function_symbols_[destructor] == kNoLowId)
			throw std::runtime_error("destructor action has no emitted binding");
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION,
			derived.function_symbols_[destructor], LowPtr());
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		derived.output_.symbols[
			derived.function_symbols_[destructor]].referenced = true;
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
	}

	void LowerDestructorObject(TypeId type, const Operand& address,
		BindingId destructor)
	{
		Derived& derived = static_cast<Derived&>(*this);
		type = derived.RemoveTopQualifiers(type);
		const TypeRecord& record = derived.program_.types.Get(type);
		if (record.kind != TYPE_ARRAY)
		{
			EmitDestructorCall(destructor, address);
			return;
		}
		if (record.bound == 0)
			throw std::runtime_error("destruction of an unbounded array");
		const Operand base = derived.DecayAddress(address);
		const std::size_t element_size = derived.program_.SizeOf(record.child);
		for (std::size_t i = static_cast<std::size_t>(record.bound);
			i != 0; --i)
		{
			const Operand element = derived.IndexAddress(LowI8(), base,
				Operand(static_cast<std::int64_t>((i - 1) * element_size),
					LowI64()), true);
			LowerDestructorObject(record.child, element, destructor);
		}
	}

	void LowerDestructorAction(const DumpNode& action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.kind != DUMP_DESTRUCTOR_ACTION ||
			action.binding == kNoBinding || action.operand_type == kNoType)
			throw std::logic_error("invalid destructor action");
		const TypeRecord& outer = derived.program_.types.Get(
			derived.RemoveTopQualifiers(action.operand_type));
		if (outer.kind == TYPE_ARRAY && action.object_binding != kNoBinding)
		{
			if (action.constant)
			{
				if (action.constant_value < 0 ||
					static_cast<std::uint64_t>(action.constant_value) >=
						outer.bound)
					throw std::logic_error(
						"invalid destructor array element identity");
				const Operand element = BoundArrayElementAddress(
					action.object_binding, action.operand_type,
					static_cast<std::size_t>(action.constant_value));
				LowerDestructorObject(outer.child, element, action.binding);
				return;
			}
			const bool member = derived.program_.bindings[
				action.object_binding].non_static_data_member;
			for (std::size_t ordinal = 0;
				ordinal < static_cast<std::size_t>(outer.bound); ++ordinal)
			{
				const std::size_t index = member ?
					static_cast<std::size_t>(outer.bound) - ordinal - 1 :
					ordinal;
				const Operand element = BoundArrayElementAddress(
					action.object_binding, action.operand_type, index);
				LowerDestructorObject(outer.child, element, action.binding);
			}
			return;
		}
		Operand destination;
		if (action.object_binding != kNoBinding)
		{
			const BindingRecord& object =
				derived.program_.bindings[action.object_binding];
			if (object.non_static_data_member)
			{
				if (derived.current_this_binding_ == kNoBinding)
					throw std::logic_error(
						"member destruction is outside a destructor");
				destination = derived.LoadStorage(derived.StorageFor(
					derived.current_this_binding_, LowPtr()), LowPtr());
				destination = derived.ProjectAggregateMember(destination,
					action.object_binding);
			}
			else destination = derived.AddressOfStorage(derived.StorageFor(
				action.object_binding,
				derived.LowerStorageType(action.operand_type)));
		}
		else
		{
			if (derived.current_this_binding_ == kNoBinding ||
				action.base_projection_count == 0)
				throw std::logic_error("base destruction has no object");
			destination = derived.LoadStorage(derived.StorageFor(
				derived.current_this_binding_, LowPtr()), LowPtr());
			destination = derived.ProjectBaseSubobjects(destination,
				action.base_projection_count);
		}
		LowerDestructorObject(action.operand_type, destination, action.binding);
	}
};

}
}

#endif
