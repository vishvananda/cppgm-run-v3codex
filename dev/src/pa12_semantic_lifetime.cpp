#include "pa12_semantic_detail.h"

#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::AddLifetimeObligation(ScopeId scope,
	BindingId object, TypeId type, bool allow_elision)
{
	if (IsInitializerListType(type)) return;
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity) return;
	EnsureClassDefinition(type);
	if (!program_->entities[entity].destructible)
		throw std::runtime_error("object type is not destructible");
	const BindingId destructor = DestructorForType(type);
	if (destructor == kNoBinding)
		throw std::logic_error("class has no destructor identity");
	if (!CanAccessMember(destructor, entity))
		throw std::runtime_error("inaccessible destructor");
	const TypeKind object_kind = program_->types.Get(
		program_->types.RemoveTopCv(type)).kind;
	if (program_->entities[entity].trivial_destructor) return;
	// Odr-use owns host emission even when an empty call can be elided.
	if (host_object_emission_)
	{
		const BindingId canonical = program_->bindings[destructor].canonical;
		const BindingId base_entry = EnsureDestructorBaseEntry(canonical);
		DemandFunction(canonical);
		if (base_entry != canonical)
			MarkFunctionObjectOutputRoot(base_entry);
	}
	if (allow_elision && object_kind != TYPE_ARRAY &&
		IsElidableAutomaticDestructor(destructor))
	{
		if (host_object_emission_)
			MarkFunctionObjectOutputRoot(destructor);
		return;
	}
	if (scope_lifetimes_.size() <= scope)
		scope_lifetimes_.resize(static_cast<std::size_t>(scope) + 1);
	if (nearest_lifetime_scopes_.size() <= scope)
		nearest_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	nearest_lifetime_scopes_[scope] = scope;
	scope_lifetimes_[scope].push_back(
		LifetimeObligation(object, destructor, type));
}

void SemanticAnalyzer::AddTemporaryLifetimeObligation(ScopeId scope,
	std::uint32_t temporary)
{
	const std::uint32_t action = MakeTemporaryDestructorAction(temporary);
	if (action == kNoDumpEdge) return;
	const DumpNode& cleanup = dump_.nodes[action];
	if (scope_lifetimes_.size() <= scope)
		scope_lifetimes_.resize(static_cast<std::size_t>(scope) + 1);
	if (nearest_lifetime_scopes_.size() <= scope)
		nearest_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	nearest_lifetime_scopes_[scope] = scope;
	scope_lifetimes_[scope].push_back(LifetimeObligation(kNoBinding,
		cleanup.binding, cleanup.operand_type, temporary));
	MarkInitializerListLifetimeScope(scope, temporary);
}

}  // namespace pa12_semantic_detail
}  // namespace cppgm
