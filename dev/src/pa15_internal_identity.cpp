#include "pa15_internal_identity.h"

namespace cppgm
{
namespace pa15_lowering_detail
{

using namespace pa11;

InternalIdentityClassifier::InternalIdentityClassifier(const Program& program)
	: program_(program), type_states_(program.types.Size(), 0),
	  entity_states_(program.entities.size(), 0)
{
}

bool InternalIdentityClassifier::BindingDeclaresInternalIdentity(
	BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_.bindings.size()) return false;
	const BindingRecord& record = program_.bindings[binding];
	return record.unnamed_namespace_linkage ||
		(record.storage_class == STORAGE_CLASS_STATIC &&
		 record.member_owner == kNoEntity);
}

bool InternalIdentityClassifier::ScopeHasInternalIdentity(ScopeId scope) const
{
	for (ScopeId current = scope; current != kNoScope;
		current = program_.ParentScope(current))
		if (program_.KindOfScope(current) == SCOPE_NAMESPACE &&
			program_.names.Get(program_.NameOfScope(current)) == "<unnamed>")
			return true;
	return false;
}

bool InternalIdentityClassifier::TemplateArgumentsHaveInternalIdentity(
	std::uint32_t begin, std::uint32_t count)
{
	if (begin == kNoBinding || begin > program_.template_arguments.size() ||
		count > program_.template_arguments.size() - begin) return false;
	for (std::size_t i = 0; i < count; ++i)
	{
		const std::size_t argument = begin + i;
		const TypeId type = program_.template_arguments[argument];
		if (type != kNoType && TypeHasInternalIdentity(type)) return true;
		if (argument < program_.canonical_template_arguments.size() &&
			BindingDeclaresInternalIdentity(program_.canonical_template_arguments[
				argument].value_binding)) return true;
	}
	return false;
}

bool InternalIdentityClassifier::EntityHasInternalIdentity(EntityId entity)
{
	if (entity == kNoEntity || entity >= program_.entities.size()) return false;
	std::uint8_t& state = entity_states_[entity];
	if (state >= 2) return state == 3;
	if (state == 1) return false;
	state = 1;
	const EntityRecord& record = program_.entities[entity];
	const bool internal = record.local_context != kNoBinding ||
		record.unnamed_class || BindingDeclaresInternalIdentity(record.declaration) ||
		ScopeHasInternalIdentity(record.owner) ||
		TemplateArgumentsHaveInternalIdentity(record.template_argument_begin,
			record.template_argument_count);
	state = internal ? 3 : 2;
	return internal;
}

bool InternalIdentityClassifier::TypeHasInternalIdentity(TypeId type)
{
	if (type == kNoType || type >= program_.types.Size()) return false;
	std::uint8_t& state = type_states_[type];
	if (state >= 2) return state == 3;
	if (state == 1) return false;
	state = 1;
	const TypeRecord& record = program_.types.Get(type);
	bool internal = false;
	if (record.kind == TYPE_NAMED || record.kind == TYPE_MEMBER_POINTER)
		internal = EntityHasInternalIdentity(record.entity);
	if (!internal && record.child != kNoType)
		internal = TypeHasInternalIdentity(record.child);
	if (!internal && record.dependent_bound_type != kNoType)
		internal = TypeHasInternalIdentity(record.dependent_bound_type);
	if (!internal && record.kind == TYPE_FUNCTION)
	{
		const TypeId* parameters = program_.types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count && !internal; ++i)
			internal = TypeHasInternalIdentity(parameters[i]);
	}
	state = internal ? 3 : 2;
	return internal;
}

bool InternalIdentityClassifier::BindingHasInternalIdentity(BindingId binding)
{
	if (binding == kNoBinding || binding >= program_.bindings.size()) return false;
	const BindingRecord& record = program_.bindings[binding];
	const BindingRecord& canonical = program_.bindings[record.canonical];
	return BindingDeclaresInternalIdentity(binding) ||
		BindingDeclaresInternalIdentity(record.canonical) ||
		TypeHasInternalIdentity(record.type) ||
		EntityHasInternalIdentity(record.member_owner) ||
		TemplateArgumentsHaveInternalIdentity(record.template_argument_begin,
			record.template_argument_count) ||
		TemplateArgumentsHaveInternalIdentity(canonical.template_argument_begin,
			canonical.template_argument_count);
}

}
}
