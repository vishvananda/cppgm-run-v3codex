#pragma once

#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa17_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowering_support;
using namespace pa15_lowir_detail;

const std::size_t kSpecialMemberArrayInlineLimit = 8;

template <typename Derived>
class SpecialMemberLowering
{
protected:
	Operand ArrayElementAddress(TypeId element_type,
		const Operand& base, const Operand& index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand displacement = index;
		const std::size_t element_size =
			derived.program_.SizeOf(element_type);
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
		return derived.IndexAddress(
			LowI8(), base, displacement, true);
	}

	Operand ArrayElementAddress(TypeId element_type,
		const Operand& base, std::size_t index)
	{
		return ArrayElementAddress(element_type, base,
			Operand(static_cast<std::int64_t>(index), LowI64()));
	}

	void LowerSpecialMemberCall(BindingId selected,
		const Operand& destination, const Operand& source)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (selected == kNoBinding ||
			selected >= derived.function_symbols_.size() ||
			derived.function_symbols_[selected] == kNoLowId)
			throw std::logic_error(
				"selected special-member helper has no lowering identity");
		const TypeRecord& function = derived.program_.types.Get(
			derived.program_.bindings[selected].type);
		Instruction call(Instruction::CALL);
		call.type = derived.LowerBoundaryResult(function.child);
		call.first = Operand(Operand::FUNCTION,
			derived.function_symbols_[selected], LowPtr());
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		arguments.Push(source);
		references.Push(1);
		derived.output_.symbols[
			derived.function_symbols_[selected]].referenced = true;
		derived.AttachCallArguments(&call, arguments, references);
		if (call.type.kind == LOW_VOID)
			derived.Emit(call);
		else
		{
			const Operand ignored = derived.Temp(call.type);
			call.dest = ignored.id;
			derived.Emit(call);
		}
	}

	Operand LoadAssignmentObject(BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return derived.LoadStorage(
			derived.StorageFor(binding, LowPtr()), LowPtr());
	}

	void LowerConstructionSubobject(TypeId type, BindingId selected,
		const Operand& destination, const Operand& source)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeId object_type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord& record = derived.program_.types.Get(object_type);
		if (record.kind == TYPE_ARRAY)
			throw std::logic_error(
				"array construction bypassed its retained loop recipe");
		if (selected != kNoBinding)
		{
			LowerSpecialMemberCall(selected, destination, source);
			return;
		}
		if (derived.IsClassObjectType(type))
		{
			derived.EmitClassObjectCopy(type, source, destination);
			return;
		}
		const LowType storage_type = derived.IsReferenceType(type) ?
			LowPtr() : derived.LowerExpressionType(type);
		const Operand value = derived.LoadStorage(source, storage_type);
		Instruction store(Instruction::STORE);
		store.type = storage_type;
		store.first = value;
		store.second = destination;
		derived.Emit(store);
	}

	void LowerAssignmentSubobject(TypeId type, BindingId selected,
		const Operand& destination, const Operand& source)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeId object_type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord& record = derived.program_.types.Get(object_type);
		if (record.kind == TYPE_ARRAY)
			throw std::logic_error(
				"array assignment bypassed its retained loop recipe");
		if (selected != kNoBinding)
		{
			LowerSpecialMemberCall(selected, destination, source);
			return;
		}
		if (derived.IsClassObjectType(type))
		{
			derived.EmitClassObjectCopy(type, source, destination);
			return;
		}
		const LowType storage_type = derived.IsReferenceType(type) ?
			LowPtr() : derived.LowerExpressionType(type);
		const Operand value = derived.LoadStorage(source, storage_type);
		Instruction store(Instruction::STORE);
		store.type = storage_type;
		store.first = value;
		store.second = destination;
		derived.Emit(store);
	}

	TypeId FlattenArrayType(TypeId type, std::size_t* count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		*count = 1;
		for (;;)
		{
			const TypeRecord& array = derived.program_.types.Get(
				derived.program_.types.RemoveTopCv(type));
			if (array.kind != TYPE_ARRAY) return type;
			if (array.bound == 0 || array.bound >
				std::numeric_limits<std::size_t>::max() / *count)
				throw std::logic_error("invalid synthesized array extent");
			*count *= static_cast<std::size_t>(array.bound);
			type = array.child;
		}
	}

	void LowerArraySubobjectStep(std::uint32_t step_node,
		TypeId type, BindingId selected, const Operand& destination,
		BindingId source_object, BindingId source_member, bool assignment)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::size_t count = 0;
		const TypeId element = FlattenArrayType(type, &count);
		if (selected == kNoBinding)
		{
			Operand source = LoadAssignmentObject(source_object);
			if (source_member != kNoBinding)
				source = derived.ProjectAggregateMember(source, source_member);
			derived.EmitClassObjectCopy(type, source, destination);
			return;
		}
		const Operand destination_base = derived.DecayAddress(destination);
		Operand source_base;
		if (count <= kSpecialMemberArrayInlineLimit)
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				const Operand destination_element = ArrayElementAddress(
					element, destination_base, i);
				if (i == 0)
				{
					Operand source = LoadAssignmentObject(source_object);
					if (source_member != kNoBinding)
						source = derived.ProjectAggregateMember(
							source, source_member);
					source_base = derived.DecayAddress(source);
				}
				const Operand source_element = ArrayElementAddress(
					element, source_base, i);
				if (assignment)
					LowerAssignmentSubobject(element, selected,
						destination_element, source_element);
				else LowerConstructionSubobject(element, selected,
					destination_element, source_element);
			}
			return;
		}
		if (count > static_cast<std::size_t>(
			std::numeric_limits<std::int64_t>::max()))
			throw std::logic_error("synthesized array extent exceeds LowIR");
		Operand source = LoadAssignmentObject(source_object);
		if (source_member != kNoBinding)
			source = derived.ProjectAggregateMember(source, source_member);
		source_base = derived.DecayAddress(source);
		const Operand index_slot(derived.EnsureGeneratedSlot(step_node,
			assignment ? "assign_array_index" : "copy_array_index", LowI64()),
			LowI64());
		const BlockId condition = derived.AddBlock(derived.NewLabel(
			assignment ? "assign_array_cond" : "copy_array_cond"));
		const BlockId body = derived.AddBlock(derived.NewLabel(
			assignment ? "assign_array_body" : "copy_array_body"));
		const BlockId end = derived.AddBlock(derived.NewLabel(
			assignment ? "assign_array_end" : "copy_array_end"));
		Instruction initialize(Instruction::STORE);
		initialize.type = LowI64();
		initialize.first = Operand(0, LowI64());
		initialize.second = index_slot;
		derived.Emit(initialize);
		derived.EmitJump(condition);
		derived.SelectBlock(condition);
		const Operand index = derived.LoadStorage(index_slot, LowI64());
		const Operand more = derived.Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = more.id;
		compare.op = LOW_OP_ULT;
		compare.type = LowI64();
		compare.first = index;
		compare.second = Operand(static_cast<std::int64_t>(count), LowI64());
		derived.Emit(compare);
		derived.EmitBranch(more, body, end);
		derived.SelectBlock(body);
		const Operand destination_element = ArrayElementAddress(
			element, destination_base, index);
		const Operand source_element = ArrayElementAddress(
			element, source_base, index);
		if (assignment)
			LowerAssignmentSubobject(element, selected,
				destination_element, source_element);
		else LowerConstructionSubobject(element, selected,
			destination_element, source_element);
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
	}

	void LowerConstructionStep(std::uint32_t step_node,
		const DumpNode& construction,
		const DumpNode& step)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (step.kind != DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION)
			throw std::logic_error("invalid synthesized construction step");
		if (step.storage_size != 0)
		{
			const Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			const Operand source = LoadAssignmentObject(
				construction.object_binding);
			Instruction copy(Instruction::COPY_OBJECT);
			copy.type = LowObject(
				static_cast<std::size_t>(step.storage_size),
				step.storage_alignment);
			copy.first = source;
			copy.second = destination;
			derived.Emit(copy);
			return;
		}
		if (derived.IsReferenceType(step.type))
		{
			if (step.binding == kNoBinding || step.selected_binding != kNoBinding)
				throw std::logic_error(
					"invalid synthesized reference construction step");
			Operand source = LoadAssignmentObject(construction.object_binding);
			source = derived.ProjectAggregateMember(source, step.binding);
			const Operand value = derived.LoadStorage(source, LowPtr());
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
			Instruction store(Instruction::STORE);
			store.type = LowPtr();
			store.first = value;
			store.second = destination;
			derived.Emit(store);
			return;
		}
		Operand destination = LoadAssignmentObject(
			derived.current_this_binding_);
		if (step.binding != kNoBinding)
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
		else if (step.base_projection_count != 0)
			destination = derived.ProjectBaseSubobjects(
				destination, step.base_projection_count);
		const TypeRecord& step_type = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(step.type));
		if (step_type.kind == TYPE_ARRAY)
		{
			LowerArraySubobjectStep(step_node, step.type,
				step.selected_binding, destination,
				construction.object_binding, step.binding, false);
			return;
		}
		Operand source = LoadAssignmentObject(construction.object_binding);
		if (step.binding != kNoBinding)
			source = derived.ProjectAggregateMember(source, step.binding);
		else if (step.base_projection_count != 0)
			source = derived.ProjectBaseSubobjects(
				source, step.base_projection_count);
		if (step.binding != kNoBinding &&
			derived.program_.bindings[step.binding].bit_field)
		{
			const Operand value = derived.LoadBitField(step.binding, source);
			(void)derived.StoreBitField(
				step.binding, destination, value, true);
			return;
		}
		LowerConstructionSubobject(step.type, step.selected_binding,
			destination, source);
	}

	Operand LowerSpecialMemberConstruction(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& construction = derived.arena_.nodes[node];
		if (construction.kind !=
				DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION ||
			construction.object_binding == kNoBinding ||
			derived.current_this_binding_ == kNoBinding)
			throw std::logic_error(
				"invalid synthesized construction action");
		const NodeChildren steps = derived.Children(node);
		for (std::size_t i = 0; i < steps.size(); ++i)
		{
			if (derived.stats_) ++derived.stats_->lowered_nodes;
			LowerConstructionStep(steps[i], construction,
				derived.arena_.nodes[steps[i]]);
		}
		return Operand(0, LowVoid());
	}

	void LowerAssignmentStep(std::uint32_t step_node,
		const DumpNode& assignment,
		const DumpNode& step)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (step.kind != DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION)
			throw std::logic_error("invalid synthesized assignment step");
		if (step.storage_size != 0)
		{
			const Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			const Operand source =
				LoadAssignmentObject(assignment.object_binding);
			Instruction copy(Instruction::COPY_OBJECT);
			copy.type = LowObject(
				static_cast<std::size_t>(step.storage_size),
				step.storage_alignment);
			copy.first = source;
			copy.second = destination;
			derived.Emit(copy);
			return;
		}
		if (step.storage_unit_transfer)
		{
			if (step.binding == kNoBinding ||
				!derived.program_.bindings[step.binding].bit_field)
				throw std::logic_error(
					"invalid synthesized storage-unit transfer");
			Operand source = LoadAssignmentObject(assignment.object_binding);
			source = derived.ProjectAggregateMember(source, step.binding);
			const LowType type = derived.LowerExpressionType(
				derived.program_.bindings[step.binding].type);
			const Operand value = derived.LoadStorage(source, type);
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
			Instruction store(Instruction::STORE);
			store.type = type;
			store.first = value;
			store.second = destination;
			derived.Emit(store);
			return;
		}
		if (step.binding == kNoBinding &&
			step.selected_binding == kNoBinding)
		{
			const Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			const Operand source =
				LoadAssignmentObject(assignment.object_binding);
			derived.EmitClassObjectCopy(step.type, source, destination);
			return;
		}

		if (step.selected_binding != kNoBinding)
		{
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			if (step.binding != kNoBinding)
				destination = derived.ProjectAggregateMember(
					destination, step.binding);
			else
				destination = derived.ProjectBaseSubobjects(destination, 1);
			const TypeRecord& step_type = derived.program_.types.Get(
				derived.program_.types.RemoveTopCv(step.type));
			if (step_type.kind == TYPE_ARRAY)
			{
				LowerArraySubobjectStep(step_node, step.type,
					step.selected_binding, destination,
					assignment.object_binding, step.binding, true);
				return;
			}
			Operand source = LoadAssignmentObject(assignment.object_binding);
			if (step.binding != kNoBinding)
				source = derived.ProjectAggregateMember(source, step.binding);
			else
				source = derived.ProjectBaseSubobjects(source, 1);
			LowerAssignmentSubobject(step.type, step.selected_binding,
				destination, source);
			return;
		}

		Operand source = LoadAssignmentObject(assignment.object_binding);
		if (step.binding != kNoBinding)
			source = derived.ProjectAggregateMember(source, step.binding);
		if (step.binding != kNoBinding &&
			derived.program_.bindings[step.binding].bit_field)
		{
			const Operand value = derived.LoadBitField(step.binding, source);
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
			(void)derived.StoreBitField(
				step.binding, destination, value, true);
			return;
		}
		if (derived.IsClassObjectType(step.type) ||
			derived.IsArrayType(step.type))
		{
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			if (step.binding != kNoBinding)
				destination = derived.ProjectAggregateMember(
					destination, step.binding);
			derived.EmitClassObjectCopy(step.type, source, destination);
			return;
		}
		const LowType type = derived.LowerExpressionType(step.type);
		const Operand value = derived.LoadStorage(source, type);
		Operand destination = LoadAssignmentObject(
			derived.current_this_binding_);
		if (step.binding != kNoBinding)
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = destination;
		derived.Emit(store);
	}

	Operand LowerSpecialMemberAssignment(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& assignment = derived.arena_.nodes[node];
		if (assignment.kind != DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION ||
			assignment.object_binding == kNoBinding ||
			derived.current_this_binding_ == kNoBinding)
			throw std::logic_error("invalid synthesized assignment action");
		const NodeChildren steps = derived.Children(node);
		for (std::size_t i = 0; i < steps.size(); ++i)
		{
			if (derived.stats_) ++derived.stats_->lowered_nodes;
			LowerAssignmentStep(steps[i], assignment,
				derived.arena_.nodes[steps[i]]);
		}
		return derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
	}
};

}
}
