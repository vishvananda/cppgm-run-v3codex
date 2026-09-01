#pragma once

#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"

#include <cstdint>
#include <limits>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::support;
using namespace lowering::ir;

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
		const Operand& destination, const Operand& source,
		bool construction = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (selected == kNoBinding ||
			selected >= derived.function_symbols_.size() ||
			derived.function_symbols_[selected] == kNoLowId)
			ThrowLoweringInternal(
				"selected special-member helper has no lowering identity");
		const TypeRecord& function = derived.program_.types.Get(
			derived.program_.bindings[selected].type);
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		const BindingRecord& selected_binding =
			derived.program_.bindings[selected];
		if (construction && derived.IncludesConstructionVtt(selected))
		{
			const EntityId complete = derived.HasCurrentImplicitVirtualBases() ?
				kNoEntity : derived.current_member_owner_;
			arguments.Push(derived.ConstructionVttArgument(
				complete, selected_binding.member_owner));
			references.Push(Instruction::CALL_PASS_VALUE);
		}
		arguments.Push(source);
		references.Push(1);
		if (construction && selected_binding.constructor_base_entry &&
			selected_binding.member_owner != kNoEntity)
		{
			std::size_t hidden_remaining =
				derived.VirtualBaseParameterCount(selected, selected_binding.type);
			const EntityId owner = selected_binding.member_owner;
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count &&
				hidden_remaining != 0; ++base)
			{
				if (!derived.CarriesVirtualBase(selected, 0, base)) continue;
				const VirtualBaseLayout& needed =
					derived.program_.VirtualBase(owner, base);
				Operand address;
				if (!derived.HasCurrentImplicitVirtualBases())
				{
					std::uint64_t offset = 0;
					if (!derived.program_.FindVirtualBase(
						derived.current_member_owner_, needed.entity, &offset))
						ThrowLoweringInternal(
							"complete synthesized constructor has no virtual base");
					const Operand complete_object = derived.LoadStorage(
						derived.StorageFor(
							derived.current_this_binding_, LowPtr()), LowPtr());
					address = derived.ProjectBaseSubobjectOffset(
						complete_object, offset);
				}
				else if (!derived.CurrentVirtualBaseAddress(
					derived.current_this_binding_, needed.entity, &address))
					ThrowLoweringInternal(
						"base synthesized constructor has no virtual base");
				arguments.Push(address);
				references.Push(Instruction::CALL_PASS_VALUE);
				--hidden_remaining;
			}
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count &&
				hidden_remaining != 0; ++base)
			{
				if (!derived.CarriesVirtualBase(selected, 1, base)) continue;
				arguments.Push(derived.RuntimeVirtualBaseAddress(
					source, owner, base));
				references.Push(Instruction::CALL_PASS_VALUE);
				--hidden_remaining;
			}
			if (hidden_remaining != 0)
				ThrowLoweringInternal(
					"synthesized constructor virtual-base ABI is incomplete");
		}
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[selected],
			derived.LowerBoundaryResult(function.child));
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
			ThrowLoweringInternal(
				"array construction bypassed its retained loop recipe");
		if (selected != kNoBinding)
		{
			LowerSpecialMemberCall(selected, destination, source, true);
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
			ThrowLoweringInternal(
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
				ThrowLoweringInternal("invalid synthesized array extent");
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
			ThrowLoweringResourceLimit(
				"synthesized array extent exceeds LowIR");
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
			ThrowLoweringInternal("invalid synthesized construction step");
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
		if (step.storage_unit_transfer)
		{
			LowerBitFieldStorageUnitTransfer(
				construction.object_binding, step);
			return;
		}
		if (derived.IsReferenceType(step.type))
		{
			if (step.binding == kNoBinding || step.selected_binding != kNoBinding)
				ThrowLoweringInternal(
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
				destination, step.base_projection_count, kNoType,
				step.base_projection_offset,
				step.has_base_projection_offset);
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
				source, step.base_projection_count, kNoType,
				step.base_projection_offset,
				step.has_base_projection_offset);
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

	LowType BitFieldStorageUnitType(BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding == kNoBinding ||
			!derived.program_.bindings[binding].bit_field)
			ThrowLoweringInternal(
				"invalid synthesized storage-unit transfer");
		const BindingLayoutFact& layout = derived.program_.BindingLayout(
			derived.program_.bindings[binding]);
		switch (layout.bit_storage_bits)
		{
		case 8: return LowU8();
		case 16: return LowU16();
		case 32: return LowU32();
		case 64: return LowU64();
		default:
			ThrowLoweringInternal(
				"unsupported synthesized storage-unit width");
		}
	}

	void LowerBitFieldStorageUnitTransfer(BindingId source_object,
		const DumpNode& step)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = BitFieldStorageUnitType(step.binding);
		Operand source = LoadAssignmentObject(source_object);
		source = derived.ProjectAggregateMember(source, step.binding);
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
		if (derived.stats_)
			++derived.stats_->bit_field_storage_unit_transfers;
	}

	Operand LowerSpecialMemberConstruction(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& construction = derived.arena_.nodes[node];
		if (construction.kind !=
				DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION ||
			construction.object_binding == kNoBinding ||
			derived.current_this_binding_ == kNoBinding)
			ThrowLoweringInternal(
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
			ThrowLoweringInternal("invalid synthesized assignment step");
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
			LowerBitFieldStorageUnitTransfer(
				assignment.object_binding, step);
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
				destination = derived.ProjectBaseSubobjects(
					destination, step.base_projection_count, kNoType,
					step.base_projection_offset,
					step.has_base_projection_offset);
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
				source = derived.ProjectBaseSubobjects(
					source, step.base_projection_count, kNoType,
					step.base_projection_offset,
					step.has_base_projection_offset);
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
			ThrowLoweringInternal("invalid synthesized assignment action");
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

template <class Derived>
class DestructorActionLowering
{
protected:
	static const std::size_t kDestructorArrayInlineLimit = 8;

	void EmitEhTarget(Instruction::Kind kind, BlockId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction instruction(kind);
		instruction.target = target;
		derived.Emit(instruction);
	}

	Operand ProjectBaseSubobjects(Operand object,
		std::uint32_t projection_count, TypeId source_type = kNoType)
	{
		Derived& derived = static_cast<Derived&>(*this);
		EntityId entity = derived.BaseEntityForType(source_type);
		std::uint64_t offset = 0;
		for (std::uint32_t i = 0; i < projection_count; ++i)
		{
			if (entity != kNoEntity)
			{
				offset += derived.program_.entities[entity].direct_base_offset;
				entity = derived.program_.entities[entity].direct_base;
			}
		}
		return projection_count == 0 ? object :
			derived.ProjectBaseSubobjectOffset(object, offset);
	}

	Operand ProjectBaseSubobjects(Operand object,
		std::uint32_t projection_count, TypeId source_type,
		std::uint64_t projection_offset, bool has_projection_offset)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!has_projection_offset)
			return ProjectBaseSubobjects(object, projection_count, source_type);
		return projection_count == 0 ? object :
			derived.ProjectBaseSubobjectOffset(object, projection_offset);
	}

	void EmitDestructorCall(BindingId destructor, const Operand& destination,
		bool base_subobject = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (destructor == kNoBinding ||
			destructor >= derived.function_symbols_.size() ||
			derived.function_symbols_[destructor] == kNoLowId)
			ThrowLoweringInternal(
				"destructor action has no emitted binding: " +
				std::to_string(destructor) + " " +
				(destructor < derived.program_.bindings.size() ?
				 derived.program_.names.Get(
					derived.program_.bindings[destructor].name) :
				 std::string("<invalid>")) + " in " +
				(derived.function_ &&
				 derived.function_->symbol < derived.output_.symbols.size() ?
				 derived.output_.strings.get(derived.output_.symbols[
					derived.function_->symbol].name) :
				 std::string("<synthetic>")));
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		const BindingRecord& binding = derived.program_.bindings[destructor];
		const EntityId owner = binding.member_owner;
		if (derived.IncludesConstructionVtt(destructor))
		{
			arguments.Push(derived.ConstructionVttArgument(
				derived.HasCurrentImplicitVirtualBases() ? kNoEntity :
					derived.current_member_owner_, owner));
			references.Push(Instruction::CALL_PASS_VALUE);
		}
		std::size_t hidden = derived.VirtualBaseParameterCount(
			destructor, binding.type);
		if (hidden == 0 && owner != kNoEntity &&
			derived.IncludesImplicitVirtualBases(destructor))
			hidden = derived.program_.entities[owner].virtual_base_count;
		for (std::size_t base = 0; owner != kNoEntity &&
			base < derived.program_.entities[owner].virtual_base_count &&
			hidden != 0; ++base)
		{
			if (!derived.CarriesVirtualBase(destructor, 0, base)) continue;
			const VirtualBaseLayout& needed =
				derived.program_.VirtualBase(owner, base);
			Operand address;
			if (base_subobject && derived.HasCurrentImplicitVirtualBases())
			{
				if (derived.current_this_binding_ == kNoBinding ||
					!derived.CurrentVirtualBaseAddress(
						derived.current_this_binding_, needed.entity, &address))
					ThrowLoweringInternal(
						"base destructor has no forwarded virtual-base address");
			}
			else if (base_subobject && derived.current_this_binding_ != kNoBinding &&
				derived.current_member_owner_ != kNoEntity)
			{
				std::uint64_t offset = 0;
				if (!derived.program_.FindVirtualBase(
					derived.current_member_owner_, needed.entity, &offset))
					ThrowLoweringInternal(
						"complete destructor has no virtual-base address");
				const Operand complete = derived.LoadStorage(derived.StorageFor(
					derived.current_this_binding_, LowPtr()), LowPtr());
				address = derived.ProjectBaseSubobjectOffset(complete, offset);
			}
			else address = derived.ProjectBaseSubobjectOffset(
				destination, needed.offset);
			arguments.Push(address);
			references.Push(0);
			if (derived.stats_)
				++derived.stats_->virtual_base_call_arguments;
			--hidden;
		}
		if (hidden != 0)
			ThrowLoweringInternal(
				"destructor call has an incomplete virtual-base contract");
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[destructor], LowVoid());
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
	}

	Operand FlatDestructorArrayElement(TypeId element_type,
		const Operand& address, const Operand& index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand displacement = index;
		const std::size_t element_size = derived.program_.SizeOf(element_type);
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
		return derived.IndexAddress(LowI8(), derived.DecayAddress(address),
			displacement, true);
	}

	Operand DecrementDestructorArrayProgress(const Operand& progress,
		const Operand& remaining)
	{
		Derived& derived = static_cast<Derived&>(*this);
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
		save.second = progress;
		derived.Emit(save);
		return previous;
	}

	void LowerLoopDestructorArray(TypeId element_type,
		const Operand& address, BindingId destructor, std::size_t count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (count > static_cast<std::size_t>(
			std::numeric_limits<std::int64_t>::max()))
			ThrowLoweringResourceLimit(
				"destructor array extent exceeds LowIR");
		const SlotId progress_id = static_cast<SlotId>(
			derived.function_->slots.size());
		Slot progress_slot;
		progress_slot.name = InternLocalName(derived.output_,
			derived.GeneratedSlotName("destructor_array_index"));
		progress_slot.type = LowI64();
		derived.function_->slots.push_back(progress_slot);
		const Operand progress(progress_id, LowI64());
		const BlockId condition = derived.AddBlock(
			derived.NewLabel("destructor_array_cond"));
		const BlockId body = derived.AddBlock(
			derived.NewLabel("destructor_array_body"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("destructor_array_end"));
		const bool cleanup_needed =
			!derived.program_.bindings[destructor].nonthrowing;
		const BlockId cleanup = cleanup_needed ? derived.AddBlock(
			derived.NewLabel("destructor_array_cleanup")) : BlockId(kNoLowId);
		const BlockId cleanup_body = cleanup_needed ? derived.AddBlock(
			derived.NewLabel("destructor_array_cleanup_body")) :
			BlockId(kNoLowId);
		const BlockId resume = cleanup_needed ? derived.AddBlock(
			derived.NewLabel("destructor_array_resume")) : BlockId(kNoLowId);
		Instruction initialize(Instruction::STORE);
		initialize.type = LowI64();
		initialize.first = Operand(static_cast<std::int64_t>(count), LowI64());
		initialize.second = progress;
		derived.Emit(initialize);
		derived.EmitJump(condition);
		derived.SelectBlock(condition);
		const Operand remaining = derived.LoadStorage(progress, LowI64());
		const Operand any = derived.Temp(LowI64());
		Instruction nonzero(Instruction::CMP);
		nonzero.dest = any.id;
		nonzero.op = LOW_OP_NE;
		nonzero.type = LowI64();
		nonzero.first = remaining;
		nonzero.second = Operand(0, LowI64());
		derived.Emit(nonzero);
		derived.EmitBranch(any, body, end);
		derived.SelectBlock(body);
		const Operand previous =
			DecrementDestructorArrayProgress(progress, remaining);
		if (cleanup_needed)
			derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		derived.EmitDestructorCall(destructor,
			FlatDestructorArrayElement(element_type, address, previous));
		if (cleanup_needed) derived.Emit(Instruction(Instruction::EH_END));
		derived.EmitJump(condition);
		if (cleanup_needed)
		{
			derived.SelectBlock(cleanup);
			const Operand cleanup_remaining =
				derived.LoadStorage(progress, LowI64());
			const Operand cleanup_any = derived.Temp(LowI64());
			Instruction cleanup_nonzero(Instruction::CMP);
			cleanup_nonzero.dest = cleanup_any.id;
			cleanup_nonzero.op = LOW_OP_NE;
			cleanup_nonzero.type = LowI64();
			cleanup_nonzero.first = cleanup_remaining;
			cleanup_nonzero.second = Operand(0, LowI64());
			derived.Emit(cleanup_nonzero);
			derived.EmitBranch(cleanup_any, cleanup_body, resume);
			derived.SelectBlock(cleanup_body);
			const Operand cleanup_previous =
				DecrementDestructorArrayProgress(progress, cleanup_remaining);
			derived.EmitDestructorCall(destructor,
				FlatDestructorArrayElement(
					element_type, address, cleanup_previous));
			derived.EmitJump(cleanup);
			derived.SelectBlock(resume);
			derived.EmitExceptionResume();
		}
		derived.SelectBlock(end);
	}

	void LowerDestructorObject(TypeId type, const Operand& address,
		BindingId destructor, bool base_subobject = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		type = derived.RemoveTopQualifiers(type);
		const TypeRecord& record = derived.program_.types.Get(type);
		if (record.kind != TYPE_ARRAY)
		{
			EmitDestructorCall(destructor, address, base_subobject);
			return;
		}
		if (record.bound == 0)
			ThrowLoweringSource("destruction of an unbounded array");
		std::size_t count = 1;
		TypeId element_type = type;
		for (;;)
		{
			const TypeRecord& array = derived.program_.types.Get(
				derived.RemoveTopQualifiers(element_type));
			if (array.kind != TYPE_ARRAY) break;
			if (array.bound == 0 || array.bound >
				std::numeric_limits<std::size_t>::max() / count)
				ThrowLoweringInternal("invalid destructor array extent");
			count *= static_cast<std::size_t>(array.bound);
			element_type = array.child;
		}
		if (count > kDestructorArrayInlineLimit)
		{
			LowerLoopDestructorArray(element_type, address, destructor, count);
			return;
		}
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
			ThrowLoweringInternal("invalid destructor action");
		if (derived.LowerInitializerListTemporaryDestructor(action)) return;
		const TypeRecord& outer = derived.program_.types.Get(
			derived.RemoveTopQualifiers(action.operand_type));
		if (outer.kind == TYPE_ARRAY && action.object_binding != kNoBinding)
		{
			if (action.constant)
			{
				if (action.constant_value < 0 ||
					static_cast<std::uint64_t>(action.constant_value) >=
						outer.bound)
					ThrowLoweringInternal(
						"invalid destructor array element identity");
				const Operand element = derived.BoundArrayElementAddress(
					action.object_binding, action.operand_type,
					static_cast<std::size_t>(action.constant_value));
				LowerDestructorObject(outer.child, element, action.binding);
				return;
			}
			std::size_t count = 1;
			TypeId element_type = action.operand_type;
			for (;;)
			{
				const TypeRecord& array = derived.program_.types.Get(
					derived.RemoveTopQualifiers(element_type));
				if (array.kind != TYPE_ARRAY) break;
				if (array.bound == 0 || array.bound >
					std::numeric_limits<std::size_t>::max() / count)
					ThrowLoweringInternal("invalid destructor array extent");
				count *= static_cast<std::size_t>(array.bound);
				element_type = array.child;
			}
			if (count <= kDestructorArrayInlineLimit)
			{
				for (std::size_t ordinal = 0;
					ordinal < static_cast<std::size_t>(outer.bound); ++ordinal)
				{
					const std::size_t index =
						static_cast<std::size_t>(outer.bound) - ordinal - 1;
					const Operand element = derived.BoundArrayElementAddress(
						action.object_binding, action.operand_type, index);
					LowerDestructorObject(outer.child, element, action.binding);
				}
				return;
			}
		}
		Operand destination;
		bool base_subobject = false;
		if (action.lifetime_object != kNoDumpEdge)
			destination = derived.AddressOfStorage(
				derived.LowerStorage(action.lifetime_object));
		else if (action.object_binding != kNoBinding)
		{
			const BindingRecord& object =
				derived.program_.bindings[action.object_binding];
			if (object.non_static_data_member)
			{
				if (derived.current_this_binding_ == kNoBinding)
					ThrowLoweringInternal(
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
		else if (action.complete_object_destruction)
		{
			if (derived.current_this_binding_ == kNoBinding)
				ThrowLoweringInternal(
					"complete-object destruction is outside a member function");
			destination = derived.LoadStorage(derived.StorageFor(
				derived.current_this_binding_, LowPtr()), LowPtr());
			base_subobject = derived.program_.bindings[action.binding].
				destructor_base_entry;
		}
		else
		{
			if (derived.current_this_binding_ == kNoBinding ||
				action.base_projection_count == 0)
				ThrowLoweringInternal("base destruction has no object");
			destination = derived.LoadStorage(derived.StorageFor(
				derived.current_this_binding_, LowPtr()), LowPtr());
			destination = derived.ProjectBaseSubobjects(destination,
				action.base_projection_count, kNoType,
				action.base_projection_offset,
				action.has_base_projection_offset);
			base_subobject = true;
		}
		LowerDestructorObject(action.operand_type, destination, action.binding,
			base_subobject);
	}
};

}
}
