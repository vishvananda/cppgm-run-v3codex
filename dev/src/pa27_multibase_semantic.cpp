#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

LookupResult SemanticAnalyzer::LookupExplicitUnqualifiedTemplateName(
	ScopeId scope, NameId name, LookupKind kind)
{
	bool ambiguous = false;
	LookupResult found = program_->LookupNameCandidate(
		scope, name, kind, &ambiguous);
	if (!ambiguous) return found;

	// Distinct injected-class-name specializations reached through different
	// base subobjects can still denote one primary template. Recover only a
	// canonical lexical template marker; ordinary member lookup stays ambiguous.
	const std::size_t no_pattern = std::numeric_limits<std::size_t>::max();
	for (ScopeId lexical = scope; lexical != kNoScope;
		lexical = program_->ParentScope(lexical))
	{
		const LookupResult direct = program_->LookupDirect(
			lexical, name, LOOKUP_TYPE);
		if (FindClassTemplateIndex(direct, name) != no_pattern ||
			FindAliasTemplateIndex(direct, name) != no_pattern)
			return direct;
	}
	if (CandidateSubstitutionActive())
	{
		RecordCandidateSubstitutionFailure();
		return LookupResult();
	}
	throw std::runtime_error("ambiguous template name");
}

std::size_t SemanticAnalyzer::FindClassTemplate(ScopeId scope,
	const NamePath& path)
{
	const std::size_t no_pattern = std::numeric_limits<std::size_t>::max();
	if (path.Empty()) return no_pattern;
	if (!path.global && path.Size() == 1)
	{
		bool ambiguous = false;
		const LookupResult found = program_->LookupNameCandidate(
			scope, path.Last(), LOOKUP_TYPE, &ambiguous);
		if (!ambiguous) return FindClassTemplateIndex(found, path.Last());
		for (ScopeId lexical = scope; lexical != kNoScope;
			lexical = program_->ParentScope(lexical))
		{
			const LookupResult direct = program_->LookupDirect(
				lexical, path.Last(), LOOKUP_TYPE);
			const std::size_t pattern =
				FindClassTemplateIndex(direct, path.Last());
			if (pattern != no_pattern) return pattern;
		}
		return no_pattern;
	}
	return FindClassTemplateIndex(
		LookupPath(scope, path, LOOKUP_TYPE), path.Last());
}

void SemanticAnalyzer::ApplyQualifiedCallNamingTarget(ExpressionInfo* value,
	EntityId naming_class, const std::vector<BindingId>& candidates)
{
	if (!value || naming_class == kNoEntity) return;
	for (std::size_t i = 0; i < candidates.size(); ++i)
		if (GetFunction(candidates[i]).member_owner != kNoType)
		{
			(void)ApplyQualifiedMemberNamingTarget(
				value, naming_class, candidates[i]);
			return;
		}
}

bool SemanticAnalyzer::CacheDestructorChainDecision(BindingId destructor,
	bool proven_empty) const
{
	if (empty_destructor_chain_cache_.size() <= destructor)
		empty_destructor_chain_cache_.resize(
			static_cast<std::size_t>(destructor) + 1, 0);
	// A conservative no-elide decision is monotonic: a later empty definition
	// may enable an optional optimization, but retaining destruction is correct.
	empty_destructor_chain_cache_[destructor] = proven_empty ? 2 : 1;
	return proven_empty;
}

bool SemanticAnalyzer::CanElideDestructorChain(BindingId destructor) const
{
	++empty_destructor_chain_visits_;
	if (destructor == kNoBinding || destructor >= program_->bindings.size())
		return false;
	if (destructor < empty_destructor_chain_cache_.size() &&
		empty_destructor_chain_cache_[destructor] != 0)
	{
		++empty_destructor_chain_cache_hits_;
		return empty_destructor_chain_cache_[destructor] == 2;
	}
	const BindingRecord& binding = program_->bindings[destructor];
	if (binding.member_owner == kNoEntity)
		return CacheDestructorChainDecision(destructor, false);
	const FunctionInfo& info = GetFunction(destructor);
	const bool empty_definition = info.definition_body != kNoNode &&
		FirstSemanticChild(info.definition_body) == kNoNode;
	if (!info.implicit_destructor && !info.defaulted_destructor &&
		!empty_definition)
		return CacheDestructorChainDecision(destructor, false);
	const EntityId entity = binding.member_owner;
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base_ordinal = 0;
		base_ordinal < owner.direct_base_count; ++base_ordinal)
	{
		const EntityId base = program_->DirectBase(
			entity, base_ordinal).entity;
		if (program_->entities[base].trivial_destructor) continue;
		const BindingId base_destructor = DestructorForType(
			program_->entities[base].type);
		if (!CanElideDestructorChain(base_destructor))
			return CacheDestructorChainDecision(destructor, false);
	}
	if (entity >= entity_data_members_.size())
		return CacheDestructorChainDecision(destructor, true);
	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		const EntityId member = DestructedEntity(
			program_->bindings[members[i]].type);
		if (member == kNoEntity ||
			program_->entities[member].trivial_destructor)
			continue;
		if (!CanElideDestructorChain(DestructorForType(
			program_->bindings[members[i]].type)))
			return CacheDestructorChainDecision(destructor, false);
	}
	return CacheDestructorChainDecision(destructor, true);
}

}
}
