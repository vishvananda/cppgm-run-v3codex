#pragma once

#include "semantic/model/program.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace semantic { namespace presentation
{

struct TemplateBinding
{
	semantic::NameId name;
	semantic::TemplateArgument argument;

	TemplateBinding(semantic::NameId name_value,
		const semantic::TemplateArgument& argument_value)
		: name(name_value), argument(argument_value) {}
};

// Opt-in presentation for dependent semantic identities.  Template parameter
// provenance stays typed in EntityRecord/TemplateArgument; callers choose the
// depth and whether a stored pack-expansion role is part of the presentation.
struct TemplateParameterIdentity
{
	std::uint32_t depth;
	bool render_pack_expansions;

	explicit TemplateParameterIdentity(std::uint32_t depth_value,
		bool render_pack_expansions_value = true)
		: depth(depth_value),
		  render_pack_expansions(render_pack_expansions_value) {}
};

// Observer-only limits for canonical template-specialization presentation.
// Entries are keyed by typed entity identity, not rendered spelling, so the
// same policy works for qualified, anonymous-namespace, and nested uses.
struct TemplateEntityArgumentLimit
{
	semantic::EntityId entity;
	std::size_t count;

	TemplateEntityArgumentLimit(semantic::EntityId entity_value,
		std::size_t count_value)
		: entity(entity_value), count(count_value) {}
};

struct TemplateArgumentElision
{
	const std::vector<TemplateEntityArgumentLimit>& limits;

	explicit TemplateArgumentElision(
		const std::vector<TemplateEntityArgumentLimit>& limit_values)
		: limits(limit_values) {}
};

std::string RenderType(const semantic::Program& program, semantic::TypeId type);
std::string RenderName(const semantic::Program& program,
	semantic::ScopeId owner, semantic::NameId name,
	bool show_anonymous_namespace = false);
std::string RenderTemplateArgument(const semantic::Program& program,
	const semantic::TemplateArgument& argument);
std::string RenderTemplateArgument(const semantic::Program& program,
	const semantic::TemplateArgument& argument,
	const TemplateParameterIdentity& identity);
std::string RenderTemplateArgument(const semantic::Program& program,
	const semantic::TemplateArgument& argument,
	const TemplateArgumentElision& elision);
std::string RenderEntity(const semantic::Program& program,
	semantic::EntityId entity, bool show_anonymous_namespace = false);
std::string RenderEntity(const semantic::Program& program,
	semantic::EntityId entity, const TemplateParameterIdentity& identity,
	bool show_anonymous_namespace = false);
std::string RenderEntity(const semantic::Program& program,
	semantic::EntityId entity, const TemplateArgumentElision& elision,
	bool show_anonymous_namespace = false);
std::string RenderFunction(const semantic::Program& program,
	semantic::BindingId binding, semantic::TypeId type,
	const std::vector<TemplateBinding>& substitutions);

} }
}
