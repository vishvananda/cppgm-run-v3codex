#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

std::size_t AlignClassBase(std::size_t value, std::size_t alignment)
{
	if (alignment <= 1) return value;
	const std::size_t remainder = value % alignment;
	return remainder == 0 ? value : value + alignment - remainder;
}

struct VirtualSlotLocation
{
	std::uint32_t view, slot;
	BindingId next;

	VirtualSlotLocation(std::uint32_t view_value, std::uint32_t slot_value,
		BindingId next_value)
		: view(view_value), slot(slot_value), next(next_value) {}
};

std::vector<VirtualSlotFact>& SlotsForView(
	ClassPolymorphismFacts* facts, std::size_t view)
{
	return view == 0 ? facts->slots : facts->views[view - 1].slots;
}

const std::vector<VirtualSlotFact>& SlotsForView(
	const ClassPolymorphismFacts& facts, std::size_t view)
{
	return view == 0 ? facts.slots : facts.views[view - 1].slots;
}

}

std::size_t SemanticAnalyzer::PreferredClassLayoutBaseOrdinal(
	EntityId entity) const
{
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t ordinal = 0; ordinal < owner.direct_base_count; ++ordinal)
	{
		const DirectBaseEdge& edge = program_->DirectBase(entity, ordinal);
		if (!edge.virtual_base && edge.entity == owner.direct_base)
			return ordinal;
	}
	return owner.direct_base_count;
}

const EntityRecord* SemanticAnalyzer::InitializeClassBaseLayout(
	EntityId entity, std::size_t packing_alignment, std::size_t* size,
	std::size_t* alignment, std::size_t* natural_alignment)
{
	EntityRecord& owner = program_->entities[entity];
	if (!owner.has_user_declared_destructor)
	{
		owner.destructible = true;
		owner.trivial_destructor = true;
	}
	if (owner.direct_base_count == 0) return 0;
	const std::size_t preferred_ordinal = PreferredClassLayoutBaseOrdinal(entity);
	owner.direct_base = kNoEntity;
	const EntityRecord* primary = 0;
	for (std::size_t layout_index = 0; layout_index < owner.direct_base_count;
		++layout_index)
	{
		const std::size_t base_index = preferred_ordinal == owner.direct_base_count ?
			layout_index : layout_index == 0 ? preferred_ordinal :
			layout_index <= preferred_ordinal ? layout_index - 1 : layout_index;
		DirectBaseEdge& edge = program_->MutableDirectBase(entity, base_index);
		const EntityRecord* base = &program_->entities[edge.entity];
		if (!base->layout_complete)
			throw std::runtime_error("direct base layout is incomplete");
		if (!edge.virtual_base && !primary)
		{
			primary = base; owner.direct_base = edge.entity;
		}
		const std::size_t base_alignment =
			static_cast<std::size_t>(base->object_alignment);
		const std::size_t effective_base_alignment = packing_alignment == 0 ?
			base_alignment : std::min(base_alignment, packing_alignment);
		std::size_t offset = 0;
		if (!edge.virtual_base && !base->empty_class)
		{
			if (primary == base && owner.polymorphic_class &&
				!base->polymorphic_class)
				offset = AlignClassBase(std::max<std::size_t>(*size, 8),
					effective_base_alignment);
			else offset = AlignClassBase(*size, effective_base_alignment);
			const std::size_t raw_base_size = static_cast<std::size_t>(
				base->nonvirtual_size == 0 ? base->object_size :
					base->nonvirtual_size);
			const std::size_t base_size =
				AlignClassBase(raw_base_size, effective_base_alignment);
			if (offset > std::numeric_limits<std::size_t>::max() - base_size)
				throw std::runtime_error("class layout is too large");
			*size = offset + base_size;
		}
		edge.offset = offset;
		owner.has_nonzero_base_subobject_offset =
			owner.has_nonzero_base_subobject_offset || offset != 0 ||
			base->has_nonzero_base_subobject_offset;
		if (primary == base) owner.direct_base_offset = offset;
		if (!edge.virtual_base)
		{
			*natural_alignment = std::max(*natural_alignment, base_alignment);
			*alignment = std::max(*alignment, effective_base_alignment);
		}
		if (!base->destructible) owner.destructible = false;
		if (!base->trivial_destructor) owner.trivial_destructor = false;
		const BindingId destructor = DestructorForType(base->type);
		if (destructor == kNoBinding || !CanAccessMember(destructor, edge.entity))
			owner.destructible = false;
	}
	return primary;
}

