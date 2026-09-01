#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace semantic
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

EntityId VirtualResultEntity(const Program& program, BindingId binding)
{
	if (binding == kNoBinding || binding >= program.bindings.size())
		return kNoEntity;
	const TypeRecord& function = program.types.Get(
		program.bindings[binding].type);
	if (function.kind != TYPE_FUNCTION) return kNoEntity;
	const TypeRecord& result = program.types.Get(function.child);
	if (result.kind != TYPE_POINTER &&
		result.kind != TYPE_LVALUE_REFERENCE &&
		result.kind != TYPE_RVALUE_REFERENCE) return kNoEntity;
	TypeId target = program.types.RemoveTopCv(result.child);
	const TypeRecord& named = program.types.Get(target);
	return named.kind == TYPE_NAMED ? named.entity : kNoEntity;
}

}

void Analyzer::BeginPolymorphicVirtualViewIndex(
	const ClassPolymorphismFacts& facts)
{
	if (polymorphic_virtual_view_marks_.size() < program_->entities.size())
	{
		polymorphic_virtual_view_marks_.resize(program_->entities.size(), 0);
		polymorphic_virtual_view_indices_.resize(program_->entities.size(), 0);
	}
	if (polymorphic_virtual_view_generation_ ==
		std::numeric_limits<std::uint32_t>::max())
	{
		std::fill(polymorphic_virtual_view_marks_.begin(),
			polymorphic_virtual_view_marks_.end(), 0);
		polymorphic_virtual_view_generation_ = 0;
	}
	const std::uint32_t generation = ++polymorphic_virtual_view_generation_;
	for (std::size_t i = 0; i < facts.views.size(); ++i)
	{
		if (!facts.views[i].virtual_base) continue;
		polymorphic_virtual_view_marks_[facts.views[i].entity] = generation;
		polymorphic_virtual_view_indices_[facts.views[i].entity] =
			static_cast<std::uint32_t>(i);
	}
}

void Analyzer::MergeSharedVirtualView(PolymorphicViewFact* retained,
	const PolymorphicViewFact& incoming)
{
	if (retained->entity != incoming.entity ||
		retained->slots.size() != incoming.slots.size())
		ThrowInternalCompilerError("shared virtual views have incompatible slots");
	for (std::size_t i = 0; i < retained->slots.size(); ++i)
	{
		VirtualSlotFact& selected = retained->slots[i];
		const VirtualSlotFact& candidate = incoming.slots[i];
		if (program_->bindings[selected.root].canonical !=
			program_->bindings[candidate.root].canonical)
			ThrowInternalCompilerError("shared virtual slot roots do not match");
		if (selected.function == kNoBinding)
		{
			if (program_->bindings[candidate.function].final_virtual)
				ThrowSemanticError(
					"virtual function overrides final function");
			continue;
		}
		const BindingId selected_function =
			program_->bindings[selected.function].canonical;
		const BindingId candidate_function =
			program_->bindings[candidate.function].canonical;
		if (selected_function == candidate_function) continue;
		const EntityId selected_owner =
			program_->bindings[selected_function].member_owner;
		const EntityId candidate_owner =
			program_->bindings[candidate_function].member_owner;
		const bool selected_is_base = selected_owner != kNoEntity &&
			candidate_owner != kNoEntity &&
			program_->IsBaseOf(selected_owner, candidate_owner);
		const bool candidate_is_base = selected_owner != kNoEntity &&
			candidate_owner != kNoEntity &&
			program_->IsBaseOf(candidate_owner, selected_owner);
		if (selected_is_base && !candidate_is_base)
			selected.function = candidate_function;
		else if (!candidate_is_base || selected_is_base)
		{
			if (program_->bindings[selected_function].final_virtual ||
				program_->bindings[candidate_function].final_virtual)
				ThrowSemanticError(
					"virtual function overrides final function");
			selected.function = kNoBinding;
		}
	}
}

