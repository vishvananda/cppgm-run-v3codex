#ifndef CPPGM_LOWERING_OBJECTS_VIRTUAL_BASES_H
#define CPPGM_LOWERING_OBJECTS_VIRTUAL_BASES_H

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/presentation/local_names.h"
#include "lowering/ir/model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

struct VirtualBaseBoundaryFact
{
	BindingId binding;
	EntityId owner, virtual_base;
	std::uint64_t static_offset;
	ParameterId parameter;
	SlotId slot;
	bool implicit_object, direct_parameter, static_source;

	VirtualBaseBoundaryFact(BindingId binding_value, EntityId owner_value,
		EntityId virtual_base_value, std::uint64_t static_offset_value,
		ParameterId parameter_value,
		bool implicit_value, bool direct_value, bool static_source_value)
		: binding(binding_value), owner(owner_value),
		  virtual_base(virtual_base_value), static_offset(static_offset_value),
		  parameter(parameter_value),
		  slot(kNoLowId), implicit_object(implicit_value),
		  direct_parameter(direct_value), static_source(static_source_value) {}
};

struct VirtualBaseParameterContract
{
	std::uint32_t parameter_ordinal;
	std::uint32_t carried_begin, carried_count;

	VirtualBaseParameterContract(std::uint32_t parameter_value = 0,
		std::uint32_t begin_value = 0, std::uint32_t count_value = 0)
		: parameter_ordinal(parameter_value), carried_begin(begin_value),
		  carried_count(count_value) {}
};

template <class Derived>
class VirtualBaseContractLookup
{
protected:
	const VirtualBaseParameterContract* VirtualBaseContract(
		BindingId binding, std::size_t parameter) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding == kNoBinding ||
			binding >= derived.virtual_base_contracts_.begins.size()) return 0;
		const std::uint32_t begin =
			derived.virtual_base_contracts_.begins[binding];
		if (begin == kNoDumpEdge) return 0;
		const std::uint32_t count =
			derived.virtual_base_contracts_.counts[binding];
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const VirtualBaseParameterContract& contract =
				derived.virtual_base_contracts_.parameters[begin + i];
			if (contract.parameter_ordinal == parameter) return &contract;
		}
		return 0;
	}

	bool CarriesVirtualBase(BindingId binding, std::size_t parameter,
		std::size_t virtual_base) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const VirtualBaseParameterContract* contract =
			VirtualBaseContract(binding, parameter);
		if (!contract) return true;
		for (std::uint32_t i = 0; i < contract->carried_count; ++i)
			if (derived.virtual_base_contracts_.ordinals[
				contract->carried_begin + i] == virtual_base) return true;
		return false;
	}
};

struct VirtualBaseContractState
{
	std::vector<std::size_t> parameter_counts;
	std::vector<std::uint32_t> begins, counts;
	std::vector<VirtualBaseParameterContract> parameters;
	std::vector<std::uint32_t> ordinals, scan_index;
	std::vector<BindingId> expression_bindings;
	std::vector<std::uint8_t> expression_binding_known;
	std::vector<std::uint32_t> expression_binding_scratch;
	std::size_t expression_node_count;

	VirtualBaseContractState() : expression_node_count(0) {}

	void Reset(std::size_t binding_count, std::size_t node_count)
	{
		parameter_counts.assign(binding_count,
			std::numeric_limits<std::size_t>::max());
		begins.assign(binding_count, kNoDumpEdge);
		counts.assign(binding_count, 0);
		scan_index.assign(binding_count, kNoDumpEdge);
		expression_bindings.clear();
		expression_binding_known.clear();
		expression_binding_scratch.clear();
		expression_node_count = node_count;
	}
};

