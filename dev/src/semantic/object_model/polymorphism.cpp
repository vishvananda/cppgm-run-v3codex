#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace semantic
{

void Analyzer::ConfigureVirtualFunction(BindingId binding,
	const SpecInfo& spec, NodeId declarator, NodeId initializer)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		ThrowInternalCompilerError("virtual function has no binding");
	BindingRecord& declaration = program_->bindings[binding];
	const BindingId canonical_id = declaration.canonical;
	BindingRecord& canonical = program_->bindings[canonical_id];

	bool override_specifier = false;
	bool final_specifier = false;
	if (declarator != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(declarator);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, ::cppgm::syntax::STAG_VIRT_SPECIFIER)) continue;
			const std::string spelling = PayloadSource(child);
			override_specifier = override_specifier || spelling == "override";
			final_specifier = final_specifier || spelling == "final";
		}
	bool pure = false;
	if (initializer != kNoNode)
	{
		const NodeId value = FirstSemanticChild(initializer);
		pure = value != kNoNode && arena_->IsTag(value, ::cppgm::syntax::STAG_LITERAL) &&
			arena_->Payload(value) == "0";
	}
	if (declaration.static_member_function &&
		(spec.virtual_specifier || override_specifier || final_specifier || pure))
		ThrowSemanticError(
			"static member function cannot have a virtual specifier");

	declaration.virtual_function = declaration.virtual_function ||
		spec.virtual_specifier;
	declaration.pure_virtual = declaration.pure_virtual || pure;
	declaration.final_virtual = declaration.final_virtual || final_specifier;
	canonical.virtual_function = canonical.virtual_function ||
		spec.virtual_specifier;
	canonical.pure_virtual = canonical.pure_virtual || pure;
	canonical.final_virtual = canonical.final_virtual || final_specifier;
	declaration.override_specifier = declaration.override_specifier ||
		override_specifier;
	canonical.override_specifier = canonical.override_specifier ||
		override_specifier;

	const EntityId owner = declaration.member_owner;
	if (owner == kNoEntity) return;
	if (entity_member_functions_.size() <= owner)
		entity_member_functions_.resize(static_cast<std::size_t>(owner) + 1);
	std::vector<BindingId>& members = entity_member_functions_[owner];
	if (std::find(members.begin(), members.end(), canonical_id) == members.end())
		members.push_back(canonical_id);
}

bool Analyzer::CovariantVirtualReturn(TypeId derived,
	TypeId base) const
{
	if (derived == base) return true;
	const TypeRecord& derived_shape = program_->types.Get(derived);
	const TypeRecord& base_shape = program_->types.Get(base);
	if (derived_shape.kind != base_shape.kind ||
		(derived_shape.kind != TYPE_POINTER &&
		 derived_shape.kind != TYPE_LVALUE_REFERENCE &&
		 derived_shape.kind != TYPE_RVALUE_REFERENCE))
		return false;
	TypeId derived_class = derived_shape.child;
	TypeId base_class = base_shape.child;
	std::uint8_t derived_cv = CV_NONE;
	std::uint8_t base_cv = CV_NONE;
	const TypeRecord& derived_target = program_->types.Get(derived_class);
	if (derived_target.kind == TYPE_QUALIFIED)
	{
		derived_cv = derived_target.cv;
		derived_class = derived_target.child;
	}
	const TypeRecord& base_target = program_->types.Get(base_class);
	if (base_target.kind == TYPE_QUALIFIED)
	{
		base_cv = base_target.cv;
		base_class = base_target.child;
	}
	if ((derived_cv & static_cast<std::uint8_t>(~base_cv)) != 0)
		return false;
	const TypeRecord& derived_named = program_->types.Get(derived_class);
	const TypeRecord& base_named = program_->types.Get(base_class);
	if (derived_named.kind != TYPE_NAMED || base_named.kind != TYPE_NAMED)
		return false;
	return BaseConversionAllowed(derived_named.entity, base_named.entity);
}