void Analyzer::AppendPolymorphicView(ClassPolymorphismFacts* facts,
	const PolymorphicViewFact& view)
{
	if (!view.virtual_base)
	{
		facts->views.push_back(view);
		return;
	}
	++polymorphic_virtual_view_lookups_;
	const EntityId entity = view.entity;
	if (entity >= polymorphic_virtual_view_marks_.size())
		ThrowInternalCompilerError("virtual view identity is out of range");
	if (polymorphic_virtual_view_marks_[entity] ==
		polymorphic_virtual_view_generation_)
	{
		const std::uint32_t index = polymorphic_virtual_view_indices_[entity];
		if (index >= facts->views.size())
			ThrowInternalCompilerError("virtual view index is invalid");
		MergeSharedVirtualView(&facts->views[index], view);
		++polymorphic_virtual_view_merges_;
		return;
	}
	polymorphic_virtual_view_marks_[entity] =
		polymorphic_virtual_view_generation_;
	polymorphic_virtual_view_indices_[entity] =
		static_cast<std::uint32_t>(facts->views.size());
	facts->views.push_back(view);
}

void Analyzer::PublishVirtualBaseStats()
{
	stats_->virtual_base_layout_edge_visits = virtual_base_layout_edge_visits_;
	stats_->virtual_base_layout_facts = virtual_base_layout_facts_;
	stats_->virtual_base_layout_lookups = program_->virtual_base_layout_lookups;
	stats_->virtual_base_layout_probes = program_->virtual_base_layout_probes;
	stats_->direct_base_validation_visits =
		program_->direct_base_validation_visits;
	stats_->polymorphic_virtual_view_lookups =
		polymorphic_virtual_view_lookups_;
	stats_->polymorphic_virtual_view_merges =
		polymorphic_virtual_view_merges_;
}

