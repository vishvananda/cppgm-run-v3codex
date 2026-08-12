#ifndef CPPGM_PA28_VIRTUAL_BASE_LOWERING_H
#define CPPGM_PA28_VIRTUAL_BASE_LOWERING_H

#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa28_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

struct VirtualBaseBoundaryFact
{
	BindingId binding;
	EntityId owner, virtual_base;
	ParameterId parameter;
	SlotId slot;
	bool implicit_object, direct_parameter;

	VirtualBaseBoundaryFact(BindingId binding_value, EntityId owner_value,
		EntityId virtual_base_value, ParameterId parameter_value,
		bool implicit_value, bool direct_value)
		: binding(binding_value), owner(owner_value),
		  virtual_base(virtual_base_value), parameter(parameter_value),
		  slot(kNoLowId), implicit_object(implicit_value),
		  direct_parameter(direct_value) {}
};

template <class Derived>
class VirtualBaseBoundaryShape
{
protected:
	EntityId VirtualBoundaryEntity(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord* shape = &derived.program_.types.Get(type);
		while (shape->kind == TYPE_LVALUE_REFERENCE ||
			shape->kind == TYPE_RVALUE_REFERENCE ||
			shape->kind == TYPE_QUALIFIED)
		{
			type = derived.program_.types.RemoveTopCv(shape->child);
			shape = &derived.program_.types.Get(type);
		}
		if (shape->kind == TYPE_POINTER)
		{
			type = derived.program_.types.RemoveTopCv(shape->child);
			shape = &derived.program_.types.Get(type);
		}
		return shape->kind == TYPE_NAMED ? shape->entity : kNoEntity;
	}

	bool SpillVirtualBaseBoundary(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord& shape = derived.program_.types.Get(type);
		if (shape.kind != TYPE_LVALUE_REFERENCE &&
			shape.kind != TYPE_RVALUE_REFERENCE)
			return false;
		type = derived.program_.types.RemoveTopCv(shape.child);
		return derived.program_.types.Get(type).kind == TYPE_NAMED;
	}

	bool HasImplicitObjectBoundary(const DumpNode& function) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (function.binding == kNoBinding) return false;
		const BindingRecord& binding =
			derived.program_.bindings[function.binding];
		return binding.member_owner != kNoEntity &&
			!binding.static_member_function;
	}

	bool IncludesImplicitVirtualBases(const DumpNode& function) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (!HasImplicitObjectBoundary(function)) return false;
		const BindingRecord& binding =
			derived.program_.bindings[function.binding];
		return !binding.constructor || binding.constructor_base_entry;
	}

	std::size_t HiddenVirtualBaseParameterCount(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& function = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		std::size_t count = 0;
		std::size_t ordinal = 0;
		bool saw_parameter = false;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& parameter = derived.arena_.nodes[children[i]];
			if (parameter.kind != DUMP_PARAMETER) continue;
			saw_parameter = true;
			EntityId owner = kNoEntity;
			if (member && ordinal == 0)
			{
				if (IncludesImplicitVirtualBases(function))
					owner = derived.program_.bindings[
						function.binding].member_owner;
			}
			else owner = VirtualBoundaryEntity(parameter.type);
			if (owner != kNoEntity)
				count += derived.program_.entities[owner].virtual_base_count;
			++ordinal;
		}
		if (!saw_parameter)
		{
			const TypeRecord& type = derived.program_.types.Get(function.type);
			if (type.kind != TYPE_FUNCTION) return 0;
			const TypeId* parameters =
				derived.program_.types.Parameters(function.type);
			for (std::size_t i = 0; i < type.parameter_count; ++i)
			{
				EntityId owner = kNoEntity;
				if (member && i == 0)
				{
					if (IncludesImplicitVirtualBases(function))
						owner = derived.program_.bindings[
							function.binding].member_owner;
				}
				else owner = VirtualBoundaryEntity(parameters[i]);
				if (owner != kNoEntity)
					count += derived.program_.entities[owner].virtual_base_count;
			}
		}
		return count;
	}

	std::size_t EmittedVirtualBaseParameterCount(BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding == kNoBinding || binding >= derived.program_.bindings.size())
			return 0;
		std::uint32_t node = derived.function_definition_[binding];
		if (node == kNoDumpEdge) node = derived.function_declaration_[binding];
		return node == kNoDumpEdge ? 0 : HiddenVirtualBaseParameterCount(node);
	}

	void AppendVirtualBaseBoundaryParameters(std::uint32_t node,
		std::vector<Parameter>* parameters) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& function = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		std::size_t ordinal = 0;
		std::size_t member_index = 0;
		std::size_t parameter_index = 0;
		bool saw_parameter = false;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& source = derived.arena_.nodes[children[i]];
			if (source.kind != DUMP_PARAMETER) continue;
			saw_parameter = true;
			const bool implicit = member && ordinal == 0;
			EntityId owner = implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner :
				 kNoEntity) : VirtualBoundaryEntity(source.type);
			if (owner != kNoEntity)
				for (std::size_t base = 0;
					base < derived.program_.entities[owner].virtual_base_count;
					++base)
				{
					Parameter parameter;
					parameter.name = implicit ?
						"__vbptr" + std::to_string(member_index++) :
						"__pvbptr" + std::to_string(parameter_index++);
					parameter.type = LowPtr();
					parameters->push_back(parameter);
				}
			++ordinal;
		}
		if (saw_parameter) return;
		const TypeRecord& type = derived.program_.types.Get(function.type);
		if (type.kind != TYPE_FUNCTION) return;
		const TypeId* source_parameters =
			derived.program_.types.Parameters(function.type);
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			const bool implicit = member && i == 0;
			EntityId owner = implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner :
				 kNoEntity) : VirtualBoundaryEntity(source_parameters[i]);
			if (owner == kNoEntity) continue;
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count; ++base)
			{
				Parameter parameter;
				parameter.name = implicit ?
					"__vbptr" + std::to_string(member_index++) :
					"__pvbptr" + std::to_string(parameter_index++);
				parameter.type = LowPtr();
				parameters->push_back(parameter);
			}
		}
	}

};

