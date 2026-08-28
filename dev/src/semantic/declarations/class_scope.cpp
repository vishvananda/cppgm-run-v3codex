// Stable class-definition scope creation and injected class-name bindings.
#include "semantic/analysis/analyzer.h"

namespace cppgm { namespace semantic {

ScopeId Analyzer::OpenClassDefinitionScope(
	TypeId type, EntityId entity, NamedFlavor flavor, ScopeId owner,
	ScopeId scope, NameId name, NameId lookup_name,
	ScopeId specialization_owner, NameId specialization_identity,
	NameId emission_name)
{
	ScopeId member_scope = program_->entities[entity].member_scope;
	if (member_scope != kNoScope) return member_scope;
	const ScopeId lexical_owner = specialization_owner == kNoScope ?
		owner : scope;
	member_scope = NewNamedScope(
		lexical_owner, SCOPE_CLASS, lookup_name, owner, name);
	// Semantic lookup may use an internal specialization slot while emission
	// follows the caller's separate typed presentation policy.
	if (specialization_owner != kNoScope)
		program_->SetScopeEmissionName(member_scope, emission_name);
	program_->SetEntityScope(entity, member_scope);
	program_->SetTypeName(member_scope, name, type);
	const BindingId injected = program_->AddBinding(
		member_scope, BIND_TYPE, name, type, false, 0, flavor);
	program_->bindings[injected].member_owner = entity;
	program_->bindings[injected].access = ACCESS_PUBLIC;
	program_->bindings[injected].compiler_generated = true;
	if (specialization_identity != 0 && specialization_identity != name)
	{
		program_->SetTypeName(member_scope, specialization_identity, type);
		const BindingId primary_injected = program_->AddBinding(
			member_scope, BIND_TYPE, specialization_identity, type,
			false, 0, flavor);
		program_->bindings[primary_injected].member_owner = entity;
		program_->bindings[primary_injected].access = ACCESS_PUBLIC;
		program_->bindings[primary_injected].compiler_generated = true;
	}
	return member_scope;
}

} }