std::size_t Analyzer::PreferredClassLayoutBaseOrdinal(
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

const EntityRecord* Analyzer::InitializeClassBaseLayout(
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
			ThrowSemanticError("direct base layout is incomplete");
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
				ThrowSemanticResourceLimit("class layout is too large");
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

void Analyzer::CompleteClassPolymorphism(EntityId entity)
{
	if (class_polymorphism_.size() <= entity)
		class_polymorphism_.resize(static_cast<std::size_t>(entity) + 1);
	ClassPolymorphismFacts& facts = class_polymorphism_[entity];
	if (facts.complete) return;
	BeginPolymorphicVirtualViewIndex(facts);
	EntityRecord& owner = program_->entities[entity];
	std::size_t primary = owner.direct_base_count;
	for (std::size_t ordinal = 0; ordinal < owner.direct_base_count; ++ordinal)
	{
		const DirectBaseEdge& edge = program_->DirectBase(entity, ordinal);
		if (edge.entity >= class_polymorphism_.size() ||
			!class_polymorphism_[edge.entity].complete)
			ThrowInternalCompilerError("base polymorphism facts are incomplete");
		if (!edge.virtual_base && primary == owner.direct_base_count &&
			(!class_polymorphism_[edge.entity].slots.empty() ||
			 !class_polymorphism_[edge.entity].views.empty()))
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
			AppendPolymorphicView(&facts, view);
		}
	}
	for (std::size_t ordinal = 0; ordinal < owner.direct_base_count; ++ordinal)
	{
		if (ordinal == primary) continue;
		const DirectBaseEdge& edge = program_->DirectBase(entity, ordinal);
		const ClassPolymorphismFacts& inherited =
			class_polymorphism_[edge.entity];
		if (inherited.slots.empty() && inherited.views.empty()) continue;
		// A secondary polymorphic base owns a physical address point even
		// when all of its virtual functions live in a shared virtual base.
		// Keep that physical view distinct from the inherited virtual view.
		{
			PolymorphicViewFact view(edge.entity,
				static_cast<std::uint32_t>(ordinal));
			view.slots = inherited.slots;
			AppendPolymorphicView(&facts, view);
		}
		if (!inherited.slots.empty())
		{
			for (std::size_t i = 0; i < inherited.primary_ancestors.size(); ++i)
			{
				PolymorphicViewFact alias(inherited.primary_ancestors[i],
					static_cast<std::uint32_t>(ordinal), 0, false);
				alias.slots = inherited.slots;
				AppendPolymorphicView(&facts, alias);
			}
		}
		for (std::size_t i = 0; i < inherited.views.size(); ++i)
		{
			PolymorphicViewFact view = inherited.views[i];
			view.direct_base_ordinal = static_cast<std::uint32_t>(ordinal);
			view.relative_offset = inherited.views[i].offset;
			AppendPolymorphicView(&facts, view);
		}
	}
	std::stable_partition(facts.views.begin(), facts.views.end(),
		[](const PolymorphicViewFact& view) { return !view.virtual_base; });

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
				ThrowSemanticResourceLimit("too many virtual slot locations");
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
		bool matched_primary = false;
		BindingId inherited_root = kNoBinding;
		while (location != kNoBinding)
		{
			VirtualSlotLocation& indexed = locations[location];
			matched_primary = matched_primary || indexed.view == 0;
			if (primary == owner.direct_base_count &&
				inherits_virtual_destructor && !binding.destructor &&
				indexed.view != 0)
				facts.views[indexed.view - 1].contributes_primary_override = true;
			std::vector<VirtualSlotFact>& slots =
				SlotsForView(&facts, indexed.view);
			VirtualSlotFact& slot = slots[indexed.slot];
			if (inherited_root == kNoBinding) inherited_root = slot.root;
			if (!VirtualSignatureMatches(member, slot.root))
				ThrowSemanticError("invalid covariant virtual return type");
			if (slot.function != kNoBinding &&
				program_->bindings[slot.function].final_virtual)
				ThrowSemanticError("virtual function overrides final function");
			slot.function = member;
			location = indexed.next;
			++virtual_overrides_;
		}
		if (matched)
		{
			binding.virtual_function = true;
			if (!matched_primary && primary == owner.direct_base_count &&
				inherits_virtual_destructor)
				facts.slots.push_back(VirtualSlotFact(inherited_root, member));
		}
		else
		{
			if (binding.override_specifier)
				ThrowSemanticError("override has no matching base virtual");
			if (binding.pure_virtual && !binding.virtual_function)
				ThrowSemanticError("pure function is not virtual");
			if (binding.final_virtual && !binding.virtual_function)
				ThrowSemanticError("final function is not virtual");
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
			if (slots[slot].function == kNoBinding)
				ThrowSemanticError("no unique final overrider");
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
				ThrowSemanticResourceLimit("too many virtual slots");
			physical_slot += width;
		}
	}
	facts.complete = true;
}

