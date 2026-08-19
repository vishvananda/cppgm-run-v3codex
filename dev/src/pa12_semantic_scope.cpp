#include "pa12_semantic_detail.h"

#include <limits>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

std::string RenderBindingPresentation(const Program& program,
	const BindingRecord& binding, SemanticAnalysisStats* stats)
{
	const NameId terminal = binding.presentation_name_override != 0 ?
		binding.presentation_name_override : binding.name;
	std::size_t components = 0;
	const std::string rendered =
		program.RenderEmissionName(binding.owner, terminal, &components);
	if (stats)
	{
		++stats->presentation_reads[
			SEMANTIC_PRESENTATION_READ_BINDING_QUALIFIED];
		++stats->presentation_renders[SEMANTIC_PRESENTATION_EMISSION_NAME];
		stats->presentation_render_components[
			SEMANTIC_PRESENTATION_EMISSION_NAME] += components;
		stats->presentation_render_bytes[
			SEMANTIC_PRESENTATION_EMISSION_NAME] += rendered.size();
	}
	return rendered;
}

const std::string& SemanticAnalyzer::ScopePrefix(ScopeId scope)
{
	return program_->names.Get(ScopePrefixId(scope));
}

NameId SemanticAnalyzer::ScopePrefixId(ScopeId scope)
{
	if (stats_) ++stats_->scope_prefix_requests;
	const NameId deferred = std::numeric_limits<NameId>::max();
	if (scope >= scope_prefixes_.size() || scope_prefixes_[scope] != deferred)
	{
		if (stats_ && scope < scope_prefixes_.size())
			++stats_->scope_prefix_cache_hits;
		return scope < scope_prefixes_.size() ? scope_prefixes_[scope] : 0;
	}
	scope_prefix_scratch_.clear();
	ScopeId current = scope;
	while (current != kNoScope && current < scope_prefixes_.size() &&
		scope_prefixes_[current] == deferred)
	{
		if (scope_prefix_segments_[current] != 0)
			scope_prefix_scratch_.push_back(scope_prefix_segments_[current]);
		current = scope_parents_[current];
	}
	std::string rendered = current != kNoScope &&
		current < scope_prefixes_.size() ?
		program_->names.Get(scope_prefixes_[current]) : std::string();
	for (std::size_t i = scope_prefix_scratch_.size(); i != 0; --i)
	{
		rendered += program_->names.Get(scope_prefix_scratch_[i - 1]);
		rendered += "::";
	}
	if (stats_)
		RecordPresentationRender(SEMANTIC_PRESENTATION_SCOPE_PREFIX, rendered,
			scope_prefix_scratch_.size());
	scope_prefixes_[scope] = program_->names.Intern(rendered);
	return scope_prefixes_[scope];
}

NameId SemanticAnalyzer::DisplayName(ScopeId owner, NameId name)
{
	// ScopePrefix may intern a deferred prefix and invalidate string references.
	const std::string terminal = program_->names.Get(name);
	const std::string qualified = ScopePrefix(owner) + terminal;
	if (stats_)
		RecordPresentationRender(
			SEMANTIC_PRESENTATION_DISPLAY_NAME, qualified, 1);
	return program_->names.Intern(qualified);
}

NameId SemanticAnalyzer::EmissionName(ScopeId owner, NameId name)
{
	program_->BuildEmissionPath(owner, name, &scope_prefix_scratch_);
	std::string rendered;
	for (std::size_t i = 0; i < scope_prefix_scratch_.size(); ++i)
	{
		if (i != 0) rendered += "::";
		rendered += program_->names.Get(scope_prefix_scratch_[i]);
	}
	if (stats_)
		RecordPresentationRender(SEMANTIC_PRESENTATION_EMISSION_NAME, rendered,
			scope_prefix_scratch_.size());
	return program_->names.Intern(rendered);
}

void SemanticAnalyzer::InitializeInitializerListLifetimeScope(
	ScopeId scope, ScopeId parent)
{
	if (nearest_initializer_list_lifetime_scopes_.size() <= scope)
		nearest_initializer_list_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	nearest_initializer_list_lifetime_scopes_[scope] = parent != kNoScope &&
		parent < nearest_initializer_list_lifetime_scopes_.size() ?
			nearest_initializer_list_lifetime_scopes_[parent] : kNoScope;
}

ScopeId SemanticAnalyzer::NewScope(ScopeId parent, ScopeKind kind,
	NameId name, NameId prefix)
{
	const ScopeId scope = program_->NewScope(parent, kind, name);
	if (scope_prefixes_.size() <= scope)
	{
		scope_prefixes_.resize(static_cast<std::size_t>(scope) + 1, 0);
		scope_prefix_segments_.resize(static_cast<std::size_t>(scope) + 1, 0);
		scope_parents_.resize(static_cast<std::size_t>(scope) + 1, kNoScope);
		nearest_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
		scope_nontrivial_object_lifetime_prefixes_.resize(
			static_cast<std::size_t>(scope) + 1, 0);
		scope_lifetime_domains_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	}
	scope_prefixes_[scope] = prefix;
	scope_parents_[scope] = parent;
	// A local class member keeps its enclosing lexical scope for lookup, but
	// belongs to a new callable.  Automatic objects and temporaries from the
	// enclosing function must not become cleanup obligations of that member.
	const ScopeId lifetime_parent =
		kind == SCOPE_FUNCTION ? kNoScope : parent;
	nearest_lifetime_scopes_[scope] = lifetime_parent != kNoScope &&
		lifetime_parent < nearest_lifetime_scopes_.size() ?
			nearest_lifetime_scopes_[lifetime_parent] : kNoScope;
	InitializeInitializerListLifetimeScope(scope, lifetime_parent);
	scope_nontrivial_object_lifetime_prefixes_[scope] =
		lifetime_parent != kNoScope && lifetime_parent <
			scope_nontrivial_object_lifetime_prefixes_.size() ?
			scope_nontrivial_object_lifetime_prefixes_[lifetime_parent] : 0;
	scope_lifetime_domains_[scope] = kind == SCOPE_FUNCTION ? scope :
		parent != kNoScope && parent < scope_lifetime_domains_.size() ?
			scope_lifetime_domains_[parent] : kNoScope;
	return scope;
}

ScopeId SemanticAnalyzer::NewNamedScope(ScopeId parent, ScopeKind kind,
	NameId lookup_name, ScopeId presentation_owner, NameId presentation_name)
{
	const NameId deferred = std::numeric_limits<NameId>::max();
	const ScopeId scope = NewScope(parent, kind, lookup_name, deferred);
	scope_prefix_segments_[scope] = presentation_name;
	scope_parents_[scope] = presentation_owner;
	return scope;
}

bool SemanticAnalyzer::HasInternalLinkageScope(ScopeId scope) const
{
	return program_->HasInternalLinkageScope(scope);
}

void SemanticAnalyzer::PublishInlineFunctionFacts(BindingId binding,
	bool inline_specifier)
{
	if (!inline_specifier) return;
	binding = program_->bindings[binding].canonical;
	program_->bindings[binding].inline_function = true;
	program_->bindings[binding].weak_odr = true;
}

}
}