template <class Derived>
class VirtualBaseLowering : protected VirtualBaseBoundaryShape<Derived>
{
protected:
	using VirtualBaseBoundaryShape<Derived>::VirtualBoundaryEntity;
	using VirtualBaseBoundaryShape<Derived>::SpillVirtualBaseBoundary;
	using VirtualBaseBoundaryShape<Derived>::HasImplicitObjectBoundary;
	using VirtualBaseBoundaryShape<Derived>::IncludesImplicitVirtualBases;
	using VirtualBaseBoundaryShape<Derived>::HiddenVirtualBaseParameterCount;
	using VirtualBaseBoundaryShape<Derived>::EmittedVirtualBaseParameterCount;

	void PrepareVirtualBaseBoundary(std::uint32_t node,
		const std::vector<Parameter>& parameters)
	{
		Derived& derived = static_cast<Derived&>(*this);
		current_virtual_base_boundary_.clear();
		const std::size_t hidden = HiddenVirtualBaseParameterCount(node);
		if (hidden > parameters.size())
			throw std::logic_error("virtual-base boundary parameter mismatch");
		std::size_t boundary = parameters.size() - hidden;
		const DumpNode& function = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		std::size_t ordinal = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& source = derived.arena_.nodes[children[i]];
			if (source.kind != DUMP_PARAMETER) continue;
			const bool implicit = member && ordinal == 0;
			const bool direct = implicit || !SpillVirtualBaseBoundary(source.type);
			EntityId owner = implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner :
				 kNoEntity) : VirtualBoundaryEntity(source.type);
			if (owner != kNoEntity)
				for (std::size_t base = 0;
					base < derived.program_.entities[owner].virtual_base_count;
					++base)
				{
					const VirtualBaseLayout& layout =
						derived.program_.VirtualBase(owner, base);
					current_virtual_base_boundary_.push_back(
						VirtualBaseBoundaryFact(source.binding, owner,
							layout.entity, ParameterId(boundary++), implicit,
							direct));
				}
			++ordinal;
		}
	}

	void PlanVirtualBaseBoundarySlots(const DumpNode& parameter)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::size_t local = 0;
		for (std::size_t i = 0; i < current_virtual_base_boundary_.size(); ++i)
		{
			VirtualBaseBoundaryFact& fact = current_virtual_base_boundary_[i];
			if (fact.binding != parameter.binding || fact.direct_parameter) continue;
			Slot slot;
			const std::string name = parameter.text == 0 ? "__param" :
				derived.program_.names.Get(parameter.text);
			slot.name = derived.UniqueSlotName(
				name + "__pvb" + std::to_string(local++));
			slot.type = LowPtr();
			fact.slot = static_cast<SlotId>(derived.function_->slots.size());
			derived.function_->slots.push_back(slot);
		}
	}

	void MaterializeVirtualBaseBoundaryParameter(const DumpNode& parameter)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0; i < current_virtual_base_boundary_.size(); ++i)
		{
			const VirtualBaseBoundaryFact& fact =
				current_virtual_base_boundary_[i];
			if (fact.binding != parameter.binding || fact.direct_parameter) continue;
			if (fact.slot == kNoLowId)
				throw std::logic_error("virtual-base parameter has no slot");
			Instruction store(Instruction::STORE);
			store.type = LowPtr();
			store.first = Operand(fact.parameter, LowPtr());
			store.second = Operand(fact.slot, LowPtr());
			derived.Emit(store);
		}
	}

	BindingId BoundaryBindingForExpression(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		for (std::size_t depth = 0;
			depth <= derived.arena_.nodes.size(); ++depth)
		{
			const DumpNode& record = derived.arena_.nodes[node];
			if (record.kind == DUMP_ID_EXPRESSION &&
				record.binding != kNoBinding)
				return record.binding;
			const NodeChildren children = derived.Children(node);
			if (children.size() != 1) break;
			node = children[0];
		}
		return kNoBinding;
	}

	bool CurrentVirtualBaseAddress(BindingId binding, EntityId virtual_base,
		Operand* address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0; i < current_virtual_base_boundary_.size(); ++i)
		{
			const VirtualBaseBoundaryFact& fact =
				current_virtual_base_boundary_[i];
			if (fact.binding != binding || fact.virtual_base != virtual_base)
				continue;
			if (fact.direct_parameter)
				*address = Operand(fact.parameter, LowPtr());
			else
			{
				if (fact.slot == kNoLowId)
					throw std::logic_error("virtual-base address has no slot");
				*address = derived.LoadStorage(Operand(fact.slot, LowPtr()), LowPtr());
			}
			return true;
		}
		return false;
	}

	bool NullBoundaryExpression(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		for (std::size_t depth = 0;
			depth <= derived.arena_.nodes.size(); ++depth)
		{
			const DumpNode& record = derived.arena_.nodes[node];
			if (record.integer_literal_zero ||
				derived.source_types_.IsNullptr(record.type))
				return true;
			const NodeChildren children = derived.Children(node);
			if (children.size() != 1) break;
			node = children[0];
		}
		return false;
	}

	bool CurrentVirtualBaseAddressForExpression(std::uint32_t expression,
		EntityId virtual_base, Operand* address)
	{
		return CurrentVirtualBaseAddress(
			BoundaryBindingForExpression(expression), virtual_base, address);
	}

	bool HasCurrentImplicitVirtualBases() const
	{
		for (std::size_t i = 0; i < current_virtual_base_boundary_.size(); ++i)
			if (current_virtual_base_boundary_[i].implicit_object) return true;
		return false;
	}

	Operand RuntimeVirtualBaseAddress(const Operand& view,
		std::size_t virtual_base_ordinal)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if ((view.kind == Operand::INTEGER && view.integer_value == 0) ||
			view.kind == Operand::NULL_POINTER)
			return Operand(0, LowPtr());
		const Operand vtable = derived.LoadStorage(view, LowPtr());
		const std::int64_t row = -24 -
			static_cast<std::int64_t>(virtual_base_ordinal) * 8;
		const Operand entry = derived.IndexAddress(
			LowI8(), vtable, Operand(row, LowI64()), false);
		const Operand offset = derived.LoadStorage(entry, LowI64());
		return derived.IndexAddress(LowI8(), view, offset, false);
	}

	Operand VirtualBaseCallAddress(std::uint32_t expression,
		const Operand& view, EntityId owner, std::size_t ordinal)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const VirtualBaseLayout& layout =
			derived.program_.VirtualBase(owner, ordinal);
		Operand inherited;
		if (CurrentVirtualBaseAddressForExpression(
			expression, layout.entity, &inherited))
			return inherited;
		if (NullBoundaryExpression(expression)) return view;
		const EntityId source =
			derived.BaseEntityForType(derived.arena_.nodes[expression].type);
		if (source == owner &&
			derived.arena_.nodes[expression].kind != DUMP_CAST_EXPRESSION)
			return derived.ProjectBaseSubobjectOffset(view, layout.offset);
		return RuntimeVirtualBaseAddress(view, ordinal);
	}

	void AppendCallVirtualBaseArguments(const DumpNode& callee,
		const NodeChildren& call_children, const CallArguments& lowered,
		CallArguments* arguments, CallArgumentFlags* references)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (call_children.size() <= 1 || lowered.size() + 1 != call_children.size())
			return;
		const bool member = callee.binding != kNoBinding &&
			derived.program_.bindings[callee.binding].member_owner != kNoEntity &&
			!derived.program_.bindings[callee.binding].static_member_function;
		const BindingRecord* binding = callee.binding == kNoBinding ? 0 :
			&derived.program_.bindings[callee.binding];
		std::size_t hidden_remaining =
			EmittedVirtualBaseParameterCount(callee.binding);
		if (hidden_remaining == 0) return;
		const TypeRecord& function = derived.program_.types.Get(callee.type);
		const TypeId* parameters = derived.program_.types.Parameters(callee.type);
		for (std::size_t i = 0; i < lowered.size(); ++i)
		{
			const bool implicit = member && i == 0;
			EntityId owner = implicit ?
				((!binding->constructor || binding->constructor_base_entry) ?
				 binding->member_owner : kNoEntity) :
				(i < function.parameter_count ?
				 VirtualBoundaryEntity(parameters[i]) : kNoEntity);
			if (owner == kNoEntity) continue;
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count &&
				hidden_remaining != 0; ++base, --hidden_remaining)
			{
				arguments->Push(VirtualBaseCallAddress(
					call_children[i + 1], lowered[i], owner, base));
				references->Push(Instruction::CALL_PASS_VALUE);
			}
		}
	}

	void ResetVirtualBaseBoundary()
	{
		current_virtual_base_boundary_.clear();
	}

	std::vector<VirtualBaseBoundaryFact> current_virtual_base_boundary_;
};

}
}

#endif