void Analyzer::FinalizeClassPolymorphismViews(EntityId entity)
{
	if (entity >= class_polymorphism_.size()) return;
	ClassPolymorphismFacts& facts = class_polymorphism_[entity];
	facts.virtual_base_offsets.clear();
	facts.virtual_call_offsets.clear();
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base = 0; base < owner.virtual_base_count; ++base)
		facts.virtual_base_offsets.push_back(static_cast<std::int64_t>(
			program_->VirtualBase(entity, base).offset));
	for (std::size_t view = 0; view < facts.views.size(); ++view)
	{
		PolymorphicViewFact& current = facts.views[view];
		const bool inherited_virtual_view = current.virtual_base;
		if (current.direct_base_ordinal >=
			program_->entities[entity].direct_base_count)
			ThrowInternalCompilerError("polymorphic view has no direct-base owner");
		const DirectBaseEdge& edge = program_->DirectBase(
			entity, current.direct_base_ordinal);
		current.offset = edge.offset + current.relative_offset;
		current.virtual_base = false;
		current.virtual_base_ordinal = kNoDumpEdge;
		current.virtual_base_offsets.clear();
		current.virtual_call_offsets.clear();
		std::uint64_t virtual_offset = 0;
		std::uint32_t virtual_ordinal = 0;
		if (program_->FindVirtualBase(entity, current.entity,
			&virtual_offset, &virtual_ordinal) &&
			(inherited_virtual_view || virtual_offset == current.offset))
		{
			current.virtual_base = true;
			current.virtual_base_ordinal = virtual_ordinal;
			current.offset = virtual_offset;
		}
		const EntityRecord& view_owner = program_->entities[current.entity];
		for (std::size_t base = 0; base < view_owner.virtual_base_count; ++base)
		{
			const EntityId target =
				program_->VirtualBase(current.entity, base).entity;
			std::uint64_t target_offset = 0;
			if (!program_->FindVirtualBase(entity, target, &target_offset))
				ThrowInternalCompilerError("view virtual base has no complete offset");
			current.virtual_base_offsets.push_back(
				static_cast<std::int64_t>(target_offset) -
				static_cast<std::int64_t>(current.offset));
		}
		if (current.virtual_base)
		{
			std::size_t call_offset_count = current.slots.size();
			if (current.contributes_primary_override &&
				current.entity < class_polymorphism_.size())
				call_offset_count = class_polymorphism_[current.entity].
					virtual_call_offsets.size();
			current.virtual_call_offsets.assign(call_offset_count, 0);
		}
		current.address_point = 8 * (current.virtual_base_offsets.size() +
			current.virtual_call_offsets.size() + 2);
		if (current.stores_vptr && current.virtual_base)
			facts.virtual_call_offsets.insert(facts.virtual_call_offsets.end(),
				current.slots.size(), 0);
	}
	facts.address_point = 8 * (facts.virtual_base_offsets.size() +
		facts.virtual_call_offsets.size() + 2);
	for (std::size_t view = 0; view <= facts.views.size(); ++view)
	{
		std::vector<VirtualSlotFact>& slots = SlotsForView(&facts, view);
		const std::uint64_t view_offset = view == 0 ? 0 : facts.views[view - 1].offset;
		for (std::size_t slot = 0; slot < slots.size(); ++slot)
		{
			slots[slot].return_adjustment = 0;
			slots[slot].return_vtable_offset = 0;
			slots[slot].return_adjustment_virtual = false;
			const EntityId target =
				program_->bindings[slots[slot].function].member_owner;
			std::uint64_t target_offset = 0;
			if (target != entity && target != kNoEntity &&
				!program_->FindVirtualBase(entity, target, &target_offset) &&
				!program_->QueryBasePath(entity, target, 0, 0, &target_offset))
				ThrowInternalCompilerError("virtual final overrider has no object path");
			slots[slot].this_adjustment =
				static_cast<std::int64_t>(target_offset) -
				static_cast<std::int64_t>(view_offset);

			const EntityId result = VirtualResultEntity(
				*program_, slots[slot].function);
			const EntityId root_result = VirtualResultEntity(
				*program_, slots[slot].root);
			if (result == kNoEntity || root_result == kNoEntity ||
				result == root_result) continue;
			std::uint64_t result_offset = 0;
			std::uint32_t virtual_ordinal = 0;
			if (program_->FindVirtualBase(result, root_result,
				&result_offset, &virtual_ordinal))
			{
				if (result >= class_polymorphism_.size() ||
					virtual_ordinal >= program_->entities[result].virtual_base_count)
					ThrowInternalCompilerError(
						"covariant result has no finalized virtual-base slot");
				const ClassPolymorphismFacts& result_facts =
					class_polymorphism_[result];
				const std::uint64_t row = 8 *
					static_cast<std::uint64_t>(virtual_ordinal);
				if (row >= result_facts.address_point)
					ThrowInternalCompilerError(
						"covariant virtual-base row is outside the vtable prefix");
				slots[slot].return_adjustment_virtual = true;
				slots[slot].return_vtable_offset =
					static_cast<std::int64_t>(row) -
					static_cast<std::int64_t>(result_facts.address_point);
			}
			else
			{
				if (!program_->QueryBasePath(
					result, root_result, 0, 0, &result_offset))
					ThrowInternalCompilerError(
						"covariant result has no finalized base path");
				slots[slot].return_adjustment =
					static_cast<std::int64_t>(result_offset);
			}
		}
	}
}

}
}