void SemanticAnalyzer::CompleteClassPolymorphism(EntityId entity)
{
	if (class_polymorphism_.size() <= entity)
		class_polymorphism_.resize(static_cast<std::size_t>(entity) + 1);
	ClassPolymorphismFacts& facts = class_polymorphism_[entity];
	if (facts.complete) return;
	EntityRecord& owner = program_->entities[entity];
	std::size_t primary = owner.direct_base_count;
	for (std::size_t ordinal = 0; ordinal < owner.direct_base_count; ++ordinal)
	{
		const DirectBaseEdge& edge = program_->DirectBase(entity, ordinal);
		if (edge.entity >= class_polymorphism_.size() ||
			!class_polymorphism_[edge.entity].complete)
			throw std::logic_error("base polymorphism facts are incomplete");
		if (!edge.virtual_base && primary == owner.direct_base_count &&
			!class_polymorphism_[edge.entity].slots.empty())
			primary = ordinal;
	}
	if (primary != owner.direct_base_count)
	{
		const EntityId base = program_->DirectBase(entity, primary).entity;
		const ClassPolymorphismFacts& inherited = class_polymorphism_[base];
		owner.direct_base = base;
		facts.slots = inherited.slots;
		facts.primary_ancestors.push_back(base);
		facts.primary_ancestors.insert(facts.primary_ancestors.end(),
			inherited.primary_ancestors.begin(), inherited.primary_ancestors.end());
		for (std::size_t i = 0; i < inherited.views.size(); ++i)
		{
			PolymorphicViewFact view = inherited.views[i];
			view.direct_base_ordinal = static_cast<std::uint32_t>(primary);
			view.relative_offset = inherited.views[i].offset;
			facts.views.push_back(view);
		}
	}
	for (std::size_t ordinal = 0; ordinal < owner.direct_base_count; ++ordinal)
	{
		if (ordinal == primary) continue;
		const DirectBaseEdge& edge = program_->DirectBase(entity, ordinal);
		const ClassPolymorphismFacts& inherited =
			class_polymorphism_[edge.entity];
		if (inherited.slots.empty() && inherited.views.empty()) continue;
		if (!inherited.slots.empty())
		{
			PolymorphicViewFact view(edge.entity,
				static_cast<std::uint32_t>(ordinal));
			view.slots = inherited.slots;
			facts.views.push_back(view);
			for (std::size_t i = 0; i < inherited.primary_ancestors.size(); ++i)
			{
				PolymorphicViewFact alias(inherited.primary_ancestors[i],
					static_cast<std::uint32_t>(ordinal), 0, false);
				alias.slots = inherited.slots;
				facts.views.push_back(alias);
			}
		}
		for (std::size_t i = 0; i < inherited.views.size(); ++i)
		{
			PolymorphicViewFact view = inherited.views[i];
			view.direct_base_ordinal = static_cast<std::uint32_t>(ordinal);
			view.relative_offset = inherited.views[i].offset;
			facts.views.push_back(view);
		}
	}

	bool inherits_virtual_destructor = false;
	for (std::size_t view = 0; view <= facts.views.size(); ++view)
	{
		const std::vector<VirtualSlotFact>& slots = SlotsForView(facts, view);
		for (std::size_t slot = 0; slot < slots.size(); ++slot)
			if (program_->bindings[slots[slot].root].destructor)
				inherits_virtual_destructor = true;
	}
	if (inherits_virtual_destructor &&
		(entity >= entity_destructor_by_entity_.size() ||
		 entity_destructor_by_entity_[entity] == kNoBinding))
	{
		const BindingId destructor = EnsureImplicitDestructor(entity);
		if (entity_member_functions_.size() <= entity)
			entity_member_functions_.resize(static_cast<std::size_t>(entity) + 1);
		std::vector<BindingId>& functions = entity_member_functions_[entity];
		if (std::find(functions.begin(), functions.end(), destructor) ==
			functions.end()) functions.push_back(destructor);
	}

	FunctionSignatureTable slot_index;
	std::vector<VirtualSlotLocation> locations;
	for (std::size_t view = 0; view <= facts.views.size(); ++view)
	{
		const std::vector<VirtualSlotFact>& slots = SlotsForView(facts, view);
		for (std::size_t slot = 0; slot < slots.size(); ++slot)
		{
			if (locations.size() >= kNoBinding)
				throw std::runtime_error("too many virtual slot locations");
			const FunctionSignatureKey key = VirtualSignatureKey(slots[slot].root);
			const BindingId next = slot_index.Find(key);
			locations.push_back(VirtualSlotLocation(
				static_cast<std::uint32_t>(view),
				static_cast<std::uint32_t>(slot), next));
			slot_index.Insert(key,
				static_cast<BindingId>(locations.size() - 1));
		}
	}
	if (entity_member_functions_.size() <= entity)
		entity_member_functions_.resize(static_cast<std::size_t>(entity) + 1);
	const std::vector<BindingId>& members = entity_member_functions_[entity];
	for (std::size_t member_index = 0; member_index < members.size(); ++member_index)
	{
		const BindingId member =
			program_->bindings[members[member_index]].canonical;
		BindingRecord& binding = program_->bindings[member];
		if (binding.static_member_function || binding.constructor) continue;
		++virtual_signature_lookups_;
		BindingId location = slot_index.Find(VirtualSignatureKey(member));
		const bool matched = location != kNoBinding;
		while (location != kNoBinding)
		{
			VirtualSlotLocation& indexed = locations[location];
			std::vector<VirtualSlotFact>& slots =
				SlotsForView(&facts, indexed.view);
			VirtualSlotFact& slot = slots[indexed.slot];
			if (!VirtualSignatureMatches(member, slot.root))
				throw std::runtime_error("invalid covariant virtual return type");
			if (program_->bindings[slot.function].final_virtual)
				throw std::runtime_error("virtual function overrides final function");
			slot.function = member;
			location = indexed.next;
			++virtual_overrides_;
		}
		if (matched) binding.virtual_function = true;
		else
		{
			if (binding.override_specifier)
				throw std::runtime_error("override has no matching base virtual");
			if (binding.pure_virtual && !binding.virtual_function)
				throw std::runtime_error("pure function is not virtual");
			if (binding.final_virtual && !binding.virtual_function)
				throw std::runtime_error("final function is not virtual");
			if (binding.virtual_function)
				facts.slots.push_back(VirtualSlotFact(member, member));
		}
	}

	owner.polymorphic_class = !facts.slots.empty() || !facts.views.empty();
	if (owner.polymorphic_class) ++polymorphic_classes_;
	owner.abstract_class = false;
	if (virtual_slot_by_binding_.size() < program_->bindings.size())
		virtual_slot_by_binding_.resize(program_->bindings.size(), kNoDumpEdge);
	for (std::size_t view = 0; view <= facts.views.size(); ++view)
	{
		const std::vector<VirtualSlotFact>& slots = SlotsForView(facts, view);
		virtual_slots_ += slots.size();
		std::uint32_t physical_slot = 0;
		for (std::size_t slot = 0; slot < slots.size(); ++slot)
		{
			const BindingId root = program_->bindings[slots[slot].root].canonical;
			const BindingId function =
				program_->bindings[slots[slot].function].canonical;
			if (program_->bindings[function].pure_virtual) owner.abstract_class = true;
			if (program_->bindings[function].destructor)
				owner.trivial_destructor = false;
			virtual_slot_by_binding_[root] = physical_slot;
			if (virtual_slot_by_binding_[function] == kNoDumpEdge)
				virtual_slot_by_binding_[function] = physical_slot;
			const std::uint32_t width = program_->bindings[root].destructor ? 2 : 1;
			if (physical_slot > std::numeric_limits<std::uint32_t>::max() - width)
				throw std::runtime_error("too many virtual slots");
			physical_slot += width;
		}
	}
	facts.complete = true;
}