FunctionSignatureKey Analyzer::VirtualSignatureKey(
	BindingId binding) const
{
	binding = program_->bindings[binding].canonical;
	const BindingRecord& record = program_->bindings[binding];
	return record.destructor ?
		FunctionSignatureKey(kNoScope, 0, kNoType) :
		FunctionSignatureKey(kNoScope, record.name,
			GetFunction(binding).signature);
}

bool Analyzer::VirtualSignatureMatches(BindingId derived,
	BindingId base) const
{
	derived = program_->bindings[derived].canonical;
	base = program_->bindings[base].canonical;
	if (!(VirtualSignatureKey(derived) == VirtualSignatureKey(base)))
		return false;
	if (program_->bindings[derived].destructor) return true;
	const FunctionInfo& derived_function = GetFunction(derived);
	const FunctionInfo& base_function = GetFunction(base);
	const TypeId derived_result =
		program_->types.Get(derived_function.type).child;
	const TypeId base_result = program_->types.Get(base_function.type).child;
	return CovariantVirtualReturn(derived_result, base_result);
}

void Analyzer::MarkVtableDemand(EntityId entity)
{
	std::vector<std::uint8_t> visited(program_->entities.size(), 0);
	std::vector<EntityId> pending(1, entity);
	while (!pending.empty())
	{
		const EntityId current = pending.back();
		pending.pop_back();
		if (current >= class_polymorphism_.size() || visited[current]) continue;
		visited[current] = 1;
		DemandClassTemplateMemberDefinitions(current);
		ClassPolymorphismFacts& facts = class_polymorphism_[current];
		if (!facts.complete) continue;
		if (!facts.vtable_demanded)
		{
			facts.vtable_demanded = true;
			++vtable_demands_;
			bool has_owned_virtual_destructor = false;
			for (std::size_t view = 0; view <= facts.views.size(); ++view)
			{
				const std::vector<VirtualSlotFact>& slots = view == 0 ?
					facts.slots : facts.views[view - 1].slots;
				for (std::size_t slot = 0; slot < slots.size(); ++slot)
				{
					if (!program_->bindings[slots[slot].function].pure_virtual)
						DemandVtableFunction(slots[slot].function);
					const BindingRecord& entry =
						program_->bindings[slots[slot].function];
					has_owned_virtual_destructor =
						has_owned_virtual_destructor ||
						(entry.destructor && !entry.pure_virtual);
				}
			}
			if (has_owned_virtual_destructor)
			{
					BindingId deallocation = kNoBinding;
					const LookupResult found = program_->LookupMember(current,
						program_->names.Intern("operatordelete"), LOOKUP_ORDINARY);
					if (found.ordinary != kNoBinding)
					{
						const std::vector<BindingId> candidates =
							FunctionSet(found.ordinary);
						for (std::size_t i = 0; i < candidates.size(); ++i)
						{
							const TypeRecord& type = program_->types.Get(
								GetFunction(candidates[i]).type);
							if (type.parameter_count == 1)
							{
								deallocation = candidates[i];
								break;
							}
							if (type.parameter_count == 2 &&
								deallocation == kNoBinding)
								deallocation = candidates[i];
						}
					}
					if (deallocation == kNoBinding)
						deallocation = EnsureBuiltinFunction(
							BUILTIN_FUNCTION_OPERATOR_DELETE);
					DemandFunction(deallocation);
			}
		}
		for (std::size_t view = 0; view <= facts.views.size(); ++view)
		{
			const std::vector<VirtualSlotFact>& slots = view == 0 ?
				facts.slots : facts.views[view - 1].slots;
			for (std::size_t slot = 0; slot < slots.size(); ++slot)
			{
				const EntityId owner =
					program_->bindings[slots[slot].function].member_owner;
				if (owner != kNoEntity && owner != current)
					pending.push_back(owner);
			}
		}
	}
}

std::uint32_t Analyzer::VirtualSlotFor(BindingId binding) const
{
	++virtual_slot_lookups_;
	if (binding == kNoBinding || binding >= program_->bindings.size())
		return kNoDumpEdge;
	binding = program_->bindings[binding].canonical;
	return binding < virtual_slot_by_binding_.size() ?
		virtual_slot_by_binding_[binding] : kNoDumpEdge;
}

}
}
