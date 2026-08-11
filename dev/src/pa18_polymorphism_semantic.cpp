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
	if (declaration.static_member_function &&
		(spec.virtual_specifier || override_specifier || final_specifier || pure))
		throw std::runtime_error(
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

FunctionSignatureKey SemanticAnalyzer::VirtualSignatureKey(
	BindingId binding) const
{
	binding = program_->bindings[binding].canonical;
	const BindingRecord& record = program_->bindings[binding];
	return record.destructor ?
		FunctionSignatureKey(kNoScope, 0, kNoType) :
		FunctionSignatureKey(kNoScope, record.name,
			GetFunction(binding).signature);
}

bool SemanticAnalyzer::VirtualSignatureMatches(BindingId derived,
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
	FunctionSignatureTable slot_index;
	for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
	{
		if (slot >= kNoBinding)
			throw std::runtime_error("too many virtual slots");
		slot_index.Insert(VirtualSignatureKey(facts.slots[slot].root),
			static_cast<BindingId>(slot));
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
		++virtual_signature_lookups_;
		const BindingId indexed = slot_index.Find(VirtualSignatureKey(member));
		const std::size_t matched = indexed == kNoBinding ?
			facts.slots.size() : static_cast<std::size_t>(indexed);
		const bool requires_override = binding.override_specifier;
		if (matched != facts.slots.size())
		{
			if (!VirtualSignatureMatches(member, facts.slots[matched].root))
				throw std::runtime_error(
					"invalid covariant virtual return type");
			const BindingId previous = facts.slots[matched].function;
			if (program_->bindings[previous].final_virtual)
				throw std::runtime_error("virtual function overrides final function");
			binding.virtual_function = true;
			facts.slots[matched].function = member;
			++virtual_overrides_;
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
			{
				facts.slots.push_back(VirtualSlotFact(member, member));
				if (facts.slots.size() > kNoBinding)
					throw std::runtime_error("too many virtual slots");
				slot_index.Insert(VirtualSignatureKey(member),
					static_cast<BindingId>(facts.slots.size() - 1));
			}
		}
	}

	EntityRecord& owner = program_->entities[entity];
	owner.polymorphic_class = !facts.slots.empty();
	if (owner.polymorphic_class) ++polymorphic_classes_;
	virtual_slots_ += facts.slots.size();
	owner.abstract_class = false;
	for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
	{
		if (program_->bindings[facts.slots[slot].function].pure_virtual)
			owner.abstract_class = true;
		if (program_->bindings[facts.slots[slot].function].destructor)
			owner.trivial_destructor = false;
	}
	if (virtual_slot_by_binding_.size() < program_->bindings.size())
		virtual_slot_by_binding_.resize(program_->bindings.size(), kNoDumpEdge);
	std::uint32_t physical_slot = 0;
	for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
	{
		const BindingId root =
			program_->bindings[facts.slots[slot].root].canonical;
		const BindingId function =
			program_->bindings[facts.slots[slot].function].canonical;
		virtual_slot_by_binding_[root] = physical_slot;
		virtual_slot_by_binding_[function] = physical_slot;
		const std::uint32_t width =
			program_->bindings[root].destructor ? 2 : 1;
		if (physical_slot > std::numeric_limits<std::uint32_t>::max() - width)
			throw std::runtime_error("too many virtual slots");
		physical_slot += width;
	}
	facts.complete = true;
}

void SemanticAnalyzer::MarkVtableDemand(EntityId entity)
{
	DemandClassTemplateMemberDefinitions(entity);
	for (std::size_t depth = 0; entity != kNoEntity &&
		depth <= program_->entities.size(); ++depth)
	{
		if (entity >= class_polymorphism_.size()) return;
		ClassPolymorphismFacts& facts = class_polymorphism_[entity];
		if (!facts.complete) return;
		if (!facts.vtable_demanded)
		{
			facts.vtable_demanded = true;
			++vtable_demands_;
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
	++virtual_slot_lookups_;
	if (binding == kNoBinding || binding >= program_->bindings.size())
		return kNoDumpEdge;
	binding = program_->bindings[binding].canonical;
	return binding < virtual_slot_by_binding_.size() ?
		virtual_slot_by_binding_[binding] : kNoDumpEdge;
}

}
}