void SemanticAnalyzer::FinalizeClassPolymorphismViews(EntityId entity)
{
	if (entity >= class_polymorphism_.size()) return;
	ClassPolymorphismFacts& facts = class_polymorphism_[entity];
	for (std::size_t view = 0; view < facts.views.size(); ++view)
	{
		PolymorphicViewFact& current = facts.views[view];
		if (current.direct_base_ordinal >=
			program_->entities[entity].direct_base_count)
			throw std::logic_error("polymorphic view has no direct-base owner");
		const DirectBaseEdge& edge = program_->DirectBase(
			entity, current.direct_base_ordinal);
		current.offset = edge.offset + current.relative_offset;
	}
	for (std::size_t view = 0; view <= facts.views.size(); ++view)
	{
		std::vector<VirtualSlotFact>& slots = SlotsForView(&facts, view);
		const std::uint64_t view_offset = view == 0 ? 0 : facts.views[view - 1].offset;
		for (std::size_t slot = 0; slot < slots.size(); ++slot)
		{
			const EntityId target =
				program_->bindings[slots[slot].function].member_owner;
			std::uint64_t target_offset = 0;
			if (target != entity && target != kNoEntity &&
				!program_->FindVirtualBase(entity, target, &target_offset) &&
				!program_->QueryBasePath(entity, target, 0, 0, &target_offset))
				throw std::logic_error("virtual final overrider has no object path");
			slots[slot].this_adjustment =
				static_cast<std::int64_t>(target_offset) -
				static_cast<std::int64_t>(view_offset);
		}
	}
}

}
}