template <class Derived>
class VirtualBaseBoundaryShape : protected VirtualBaseContractLookup<Derived>
{
protected:
	using VirtualBaseContractLookup<Derived>::CarriesVirtualBase;
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
		return !binding.virtual_function &&
			(!binding.constructor || binding.constructor_base_entry) &&
			(!binding.destructor || binding.destructor_base_entry);
	}

	bool IncludesImplicitVirtualBases(BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding == kNoBinding || binding >= derived.program_.bindings.size())
			return false;
		const BindingRecord& function = derived.program_.bindings[binding];
		return function.member_owner != kNoEntity &&
			!function.static_member_function && !function.virtual_function &&
			(!function.constructor || function.constructor_base_entry) &&
			(!function.destructor || function.destructor_base_entry);
	}

	bool IncludesConstructionVtt(BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding == kNoBinding || binding >= derived.program_.bindings.size())
			return false;
		const BindingRecord& function = derived.program_.bindings[binding];
		if (!((function.constructor && function.constructor_base_entry) ||
			  (function.destructor && function.destructor_base_entry)) ||
			function.member_owner == kNoEntity ||
			function.member_owner >= derived.graph_.class_polymorphism.size() ||
			derived.program_.entities[function.member_owner].virtual_base_count == 0)
			return false;
		const ClassPolymorphismFacts& facts =
			derived.graph_.class_polymorphism[function.member_owner];
		return !facts.slots.empty() || !facts.views.empty();
	}

	bool OmitsCopySourceVirtualBases(BindingId binding, TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding == kNoBinding || binding >= derived.program_.bindings.size())
			return false;
		const BindingRecord& constructor = derived.program_.bindings[binding];
		const TypeRecord& function = derived.program_.types.Get(type);
		if (!constructor.constructor_base_entry ||
			constructor.member_owner == kNoEntity ||
			function.kind != TYPE_FUNCTION || function.parameter_count < 2)
			return false;
		return VirtualBoundaryEntity(
			derived.program_.types.Parameters(type)[1]) == constructor.member_owner;
	}

	std::size_t HiddenVirtualBaseParameterCount(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& function = derived.arena_.nodes[node];
		if (function.binding != kNoBinding &&
			function.binding < derived.virtual_base_contracts_.parameter_counts.size() &&
			derived.virtual_base_contracts_.parameter_counts[function.binding] !=
				std::numeric_limits<std::size_t>::max())
			return derived.virtual_base_contracts_.parameter_counts[function.binding];
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		const bool omit_copy_source =
			OmitsCopySourceVirtualBases(function.binding, function.type);
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
			else if (!(omit_copy_source && ordinal == 1))
				owner = VirtualBoundaryEntity(parameter.type);
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
				else if (!(omit_copy_source && i == 1))
					owner = VirtualBoundaryEntity(parameters[i]);
				if (owner != kNoEntity)
					count += derived.program_.entities[owner].virtual_base_count;
			}
		}
		return count;
	}

	void AppendVirtualBaseBoundaryParameters(std::uint32_t node,
		std::vector<Parameter>* parameters) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& function = derived.arena_.nodes[node];
		if (IncludesConstructionVtt(function.binding))
		{
			Parameter vtt;
			vtt.name = InternLocalName(derived.output_, "__vtt");
			vtt.type = LowPtr();
			parameters->insert(parameters->begin() + 1, vtt);
		}
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		const bool omit_copy_source =
			OmitsCopySourceVirtualBases(function.binding, function.type);
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
			EntityId owner = omit_copy_source && ordinal == 1 ? kNoEntity : implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner :
				 kNoEntity) : VirtualBoundaryEntity(source.type);
			if (owner != kNoEntity)
				for (std::size_t base = 0;
					base < derived.program_.entities[owner].virtual_base_count;
					++base)
				{
					if (!CarriesVirtualBase(
						function.binding, ordinal, base)) continue;
					Parameter parameter;
					parameter.name = implicit ?
						lowering::presentation::InternOrdinalName(derived.output_,
							"__vbptr", 7, static_cast<std::uint32_t>(member_index++)) :
						lowering::presentation::InternOrdinalName(derived.output_,
							"__pvbptr", 8, static_cast<std::uint32_t>(parameter_index++));
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
			EntityId owner = omit_copy_source && i == 1 ? kNoEntity : implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner :
				 kNoEntity) : VirtualBoundaryEntity(source_parameters[i]);
			if (owner == kNoEntity) continue;
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count; ++base)
			{
				if (!CarriesVirtualBase(function.binding, i, base)) continue;
				Parameter parameter;
				parameter.name = implicit ?
					lowering::presentation::InternOrdinalName(derived.output_,
						"__vbptr", 7, static_cast<std::uint32_t>(member_index++)) :
					lowering::presentation::InternOrdinalName(derived.output_,
						"__pvbptr", 8, static_cast<std::uint32_t>(parameter_index++));
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
	using VirtualBaseBoundaryShape<Derived>::IncludesConstructionVtt;
	using VirtualBaseBoundaryShape<Derived>::OmitsCopySourceVirtualBases;
	using VirtualBaseBoundaryShape<Derived>::CarriesVirtualBase;
	using VirtualBaseBoundaryShape<Derived>::HiddenVirtualBaseParameterCount;

	std::size_t CountVirtualBaseParameters(TypeId type,
		BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const TypeRecord* function = &derived.program_.types.Get(type);
		if (function->kind == TYPE_POINTER || function->kind == TYPE_MEMBER_POINTER)
		{
			type = function->child;
			function = &derived.program_.types.Get(type);
		}
		if (function->kind != TYPE_FUNCTION) return 0;
		const TypeId* parameters = derived.program_.types.Parameters(type);
		const bool member = binding != kNoBinding &&
			binding < derived.program_.bindings.size() &&
			derived.program_.bindings[binding].member_owner != kNoEntity &&
			!derived.program_.bindings[binding].static_member_function;
		const bool omit_copy_source =
			OmitsCopySourceVirtualBases(binding, type);
		std::size_t count = 0;
		for (std::size_t parameter = 0;
			parameter < function->parameter_count; ++parameter)
		{
			const EntityId owner = omit_copy_source && parameter == 1 ? kNoEntity :
				member && parameter == 0 ?
				(IncludesImplicitVirtualBases(binding) ?
				 derived.program_.bindings[binding].member_owner : kNoEntity) :
				VirtualBoundaryEntity(parameters[parameter]);
			if (owner != kNoEntity)
				count += derived.program_.entities[owner].virtual_base_count;
		}
		return count;
	}

	void RecordVirtualBaseDemand(std::size_t parameter, EntityId owner,
		EntityId target, std::vector<std::vector<std::uint32_t> >* demanded)
	{
		Derived& derived = static_cast<Derived&>(*this);
		EntityId anchor = kNoEntity;
		std::uint64_t relative_offset = 0;
		if (!VirtualBasePathAnchor(
			owner, target, &anchor, &relative_offset)) return;
		(void)relative_offset;
		const EntityRecord& record = derived.program_.entities[owner];
		for (std::size_t ordinal = 0;
			ordinal < record.virtual_base_count; ++ordinal)
		{
			if (derived.program_.VirtualBase(owner, ordinal).entity != anchor)
				continue;
			std::vector<std::uint32_t>& uses = (*demanded)[parameter];
			if (std::find(uses.begin(), uses.end(), ordinal) == uses.end())
				uses.push_back(static_cast<std::uint32_t>(ordinal));
			return;
		}
	}

	void ScanVirtualBaseBoundaryUses(std::uint32_t node,
		const std::vector<EntityId>& owners, std::vector<std::uint8_t>* forwarded,
		std::vector<std::vector<std::uint32_t> >* demanded)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			if (derived.stats_)
				++derived.stats_->virtual_base_boundary_scan_nodes;
			const DumpNode& record = derived.arena_.nodes[current];
			const NodeChildren children = derived.Children(current);
			if (record.kind == DUMP_CALL_EXPRESSION ||
				record.kind == DUMP_CONSTRUCTOR_ACTION)
			{
				const std::size_t first =
					record.kind == DUMP_CALL_EXPRESSION ? 1 : 0;
				for (std::size_t i = first; i < children.size(); ++i)
				{
					const BindingId binding =
						BoundaryBindingForExpression(children[i]);
					if (binding == kNoBinding ||
						binding >= derived.virtual_base_contracts_.scan_index.size())
						continue;
					const std::uint32_t parameter =
						derived.virtual_base_contracts_.scan_index[binding];
					if (parameter != kNoDumpEdge) (*forwarded)[parameter] = 1;
				}
			}
			if (record.kind == DUMP_MEMBER_EXPRESSION &&
				record.binding != kNoBinding && !children.empty())
			{
				const BindingId binding =
					BoundaryBindingForExpression(children[0]);
				if (binding != kNoBinding &&
					binding < derived.virtual_base_contracts_.scan_index.size())
				{
					const std::uint32_t parameter =
						derived.virtual_base_contracts_.scan_index[binding];
					if (parameter != kNoDumpEdge && owners[parameter] != kNoEntity)
						RecordVirtualBaseDemand(parameter, owners[parameter],
							derived.program_.bindings[record.binding].member_owner,
							demanded);
				}
			}
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	void CacheVirtualBaseBoundary(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& function = derived.arena_.nodes[node];
		if (function.binding == kNoBinding ||
			function.binding >= derived.virtual_base_contracts_.parameter_counts.size()) return;
		std::size_t& cached =
			derived.virtual_base_contracts_.parameter_counts[function.binding];
		if (cached != std::numeric_limits<std::size_t>::max()) return;
		if (function.kind != DUMP_FUNCTION_DEFINITION) return;
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		std::vector<BindingId> bindings;
		std::vector<EntityId> owners;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& parameter = derived.arena_.nodes[children[i]];
			if (parameter.kind != DUMP_PARAMETER) continue;
			const bool implicit = member && bindings.empty();
			bindings.push_back(parameter.binding);
			owners.push_back(implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner : kNoEntity) :
				VirtualBoundaryEntity(parameter.type));
		}
		std::vector<std::uint8_t> forwarded(bindings.size(), 0);
		std::vector<std::vector<std::uint32_t> > demanded(bindings.size());
		bool has_virtual_boundary = false;
		for (std::size_t i = 0; i < owners.size(); ++i)
			has_virtual_boundary = has_virtual_boundary ||
				(owners[i] != kNoEntity &&
				 derived.program_.entities[owners[i]].virtual_base_count != 0);
		derived.virtual_base_contracts_.begins[function.binding] =
			static_cast<std::uint32_t>(
				derived.virtual_base_contracts_.parameters.size());
		if (!has_virtual_boundary)
		{
			derived.virtual_base_contracts_.counts[function.binding] = 0;
			cached = 0;
			return;
		}
		for (std::size_t i = 0; i < bindings.size(); ++i)
			if (bindings[i] != kNoBinding)
				derived.virtual_base_contracts_.scan_index[bindings[i]] =
					static_cast<std::uint32_t>(i);
		ScanVirtualBaseBoundaryUses(node, owners, &forwarded, &demanded);
		for (std::size_t i = 0; i < bindings.size(); ++i)
			if (bindings[i] != kNoBinding)
				derived.virtual_base_contracts_.scan_index[bindings[i]] = kNoDumpEdge;
		const bool omit_copy_source =
			OmitsCopySourceVirtualBases(function.binding, function.type);
		cached = 0;
		for (std::size_t parameter = 0; parameter < owners.size(); ++parameter)
		{
			if (owners[parameter] == kNoEntity) continue;
			const std::uint32_t ordinal_begin = static_cast<std::uint32_t>(
				derived.virtual_base_contracts_.ordinals.size());
			const std::size_t available =
				derived.program_.entities[owners[parameter]].virtual_base_count;
			const bool carry_all = !(omit_copy_source && parameter == 1) &&
				((member && parameter == 0) || forwarded[parameter] ||
				 demanded[parameter].empty());
			if (carry_all)
				for (std::size_t base = 0; base < available; ++base)
					derived.virtual_base_contracts_.ordinals.push_back(
						static_cast<std::uint32_t>(base));
			else if (!(omit_copy_source && parameter == 1))
				derived.virtual_base_contracts_.ordinals.insert(
					derived.virtual_base_contracts_.ordinals.end(),
					demanded[parameter].begin(), demanded[parameter].end());
			const std::uint32_t carried = static_cast<std::uint32_t>(
				derived.virtual_base_contracts_.ordinals.size() - ordinal_begin);
			derived.virtual_base_contracts_.parameters.push_back(
				VirtualBaseParameterContract(static_cast<std::uint32_t>(parameter),
					ordinal_begin, carried));
			cached += carried;
		}
		derived.virtual_base_contracts_.counts[function.binding] =
			static_cast<std::uint32_t>(
				derived.virtual_base_contracts_.parameters.size() -
				derived.virtual_base_contracts_.begins[function.binding]);
		if (derived.stats_)
			derived.stats_->virtual_base_boundary_facts += cached;
	}

	std::size_t VirtualBaseParameterCount(BindingId binding, TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (binding != kNoBinding &&
			binding < derived.virtual_base_contracts_.parameter_counts.size() &&
			derived.virtual_base_contracts_.parameter_counts[binding] !=
				std::numeric_limits<std::size_t>::max())
			return derived.virtual_base_contracts_.parameter_counts[binding];
		return CountVirtualBaseParameters(type, binding);
	}

	void PrepareVirtualBaseBoundary(std::uint32_t node,
		const std::vector<Parameter>& parameters)
	{
		Derived& derived = static_cast<Derived&>(*this);
		current_virtual_base_boundary_.clear();
		current_construction_vtt_ = kNoLowId;
		const DumpNode& function = derived.arena_.nodes[node];
		const std::size_t hidden = HiddenVirtualBaseParameterCount(node);
		if (hidden > parameters.size())
			ThrowLoweringInternal("virtual-base boundary parameter mismatch: " +
				derived.program_.names.Get(function.text));
		std::size_t boundary = parameters.size() - hidden;
		const bool omit_copy_source =
			OmitsCopySourceVirtualBases(function.binding, function.type);
		if (IncludesConstructionVtt(function.binding))
			current_construction_vtt_ = 1;
		const NodeChildren children = derived.Children(node);
		const bool member = HasImplicitObjectBoundary(function);
		std::size_t ordinal = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& source = derived.arena_.nodes[children[i]];
			if (source.kind != DUMP_PARAMETER) continue;
			const bool implicit = member && ordinal == 0;
			const bool spilled = SpillVirtualBaseBoundary(source.type);
			const std::size_t source_parameter = ordinal +
				(derived.current_indirect_result_ ? 1 : 0) +
				(HasCurrentConstructionVtt() && ordinal != 0 ? 1 : 0);
			EntityId owner = implicit ?
				(IncludesImplicitVirtualBases(function) ?
				 derived.program_.bindings[function.binding].member_owner :
				 kNoEntity) : VirtualBoundaryEntity(source.type);
			if (owner != kNoEntity)
				for (std::size_t base = 0;
					base < derived.program_.entities[owner].virtual_base_count;
					++base)
				{
					const bool carried = CarriesVirtualBase(
						function.binding, ordinal, base);
					const bool static_source =
						(omit_copy_source && ordinal == 1) || (!carried && spilled);
					if (!carried && !static_source) continue;
					const bool direct = carried && (implicit || !spilled);
					const VirtualBaseLayout& layout =
						derived.program_.VirtualBase(owner, base);
					current_virtual_base_boundary_.push_back(
						VirtualBaseBoundaryFact(source.binding, owner,
							layout.entity, layout.offset,
							static_source ? ParameterId(source_parameter) :
								ParameterId(boundary++), implicit,
							direct, static_source));
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
			if (derived.output_.retain_local_names)
			{
				const std::string name = parameter.text == 0 ? "__param" :
					derived.program_.names.Get(parameter.text);
				slot.name = InternLocalName(derived.output_, derived.UniqueSlotName(
					name + "__pvb" + std::to_string(local++)));
			}
			slot.type = LowPtr();
			fact.slot = static_cast<SlotId>(derived.function_->slots.size());
			derived.function_->slots.push_back(slot);
		}
	}

	void MaterializeVirtualBaseBoundaryFact(
		const VirtualBaseBoundaryFact& fact)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (fact.slot == kNoLowId)
			ThrowLoweringInternal("virtual-base parameter has no slot");
		Instruction store(Instruction::STORE);
		store.type = LowPtr();
		if (fact.static_source)
		{
			const Operand source(fact.parameter, LowPtr());
			store.first = derived.ProjectBaseSubobjectOffset(
				source, fact.static_offset);
		}
		else store.first = Operand(fact.parameter, LowPtr());
		store.second = Operand(fact.slot, LowPtr());
		derived.Emit(store);
	}

	void MaterializeVirtualBaseBoundaryParameter(const DumpNode& parameter)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t phase = 0; phase != 2; ++phase)
		for (std::size_t i = 0; i < current_virtual_base_boundary_.size(); ++i)
		{
			const VirtualBaseBoundaryFact& fact =
				current_virtual_base_boundary_[i];
			if (fact.binding != parameter.binding || fact.direct_parameter) continue;
			const bool has_nested_virtual_bases =
				derived.program_.entities[fact.virtual_base].virtual_base_count != 0;
			if (has_nested_virtual_bases != (phase != 0)) continue;
			MaterializeVirtualBaseBoundaryFact(fact);
		}
	}

	BindingId BoundaryBindingForExpression(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		VirtualBaseContractState& state = derived.virtual_base_contracts_;
		if (node >= state.expression_node_count)
			ThrowLoweringInternal("virtual-base expression is out of range");
		if (state.expression_bindings.empty())
		{
			state.expression_bindings.assign(
				state.expression_node_count, kNoBinding);
			state.expression_binding_known.assign(
				state.expression_node_count, 0);
			if (derived.stats_)
				derived.stats_->virtual_base_boundary_binding_table_growth +=
					state.expression_node_count;
		}
		if (state.expression_binding_known[node])
		{
			if (derived.stats_)
				++derived.stats_->virtual_base_boundary_binding_cache_hits;
			return state.expression_bindings[node];
		}
		state.expression_binding_scratch.clear();
		BindingId result = kNoBinding;
		std::uint32_t current = node;
		for (std::size_t depth = 0;
			depth <= derived.arena_.nodes.size(); ++depth)
		{
			if (state.expression_binding_known[current])
			{
				if (derived.stats_)
					++derived.stats_->virtual_base_boundary_binding_cache_hits;
				result = state.expression_bindings[current];
				break;
			}
			state.expression_binding_scratch.push_back(current);
			if (derived.stats_)
				++derived.stats_->virtual_base_boundary_binding_steps;
			const DumpNode& record = derived.arena_.nodes[current];
			if (record.kind == DUMP_ID_EXPRESSION &&
				record.binding != kNoBinding)
			{
				result = record.binding;
				break;
			}
			const NodeChildren children = derived.Children(current);
			if (children.size() != 1) break;
			current = children[0];
			if (current >= state.expression_node_count)
				ThrowLoweringInternal(
					"virtual-base expression edge is out of range");
		}
		for (std::size_t i = 0;
			i < state.expression_binding_scratch.size(); ++i)
		{
			const std::uint32_t cached = state.expression_binding_scratch[i];
			state.expression_bindings[cached] = result;
			state.expression_binding_known[cached] = 1;
		}
		return result;
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
					ThrowLoweringInternal("virtual-base address has no slot");
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

	bool VirtualBasePathAnchor(EntityId owner, EntityId target,
		EntityId* anchor, std::uint64_t* relative_offset) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		std::vector<std::uint32_t> path;
		if (!derived.program_.QueryBasePath(
			owner, target, 0, 0, 0, 0, &path)) return false;
		EntityId current = owner;
		bool found = false;
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			const DirectBaseEdge& edge =
				derived.program_.DirectBase(current, path[i]);
			current = edge.entity;
			if (edge.virtual_base)
			{
				*anchor = current;
				*relative_offset = 0;
				found = true;
				if (current == target || derived.program_.FindVirtualBase(
					current, target, relative_offset)) return true;
			}
			else if (found) *relative_offset += edge.offset;
		}
		return found;
	}

	bool CurrentVirtualBasePathAddressForExpression(std::uint32_t expression,
		EntityId target, Operand* address, bool* adjusted = 0)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (adjusted) *adjusted = false;
		const EntityId owner = derived.BaseEntityForType(
			derived.arena_.nodes[expression].type);
		EntityId anchor = kNoEntity;
		std::uint64_t relative_offset = 0;
		if (owner != kNoEntity &&
			VirtualBasePathAnchor(owner, target, &anchor, &relative_offset) &&
			CurrentVirtualBaseAddressForExpression(expression, anchor, address))
		{
			if (relative_offset != 0)
				*address = derived.ProjectBaseSubobjectOffset(
					*address, relative_offset);
			if (adjusted) *adjusted = relative_offset != 0;
			return true;
		}
		return CurrentVirtualBaseAddressForExpression(
			expression, target, address);
	}

	bool HasCurrentImplicitVirtualBases() const
	{
		for (std::size_t i = 0; i < current_virtual_base_boundary_.size(); ++i)
			if (current_virtual_base_boundary_[i].implicit_object) return true;
		return false;
	}

	bool HasCurrentConstructionVtt() const
	{
		return current_construction_vtt_ != kNoLowId;
	}

	Operand CurrentConstructionVtt() const
	{
		if (!HasCurrentConstructionVtt())
			ThrowLoweringInternal("construction VTT parameter is unavailable");
		return Operand(current_construction_vtt_, LowPtr());
	}

	Operand ConstructionVttArgument(EntityId complete, EntityId base)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const EntityId owner = complete == kNoEntity ?
			derived.current_member_owner_ : complete;
		if (owner == kNoEntity)
			ThrowLoweringInternal("construction VTT has no owning class");
		if (complete == kNoEntity)
		{
			if (!HasCurrentConstructionVtt())
				ThrowLoweringInternal("nested base constructor has no VTT");
			if (base == owner) return CurrentConstructionVtt();
		}
		if (complete != kNoEntity &&
			(complete >= derived.polymorphism_.class_vtt_symbols.size() ||
			 derived.polymorphism_.class_vtt_symbols[complete] == kNoLowId))
			ThrowLoweringInternal("complete constructor has no VTT symbol");
		std::uint64_t offset = std::numeric_limits<std::uint64_t>::max();
		const EntityRecord& owner_record = derived.program_.entities[owner];
		for (std::size_t ordinal = 0;
			ordinal < owner_record.direct_base_count; ++ordinal)
			if (derived.program_.DirectBase(owner, ordinal).entity == base &&
				ordinal < derived.polymorphism_.class_construction_vtt_offsets[
					owner].size())
			{
				offset = derived.polymorphism_.class_construction_vtt_offsets[
					owner][ordinal];
				break;
			}
		if (offset == std::numeric_limits<std::uint64_t>::max())
			ThrowLoweringInternal("base constructor has no construction VTT slice");
		if (complete == kNoEntity)
			return derived.IndexAddress(LowI8(), CurrentConstructionVtt(),
				Operand(static_cast<std::int64_t>(offset), LowI64()), false);
		const SymbolId symbol =
			derived.polymorphism_.class_vtt_symbols[complete];
		derived.output_.symbols[symbol].referenced = true;
		const Operand table = derived.Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = table.id;
		address.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		derived.Emit(address);
		return derived.IndexAddress(LowI8(), table,
			Operand(static_cast<std::int64_t>(offset), LowI64()), false);
	}

	Operand RuntimeVirtualBaseAddress(const Operand& view, EntityId owner,
		std::size_t virtual_base_ordinal)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if ((view.kind == Operand::INTEGER && view.integer_value == 0) ||
			view.kind == Operand::NULL_POINTER)
			return Operand(0, LowPtr());
		const Operand vtable = derived.LoadStorage(view, LowPtr());
		const bool has_polymorphic_row =
			owner < derived.graph_.class_polymorphism.size() &&
			virtual_base_ordinal < derived.graph_.class_polymorphism[
				owner].virtual_base_offsets.size() &&
			(!derived.graph_.class_polymorphism[owner].slots.empty() ||
			 !derived.graph_.class_polymorphism[owner].views.empty());
		const std::int64_t row = has_polymorphic_row ?
			-static_cast<std::int64_t>(
				derived.graph_.class_polymorphism[owner].address_point) +
			(derived.output_.host_object_emission ? static_cast<std::int64_t>(
				derived.graph_.class_polymorphism[owner].virtual_call_offsets.size()) * 8 : 0) +
			static_cast<std::int64_t>(virtual_base_ordinal) * 8 :
			-static_cast<std::int64_t>(24 + virtual_base_ordinal * 8);
		const Operand entry = derived.IndexAddress(
			LowI8(), vtable, Operand(row, LowI64()), false);
		const Operand offset = derived.LoadStorage(entry, LowI64());
		return derived.IndexAddress(LowI8(), view, offset, false);
	}

	Operand ProjectNullableVirtualBaseAddress(const Operand& view,
		std::uint64_t offset)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Slot slot;
		slot.name = InternLocalName(derived.output_,
			derived.GeneratedSlotName("basecast"));
		slot.type = LowPtr();
		const Operand result(
			static_cast<SlotId>(derived.function_->slots.size()), LowPtr());
		derived.function_->slots.push_back(slot);
		const BlockId null_block = derived.AddBlock(
			derived.NewLabel("basecast_null"));
		const BlockId adjust_block = derived.AddBlock(
			derived.NewLabel("basecast_adjust"));
		const BlockId end_block = derived.AddBlock(
			derived.NewLabel("basecast_end"));
		const Operand is_null = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = is_null.id;
		compare.op = LOW_OP_EQ;
		compare.type = LowPtr();
		compare.first = view;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		derived.EmitBranch(is_null, null_block, adjust_block);
		derived.SelectBlock(null_block);
		Instruction store(Instruction::STORE);
		store.type = LowPtr();
		store.first = Operand(0, LowPtr());
		store.second = result;
		derived.Emit(store);
		derived.EmitJump(end_block);
		derived.SelectBlock(adjust_block);
		store.first = derived.ProjectBaseSubobjectOffset(view, offset);
		derived.Emit(store);
		derived.EmitJump(end_block);
		derived.SelectBlock(end_block);
		return derived.LoadStorage(result, LowPtr());
	}

	bool RuntimeVirtualBaseAddressForExpression(std::uint32_t expression,
		const Operand& view, EntityId target, Operand* address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const EntityId owner = derived.BaseEntityForType(
			derived.arena_.nodes[expression].type);
		if (owner == kNoEntity ||
			owner >= derived.graph_.class_polymorphism.size() ||
			(derived.graph_.class_polymorphism[owner].slots.empty() &&
			 derived.graph_.class_polymorphism[owner].views.empty())) return false;
		EntityId anchor = kNoEntity;
		std::uint64_t relative_offset = 0;
		if (!VirtualBasePathAnchor(
			owner, target, &anchor, &relative_offset)) return false;
		const EntityRecord& record = derived.program_.entities[owner];
		for (std::size_t ordinal = 0;
			ordinal < record.virtual_base_count; ++ordinal)
		{
			if (derived.program_.VirtualBase(owner, ordinal).entity != anchor)
				continue;
			*address = derived.ProjectBaseSubobjectOffset(
				RuntimeVirtualBaseAddress(view, owner, ordinal), relative_offset);
			return true;
		}
		return false;
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
		const TypeId expression_type = derived.program_.types.RemoveTopCv(
			derived.arena_.nodes[expression].type);
		const TypeRecord& expression_shape =
			derived.program_.types.Get(expression_type);
		const bool reference_to_pointer =
			(expression_shape.kind == TYPE_LVALUE_REFERENCE ||
			 expression_shape.kind == TYPE_RVALUE_REFERENCE) &&
			derived.program_.types.Get(derived.program_.types.RemoveTopCv(
				expression_shape.child)).kind == TYPE_POINTER;
		if (reference_to_pointer && source == owner)
			return ProjectNullableVirtualBaseAddress(view, layout.offset);
		bool defined_reference_call = false;
		if (derived.arena_.nodes[expression].kind == DUMP_CALL_EXPRESSION)
		{
			const NodeChildren call = derived.Children(expression);
			if (!call.empty())
			{
				const BindingId callee = derived.arena_.nodes[call[0]].binding;
				defined_reference_call = callee != kNoBinding &&
					callee < derived.function_definition_.size() &&
					derived.function_definition_[callee] != kNoDumpEdge;
			}
		}
		const bool known_complete_object = source == owner &&
			((derived.arena_.nodes[expression].kind == DUMP_TEMPORARY_OBJECT) ||
			 (derived.arena_.nodes[expression].kind == DUMP_ID_EXPRESSION &&
			  expression_shape.kind == TYPE_NAMED) || reference_to_pointer ||
			 ((derived.current_this_binding_ != kNoBinding &&
			 BoundaryBindingForExpression(expression) ==
				derived.current_this_binding_) ||
			 defined_reference_call));
		if (!known_complete_object ||
			(owner < derived.graph_.class_polymorphism.size() &&
			(!derived.graph_.class_polymorphism[owner].slots.empty() ||
			 !derived.graph_.class_polymorphism[owner].views.empty()) &&
			ordinal < derived.graph_.class_polymorphism[
				owner].virtual_base_offsets.size()))
			return RuntimeVirtualBaseAddress(view, owner, ordinal);
		if (source == owner &&
			derived.arena_.nodes[expression].kind != DUMP_CAST_EXPRESSION)
		{
			Operand complete_view = view;
			if (derived.current_this_binding_ != kNoBinding &&
				BoundaryBindingForExpression(expression) ==
				derived.current_this_binding_)
				complete_view = derived.LoadStorage(derived.StorageFor(
					derived.current_this_binding_, LowPtr()), LowPtr());
			return derived.ProjectBaseSubobjectOffset(
				complete_view, layout.offset);
		}
		return RuntimeVirtualBaseAddress(view, owner, ordinal);
	}

	void AppendCallVirtualBaseArguments(const DumpNode& callee,
		TypeId function_type,
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
			VirtualBaseParameterCount(callee.binding, function_type);
		if (hidden_remaining == 0) return;
		const TypeRecord& function = derived.program_.types.Get(function_type);
		const TypeId* parameters = derived.program_.types.Parameters(function_type);
		for (std::size_t i = 0; i < lowered.size(); ++i)
		{
			const bool implicit = member && i == 0;
			EntityId owner = implicit ?
				(IncludesImplicitVirtualBases(callee.binding) ?
				 binding->member_owner : kNoEntity) :
				(i < function.parameter_count ?
					 VirtualBoundaryEntity(parameters[i]) : kNoEntity);
			if (owner == kNoEntity) continue;
			Operand boundary_view = lowered[i];
			bool reference_to_pointer = false;
			if (!implicit && i < function.parameter_count)
			{
				const TypeRecord& top = derived.program_.types.Get(
					derived.program_.types.RemoveTopCv(parameters[i]));
				if ((top.kind == TYPE_LVALUE_REFERENCE ||
					top.kind == TYPE_RVALUE_REFERENCE) &&
					derived.program_.types.Get(
						derived.program_.types.RemoveTopCv(top.child)).kind == TYPE_POINTER)
				{
					reference_to_pointer = true;
					boundary_view = derived.LoadStorage(boundary_view, LowPtr());
				}
			}
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count &&
				hidden_remaining != 0; ++base)
			{
				if (!CarriesVirtualBase(callee.binding, i, base)) continue;
				arguments->Push(reference_to_pointer ?
					ProjectNullableVirtualBaseAddress(boundary_view,
						derived.program_.VirtualBase(owner, base).offset) :
					VirtualBaseCallAddress(
						call_children[i + 1], boundary_view, owner, base));
				references->Push(Instruction::CALL_PASS_VALUE);
				if (derived.stats_) ++derived.stats_->virtual_base_call_arguments;
				--hidden_remaining;
			}
		}
	}

	void ResetVirtualBaseBoundary()
	{
		current_virtual_base_boundary_.clear();
		current_construction_vtt_ = kNoLowId;
	}

	std::vector<VirtualBaseBoundaryFact> current_virtual_base_boundary_;
	ParameterId current_construction_vtt_;
};

}
}

#endif
