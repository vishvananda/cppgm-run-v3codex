#ifndef CPPGM_PA16_DESTRUCTOR_ACTION_LOWERING_H
#define CPPGM_PA16_DESTRUCTOR_ACTION_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "semantic/model/graph.h"

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
			throw std::runtime_error(
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
					throw std::logic_error(
						"base destructor has no forwarded virtual-base address");
			}
			else if (base_subobject && derived.current_this_binding_ != kNoBinding &&
				derived.current_member_owner_ != kNoEntity)
			{
				std::uint64_t offset = 0;
				if (!derived.program_.FindVirtualBase(
					derived.current_member_owner_, needed.entity, &offset))
					throw std::logic_error(
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
			throw std::logic_error(
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
			throw std::logic_error("destructor array extent exceeds LowIR");
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
			throw std::runtime_error("destruction of an unbounded array");
		std::size_t count = 1;
		TypeId element_type = type;
		for (;;)
		{
			const TypeRecord& array = derived.program_.types.Get(
				derived.RemoveTopQualifiers(element_type));
			if (array.kind != TYPE_ARRAY) break;
			if (array.bound == 0 || array.bound >
				std::numeric_limits<std::size_t>::max() / count)
				throw std::logic_error("invalid destructor array extent");
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
			throw std::logic_error("invalid destructor action");
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
					throw std::logic_error(
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
					throw std::logic_error("invalid destructor array extent");
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
		else if (action.complete_object_destruction)
		{
			if (derived.current_this_binding_ == kNoBinding)
				throw std::logic_error(
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
				throw std::logic_error("base destruction has no object");
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

#endif
