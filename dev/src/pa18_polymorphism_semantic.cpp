#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::ConfigureVirtualFunction(BindingId binding,
	const SpecInfo& spec, NodeId declarator, NodeId initializer)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		throw std::logic_error("virtual function has no binding");
	BindingRecord& declaration = program_->bindings[binding];
	const BindingId canonical_id = declaration.canonical;
	BindingRecord& canonical = program_->bindings[canonical_id];
	if (declaration.static_member_function && spec.virtual_specifier)
		throw std::runtime_error("static member function cannot be virtual");

	bool override_specifier = false;
	bool final_specifier = false;
	if (declarator != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(declarator);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, "virt-specifier")) continue;
			const std::string spelling = PayloadSource(child);
			override_specifier = override_specifier || spelling == "override";
			final_specifier = final_specifier || spelling == "final";
		}
	bool pure = false;
	if (initializer != kNoNode)
	{
		const NodeId value = FirstSemanticChild(initializer);
		pure = value != kNoNode && arena_->IsTag(value, "literal") &&
			arena_->Payload(value) == "0";
	}

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

bool SemanticAnalyzer::CovariantVirtualReturn(TypeId derived,
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
	TypeId derived_class = program_->types.RemoveTopCv(derived_shape.child);
	TypeId base_class = program_->types.RemoveTopCv(base_shape.child);
	const TypeRecord& derived_named = program_->types.Get(derived_class);
	const TypeRecord& base_named = program_->types.Get(base_class);
	if (derived_named.kind != TYPE_NAMED || base_named.kind != TYPE_NAMED)
		return false;
	return program_->IsBaseOf(base_named.entity, derived_named.entity);
}

bool SemanticAnalyzer::VirtualSignatureMatches(BindingId derived,
	BindingId base) const
{
	derived = program_->bindings[derived].canonical;
	base = program_->bindings[base].canonical;
	const BindingRecord& derived_binding = program_->bindings[derived];
	const BindingRecord& base_binding = program_->bindings[base];
	if (derived_binding.destructor || base_binding.destructor)
		return derived_binding.destructor && base_binding.destructor;
	if (derived_binding.name != base_binding.name) return false;
	const FunctionInfo& derived_function = GetFunction(derived);
	const FunctionInfo& base_function = GetFunction(base);
	if (derived_function.signature != base_function.signature) return false;
	const TypeId derived_result =
		program_->types.Get(derived_function.type).child;
	const TypeId base_result = program_->types.Get(base_function.type).child;
	return CovariantVirtualReturn(derived_result, base_result);
}

void SemanticAnalyzer::CompleteClassPolymorphism(EntityId entity)
{
	if (class_polymorphism_.size() <= entity)
		class_polymorphism_.resize(static_cast<std::size_t>(entity) + 1);
	ClassPolymorphismFacts& facts = class_polymorphism_[entity];
	if (facts.complete) return;
	const EntityId base = program_->entities[entity].direct_base;
	if (base != kNoEntity)
	{
		if (base >= class_polymorphism_.size() ||
			!class_polymorphism_[base].complete)
			throw std::logic_error("base polymorphism facts are incomplete");
		facts.slots = class_polymorphism_[base].slots;
	}
	bool inherits_virtual_destructor = false;
	for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
		if (program_->bindings[facts.slots[slot].root].destructor)
			inherits_virtual_destructor = true;
	if (inherits_virtual_destructor &&
		(entity >= entity_destructor_by_entity_.size() ||
		 entity_destructor_by_entity_[entity] == kNoBinding))
	{
		const BindingId destructor = EnsureImplicitDestructor(entity);
		std::vector<BindingId>& functions = entity_member_functions_[entity];
		if (std::find(functions.begin(), functions.end(), destructor) ==
			functions.end())
			functions.push_back(destructor);
	}

	const std::vector<BindingId>& members = entity_member_functions_[entity];
	for (std::size_t member_index = 0; member_index < members.size();
		++member_index)
	{
		const BindingId member = program_->bindings[members[member_index]].canonical;
		BindingRecord& binding = program_->bindings[member];
		if (binding.static_member_function || binding.constructor) continue;
		std::size_t matched = facts.slots.size();
		for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
			if (VirtualSignatureMatches(member, facts.slots[slot].root))
			{
				matched = slot;
				break;
			}
		const bool requires_override = binding.override_specifier;
		if (matched != facts.slots.size())
		{
			const BindingId previous = facts.slots[matched].function;
			if (program_->bindings[previous].final_virtual)
				throw std::runtime_error("virtual function overrides final function");
			binding.virtual_function = true;
			facts.slots[matched].function = member;
		}
		else
		{
			if (requires_override)
				throw std::runtime_error("override has no matching base virtual");
			if (binding.pure_virtual && !binding.virtual_function)
				throw std::runtime_error("pure function is not virtual");
			if (binding.final_virtual && !binding.virtual_function)
				throw std::runtime_error("final function is not virtual");
			if (binding.virtual_function)
				facts.slots.push_back(VirtualSlotFact(member, member));
		}
	}

	EntityRecord& owner = program_->entities[entity];
	owner.polymorphic_class = !facts.slots.empty();
	owner.abstract_class = false;
	for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
	{
		if (program_->bindings[facts.slots[slot].function].pure_virtual)
			owner.abstract_class = true;
		if (program_->bindings[facts.slots[slot].function].destructor)
			owner.trivial_destructor = false;
	}
	facts.complete = true;
	for (std::size_t member_index = 0; member_index < members.size();
		++member_index)
	{
		const BindingId member = program_->bindings[members[member_index]].canonical;
		if (program_->bindings[member].virtual_function &&
			!program_->bindings[member].pure_virtual &&
			GetFunction(member).defined)
		{
			MarkVtableDemand(entity);
			break;
		}
	}
}

void SemanticAnalyzer::MarkVtableDemand(EntityId entity)
{
	for (std::size_t depth = 0; entity != kNoEntity &&
		depth <= program_->entities.size(); ++depth)
	{
		if (entity >= class_polymorphism_.size()) return;
		ClassPolymorphismFacts& facts = class_polymorphism_[entity];
		if (!facts.complete) return;
		if (!facts.vtable_demanded)
		{
			facts.vtable_demanded = true;
			for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
				if (!program_->bindings[facts.slots[slot].function].pure_virtual)
					DemandFunction(facts.slots[slot].function);
			for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
				if (program_->bindings[facts.slots[slot].function].destructor)
				{
					BindingId deallocation = kNoBinding;
					const LookupResult found = program_->LookupMember(entity,
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
					break;
				}
		}
		entity = program_->entities[entity].direct_base;
	}
}

std::uint32_t SemanticAnalyzer::VirtualSlotFor(BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		return kNoDumpEdge;
	binding = program_->bindings[binding].canonical;
	const EntityId owner = program_->bindings[binding].member_owner;
	if (owner == kNoEntity || owner >= class_polymorphism_.size())
		return kNoDumpEdge;
	const std::vector<VirtualSlotFact>& slots = class_polymorphism_[owner].slots;
	std::uint32_t physical_slot = 0;
	for (std::size_t slot = 0; slot < slots.size(); ++slot)
	{
		if (program_->bindings[slots[slot].root].canonical == binding ||
			program_->bindings[slots[slot].function].canonical == binding)
		{
			return physical_slot;
		}
		const std::uint32_t width =
			program_->bindings[slots[slot].root].destructor ? 2 : 1;
		if (physical_slot > std::numeric_limits<std::uint32_t>::max() - width)
			throw std::runtime_error("too many virtual slots");
		physical_slot += width;
	}
	return kNoDumpEdge;
}

}
}
